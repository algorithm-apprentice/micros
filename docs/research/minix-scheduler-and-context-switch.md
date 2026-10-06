# MINIX Scheduler and Context-Switch Study

## Purpose

This study traces the complete MINIX 3 scheduling path before `micros`
implements runnable queues and preemption. It focuses on mechanism boundaries,
not source reuse:

- runnable representation;
- ready queues;
- timer accounting and quantum expiration;
- kernel versus user-space scheduler authority;
- user-origin and kernel-origin interrupt return paths;
- architecture context restore;
- blocking and wakeup interactions;
- SMP extension boundaries.

The reference checkout is MINIX commit
`4db99f4012570a577414fe2a43697b2f239b699e`. `micros` remains an independent
RISC-V implementation: it reproduces the verified mechanism and authority
boundaries without copying MINIX source. Under
[ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md), the
first implementation also reproduces verified internal scheduling semantics
before optional redesign. That is a project baseline choice, not a claim that
MINIX's public scheduler calls mathematically require one representation.

## End-to-end execution path

### Kernel entry saves into the process object

MINIX architecture entry code associates saved user state with the current
`struct proc`, not with a scheduler-owned transient object:

- ARM user exceptions call `save_process_ctx`, then branch to the common
  `switch_to_user` path after handling
  (`minix/kernel/arch/earm/mpx.S:80-154`);
- x86 full interrupt, soft-interrupt, and exception paths save the process
  frame and ordinarily jump to `switch_to_user`
  (`minix/kernel/arch/i386/mpx.S:52-145,262-373`).

x86 fast `SYSENTER` and `SYSCALL` are deliberate exceptions. User-mapped
stubs preserve most registers on the user stack, while kernel entry records
only the return PC, SP, status, and result registers
(`minix/kernel/arch/i386/usermapped_glo_ipc.S:29-78`,
`minix/kernel/arch/i386/mpx.S:200-260,375-432`). This is an optimized ABI, not
the general scheduler representation that `micros` must copy before it has a
working baseline.

The portable process record begins with the architecture-visible register
frame and also contains scheduling, accounting, ready-link, IPC, and fault
state (`minix/kernel/proc.h:15-120`).

### Ordinary reschedulable returns converge on `switch_to_user`

`switch_to_user()` is the central scheduling safe point
(`minix/kernel/proc.c:297-458`). It:

1. keeps the current process if it remains runnable;
2. repairs early-preemption state;
3. picks another process or idles when necessary;
4. switches the address space;
5. completes deferred kernel work and message delivery;
6. handles quantum exhaustion only when it is safe to notify a scheduler;
7. rechecks runnable state after every potentially blocking action;
8. calls the architecture finish hook;
9. restores the selected process context through assembly.

Timer, IPC, VM-resume, signal, and ordinary syscall paths therefore do not
each invent their own final context-switch sequence.

There are narrow architecture-local same-process returns. A successful lazy
FPU restore explicitly resumes the same process without scheduling
(`minix/kernel/proc.c:1922-1958`), and an invalid x86 fast-call selector
returns directly (`minix/kernel/arch/i386/mpx.S:249-260`). The baseline lesson
is not that every return instruction calls one C function. It is that every
path allowed to select another process converges on one scheduling path.

### User-origin and kernel-origin IRQs differ

On ARM, a user-origin IRQ saves the process context and branches to
`switch_to_user`, while a kernel-origin IRQ restores the interrupted kernel
registers and PC directly (`minix/kernel/arch/earm/mpx.S:135-174`). Before
that origin test, ARM forces IRQ and FIQ masked in the saved status
(`minix/kernel/arch/earm/mpx.S:57-70`).

x86 PIC and APIC handlers make the same distinction. A user-origin interrupt
enters the scheduling path, while a kernel-origin interrupt returns directly
with IF forced clear
(`minix/kernel/arch/i386/mpx.S:52-145`,
`minix/kernel/arch/i386/apic_asm.S:17-64`,
`minix/kernel/arch/i386/sconst.h:91-98`).

This is the same conceptual boundary needed by `micros`:

- a captured U-origin frame is a scheduling safe point;
- an S-origin frame must not be replaced until resumable kernel continuations
  exist.

## Runnable representation

### Runnable is derived from blocking flags

MINIX does not use a single enum for runnable, blocked, and running. A process
is runnable exactly when `p_rts_flags == 0`
(`minix/kernel/proc.h:140-175`).

The flags represent independent reasons a process cannot run, including:

- sending or receiving IPC;
- process stop or tracing;
- missing privilege or endpoint;
- VM inhibit/request/page fault;
- quantum exhaustion;
- early preemption.

`RTS_SET` automatically dequeues a process when the first blocking reason is
added. `RTS_UNSET` automatically enqueues it when the last reason is removed
(`minix/kernel/proc.h:200-230`).

This tightly couples runnable ownership to every blocking mechanism and avoids
the invalid state "blocked but still in the ready queue."

### The running process remains queue-reachable

MINIX ready queues are singly linked per CPU and priority. `pick_proc()` returns
the head without removing it (`minix/kernel/proc.c:1783-1816`). A runnable
process can therefore be both selected as current and still represented at the
queue head.

Quantum expiration or blocking sets an RTS flag, which dequeues it. Later
rescheduling clears the flag and enqueues it again.

ADR-0025 makes this verified MINIX representation part of the first `micros`
baseline rather than an immediate optimization opportunity. `micros` may store
the state on its distinct thread object and may keep a typed hart-current
handle, but every thread with no run-time blocking reason should remain
represented in exactly one ready queue, including the selected current
thread.

## Ready queues and fairness

### Kernel mechanism

`enqueue()` appends a runnable process to the tail of its CPU/priority queue
(`minix/kernel/proc.c:1593-1660`). `dequeue()` removes a process from any queue
position and repairs the tail (`minix/kernel/proc.c:1714-1777`).

Early preemption differs from quantum expiration:

- a process preempted before using its quantum is reinserted at the head
  (`enqueue_head`, `minix/kernel/proc.c:1662-1712`);
- a process whose quantum expires is later enqueued at a policy-selected queue
  tail.

This distinction prevents an early-preempted process from losing the remainder
of its turn.

### Priority selection

`pick_proc()` scans priority queues in order and selects the first nonempty
head (`minix/kernel/proc.c:1783-1816`). The queue implementation is kernel
mechanism; priority and quantum values may come from user space.

MINIX has 16 priority queues
(`minix/include/minix/config.h:66-74`). `micros` can initially assign all
bootstrap user threads the same priority, but using one structurally different
FIFO would not reproduce the selected baseline scheme. Under ADR-0025,
priority queues, tail enqueue, and preempted-head restoration belong in the
first scheduler representation; a later measured optimization may supersede
them.

## Timer and quantum expiration

The clock interrupt charges tick-based user/system accounting, handles virtual
timers, updates load, and invokes the architecture timer hook
(`minix/kernel/clock.c:70-172`). It does not directly decrement the scheduling
quantum.

`context_stop(process)` reads the architecture cycle counter and deducts the
elapsed process-execution delta from `p_cpu_time_left` when a user process
enters the kernel
(`minix/kernel/arch/i386/arch_clock.c:208-330`,
`minix/kernel/arch/earm/arch_clock.c:71-143`). IPC, kernel-call, interrupt, and
exception entries all reach this process-accounting path, so a process that
enters the kernel frequently is still charged even when it is not running at
the instant of a periodic clock interrupt
(`minix/kernel/arch/i386/mpx.S:52-145,262-373`,
`minix/kernel/arch/earm/mpx.S:80-238`).

Before returning to user mode, `switch_to_user()` calls
`context_stop(KERNEL)`. That accounts the kernel interval separately and
resets the context-boundary timestamp; it does not deduct the selected
process's quantum (`minix/kernel/proc.c:437-440`,
`minix/kernel/arch/i386/arch_clock.c:208-340`). IPC and kernel-call attribution
also has separate accounting fields.

`switch_to_user()` notices zero time only at the common safe point and calls
`proc_no_time()` (`minix/kernel/proc.c:418-445`).

For a user-scheduled process, `proc_no_time()`:

1. sets `RTS_NO_QUANTUM`, which dequeues the process;
2. sends a kernel-origin `SCHEDULING_NO_QUANTUM` message to its scheduler;
3. leaves the process non-runnable until rescheduled
   (`minix/kernel/proc.c:1820-1908`).

For a kernel-scheduled or non-preemptible process, it simply renews the quantum.

The key boundary is:

- process-entry accounting records consumed process CPU time while
  kernel-return accounting records the kernel interval separately;
- the periodic timer supplies bounded preemption latency and ordinary clock
  services;
- the common user-return path performs quantum-expiration and scheduling
  transitions.

The existing `micros` choice to count one accepted timer expiration as one
complete quantum is therefore not MINIX baseline behavior.

### Expired-only timer restart at return boundaries

MINIX also prepares the timer immediately before leaving kernel work:

- `switch_to_user()` calls `restart_local_timer()` immediately before user
  context restore (`minix/kernel/proc.c:466-472`);
- `idle()` restarts the boot CPU timer before sleeping
  (`minix/kernel/proc.c:198-204`);
- x86 delegates to the APIC timer
  (`minix/kernel/arch/i386/arch_clock.c:168-174`);
- the APIC restart programs a new one-shot only if the previous counter has
  reached zero (`minix/kernel/arch/i386/apic.c:574-579`).

The equivalent SBI TIME boundary is a **Required adaptation**: if kernel
preflight outlasted the programmed deadline, program a future deadline before
user return or idle wait. This does not count an extra expiration, renew
thread quantum, or select another thread. It prevents immediate same-PC timer
retrap from starving actual user execution. The exact contract is defined by
[ADR-0027](../adr/0027-return-boundary-timer-rearming.md).

## Kernel and user scheduler split

The SCHED server owns policy fields such as maximum priority, current priority,
time slice, and CPU assignment (`minix/servers/sched/schedproc.h:1-40`).

On quantum expiration, `do_noquantum()` lowers priority and calls the kernel
schedule interface (`minix/servers/sched/schedule.c:84-108`).

SCHED also arms a five-second balance timer. `balance_queues()` periodically
raises demoted processes one queue toward their configured maximum priority,
and clock notifications invoke that transition
(`minix/servers/sched/schedule.c:16-18,334-368`,
`minix/servers/sched/main.c:44-49,132-133`). Demotion without this restoration
is not the complete stable MINIX policy.

At scheduler takeover, SCHED:

1. validates PM/RS authority;
2. records policy;
3. calls `sys_schedctl` to become the process scheduler;
4. marks its local slot in use;
5. selects a CPU;
6. separately calls `sys_schedule` with priority, quantum, and CPU
   (`minix/servers/sched/schedule.c:130-240`).

These are two distinct kernel transitions. `do_schedctl()` only installs the
authorized `p_scheduler`; it does not renew a quantum, clear
`RTS_NO_QUANTUM`, or enqueue the process
(`minix/kernel/system/do_schedctl.c:7-45`). `do_schedule()` later permits only
that stored scheduler to update policy
(`minix/kernel/system/do_schedule.c:8-29`). `sched_proc()` clears only
`RTS_NO_QUANTUM`; any independent blocking flags still keep the process off
the queues (`minix/kernel/system.c:642-699`,
`minix/kernel/proc.h:200-230`).

This checkout has no rollback if the first takeover succeeds and the first
schedule operation fails
(`minix/servers/sched/schedule.c:216-237`).

Two policy updates have the same defect:

- `do_noquantum()` lowers its local priority before `sys_schedule()` and does
  not restore it when the kernel call fails
  (`minix/servers/sched/schedule.c:98-105`);
- `balance_queues()` raises local priority and ignores the scheduling result
  (`minix/servers/sched/schedule.c:344-365`).

These are known failure-atomicity defects under ADR-0025, not baseline behavior
to reproduce. The later SCHED ADR must require transactional takeover and
rollback or commit-after-success for demotion and promotion, so SCHED's local
policy and kernel-installed policy agree after every failure. MINIX
`do_nice()` already demonstrates local rollback for a failed update
(`minix/servers/sched/schedule.c:264-291`).

The kernel `sched_proc()` checks the request, temporarily removes a runnable
process, updates priority/quantum/CPU, and reenqueues it
(`minix/kernel/system.c:630-705`). Its upper-bound check is defective: it
accepts `priority == NR_SCHED_QUEUES` even though valid indices end at
`NR_SCHED_QUEUES - 1`
(`minix/kernel/system.c:642-646`,
`minix/include/minix/config.h:66-74`). `micros` must preserve the policy range,
not this out-of-bounds validation bug.

Thus the user scheduler never owns raw queue links or context restore. The
kernel remains authoritative for:

- runnable/blocking state;
- ready queues;
- current process;
- address-space switch;
- context restore;
- timer accounting.

MINIX SCHED does not submit an exact next process. It supplies priority,
quantum, CPU, and niceness parameters; the kernel still selects the
highest-priority ready-queue head. ADR-0024's exact-next-thread option is
therefore a post-baseline redesign, not part of the first SCHED implementation.

## Context restore

Architecture finish hooks prepare only architecture-specific return state:

- x86 places the selected process pointer on the kernel stack and forces user
  interrupt enable (`minix/kernel/arch/i386/arch_system.c:480-520`);
- ARM records the supervisor stack pointer and clears interrupt mask bits in
  the selected process status (`minix/kernel/arch/earm/arch_system.c:140-170`).

Assembly then restores the process frame. The portable scheduler does not
manipulate individual architecture registers.

MINIX uses per-CPU kernel stacks, not one kernel stack per process. x86 and ARM
set the CPU's TSS/SVC stack and place the selected process pointer at its top
before user return
(`minix/kernel/arch/i386/arch_system.c:480-520`,
`minix/kernel/arch/earm/arch_system.c:140-170`,
`minix/kernel/arch/i386/include/arch_proto.h:228-233`,
`minix/kernel/arch/earm/include/arch_proto.h:49-54`). User register state lives
in `struct proc`, so a blocked process does not retain an in-kernel C
continuation on a private stack.

For `micros`, the equivalent boundary is:

- scheduler selects exact thread ownership and root;
- user-execution code validates and installs the complete return frame;
- architecture assembly performs the final register/CSR restore.

## Implications for `micros`

### Centralize scheduling at one U-return safe point

The replacement scheduler design must not let timer, future IPC, and future
page-fault paths each duplicate switch ordering. After a U-origin trap:

1. capture the current thread;
2. mechanism handlers update timer, blocking, wakeup, or pending-reschedule
   state;
3. one common scheduler-return function decides whether to keep or replace the
   user frame;
4. one user-execution commit installs the selected context/root;
5. the ordinary trap epilogue returns.

This is the strongest lesson from MINIX `switch_to_user()`.

### Reproduce runnable flags and queue-reachable current selection

The first baseline scheduler should place independent run-time blocking reasons
on the thread object and derive runnable state from their zero value. Canonical
set/unset operations must perform the corresponding dequeue/enqueue exactly
when the zero/nonzero boundary changes.

The selected current thread remains in its ready queue, normally at the head
of the highest-priority nonempty queue. The hart current handle is a typed
reference to that selected queue member, not an exclusive ownership state that
removes it from queue reachability.

The already implemented distinct process/thread/hart objects remain compatible:
MINIX `struct proc` scheduling fields map to the `micros` thread, endpoint and
address-space ownership remain on the `micros` process, and CPU-local selection
maps to the hart.

### Separate process and kernel accounting intervals

`micros` should maintain a per-thread remaining quantum in platform-counter
units and charge only elapsed thread execution when a U-origin entry stops that
thread's accounted interval. Before user return, a separate kernel interval is
closed and the counter baseline is reset for the selected thread. A timer
interrupt forces a bounded check, but does not itself mean that an entire
quantum elapsed.

### Separate quantum expiration from early preemption

For an externally scheduled, preemptible MINIX process, exhaustion sets
`RTS_NO_QUANTUM`, removes it from the ready queues, and leaves it blocked until
its authorized scheduler renews policy. A kernel-scheduled or non-preemptible
process instead renews its quantum in place and remains runnable
(`minix/kernel/proc.c:1893-1908`,
`minix/kernel/proc.h:177-179`).

The dependency-ready `micros` scheduler cannot yet call a user SCHED server.
Its temporary in-kernel policy must therefore be documented as a staged
substitution: synchronously emulate the later SCHED response for bootstrap
threads by renewing policy and requeueing after `NO_QUANTUM`. It must not
misdescribe immediate rotation as the MINIX kernel-scheduled fallback.

The temporary policy may keep one fixed priority rather than duplicate
SCHED's demotion and five-second promotion before IPC and SCHED are
dependency-ready. That is an explicit staged substitution. The later SCHED
task must add both quantum demotion and periodic restoration together.

A thread displaced by a newly runnable higher-priority thread retains its
remaining quantum and is restored to the head. Early preemption and quantum
expiration remain distinct transitions.

### Preserve kernel authority after policy handoff

MINIX confirms that moving policy to a server does not require giving it queue
or context authority. The later `micros` scheduler-server ADR should expose
validated priority, quantum, CPU, and niceness decisions while the kernel
retains selection, queues, context mechanism, and stale-generation checks.
Exact-next-thread policy is deferred until after baseline completion.

### Define invalid-context handling before untrusted workloads

MINIX has explicit RTS fault/block reasons and does not leave an invalid
process at a selectable queue head. `micros` currently chooses a fatal policy
for trusted bootstrap payloads. Before untrusted applications, the process
fault ADR must atomically remove the exact thread from scheduler ownership and
report it to PM.

### Do not claim SMP readiness from per-hart queues alone

MINIX keeps current process, billing state, loaded page-table owner, ready
queues, idle state, and FPU owner per CPU
(`minix/kernel/cpulocals.h:37-75`). Its x86 SMP path also uses a big kernel
lock at context boundaries and synchronous scheduling IPIs for remote stop,
full-context/FPU capture, and migration
(`minix/kernel/arch/i386/arch_clock.c:208-264`,
`minix/kernel/smp.c:70-203`). ARM explicitly rejects `CONFIG_SMP`
(`minix/kernel/arch/earm/arch_clock.c:20-23`).

The baseline evidence is therefore x86-specific and keeps migration, IPI
coordination, TLB/FPU state, and locking in the kernel. `micros` should keep
typed per-hart representation but must continue to describe SMP as deferred.

## Required changes to the current scheduler plan

Under ADR-0025's baseline-first project requirement, before implementation
continues:

1. supersede ADR-0024's timer-specific final switching entry with one common
   U-return scheduling safe point;
2. replace the separate `INACTIVE`/`RUNNABLE`/`RUNNING` ownership model with
   independent thread RTS reasons, zero-flags runnable derivation, and exact
   queue membership for every runnable thread, including current;
3. use per-hart arrays of priority queues with tail enqueue and preempted-head
   restoration, even if all first bootstrap threads share one priority;
4. charge remaining quantum when process/thread execution stops, account the
   kernel interval separately, and keep timer handling to clock service plus
   bounded preemption entry;
5. route timer preemption, deferred work, future IPC block/wakeup, and future
   faults through the common selector;
6. restrict the first user-scheduler ABI to priority, quantum, CPU, and
   niceness policy; keep next-thread selection in the kernel;
7. include quantum demotion and periodic priority restoration when SCHED
   becomes dependency-ready, while documenting the interim fixed-priority
   kernel policy as a staged substitution;
8. reject `priority >= queue_count`, including the MINIX off-by-one case;
9. keep kernel queue/context mechanism authoritative during policy handoff;
10. make scheduler takeover, quantum demotion, and periodic promotion
    failure-atomic, with tests proving local/kernel policy agreement after
    every injected failure;
11. retain separate native proofs for flags and queues and target proofs for
    process/kernel accounting plus the common return selector.

## Source map

- `minix/kernel/proc.h`
- `minix/kernel/proc.c`
- `minix/kernel/cpulocals.h`
- `minix/kernel/clock.c`
- `minix/kernel/smp.c`
- `minix/kernel/system.c`
- `minix/kernel/arch/i386/mpx.S`
- `minix/kernel/arch/i386/apic_asm.S`
- `minix/kernel/arch/i386/arch_clock.c`
- `minix/kernel/arch/i386/arch_system.c`
- `minix/kernel/arch/earm/mpx.S`
- `minix/kernel/arch/earm/arch_clock.c`
- `minix/kernel/arch/earm/arch_system.c`
- `minix/servers/sched/main.c`
- `minix/servers/sched/schedule.c`
- `minix/servers/sched/schedproc.h`

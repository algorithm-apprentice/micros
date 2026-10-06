# ADR-0026: MINIX-Baseline Kernel Scheduler

- Status: Accepted
- Date: 2026-10-06
- Replaces: scheduler design invalidated by ADR-0025
- Supersedes in part:
  - ADR-0016 handled U-origin timer interrupts returning through the unchanged
    frame; S-origin timer return remains unchanged
  - ADR-0019 thread lifecycle, current-thread binding, and validation rules
  - ADR-0022 production direct-entry authority, `RUNNING`-state capture
    requirements, scheduler-recognized U-origin traps being fatal after
    capture, and test-only supervisor-return cleanup ordering

## Context

ADR-0025 superseded ADR-0024 before scheduler production implementation.
Research against MINIX commit
`4db99f4012570a577414fe2a43697b2f239b699e` established that the first design
did not reproduce the reference scheduling scheme:

- MINIX derives runnable state from independent run-time flags rather than a
  mutually exclusive `INACTIVE`/`RUNNABLE`/`RUNNING` enum;
- every runnable process remains in one ready queue, including the selected
  current process;
- each CPU owns an array of priority queues;
- a newly runnable higher-priority process preempts the current process without
  consuming its remaining quantum;
- elapsed process execution is charged when process execution stops, while
  kernel execution is accounted separately;
- quantum exhaustion and early preemption are different transitions;
- every path allowed to select another process converges on
  `switch_to_user()`;
- user SCHED supplies priority, quantum, CPU, and niceness policy while the
  kernel retains queue and next-process selection.

`micros` must reproduce that behavior while preserving its Accepted RISC-V
platform, separate process/thread/hart objects, generation-safe handles,
per-thread supervisor stacks, Sv39 roots, and one-hart development boundary.

The current implementation can enter one prepared U-mode thread, capture its
context, and restore it. It has no production ready queues, repeated
selection, elapsed-execution accounting, idle transition, or common
reschedulable user-return path.

## Decision

### Scope

This slice implements:

- MINIX-style independent thread run-time flags;
- one generation-safe singly linked ready queue per priority and hart;
- queue-reachable current-thread selection;
- fixed-priority bootstrap round robin as a staged policy substitution;
- elapsed thread-execution accounting in platform counter ticks;
- separate kernel and idle accounting ownership;
- timer-bounded U-mode preemption;
- one common reschedulable U-return selector;
- context-switch preflight followed by a non-failing commit;
- a real no-runnable transition to a hart idle continuation;
- native and QEMU proofs of queue, accounting, context, and idle behavior.

This slice does not implement:

- IPC send/receive blocking flags or wait queues;
- page-fault delivery to VM or PM fault policy;
- user-space SCHED, policy IPC, demotion, or periodic priority balancing;
- niceness;
- CPU affinity, migration, scheduling IPIs, or SMP synchronization;
- floating-point or vector context;
- ASIDs, remote TLB shootdown, or load balancing;
- untrusted-process recovery after an invalid saved context.

Those omissions do not change the first scheduler representation. Later tasks
add blocking reasons and policy owners to the same thread flags, queues, and
kernel selection mechanism.

### MINIX-to-`micros` state mapping

| MINIX state | `micros` owner |
| --- | --- |
| `struct proc.p_reg` | `micros_thread.user_context` |
| `p_rts_flags` | `micros_thread.runtime_flags` |
| `p_priority` | `micros_thread.priority` |
| `p_quantum_size_ms` | `micros_thread.quantum_counter_ticks` |
| `p_cpu_time_left` | `micros_thread.remaining_counter_ticks` |
| `p_nextready` | generation-safe `micros_thread.ready_next` |
| `p_cpu` | `micros_thread.assigned_hart` |
| per-CPU ready head/tail arrays | `micros_hart.ready_head/tail` |
| per-CPU `proc_ptr` | `micros_hart.current_thread` |
| per-CPU accounting timestamp | `micros_hart.accounting_started_at` |
| kernel/user accounting owner | `micros_hart.accounting_owner` |

Process address-space, endpoint, privilege, grant, and lifecycle ownership
remain on `micros_process`. Scheduling, register state, blocking reasons,
priority, quantum, queue link, and current execution remain on
`micros_thread`.

The mapping reproduces one MINIX schedulable process with one `micros` process
plus its sole v0.1 thread. It does not collapse those object types.

### Thread lifecycle and run-time flags

The `micros_thread_state` enum is removed. A live thread instead owns a
32-bit run-time flag word.

This slice defines:

```text
MICROS_THREAD_RTS_INACTIVE   = 1 << 0
MICROS_THREAD_RTS_NO_QUANTUM = 1 << 1
MICROS_THREAD_RTS_PREEMPTED  = 1 << 2
```

Future ADRs allocate independent bits for IPC send, IPC receive, page fault,
process stop, tracing, privilege inhibit, and other blocking reasons. They do
not replace the word.

A live thread is runnable exactly when:

```text
runtime_flags == 0
```

The meanings are:

- `INACTIVE`: the thread exists but has not been admitted to scheduling, or
  has been held for teardown;
- `NO_QUANTUM`: an externally scheduled thread has exhausted policy, or the
  bootstrap policy is transactionally renewing it;
- `PREEMPTED`: a higher-priority thread became runnable before the current
  thread consumed its quantum.

Creation initializes `INACTIVE`. Context preparation and detachment require
`INACTIVE`. A held thread may retain its assigned hart and policy so a later
wakeup can clear `INACTIVE`. Before context detachment or release, a separate
scheduler-removal operation requires `INACTIVE`, no queue membership, and no
current-hart reference, then clears all assigned-hart and policy metadata.
Release additionally requires no attached execution context.

There is no stored `RUNNING` state. Running is derived from one exact hart's
`current_thread` handle. A current thread may temporarily be non-runnable and
off all ready queues after it blocks or is preempted, until the common return
selector chooses another thread. This is valid MINIX-style in-kernel state.

### Canonical flag transitions

Portable scheduler operations implement the MINIX zero/nonzero boundary:

- setting the first run-time flag on a runnable thread removes it from its
  exact ready queue before returning;
- adding another flag to an already non-runnable thread does not touch queues;
- clearing a flag enqueues at the assigned priority tail only when the complete
  resulting word becomes zero and the thread is not current;
- clearing one of several flags leaves the thread off all queues;
- every failed transition preserves objects and output arguments byte for
  byte.

A standalone final-flag clear targeting the exact current thread is rejected.
Making a blocked current thread runnable, enqueueing it, and either retaining
or replacing current ownership must occur inside one return-selection plan.
This prevents a current thread from returning behind a same-priority queue
head.

`PREEMPTED` is restored only inside a complete return-selection plan rather
than by an independently returning mutation:

1. clear only `PREEMPTED`;
2. if another flag remains, keep the thread off queues;
3. if the thread becomes runnable with remaining quantum, insert it at its
   priority head;
4. if it becomes runnable with zero remaining quantum, insert it at the tail
   for subsequent no-quantum handling;
5. select and install the resulting current thread in the same commit.

The generic flag API rejects unknown bits and cannot clear `PREEMPTED`. No
public operation may return after making a current thread runnable at a
non-head queue position. In particular, zero-remaining restoration behind a
same-priority peer and current replacement are one planned transaction.

### Priority and quantum policy fields

The kernel mechanism has exactly 16 priorities:

```text
MICROS_SCHEDULER_PRIORITY_COUNT = 16
MICROS_SCHEDULER_PRIORITY_HIGHEST = 0
MICROS_SCHEDULER_PRIORITY_LOWEST = 15
MICROS_SCHEDULER_PRIORITY_DEFAULT_USER = 7
```

Lower numeric values have higher priority. Every accepted policy requires:

- `priority < MICROS_SCHEDULER_PRIORITY_COUNT`;
- nonzero `quantum_counter_ticks`;
- a registered exact assigned hart;
- an attached execution context.

The strict `<` check deliberately corrects MINIX's off-by-one validation defect
that accepts `priority == NR_SCHED_QUEUES`.

Each admitted thread stores:

- assigned hart handle;
- current priority;
- configured nonzero quantum in platform counter ticks;
- remaining counter ticks;
- preemptible flag;
- ready-link metadata.

The MVP expresses quantum only in the same `time` counter units already used by
ADR-0016. It does not claim a millisecond ABI before a timebase-frequency
contract exists.

### Ready queues

Each hart owns:

```text
ready_head[MICROS_SCHEDULER_PRIORITY_COUNT]
ready_tail[MICROS_SCHEDULER_PRIORITY_COUNT]
```

Each thread owns one generation-safe `ready_next` handle, one
`ready_linked` bit, and its exact assigned hart. The queues are singly linked
to reproduce the MINIX mechanism. Queue operations allocate no memory.

Tail enqueue:

1. requires an exact runnable attached thread with null link metadata;
2. appends it to its priority queue;
3. records exact generation-safe reachability;
4. if it outranks the current preemptible thread on the same hart, sets
   `PREEMPTED` on that current thread, which removes the current thread from
   its queue.

Head enqueue is used only to restore an early-preempted thread with remaining
quantum.

Dequeue searches the exact priority queue, repairs head and tail, clears the
thread link, and rejects missing, duplicated, stale, cross-hart, or malformed
membership as an invariant error. The bounded scan is at most 128 thread
slots.

### Current-thread selection

`pick_ready` scans priorities from 0 through 15 and returns the first nonempty
head without dequeuing it.

Selecting a current thread:

- requires the exact returned queue head;
- leaves its runtime flags and queue link unchanged;
- writes the hart current handle;
- selects that thread's supervisor stack in the stable trap anchor.

When the selected thread remains runnable, it is both:

- current on exactly one hart; and
- reachable exactly once as the head of its priority queue.

This dual relationship is intentional MINIX baseline behavior, not duplicate
ownership.

Changing from one runnable current thread to another changes only hart current
selection and stack/root return state. Queue movement occurs because of a
separate blocking, preemption, or policy transition.

Clearing current ownership requires the exact current thread to be
non-runnable and absent from every ready queue. It restores the hart's idle
primary stack and clears the handle atomically.

### Portable API

The portable scheduler/object layer exposes operations equivalent to:

```c
micros_thread_scheduler_admit(
    objects, hart, thread, priority, quantum, preemptible
);
micros_thread_scheduler_hold(objects, thread);
micros_thread_scheduler_remove(objects, thread);
micros_thread_runtime_flags_set(objects, thread, flags);
micros_thread_runtime_flags_unset(objects, thread, flags);
micros_thread_install_policy(objects, thread, priority, quantum);
micros_hart_pick_ready(objects, hart, thread);
micros_hart_plan_user_return(objects, hart, plan);
micros_hart_select_current(objects, hart, thread);
micros_hart_clear_current(objects, hart, thread);
```

The exact declarations may group preflight data into bounded plan structures,
but they preserve these authority and failure-atomicity boundaries.

`install_policy` is failure-atomic. For a runnable thread it preflights,
temporarily applies the logical `NO_QUANTUM` dequeue in its plan, installs the
new priority and quantum, clears `NO_QUANTUM`, and reenqueues only after every
check has succeeded. No caller can observe local policy differing from queue
placement. Standalone policy installation rejects the exact current thread;
current policy renewal is part of the return-selection plan so requeue and
current replacement are indivisible.

`scheduler_hold` sets `INACTIVE` through the canonical flag transition and
retains valid policy for wakeup. `scheduler_remove` requires the held thread
not to be current and clears assigned hart, priority, quantum, remaining time,
preemptibility, and link metadata. Context detach and object release require
that removed state.

### Registry invariants

Full validation additionally requires:

- every live thread has only defined run-time flag bits;
- `runtime_flags == 0` if and only if the thread is reachable exactly once in
  its assigned hart's ready queues;
- every ready handle resolves the exact live thread generation;
- every reachable thread records the walked hart and queue priority;
- every head/tail pair is both null or both non-null;
- every tail has null `ready_next`;
- every forward walk terminates within thread capacity;
- no thread is reachable from two queues or harts;
- free and quarantined slots have zero scheduler metadata;
- `INACTIVE` threads are not queued, but may retain valid assigned policy until
  scheduler removal;
- every queued or current thread has an attached context and valid nonzero
  policy;
- a runnable current thread is the head of its own priority queue;
- a non-runnable current thread is absent from all queues;
- one thread is current on at most one hart;
- a hart with current selects that exact thread stack;
- a hart without current selects its immutable idle stack;
- accounting ownership and current-thread identity agree.

Validation detects corruption and never repairs it.

### Accounting ownership

Each hart owns:

```text
accounting_owner = NONE | KERNEL | THREAD | IDLE
accounting_started_at
accounted_thread
kernel_counter_ticks
idle_counter_ticks
```

The platform reads the existing RV64 `time` CSR. Unsigned subtraction computes
the elapsed interval; a delta greater than or equal to remaining quantum
saturates remaining time to zero.

Scheduler initialization begins with `KERNEL` accounting and the current
counter value.

On every U-origin trap:

1. perform only the minimal wired frame, hart, current-handle, and
   accounting-owner routing needed to identify the interrupted thread;
2. require `accounting_owner == THREAD` and the exact same accounted handle;
3. read `time`;
4. subtract only that elapsed thread interval from its remaining quantum;
5. switch accounting owner to `KERNEL` at the same timestamp;
6. only then copy, inspect, or validate the saved user context and dispatch
   the trap-specific C handler.

Before returning to U-mode:

1. close and accumulate the `KERNEL` interval;
2. select the exact return thread as accounting owner;
3. store the new counter timestamp;
4. commit the selected context and execute the ordinary trap epilogue or
   direct first-entry assembly.

Kernel time is not deducted from a user thread's quantum. The short
architecture restore interval after the ownership switch is charged with the
following user interval, matching MINIX's boundary placement.

An S-origin trap while kernel accounting is active leaves that owner
unchanged. An S-origin trap from the idle interrupt-enable window performs the
same minimal route, reads `time`, closes `IDLE`, and starts `KERNEL` before
timer dispatch or other C work. After the mandatory adjacent SIE-clear
instruction executes, selection either closes `KERNEL` and restarts `IDLE`
before sleeping or closes `KERNEL` and starts the selected thread before user
entry.

All portable accounting arithmetic is also exposed through an injected
counter-value helper for deterministic native tests. Target code uses only
`riscv_read_time()`.

### Timer role

ADR-0016's OpenSBI TIME mechanism remains unchanged:

- absolute deadline programming;
- stale-pending rejection;
- accepted-expiration count;
- rearm from current time;
- explicit inactive, overflow, and programming failures.

An accepted timer expiration is a bounded scheduling check, not an entire
quantum. After U-origin accounting closes the thread interval:

- if remaining quantum is nonzero, the return selector may keep the current
  thread;
- if remaining quantum is zero, policy handling runs before selection.

The timer's periodic interval and each thread's configured quantum are separate
nonzero values. A quantum may span one or more accepted timer expirations.

Timer programming failure during scheduler start preserves the complete
thread, queue, hart, stack anchor, accounting, root, and timer state.

### Bootstrap scheduling policy

User SCHED is not dependency-ready before IPC. The initial kernel policy is an
explicit staged substitution:

- every bootstrap user thread uses a fixed configured priority;
- quantum exhaustion logically sets `NO_QUANTUM` and dequeues the current
  thread;
- the kernel synchronously renews the same priority and configured quantum;
- clearing `NO_QUANTUM` appends the thread at its priority tail;
- the kernel then selects the highest-priority queue head.

With equal priorities this is round robin. It is not described as MINIX's
kernel-scheduled fallback, which renews in place, and it does not partially
copy user SCHED's demotion without its periodic promotion.

The bootstrap policy implements neither dynamic demotion nor five-second
priority restoration. Both arrive together with the dependency-ready SCHED
server.

### Early priority preemption

When a higher-priority thread becomes runnable on the same hart:

1. enqueue it at its priority tail;
2. set `PREEMPTED` on the current preemptible thread;
3. automatically dequeue that current thread;
4. at the common return selector, clear `PREEMPTED`;
5. restore it to its old priority head when remaining quantum is nonzero;
6. select the highest-priority queue head.

The preempted thread retains its complete remaining quantum. A same- or
lower-priority enqueue does not preempt it.

The one-hart target performs the operation with SIE clear. Cross-hart wakeup
and scheduling IPIs are deferred.

### Common reschedulable user-return path

Every U-origin path that may return to user mode uses one target sequence:

1. route the exact hart and verify the frame lies on its current thread stack;
2. stop and charge the outgoing thread accounting interval after only minimal
   wired routing;
3. capture and validate the outgoing context before scheduler mutation;
4. dispatch the trap-specific mechanism or policy handler;
5. process early-preemption repair;
6. keep the current thread as the candidate if it remains runnable, otherwise
   choose the highest-priority ready head;
7. if the candidate has zero remaining quantum, apply its no-quantum policy
   plan and restart candidate selection;
8. choose idle if no runnable candidate exists;
9. preflight the complete selected context, process root, stack, queue/current
   relation, return frame storage, and accounting transition;
10. commit all planned flag, queue, policy, current, and stack changes with no
   recoverable operation;
11. install the selected user context and `satp`, start selected-thread
    accounting, and return through the ordinary epilogue.

Timer, future IPC, future VM-resume, notification, and future syscall paths do
not own separate final switch sequences. A handler may update flags or saved
state, but only this selector chooses the return thread.

The selector may keep the same thread. That still closes kernel accounting,
revalidates the return context, and restarts its thread interval.

The selection loop is bounded by ready-thread count because bootstrap renewal
installs a positive quantum before tail enqueue. A later external SCHED policy
may leave a no-quantum thread blocked, removing it from subsequent scans.

### Switch planning and commit

The target uses bounded wired plan storage. Preflight:

- verifies the outgoing frame and freshly captured context;
- plans, but does not yet apply, PREEMPTED repair and bootstrap no-quantum
  movement when those transitions determine the next head;
- identifies the exact resulting ready head;
- validates the selected thread's saved status, executable PC, writable stack,
  process root, assigned hart, priority, queue position, and kernel stack;
- snapshots the selected context and expected `satp`;
- prepares exact queue/head/tail/current/accounting values.

Only after preflight succeeds does commit:

- apply the precomputed runtime-flag and queue mutations, including any
  zero-remaining PREEMPTED tail insertion;
- change current selection and trap-anchor stack;
- copy the selected context into the still-wired outgoing frame, or enter it
  directly from idle/start;
- activate the prevalidated process root through the existing complete-fence
  sequence;
- install the accounting owner and timestamp.

No callback, allocation, tree walk, policy lookup, owner lookup, or recoverable
check occurs after the first commit mutation. An impossible `satp` readback
mismatch is fatal.

The outgoing frame remains valid through `sret` even when the outgoing thread
has moved to a queue tail because its static supervisor stack remains attached
and wired.

### First entry

The target API is equivalent to:

```c
micros_scheduler_initialize(preemption_interval);
micros_scheduler_admit(thread, priority, quantum);
micros_scheduler_start();
```

Initialization requires:

- one registered boot hart;
- no current thread;
- empty ready queues;
- SIE clear;
- a nonzero timer interval;
- successful timer initialization;
- `KERNEL` accounting start.

Admission resolves one exact prepared `INACTIVE` production thread, validates
its process root, context, and stack, installs fixed bootstrap policy, clears
`INACTIVE`, and enqueues it.

Start requires at least one ready thread. It:

1. picks and preflights the exact highest-priority head;
2. starts the timer before changing current or queues;
3. commits current selection, thread-stack selection, process-root activation,
   and thread accounting;
4. restores the saved context and executes `sret`.

Timer-start rejection or SBI failure returns an explicit scheduler error with
all scheduler and execution state unchanged. Success does not return.

The former production `micros_user_execution_enter` authority is removed.
The isolated user-execution component test may use a test-only no-timer entry
wrapper, but that wrapper still uses the production admission, queue-head,
current-selection, stack, root, and accounting commit. It does not create an
unqueued runnable current thread. Production user entry is scheduler-owned.

ADR-0022's test-only supervisor-return cleanup is replaced. On the final
U-origin test trap, the handler:

1. captures the context and closes thread accounting through the common entry;
2. sets `INACTIVE`, which dequeues the exact current thread;
3. activates the kernel root;
4. restores the hart idle-primary anchor;
5. clears the now non-runnable, off-queue current handle;
6. selects the validated S-mode continuation in the live outgoing frame;
7. closes kernel accounting for that continuation and returns through the
   ordinary epilogue, which rearms `sscratch`.

The S-mode continuation then removes scheduler metadata before detaching and
releasing the thread. It cannot directly clear a runnable queued current
thread as ADR-0022 previously allowed.

### S-origin timer handling

An S-origin timer frame is never replaced with a user context.

The handler:

1. updates/rearms the timer mechanism;
2. leaves thread quantum unchanged unless thread accounting was active, which
   is an invariant violation for a supervisor-origin frame;
3. records `reschedule_pending` when a scheduling check is needed;
4. returns through the unchanged supervisor frame.

For an idle-origin timer, `sret` restores the interrupt-enable state needed to
resume the assembly wait helper, and the immediately adjacent instruction
clears SIE before any C selection logic. For any other S-origin continuation,
the interrupted supervisor state is resumed without selecting a user context.
The next common U-return selector or idle-loop selection consumes the pending
check. No API switches from an arbitrary S-origin continuation.

### Idle transition

If no ready head exists after a captured U-origin thread becomes non-runnable,
the selector performs a non-returning idle commit:

1. require the outgoing context already captured and attached;
2. activate the kernel root;
3. restore the hart idle-primary trap stack;
4. clear the exact non-runnable current handle;
5. close kernel accounting and start `IDLE`;
6. with SIE clear, write the validated hart-anchor address back to `sscratch`;
7. use a small audited assembly pivot to install the idle stack;
8. enter the race-free assembly wait helper.

The pivot cannot use the ordinary trap epilogue because it abandons the
outgoing U-origin frame. Rearming `sscratch` is therefore mandatory before the
next interrupt; otherwise the next trap would be misclassified as nested.

The idle wait helper uses the same race-free `wfi`; set SIE; clear SIE window
already established by the timer test. An interrupt is taken between the
adjacent set and clear instructions. Trap entry transitions `IDLE` to
`KERNEL` before dispatch. After `sret`, the clear instruction executes before
the helper considers calling C selection code.

RISC-V permits `wfi` to return without a trap. After the adjacent SIE clear,
the assembly helper reads the hart-local accounting owner directly through
the validated `tp` hart pointer:

- if ownership remains `IDLE`, no trap occurred and the helper repeats the
  assembly wait without entering C;
- only `KERNEL` ownership proves that trap entry closed the idle interval, so
  only then may the helper call C selection.

The accounting-owner offset is part of the existing C/assembly layout contract
and is verified with static assertions. With SIE clear, selection then:

- checks the ready queues with SIE clear;
- if all are empty, closes the kernel interval, restarts `IDLE`, and repeats
  the assembly wait window;
- otherwise preflights the highest-priority head, closes kernel accounting,
  selects its stack/root/context, starts thread accounting, and enters U-mode.

The idle object is hart-local kernel mechanism, not a process or thread slot.
It owns no endpoint or user context.

### Invalid bootstrap contexts

This slice runs trusted embedded test payloads before PM and process-fault IPC.
An invalid freshly captured outgoing context or selected ready context:

- is detected before the return-plan commit;
- causes trap-aware panic `invalid-bootstrap-user-context`;
- never returns through the invalid state.

When no earlier legitimate event changed scheduler ownership, the panic test
requires queue heads/tails, runtime flags, current handle, anchor, root, and
accounting owner to match the preflight snapshot.

Before untrusted applications run, a later fault ADR replaces this fatal
policy with an atomic fault/block transition and PM notification.

### Future SCHED server boundary

The dependency-ready SCHED server reproduces MINIX policy authority:

- the kernel reports exact no-quantum events and accounting;
- the authorized server supplies priority, quantum, CPU, and niceness policy;
- the kernel validates and installs those fields;
- the kernel continues to own runtime flags, ready links, queue selection,
  current ownership, context validation, root activation, and final restore;
- the server does not submit an exact next-thread decision.

Because `micros` separates process and thread identity, the later ABI must name
one exact generation-safe schedulable thread without treating the process
endpoint as permanent thread identity. That ABI is deferred to the SCHED task.

The later policy includes both:

- quantum-exhaustion priority demotion; and
- periodic promotion toward each thread's configured maximum priority.

It also corrects three MINIX failure-atomicity defects:

- scheduler takeover and first policy installation are transactional;
- failed demotion preserves the previous local and kernel policy;
- failed periodic promotion preserves the previous local and kernel policy.

No policy transition reports success while local and kernel-installed state
disagree.

### Serialization and SMP boundary

Portable operations require external serialization. The one-hart target holds
SIE clear for:

- runtime-flag and queue transitions;
- selection planning and commit;
- accounting-owner transitions;
- timer/policy coupling;
- idle/current changes.

This is not an SMP implementation. A later multi-hart ADR must add:

- locks or another shared-object serialization rule;
- scheduling IPIs;
- remote current-thread stop and complete-context capture;
- FPU/vector ownership;
- migration and affinity;
- remote TLB state;
- idle-hart wakeup.

Per-hart queues and typed handles prevent an identity redesign but do not by
themselves make the scheduler SMP-safe.

## Test-first evidence

### Native scheduler model

Native tests compile production portable queue, flag, selection, policy, and
accounting logic under ASan and UBSan.

Deterministic cases cover:

- all 16 priority bounds, including rejection of priority 16;
- empty, singleton, and multi-thread queues;
- current remaining queue-reachable at the exact priority head;
- tail enqueue and bounded singly linked dequeue from head, middle, and tail;
- stale head, tail, next, assigned-hart, and current generations;
- duplicate and cross-hart queue membership;
- independent flag set/unset combinations and zero-boundary enqueue/dequeue;
- standalone final-flag clear rejection for the current thread with a
  same-priority peer, plus atomic clear/enqueue/current replacement through
  the return plan;
- ordinary clear rejection for `PREEMPTED`;
- higher-priority early preemption and head restoration with unchanged
  remaining quantum;
- same/lower-priority enqueue without preemption;
- bootstrap no-quantum removal, renewal, tail enqueue, and next selection;
- policy replacement while runnable and while independently blocked;
- standalone current-policy replacement rejection and atomic current renewal
  with a same-priority peer;
- process-accounting saturation, exact deltas, counter wrap subtraction, and
  kernel/idle separation;
- a bounded injected-counter scenario proving that context copy, mapping
  validation, timer dispatch, and selection work after the entry timestamp
  increase only kernel accounting;
- a portable accounting-owner model case for a spurious idle indication;
- zero-remaining PREEMPTED repair behind a same-priority peer, proving the
  repair and replacement-current commit are indivisible;
- current runnable, current blocked, no-current idle, and queue corruption;
- output and complete-state preservation on every failed operation;
- one and two harts with several threads per process in model policy.

A replayable seeded model uses mixed high-bit operation selection and at least
4,096 transitions. It compares:

- runtime flags;
- priority and quantum;
- assigned hart;
- every queue head/tail/next link;
- current handles;
- accounting owner, timestamp, and remaining quantum;
- expected operation result.

It prints the seed, complete failing operation, and state snapshot. Coverage
counters require every core transition class to execute.

### QEMU scheduler component

One normal scheduler image uses two processes and two prepared threads with
distinct address spaces, counters, stacks, and register patterns.

It proves:

1. timer initialization and accounting initialization leave SIE clear;
2. one injected SBI TIME programming failure makes scheduler start return with
   byte-exact queue, current, stack, root, timer, and accounting preservation;
3. successful start selects the highest-priority queue head without dequeuing
   it;
4. an early test-only user environment call charges a nonzero process interval
   before any accepted timer expiration, keeps the same current thread, and
   accounts the kernel interval separately;
5. real timer interrupts eventually exhaust each configured quantum;
6. equal-priority expiration moves the exhausted thread to the tail and
   produces repeated A/B alternation for at least six committed switches;
7. at every user return, current is reachable exactly once at its priority
   head and the selected `satp` and thread stack match;
8. every x1-x31 value and each private counter survives repeated switching;
9. a test-only transition makes all threads inactive, causing a real
   user-to-idle stack/root/accounting transition;
10. a QEMU-only assembly hook forces exactly one idle-helper iteration to
    bypass `wfi` without taking a trap, proves accounting remains `IDLE`, a
    C-selector entry counter remains unchanged, and the helper repeats its
    assembly wait;
11. the next real S-origin timer event changes accounting to `KERNEL`, makes
    one saved thread runnable, enters the C selector exactly once, and resumes
    U-mode at the expected continuation;
12. a final U-origin trap holds/dequeues both threads, activates the kernel
    root, restores the idle anchor, clears the exact non-runnable current, and
    returns through the ordinary epilogue to a test-only S continuation;
13. that continuation removes scheduler metadata, detaches both threads,
    destroys both roots, and restores object and ownership baselines.

Only then does it emit:

```text
MICROS_SCHEDULER_TEST_PASS queues=minix-priority current=reachable accounting=separate switches=alternating idle=resumed registers=preserved
```

The host gate requires exact readiness and pass records, clean SBI shutdown,
no panic or explicit failure, and no timeout.

### Isolated fatal-context images

Two expected-panic images use a real U-origin timer or test environment entry:

- invalid outgoing captured PC, SP, or status;
- invalid selected ready-thread PC, SP, or status.

Both require:

- reason `invalid-bootstrap-user-context`;
- a U-origin `MICROS_TRAP_CONTEXT`;
- scenario-specific diagnostics proving no return-plan commit;
- no scheduler pass record;
- clean SBI system-failure shutdown.

### Idle and timer failure boundaries

Test hooks exist only in isolated scheduler images:

- fail exactly the next SBI timer program and count attempts;
- bypass exactly one idle `wfi` without a trap and count C-selector entries;
- make one exact held thread runnable on the first accepted idle timer event;
- expose read-only scheduler snapshots for diagnostics.

Production has no failure bypass or synthetic success path.

## Consequences

- Scheduler state now follows the selected MINIX baseline before optimization.
- Process/thread/hart separation remains intact.
- Runnable state automatically stays synchronized with ready-queue membership.
- Current selection no longer implies removal from the ready queue.
- Timer interrupts bound observation but do not define quantum consumption.
- One final U-return selector becomes the integration point for future IPC,
  VM-resume, faults, notifications, and syscalls.
- The real idle path prevents later blocking IPC from replacing the stack/root
  transition model.
- Sixteen queues and singly linked removal are more work than one FIFO but
  avoid another scheduler representation change when SCHED arrives.
- Per-thread supervisor stacks remain a compatible extension even though MINIX
  uses per-CPU stacks.

## Alternatives considered

### Keep ADR-0024's separate `RUNNING` state and dequeue current

This is internally coherent but does not reproduce the user-selected MINIX
baseline and complicates direct comparison with later IPC run-time flags.

### Use one FIFO until user SCHED exists

This omits early priority preemption, head restoration, and the kernel
mechanism through which SCHED policy operates. Adding priorities later would
replace queue and selection invariants.

### Treat each accepted timer event as one quantum

This charges kernel execution to the user and gives frequently trapping
threads different semantics from MINIX. Counter-delta accounting is directly
testable and uses the existing hardware time source.

### Implement user SCHED now

SCHED depends on generation-safe IPC, privilege, notification, and service
startup mechanisms that are not dependency-ready. The fixed-priority kernel
policy is an explicit staged substitution.

### Defer idle until IPC

Blocking IPC can remove the last runnable thread. Deferring idle would make the
IPC task invent a new root, stack, accounting, and direct-entry transition
while also implementing wait queues. The scheduler slice owns that mechanism.

### Reproduce MINIX scheduler defects exactly

Priority index overflow and non-atomic policy updates are known defects under
ADR-0025. Baseline-first reproduces intended behavior, not memory corruption
or success-shaped inconsistent state.

## Specification basis

- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0010: Testing and Observability](0010-testing-and-observability.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0016: Supervisor Timer Interrupts](0016-supervisor-timer-interrupts.md)
- [ADR-0019: Kernel Object Identity and Ownership](0019-kernel-object-identity-and-ownership.md)
- [ADR-0021: Generation-Safe User Address Spaces](0021-generation-safe-user-address-spaces.md)
- [ADR-0022: User Execution Contexts and U-Mode Entry](0022-user-execution-contexts-and-u-mode-entry.md)
- [ADR-0023: Canonical User Status Summary Bits](0023-canonical-user-status-summary-bits.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](0025-minix-behavioral-baseline-before-optimization.md)
- [MINIX scheduler and context-switch study](../research/minix-scheduler-and-context-switch.md)
- [MINIX baseline parity audit](../research/minix-baseline-parity-audit.md)
- [Development dependency DAG](../architecture/development-dag.md)

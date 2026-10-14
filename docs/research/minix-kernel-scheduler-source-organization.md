# MINIX Kernel Scheduler Source-Organization Baseline

## Scope and provenance

This is the responsibility trace for the behavior-preserving scheduler
correction in
[ADR-0062](../adr/0062-kernel-scheduler-source-decomposition.md), not a new
scheduler design. The full behavioral study remains
[MINIX Scheduler and Context-Switch Study](minix-scheduler-and-context-switch.md).

The fixed reference is MINIX commit
`4db99f4012570a577414fe2a43697b2f239b699e`. Primary-source inspection uses a
local checkout and read-only Git/file access; the links below bind the same
immutable revision. MINIX is evidence, not source to copy or a build
dependency.

The current `micros` source baseline is
`e5b4da6e2fd33f752cfdadc7beb77f0ebf9b025b`. Accepted ADRs
[0026](../adr/0026-minix-baseline-kernel-scheduler.md),
[0027](../adr/0027-return-boundary-timer-rearming.md), and
[0035](../adr/0035-riscv-ipc-syscall-and-bootstrap-buffers.md)
remain authoritative.

## Primary responsibility trace

| Fixed MINIX evidence | Responsibility and boundary |
| --- | --- |
| [`proc.h:15-120`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/proc.h#L15-L120) | Saved registers, scheduling, ready/IPC links, and deferred delivery are process fields; this does not require combining the distinct `micros` process, thread, endpoint, and hart objects. |
| [`proc.h:140-230`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/proc.h#L140-L230) | Zero RTS flags derive runnable state. First blocking set dequeues; final clear enqueues. Independent reasons are not a single RUNNING/BLOCKED enum. |
| [`proc.c:1593-1660`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/proc.c#L1593-L1660) | Tail enqueue owns queue links and higher-priority preemption of a preemptible current process. |
| [`proc.c:1662-1816`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/proc.c#L1662-L1816) | Early-preempted work with remaining time returns to the head; dequeue repairs arbitrary position/tail; priority pick returns a head without removing it. |
| [`proc.c:297-472`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/proc.c#L297-L472) | Common user return repairs preemption, selects or idles, completes deferred work/message delivery, handles exhaustion, accounts kernel time, prepares the timer, and restores selected context. |
| [`proc.c:241-295`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/proc.c#L241-L295) | Deferred message copy clears delivery/result state only after success; an unresolved copy can suspend through VM rather than silently complete. |
| [`arch/earm/arch_clock.c:71-153`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/arch/earm/arch_clock.c#L71-L153) | `context_stop` charges the elapsed process interval, saturates remaining quantum, and maintains a separate timestamp/accounting boundary. Pseudo kernel/idle tasks are not charged as user quantum. |
| [`proc.c:180-224`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/proc.c#L180-L224) | Idle changes address-space/accounting ownership and restarts the timer before waiting. |
| [`arch/i386/arch_clock.c:168-174`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/arch/i386/arch_clock.c#L168-L174), [`apic.c:574-579`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/arch/i386/apic.c#L574-L579) | Return-boundary restart delegates to the APIC and rearms only after expiration; ARM's empty restart is not evidence that SBI TIME may omit this boundary. |
| [`arch/earm/mpx.S:80-174`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/arch/earm/mpx.S#L80-L174) | User entry saves process context and converges on selected return; a kernel-origin IRQ restores its interrupted kernel continuation directly. |
| [`system.c:642-705`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/system.c#L642-L705), [`proc.c:1860-1908`](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/proc.c#L1860-L1908) | Kernel mechanism installs policy and preserves independent flags; externally scheduled exhaustion notifies SCHED, while kernel/non-preemptible fallback renews locally. |

MINIX keeps several of these responsibilities in one large `proc.c`.
The lesson is ownership and transition order, not a requirement to reproduce
that physical source arrangement.

## Difference classification

The classes are those of
[ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md).
This source-only task introduces no new behavioral divergence.

| Existing `micros` choice | Classification | Preservation obligation |
| --- | --- | --- |
| Sixteen per-hart queues, zero-flags runnable state, queue-reachable current, tail wake, and preempted-head repair | Baseline parity | Separate identity/queue/policy owners without changing links, selection, flags, or error precedence. |
| Elapsed thread charging on U-entry, separate kernel/idle accounting, and a common selected-user-return boundary | Baseline parity | Keep minimal accounting before expensive capture/validation and preserve counter-wrap/overflow behavior. |
| RISC-V frame/CSR restore, Sv39 root activation, OpenSBI TIME, and the idle SIE window | Required adaptation | Retain architecture bytes, IRQ state, fences, trap anchor, and expired-only timer preparation. |
| Distinct generation-checked process/thread/hart objects, exact stacks, complete validation and prevalidated non-failing commits | Compatible extension | Keep public layouts, whole-state validation, authority, and failure atomicity; no validation shortcut. |
| Reversible current IPC guard and retained one/two-page completion plan | Compatible extension | Preserve the accepted held-caller bridge, all-record preflight, deferred completion, and the same patched context image at store/install. |
| Fatal invalid trusted context or lost accepted buffer rather than VM-assisted retry | Staged substitution | Preserve the current explicit panic; later reviewed fault delivery owns replacement. Do not import MINIX VM suspension. |
| Immediate fixed-policy renewal/rotation before a user SCHED service exists | Staged substitution | Keep ADR-0026's existing bootstrap policy; later SCHED must add its documented takeover/demotion/restoration contract. |
| Strict priority upper bound and transactional rejection instead of MINIX's `priority == NR_SCHED_QUEUES` defect | Compatible extension | Preserve the already Accepted correction; never reproduce the known out-of-bounds validation defect. |
| Responsibility-specific source files and deterministic independent native/target evidence | Compatible extension | Expose existing work only; do not import a MINIX harness or add execution identities. |

The last two rows do not authorize new policy or tests. They classify the
already Accepted source/test development discipline and safety behavior.

## Derived source direction

Identity and queue primitives are below policy/IPC transitions. Whole-state
validation may inspect every object but calls base object validation, not the
recursive combined validator or a mutation handler. Planning owns its one
existing scratch array; checked commit recomputes the plan; prevalidated apply
performs bounded stores.

Target context/root/accounting preparation and completion primitives are
below start, normal/captured return, interrupt adaptation, and idle
orchestration. Completion and timer handling do not choose a thread.
Architecture assembly alone restores registers and pivots the idle stack.

Evidence has its own fixture, action, projection, and final-balance owners.
The two-hart model retains independent queue arrays and transition math; it
may call the existing public production APIs for the native side, never
production-private queue/planner helpers for its expected side.

The exact file inventories, state fields, source sets, required function
phases, metadata, and acceptance are defined once in the
[scheduler source contract](../architecture/kernel-scheduler-source-organization.md).
There is no new SCHED, VM retry, process fault, reset, recovery, SMP, or
directory-layout contract in this study.

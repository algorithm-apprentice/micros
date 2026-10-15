# MINIX Kernel Object Source-Organization Baseline

## Purpose and fixed reference

This study precedes the source-only correction in
[ADR-0068](../adr/0068-kernel-object-source-decomposition.md). The fixed,
read-only local MINIX reference is:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

The `micros` reviewed base is endpoint implementation #136,
`4eb1c4d68de3830b888a0d239a2874bd79792244`. The
[merged-foundation parity audit](minix-baseline-parity-audit.md),
[scheduler study](minix-scheduler-and-context-switch.md), and Accepted
object/scheduler ADRs remain the behavioral ledger. MINIX is evidence, not
a source or comment donor.

## Fixed source trace

| Responsibility | MINIX evidence | Boundary to retain |
| --- | --- | --- |
| Combined process representation | `minix/kernel/proc.h:22-137` | Saved registers, address-space state, endpoint, scheduling and IPC fields are distinguishable responsibilities even though MINIX stores them in one process record. |
| Runtime inhibition | `minix/kernel/proc.h:141-224` | Runnable state follows the complete runtime flag word; independent blocking reasons are not a mutually exclusive running-state enum. |
| Initial slots and CPU-local idle records | `minix/kernel/proc.c:119-158`, `minix/kernel/main.c:149-197` | Slot initialization, architecture reset, boot-image installation and permission to run are separate transitions. |
| Endpoint representation | `minix/include/minix/endpoint.h:10-69` | Slot, generation and special values form one identity boundary rather than memory or scheduling policy. |
| Exact live resolution | `minix/kernel/proc.c:1828-1857` | A valid process number and nonempty slot are insufficient without exact complete endpoint equality. |
| Exact-slot creation and generation | `minix/kernel/system/do_fork.c:41-72` | Validate the source and destination before preparing a new identity for the requested slot. MINIX's generation wrap is not the accepted `micros` quarantine rule. |
| Child state preparation | `minix/kernel/system/do_fork.c:74-130` | Register, accounting, privilege inhibition and address-space readiness are separately owned fields; no whole-record copy is imported. |
| Endpoint invalidation and dependent cleanup | `minix/kernel/system.c:540-607` | Stop communication and remove exact references before a slot can be reused; cancellation is not a record-shape validator's job. |
| Final slot cleanup | `minix/kernel/system/do_clear.c:29-68` | Address-space, IRQ, IPC, timers and execution reachability are handled before final free-slot publication. |
| Executable context installation | `minix/kernel/system/do_exec.c:27-57`, `minix/kernel/arch/i386/memory.c:719-732` | Lifecycle coordination and architecture register construction have different owners. |
| CPU-local current execution and queues | `minix/kernel/cpulocals.h:7-75`, `minix/kernel/proc.c:299-349` | Current execution, queues, address-space selection and accounting remain CPU-local, not one process-global current variable. |
| User context versus supervisor stack | `minix/kernel/arch/earm/mpx.S:48-101` | User registers are saved into the process record while kernel handling uses CPU-local supervisor state. The `micros` per-thread stack is an explicit extension. |

MINIX separates representation headers, common process/IPC mechanics,
CPU-local storage, privileged lifecycle handlers, and architecture helpers.
That is an organizational lesson, not the exact `micros` file list.

## End-to-end state and authority

Boot marks slots free, establishes their identity representation and
architecture state, then installs the boot image and its inhibition/policy.
A privileged creation operation selects an exact destination, establishes
the next identity, and prepares non-runnable child state. Resolution checks
the full identity before later operations.

Executable installation changes the saved context through architecture
mechanism. Selection reads CPU-local current/queue state and switches the
address space before returning to execution. Exit removes communication
references and dependent resources before final slot reuse.

`micros` preserves the ownership boundary through separate process, thread,
endpoint and hart objects. PM and VM already coordinate their accepted
lifecycles; object primitives do not take over policy, IPC cancellation,
mapping validation or instruction synchronization. The public combined
object validator delegates to the scheduler, which calls the lower object
base validator. Extraction must not reverse that direction.

## Classified differences

Each row is one ADR-0025 classification, not a new runtime decision.

| Difference | Classification | Required disposition |
| --- | --- | --- |
| Exact requested process slot in `create_at` | Baseline parity | Preserve exact-slot refusal rather than falling back to another free slot. |
| Lowest-free convenience allocation and reproducible native models | Compatible extension | Preserve existing deterministic order; do not present this helper as MINIX PM policy. |
| Separate process/thread/endpoint/hart objects | Compatible extension | Retain ADR-0011/0019 structural ownership and checked production limits; never embed one thread or one hart as the identity representation. |
| Nonzero typed handles, unsigned 12/20 process encoding, independent 32-bit thread/hart generations and terminal quarantine | Compatible extension | Preserve accepted stale protection and reserved-value exclusion rather than copying generation wrap or signed task-slot encoding. |
| Full identity resolution before resource operations | Baseline parity | Preserve exact generation and live-slot checks and unchanged outputs on failure. |
| RV64 integer context, Sv39 root and hart-anchor/trap mechanics | Required adaptation | Retain Accepted target layout, status, routing and fence contracts; portable object code does not acquire architecture policy. |
| Per-thread fixed supervisor stacks and explicit attach/detach lifetime | Compatible extension | Retain ADR-0022 geometry and disjointness. Do not claim these are MINIX per-CPU kernel stacks. |
| Explicit process/thread/root release prerequisites and failure-atomic stores | Compatible extension | Keep current safety boundaries; do not import whole `struct proc` copying or cross-subsystem cleanup into object release. |
| Runtime flags and hart-local current/queue ownership | Baseline parity | Follow Accepted ADR-0026/0030 rather than historical `RUNNING`-enum descriptions superseded by those decisions. |
| Portable complete base checks plus independent native/reference and real target evidence | Compatible extension | Preserve all existing checks, model draws, snapshots, output formats and target events without adding a new execution or cache. |
| External single-hart SIE-clear serialization and IRQ restoration | Required adaptation | Keep explicit callers and target IRQ lifetime, not implicit locking or a new hidden current object. |
| Responsibility-specific source owners and bounded immutable reconstruction | Compatible extension | Apply ADR-0051 without importing MINIX module granularity, recovery, SMP or userland breadth. |

No divergence requiring a behavior correction was identified for this
source-only task. Existing Accepted ADRs, including later scheduler,
endpoint, IPC and spawn refinements, remain authoritative. This study does
not authorize optimizing validation or extending a boot deadline.

## Derived source constraints

1. Identity and range geometry remain leaves; validation does not own
   lifecycle mutation.
2. The scheduler/object-base validation boundary remains one-way and keeps
   the existing private declaration.
3. One caller registry is authoritative. Representation helpers and source
   movement add no runtime registry or ambient getter.
4. Native cases and the existing seeded model share their original registry;
   reference mathematics does not import production private helpers.
5. Original static and automatic lifetimes are retained, including the
   automatic native case table and target's two static resources.
6. Whole-registry snapshot/reset macros keep evaluation order, and target
   failure returns through the original single IRQ restoration.

The exact design outcome is the
[kernel-object source contract](../architecture/kernel-object-source-organization.md).
Independent design review is clean; the separate Accepted design merge
still precedes implementation.

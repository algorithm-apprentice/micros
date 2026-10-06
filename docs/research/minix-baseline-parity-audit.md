# MINIX Baseline Parity Audit for Merged Kernel Foundations

## Purpose and scope

This audit compares every merged `micros` kernel foundation through first
U-mode execution with the corresponding MINIX 3 mechanism. It determines what
already reproduces the MINIX behavioral baseline, what must differ for
RISC-V64, which stronger mechanisms are compatible, and which decisions must
be corrected before development continues.

The audited `micros` baseline is `origin/main` commit
`761b8ecde6dc4e9dfc0586cee90125c1c8061498`. Scheduler production code is not
merged and is assessed separately as proposed work.

The MINIX reference is commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

This is a behavioral and authority audit. It does not authorize copying MINIX
source.

## Classification

| Classification | Meaning |
| --- | --- |
| Baseline parity | The MINIX behavior and authority boundary are reproduced |
| Required adaptation | RISC-V, firmware, page tables, toolchain, or another Accepted platform constraint requires a different mechanism |
| Compatible extension | An Accepted safety, structural, or development extension preserves baseline behavior |
| Staged substitution | A dependency-ready temporary owner has an explicit later handoff |
| Divergence requiring correction | An unrequired behavioral or authority difference must be superseded |

A summary row may list more than one classification when it contains separate
atomic differences, such as baseline-equivalent ownership plus a required
architecture mechanism. Each individual difference receives exactly one
classification.

## Result

No merged production code needs to be rolled back. Existing differences are
required adaptations, staged bootstrap substitutions, or compatible
extensions.

Two accepted design statements must be corrected before scheduler production
work:

1. an accepted supervisor timer expiration is not itself one consumed MINIX
   quantum; MINIX charges process execution on entry and accounts the kernel
   interval separately;
2. ADR-0024's separate `RUNNING` state, single FIFO, timer-specific switch
   path, and no-priority policy do not reproduce MINIX scheduling.

The scheduler requires a superseding ADR and replacement Red gate. The dirty
uncommitted scheduler scaffolding is not baseline evidence.

## Summary matrix

| Merged subsystem | MINIX baseline relationship | Classifications | Required action |
| --- | --- | --- | --- |
| Toolchain and target | MINIX targets x86/ARM; `micros` targets RISC-V64 | Required adaptation | Keep |
| Boot memory discovery | Same exclusion behavior through different firmware inputs | Baseline parity; Required adaptation | Keep |
| Early serial and panic | Same fatal-output purpose with structured records | Baseline parity; Compatible extension (safety) | Keep |
| Trap entry | Same user-context ownership through RISC-V entry mechanics | Baseline parity; Required adaptation | Keep |
| Nested trap handling | `micros` adds a hart emergency stack | Compatible extension (safety) | Keep |
| Supervisor timer | Same forced-entry purpose through SBI TIME | Baseline parity; Required adaptation | Keep mechanism; supersede quantum interpretation |
| Bootstrap physical memory | Same temporary kernel authority before VM | Baseline parity; Staged substitution | Keep; complete tested VM handoff later |
| Typed frame ownership | No equivalent bootstrap MINIX ledger | Compatible extension (safety) | Keep |
| Kernel page tables | Same bootstrap and shared-kernel purpose through Sv39 | Baseline parity; Required adaptation | Keep |
| Process address spaces | VM is final policy owner; kernel currently substitutes | Baseline parity; Staged substitution; Compatible extension (safety) | Keep bootstrap API; revoke it at VM handoff |
| Endpoint generations | Same stale-slot rejection with quarantine | Baseline parity; Compatible extension (safety) | Keep |
| Process/thread/hart split | Splits MINIX `struct proc` and CPU locals | Compatible extension (structural) | Keep one-thread mapping; put MINIX scheduling state on thread |
| Saved user context | Same execution-state ownership through RV64 layout | Baseline parity; Required adaptation | Keep |
| Kernel stacks | MINIX per-CPU versus `micros` per-thread | Compatible extension (structural) | Keep, but do not claim it is MINIX behavior |
| U-mode entry and restore | Same root/context selection through RISC-V return | Baseline parity; Required adaptation | Keep |
| Native/QEMU observability | Stronger deterministic development evidence | Compatible extension (development) | Keep |
| Proposed scheduler | Differs from MINIX queue, quantum, selection, and policy semantics | Divergence requiring correction | ADR-0024 is superseded; replace before implementation |

## Platform entry and boot memory

### MINIX baseline

MINIX receives a Multiboot memory map and boot modules, copies the information
into kernel-owned boot data, keeps only available memory, and removes every
kernel and module range before bootstrap allocation
(`minix/kernel/arch/i386/pre_init.c:87-207`). The ARM port synthesizes the same
Multiboot-shaped contract from board boot information
(`minix/kernel/arch/earm/pre_init.c:188-340`).

The kernel constructs bootstrap page tables, specially maps and loads VM, then
returns the temporary bootstrap range to the memory map
(`minix/kernel/arch/i386/protect.c:320-465`,
`minix/kernel/main.c:115-323`). Before the first user return, it disables
further kernel boot allocation because VM is about to own memory policy
(`minix/kernel/main.c:65-108`).

### `micros`

OpenSBI supplies the boot hart and FDT. `kernel/fdt.c`,
`kernel/bootstrap_memory.c`, and ADRs 0001 and 0017 validate all described
memory and reservations, reserve the kernel image and firmware ranges, and
derive managed frames. `kernel/address_space.c` constructs the bootstrap Sv39
root before U-mode execution.

### Assessment

FDT, OpenSBI, SBI reset, UART addresses, and Sv39 are required platform
adaptations. The authority sequence matches MINIX: firmware description,
kernel bootstrap allocation, then a later VM owner. The static launcher and
VM are not dependency-ready, so the current kernel allocator is a staged
substitution rather than a permanent divergence.

No correction is required in merged code. The future VM task must complete the
already documented one-way revocation instead of leaving kernel user-memory
policy active.

## Early console and panic

### MINIX baseline

MINIX keeps a kernel message buffer and optional direct serial output. `panic`
guards against recursive panic, prints the fatal report and CPU stack trace,
and invokes shutdown
(`minix/kernel/utility.c:22-85`).

### `micros`

ADRs 0014 and 0015 plus `kernel/panic.c` and
`arch/riscv64/panic.S` provide fixed machine-readable records, direct and
trap-origin context, interrupt disable, panic-time UART seizure, and SBI
system-failure shutdown.

### Assessment

The same baseline purpose is present, while deterministic fields and
trap-context separation are compatible safety and observability extensions.
They do not alter process or service semantics and should remain.

## Trap entry and return

### MINIX baseline

ARM exceptions and user-origin IRQs save the complete user register frame into
the current `struct proc`, run the handler on a per-CPU supervisor stack, and
enter the common reschedulable return path
(`minix/kernel/arch/earm/mpx.S:45-238`).

x86 full interrupts, soft interrupts, and exceptions similarly save process
state and ordinarily enter `switch_to_user`
(`minix/kernel/arch/i386/mpx.S:52-145,262-373`). Its optimized
`SYSENTER`/`SYSCALL` ABI lets user stubs preserve most registers, which is an
optimization rather than a required scheduler representation
(`minix/kernel/arch/i386/usermapped_glo_ipc.S:29-78`,
`minix/kernel/arch/i386/mpx.S:200-260,375-432`).

Kernel-origin IRQs return to the interrupted kernel continuation without
selecting another process and force interrupts masked on return.

### `micros`

ADRs 0015, 0019, 0021, 0022, and 0023 plus
`arch/riscv64/trap.S` and `kernel/trap.c` save all integer registers and trap
CSRs, route through the exact hart, clear live SUM, capture U-origin state into
the current thread, and restore through one mutable frame.

### Assessment

The architecture mechanics are a required adaptation. Full save on every path
is a conservative baseline-compatible choice. Hart anchors, explicit
trap-frame validation, and a separate emergency stack are safety extensions.

The scheduler must preserve the MINIX distinction:

- U-origin frames may reach the common reschedulable return selector;
- S-origin frames resume the kernel continuation and are not replaced.

Merged trap code has not yet violated this rule because repeated scheduling is
not implemented.

## Timer and CPU-time accounting

### MINIX baseline

The clock interrupt updates tick accounting, virtual timers, load, and clock
events (`minix/kernel/clock.c:70-172`). It is not the sole quantum-accounting
path.

`context_stop(process)` charges the elapsed process-execution delta when a
user process enters the kernel and subtracts it from `p_cpu_time_left`
(`minix/kernel/arch/i386/arch_clock.c:208-330`,
`minix/kernel/arch/earm/arch_clock.c:71-143`). Before user return,
`context_stop(KERNEL)` accounts the kernel interval separately and resets the
counter baseline (`minix/kernel/proc.c:437-440`). The periodic timer guarantees
bounded entry so an exhausted quantum is eventually observed.

`switch_to_user()` handles zero remaining time only at the common scheduling
safe point (`minix/kernel/proc.c:297-458`).

### `micros`

ADR-0016 and `kernel/timer.c` implement OpenSBI TIME deadlines, stale-pending
rejection, accepted-expiration counting, rearm, and explicit failure handling.
There is not yet a production scheduler or remaining-quantum field.

### Assessment

The timer mechanism is a valid required adaptation and does not need rollback.
The ADR statement that accepted timer expirations represent scheduling quanta
is not MINIX baseline behavior. A superseding scheduler ADR must treat the
timer as bounded forced entry, charge elapsed thread execution on U-origin
entry, and account the kernel interval separately before return.

## Bootstrap physical memory and VM authority

### MINIX baseline

During bootstrap, MINIX mutates the available memory map and allocates pages
for kernel and VM startup
(`minix/kernel/arch/i386/pg_utils.c:28-180`,
`minix/kernel/arch/earm/pg_utils.c:25-170`). It then disables further kernel
boot allocation.

VM obtains the remaining chunks, initializes the physical-page allocator,
creates process page tables and regions, and owns normal mapping policy
(`minix/servers/vm/main.c:430-500`,
`minix/servers/vm/alloc.c:240-480`).

### `micros`

ADRs 0017 and 0020 plus `kernel/frame_allocator.c`,
`kernel/frame_ownership.c`, and `kernel/bootstrap_memory.c` separate physical
availability from typed semantic ownership, bind exact allocator geometry,
carry process generations, and stage an irreversible VM handoff.

### Assessment

The kernel-to-VM authority boundary is baseline parity. The bounded bitmap,
dense owner ledger, failure-atomic release sets, and one-way classification
commit are compatible extensions with the safety subtype. They strengthen
provenance without changing who ultimately owns memory policy.

The exact MINIX allocation scan order is not an observable subsystem contract;
`micros` may retain deterministic lowest-frame bootstrap allocation.

## Kernel and process page tables

### MINIX baseline

MINIX creates a bootstrap architecture page table and maps the kernel
(`minix/kernel/arch/i386/pg_utils.c:180-260`,
`minix/kernel/arch/earm/pg_utils.c:170-250`).

VM later creates each process root, maps the kernel into it, and binds the root
to the kernel process slot
(`minix/servers/vm/pagetable.c:988-1065,1358-1422,1440-1510`). The kernel
installs CR3 or TTBR through `SYS_VMCTL` and performs final address-space
selection during scheduling
(`minix/kernel/system/do_vmctl.c:15-180`,
`minix/kernel/arch/i386/arch_do_vmctl.c:19-60`,
`minix/kernel/proc.c:297-365`).

### `micros`

ADRs 0018 and 0021 plus `kernel/address_space.c`,
`kernel/user_address_space.c`, and `arch/riscv64/mmu.S` create exact Sv39
roots, share immutable supervisor subtrees, own private user tables, validate
typed reachability, reject active-root mutation, and use full ASID-zero
invalidation.

### Assessment

Sv39 layout, fences, page sizes, permission encoding, and the accepted private
root-index-1 platform layout are required adaptations. Exact generation-bound
ownership, W^X, complete-page zeroing, alias/orphan checks, and
failure-atomic teardown are compatible extensions with the safety subtype.

The current bootstrap kernel chooses anonymous mappings only because VM is not
dependency-ready. That is a staged substitution. The future VM interface must
own mapping policy while the kernel retains validated PTE mechanism and root
activation, matching MINIX's authority boundary.

## Process identity, endpoints, threads, and harts

### MINIX baseline

MINIX `struct proc` combines:

- saved architecture context;
- run-time blocking flags;
- scheduling metadata and ready link;
- IPC state;
- generation-aware endpoint;
- address-space root;
- accounting and deferred work.

The endpoint packs a process slot and generation
(`minix/include/minix/endpoint.h:8-70`). CPU-local storage owns current
process, billing process, loaded page-table owner, ready queues, idle state,
and FPU owner (`minix/kernel/cpulocals.h:37-75`).

### `micros`

ADRs 0011 and 0019 plus `kernel/kernel_objects.c` and
`kernel/kernel_object_runtime.c` split process, thread, endpoint-ready process
identity, and hart state. Process and thread handles carry exact generations;
the one-thread and one-hart limits are policies rather than layout
equivalences.

### Assessment

For v0.1, one MINIX `struct proc` maps to one `micros` process plus its sole
thread, while CPU-local fields map to the sole hart. This is a compatible
structural extension required by the accepted future-multithreading goal.

Generation quarantine is stronger than MINIX's wrap-to-one behavior and should
remain. It prevents stale identity resurrection without changing valid
endpoint behavior.

MINIX scheduler and IPC run-time flags belong on the `micros` thread because
that is the schedulable and blockable object. Address-space, endpoint, grant,
and privilege ownership remains on the process.

## User contexts and kernel stacks

### MINIX baseline

Architecture user state lives at the start of `struct proc`
(`minix/kernel/proc.h:15-35`). Architecture restore selects that frame after
the scheduler chooses the process.

MINIX uses per-CPU kernel stacks. x86 TSS and ARM SVC setup select the CPU
stack, and the selected `struct proc` pointer is placed at its top before user
return
(`minix/kernel/arch/i386/arch_system.c:480-520`,
`minix/kernel/arch/earm/arch_system.c:140-170`,
`minix/kernel/arch/i386/include/arch_proto.h:228-233`,
`minix/kernel/arch/earm/include/arch_proto.h:49-54`). A blocked process does
not retain a private in-kernel C continuation.

### `micros`

ADRs 0022 and 0023 plus `kernel/user_execution.c` and
`arch/riscv64/user_entry.S` store the RISC-V user context on the thread,
validate PC/SP/status and mappings, select the process root, and restore all
integer registers.

Each thread slot also owns a static 16 KiB supervisor stack selected through
the hart trap anchor.

### Assessment

The saved-context representation and RISC-V return sequence are required
adaptations that preserve baseline behavior.

Per-thread kernel stacks are not MINIX behavior. They are an already merged
compatible extension with the structural subtype: they preserve a stable wired
trap frame for each thread and do not expose an ABI. They should not be
removed merely for structural resemblance, but future code must not assume
resumable blocking kernel continuations unless a later ADR introduces them.

## Testing and observability

The deterministic native models, target component images, machine-readable
records, generation-corruption cases, and exact failure-atomic snapshots have
no direct MINIX runtime counterpart. They are development and safety
extensions required by ADRs 0010 and 0013.

They remain mandatory. Baseline-first changes what behavior the tests specify,
not the requirement for independent native and QEMU evidence.

## Proposed scheduler assessment

ADR-0024 and the uncommitted Red scaffolding differ materially from MINIX:

| Topic | MINIX baseline | ADR-0024 |
| --- | --- | --- |
| Runnable state | `p_rts_flags == 0` | `INACTIVE/RUNNABLE/RUNNING` enum |
| Current membership | Current remains ready-queue reachable | Current removed from FIFO |
| Queue shape | Per-CPU array of 16 priority queues | One FIFO per hart |
| Selection | Highest-priority nonempty head | FIFO head |
| Early preemption | Remaining quantum; reinsert at head | Not represented |
| Quantum accounting | Process delta on entry; kernel interval separate | One accepted timer tick rotates |
| Expiration | `RTS_NO_QUANTUM`, then kernel/user scheduler renewal | Immediate autonomous tail rotation |
| Return path | Common reschedulable `switch_to_user()` | Timer-specific switch entry plus separate safe point |
| External policy | Priority/quantum/CPU/niceness; kernel selects | Future exact-next-thread or policy handoff |
| Priority recovery | Five-second promotion toward maximum priority | Not represented |
| Priority validation | Valid index is `< NR_SCHED_QUEUES` | No policy bound yet |
| Policy failure handling | MINIX has three non-atomic local/kernel update windows | Not specified |

MINIX `sched_proc()` itself contains an off-by-one check that admits
`priority == NR_SCHED_QUEUES`; that is a known validation defect and must be
rejected by `micros`.

MINIX SCHED also leaves local and kernel state inconsistent when initial
takeover scheduling, no-quantum demotion, or periodic promotion fails. Those
are known failure-atomicity defects. The later `micros` SCHED ADR must require
transactional takeover and rollback or commit-after-success for both policy
updates.

These are corrections, not optional refinements. ADR-0025 supersedes the
affected ADR-0016 interpretation and ADR-0024 in full. No production scheduler
implementation may begin until a new scheduler ADR defines the replacement
and the Red gate is rewritten from the MINIX baseline.

## Forward rules

Before each later dependency-ready subsystem:

1. trace the MINIX path end to end;
2. record mechanism, policy, ownership, transitions, and failure behavior;
3. classify every proposed difference;
4. accept required adaptations or safety corrections in an ADR;
5. write baseline-derived Red tests;
6. implement baseline behavior before optional optimization.

This applies next to scheduler, then endpoint/IPC blocking, grants, launcher,
VM, PM, TTY, filesystem services, and control-plane services in development
DAG order.

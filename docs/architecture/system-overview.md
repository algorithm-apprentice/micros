# System Overview

## Purpose

`micros` is an educational operating system that uses a small privileged
kernel and isolated user-space services. It independently reimplements the
verified behavioral architecture of MINIX 3 without attempting source
compatibility, binary compatibility, or a line-by-line translation.

The first complete product increment is a shell-capable system running under
QEMU. It should be small enough that one developer can trace a process from a
user request through IPC, memory management, filesystem lookup, and back.

## Design principles

1. Keep policy outside supervisor mode unless moving it would create an
   unresolved bootstrap cycle.
2. Introduce one new dependency layer at a time.
3. Prefer explicit protocols and ownership over shared writable state.
4. Make stale identities, invalid transitions, and ownership violations fail
   immediately.
5. Use the smallest mechanism that reproduces the dependency-ready MINIX
   baseline.
6. Defer compatibility, migration, and recovery machinery until a working
   system creates a concrete need for them.
7. Keep implementation provenance clear: MINIX defines behavioral evidence,
   not source to copy.
8. Preserve explicit extension boundaries for foreseeable features without
   implementing those features before they are needed.
9. Optimize or redesign semantics only after the corresponding baseline works
   end to end.

The classification and research gate are defined by
[ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md).
Accepted subsystem ADRs remain authoritative until explicitly superseded.

## Target environment

The proposed initial target is:

- RISC-V 64-bit, little-endian.
- QEMU `virt`.
- OpenSBI firmware.
- One hart.
- One user thread per process.
- Sv39 virtual memory.
- PLIC interrupt delivery with QEMU AIA disabled.
- Minimal FDT parsing for physical memory and reserved ranges.
- Supervisor-mode kernel and user-mode services.
- C17 with a small amount of RISC-V assembly.
- LLVM/Clang, LLD, CMake, and Ninja.

The precise decision is defined by
[ADR-0001](../adr/0001-target-platform.md) and
[ADR-0002](../adr/0002-language-and-toolchain.md).

## Trust boundary

The kernel is the only supervisor-mode `micros` component. It owns mechanisms
that cannot safely be delegated:

- trap and interrupt entry;
- thread execution contexts and context switching;
- page-table activation and validated map/unmap operations;
- physical-frame availability plus typed bootstrap ownership and reservations
  required by the kernel itself;
- endpoint identity and IPC queues;
- IPC permission enforcement;
- direct memory grants and checked cross-address-space copies;
- interrupt routing;
- the initial round-robin scheduling mechanism;
- bootstrap operations required before the first servers are available.

The kernel does not own filesystem namespaces, pathname lookup, process
semantics, executable policy, terminal policy, dynamic service discovery, or
service recovery.

## Components

| Component | Mode | Initial responsibility |
| --- | --- | --- |
| Kernel | Supervisor | Traps, process/thread objects, page-table mechanism, IPC, grants, IRQ routing, bootstrap memory, initial scheduling |
| Bootstrap launcher | User | Release statically embedded services in dependency order and verify readiness |
| VM server | User | User-frame ownership, address-space policy, mappings, and page-fault decisions |
| PM server | User | Process identity, parent/child relationships, spawn, exit, and wait |
| TTY server | User | Serial terminal buffering and character-device protocol |
| RAMFS server | User | Volatile files, directories, and file data |
| VFS server | User | File descriptors, pathname routing, mounts, and filesystem protocol |
| Init | User | Start the initial user environment |
| Shell | User | Interactive command parsing and process launch |
| DS server | User | Dynamic service name and endpoint publication after the shell MVP |
| Scheduler server | User | Scheduling policy after the kernel scheduler is stable |
| RS server | User | Service lifecycle, restart, and recovery after stable bootstrap |

The kernel represents processes, schedulable threads, endpoints, and harts as
distinct objects. PM represents semantic process relationships. VM represents
address-space policy. v0.1 enforces one thread per process and one hart, but the
scheduler and blocking paths operate on threads rather than treating a process
as a thread. See
[ADR-0011](../adr/0011-process-thread-and-hart-model.md). The implemented
identity substrate uses fixed-capacity process, thread, and hart tables with
typed slot/generation handles, deterministic reuse, stale-reference rejection,
and quarantine before a generation can wrap or become an endpoint-reserved
value. Production configures one thread per process and one registered hart;
native models exercise larger policies.

## Boot sequence

### Phase 1: firmware and platform entry

OpenSBI initializes the machine and enters the kernel in supervisor mode with
the boot hart ID and FDT address. The kernel establishes a stack, clears
uninitialized data, initializes the kernel-object registry, registers the boot
hart, installs that hart's primary and emergency trap stacks, installs the trap
vector, and then begins serial output.

### Phase 2: kernel bootstrap

The kernel validates the FDT, reads physical memory and reserved ranges,
reserves firmware and every physical address through its linker-defined end,
reclaims the parsed FDT blob, initializes the bootstrap frame allocator,
binds a static typed ownership ledger to that allocator's exact geometry,
creates 4 KiB-leaf kernel page tables through the `KERNEL_PAGE_TABLE` class,
verifies agreement among the reachable table tree, allocator bitmap, and
ledger, verifies exact supervisor-only RX, R, and RW/NX mappings, enables Sv39
through an ordered translation fence, and starts timer interrupts. Managed RAM
is identity-mapped RW/NX, while UART and PLIC device addresses remain fixed
platform constants in v0.1; only UART is mapped during this phase.

Every delivered trap frame carries the registered hart context. Outside
dispatch, `sscratch` points to the hart's trap anchor; during dispatch it is
zero as the nested-trap sentinel and kernel `tp` identifies the same hart. A
nested fault therefore selects that hart's emergency stack without a global
hart ID. The OpenSBI timer's initialized, active, interval, deadline, and tick
state are embedded in the hart object.

### Phase 3: process and IPC substrate

The kernel creates statically described initial process slots and
generation-bound private Sv39 roots for the bootstrap services. Each root
shares the immutable supervisor-only kernel entries and owns a private user
subtree at root index 1. Bootstrap anonymous user pages are fully zeroed,
typed to the exact process generation, and may be mutated only while their
root is inactive. One thread per process, saved contexts, and U-mode entry are
represented explicitly: each thread owns a saved integer context and one
slot-derived 16 KiB supervisor stack, and the boot hart switches its trap
anchor from the idle stack to that thread stack before `sret` enters U-mode.
User-origin traps capture the exact current thread before any test or future
syscall handling. Independent run-time flags, 16 priority queues, a
queue-reachable current thread, separate accounting, and repeated timer-driven
switching are implemented. A no-runnable return restores the kernel root and
idle stack, waits through the race-free interrupt window, and resumes a newly
runnable thread after a real wake. Only the bootstrap launcher receives the
temporary authority to release boot services and install their exact manifest
privilege profiles.

### Phase 4: VM handoff

The VM server starts with a kernel-provided memory map and reservation list.
Its code, data, stack, frame database, IPC buffers, and grant buffers are wired
before it completes a one-way handoff. The kernel stages exact
`VM_WIRED`/`VM_TRANSFERABLE` targets separately from current owners, validates
the complete plan while mutation is quiesced, then installs every class and
the irreversible handed-off phase in one non-failing pass. After that point,
VM decides user-memory policy while the kernel continues to validate and apply
page-table operations. A page fault originating from VM is fatal in v0.1
because VM cannot resolve its own fault.

Before the ownership commit, the address-space handoff path validates every
live private root and requires every reachable bootstrap user leaf to be
planned `VM_WIRED` for the same process generation. After commit, read-only
root validation, lookup, translation, and activation use exact `VM_WIRED`
leaf ownership, so the launcher and initial services retain IPC-buffer and
checked-grant access. `VM_TRANSFERABLE` carries no process identity and cannot
appear in a live user PTE until the later VM mapping protocol installs an
exact mapping authority. This read boundary does not reopen bootstrap
execution-context preparation: every live thread is already prepared at
handoff, and only the later PM transaction may prepare a new executable
context.

### Phase 5: core user services

The launcher starts PM, TTY, RAMFS, and VFS in dependency order. Every service
must send an explicit ready message before its dependents are released. Before
TTY can map or configure the UART, the launcher asks the kernel to quiesce the
early console. TTY then commits UART/PLIC ownership and reports readiness.
Bootstrap failure is fatal in the first MVP; automatic recovery is deferred.

### Phase 6: first user environment

PM, VFS, and VM cooperate to load `init`. VFS creates one synthetic console
object bound to the fixed TTY endpoint and installs it as init descriptors 0,
1, and 2. Init starts the shell, and later spawn descriptor actions duplicate
the parent's console descriptors. The shell can run a small built-in userland
including at least `echo`, `cat`, `ls`, and `ps`.

## Runtime interactions

### IPC

Synchronous fixed-size IPC is the control plane. The kernel validates endpoint
generations, sender privilege profiles, receiver selection, reply tokens, and
blocking thread transitions. A call receives a kernel-generated one-shot reply
token so a future multithreaded process can have several concurrent calls
without reply ambiguity. Pointers inside a message have no cross-process
meaning.

### Bulk data

Direct memory grants are the initial data plane. A grant identifies a bounded
region, an authorized endpoint, and read/write permissions. Grants are
non-transitive: a grantee cannot forward the grant to another service. VFS
therefore uses resident page-sized bounce buffers for application-to-filesystem
and application-to-TTY transfers. The kernel performs checked copies; user
services never map arbitrary memory from another process.

### Process creation

PM coordinates process creation. It reserves a process, its first thread, and
an endpoint, then asks the kernel to install the immutable application
privilege profile. VFS resolves and buffers the executable and exposes its
resident contents to VM through a read-only direct grant. VM maps destination
child frames into a VM-only scratch window, copies and validates each ELF
segment, removes the scratch mappings, maps the frames into the child, freezes
the result, and returns a kernel-issued load-complete token to PM. VM cannot
publish or run the child. PM is the sole caller of the kernel preparation
transition: it presents the token, initializes the first-thread context, and
causes the required local `fence.i`. The kernel records and seals the validated
mapping generation while the thread is held; VM mapping changes are rejected.
After PM and VFS commit their prepared state, PM invokes final activation,
which revalidates that generation before making the endpoint visible and the
thread runnable. This directed transaction avoids the PM/VFS/VM cycles found
in MINIX.

### File I/O

Applications call VFS. VFS owns descriptors and routes requests to RAMFS.
Because direct grants cannot be forwarded, each transfer uses two checked
grant hops through a resident VFS bounce buffer. A VFS-owned synthetic console
object routes descriptors 0, 1, and 2 to TTY without adding RAMFS device nodes
or dynamic driver discovery. RAMFS does not communicate with block drivers in
the shell MVP.

### Interrupts

The boot hart timer uses OpenSBI TIME absolute deadlines. The kernel owns
per-hart timer mechanism state, enables `sie.STIE` independently from global
`sstatus.SIE`, rejects stale pending delivery before counting a tick, and
rearms relative to the current counter. Scheduling and preemption policy are
separate consumers added only after thread and hart objects exist.

Before TTY starts, the kernel owns a polled transmit-only early console. The
launcher begins handoff by asking the kernel to stop ordinary console output
before TTY receives its MMIO mapping. TTY initializes the UART, commits
ownership through an authorized kernel operation, and only then reports ready.
For each interrupt the kernel claims the PLIC source, notifies TTY, and leaves
the source in service. TTY drains the UART and explicitly completes the
interrupt. A fatal kernel panic may seize the UART in polled mode with
interrupts disabled.

## Architectural invariants

- An endpoint is valid only while both its slot and generation match.
- Every thread belongs to exactly one process.
- v0.1 permits at most one live thread in a process.
- Process and thread slot reuse advances a nonzero generation; stale handles
  never resolve, and exhausted generations quarantine the slot.
- A thread is present in at most one run queue and at most one IPC wait queue.
- A running thread is current on exactly one hart, and an inactive thread is
  current on none.
- The current thread, trap stacks, nested-trap route, and timer mechanism state
  are hart-local, never standalone global execution state.
- IPC payloads never authorize memory access by themselves.
- A reply token is one-shot and resolves to exactly one blocked caller thread.
- Endpoint close atomically removes every exact-generation IPC reference,
  wakes exact foreign dependents with a dead-endpoint result, and leaves
  unrelated `ANY` receivers blocked. A later close may discard its own staged
  dead-endpoint result, but a successfully staged message or notification must
  be drained before close.
- Every cross-address-space copy is bounded by an active grant.
- A direct grant is never forwarded across a second service boundary.
- Executable bytes are followed by a local instruction-fetch synchronization
  before the new thread can run.
- A VM load-complete token is bound to one child mapping generation, and only
  PM may consume it to prepare and activate that spawn transaction.
- A prepared address space is kernel-sealed; activation revalidates its mapping
  generation, and changes require aborting and repeating load preparation.
- The allocator bitmap answers availability, while the typed frame ledger is
  the authoritative semantic owner for every allocated managed frame.
- Process-bound frame owners carry the exact live process slot and generation;
  stale generations cannot release or reclassify them.
- Every process root is a private `PROCESS_PAGE_TABLE` frame. Its user subtree
  occupies `[0x40000000, 0x80000000)`, while every other root entry exactly
  matches the immutable kernel root.
- Process-root mutation and destruction are rejected while that root is
  active; ASID-zero activation performs complete local invalidation.
- During `BOOTSTRAP`, every user leaf resolves one distinct exact
  `PROCESS_USER` owner, and every process-owned table or user frame is
  reachable in the matching role.
- Trap entry preserves interrupted SUM in the saved frame but clears live SUM
  before arming the nested sentinel or entering C.
- Every attached thread context uses the exact 264-byte integer ABI shared with
  the trap frame and one pairwise-disjoint slot-derived kernel stack.
- A hart running U-mode names exactly one runnable current thread, selects that
  thread's stack in its stable trap anchor, and activates the matching process
  root before entry.
- U-mode return status fixes SPP/SIE/SUM/MXR/UBE and extension state, requires
  RV64 UXL, canonicalizes the derived SD summary, and revalidates executable
  PC plus writable aligned stack mappings.
- User-managed frames and kernel-reserved frames never overlap.
- Kernel text is RX, read-only data is R, writable kernel state and managed RAM
  are RW/NX, and none of those mappings has the user bit.
- Every reachable bootstrap page-table frame remains allocated, is represented
  by the live allocator bitmap, and has exact `KERNEL_PAGE_TABLE` ownership;
  all three independent counts agree.
- Bootstrap ownership handoff is staged, failure-atomic, and irreversible;
  allocation, release, and plan mutation are rejected after sealing.
- Every user leaf reachable at handoff has an exact `VM_WIRED` target for the
  same process generation; after handoff, read-only address resolution
  requires that exact wired owner.
- A `VM_TRANSFERABLE` frame is not live user-mapping authority until a reviewed
  VM mapping transition binds it to an exact process generation.
- Generic execution-context preparation is unavailable after handoff; existing
  prepared threads may return, while new preparation requires the PM-only
  load-complete-token transition.
- Only VM may request user mapping changes after the handoff.
- VM's fault-handling working set is wired, and a VM-originated fault is fatal.
- Only PM publishes process lifecycle state.
- A spawned process receives exactly one immutable application privilege
  profile before its endpoint becomes visible or its first thread runs.
- Only VFS publishes file-descriptor and namespace state.
- Init receives descriptors 0, 1, and 2 from one VFS-owned synthetic console
  object, and children receive them only through explicit descriptor actions.
- A claimed user-driver IRQ is completed only after its owner acknowledges it.
- Bootstrap-only privileges become unavailable after their transition point.
- A service is not released until all development-DAG prerequisites have
  passed their readiness gate.

## Proposed source layout

The layout is descriptive until the toolchain ADRs are accepted:

```text
arch/riscv64/       RISC-V entry, traps, context switch, and platform code
kernel/             Architecture-independent privileged mechanisms
include/micros/     Shared ABI headers
lib/runtime/        Freestanding user-service runtime
servers/            VM, PM, VFS, RAMFS, TTY, and later control-plane services
user/               Init, shell, and small user programs
tests/host/         Native unit and property tests
tests/qemu/         Kernel, integration, and system scenarios
tools/              Build-time image and test utilities
cmake/              Cross-compilation and build helpers
docs/               Architecture, research, decisions, and plans
```

## Scope boundary for the first shell MVP

The shell MVP includes isolated address spaces, separate process/thread/hart
objects, preemption, generation-aware IPC, one-shot reply rights, privileges,
direct grants, VM, PM, TTY, RAMFS, VFS, executable loading, init, and a shell.

It excludes thread creation, `fork`, signals, persistent filesystems, dynamic
drivers, DS, user-space scheduling policy, RS recovery, networking, SMP, and
NetBSD userland compatibility.

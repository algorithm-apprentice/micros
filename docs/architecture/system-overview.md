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
| Bootstrap launcher | User | Release exact manifest-bound services one at a time, acknowledge readiness, then surrender bootstrap authority |
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
platform constants in v0.1. The kernel maps the UART page and only the three
PLIC pages required for source-10 priority, hart-0 supervisor enable, and
hart-0 supervisor claim/complete.

Every delivered trap frame carries the registered hart context. Outside
dispatch, `sscratch` points to the hart's trap anchor; during dispatch it is
zero as the nested-trap sentinel and kernel `tp` identifies the same hart. A
nested fault therefore selects that hart's emergency stack without a global
hart ID. The OpenSBI timer's initialized, active, interval, deadline, and tick
state are embedded in the hart object.

### Phase 3: process, IPC, and static image preparation

The kernel validates one pointer-free immutable bootstrap manifest and its
generated catalog of already linked service images. It computes a
lowest-service-ID topological order and rejects unknown versions, missing
profiles or images, duplicate identities, invalid device fields, missing
prerequisites, and cycles before any manifest-directed service mutation.

For every active manifest entry, the kernel reserves the exact process slot,
creates a generation-bound private Sv39 root, loads the checked RX, R, and
RW/NX image, creates the external zeroed stack, prepares the sole initial
thread context, reserves the endpoint, installs bootstrap scheduling policy,
and leaves the thread held. Each root shares the immutable supervisor-only
kernel entries and owns a private user subtree at root index 1. Bootstrap user
pages are fully zeroed and typed to the exact process generation.

Only the launcher receives its exact immutable profile, active endpoint, and
runnable thread. Every other endpoint remains `RESERVED`, every other profile
remains uninstalled, and every other thread retains `INACTIVE`. The launcher
reads a read-only copy of the validated manifest and may select only
kernel-prepared manifest entries.

One thread per process, saved contexts, and U-mode entry remain explicit: each
thread owns a saved integer context and one slot-derived 16 KiB supervisor
stack. Independent run-time flags, 16 priority queues, a queue-reachable
current thread, separate accounting, repeated timer-driven switching, and the
race-free idle return remain the common execution mechanism.

The implemented exact contract is
[ADR-0043](../adr/0043-static-bootstrap-launcher.md).

### Phase 4: VM handoff

The VM server starts with one kernel-patched, fixed-capacity boot-information
and frame-database object. It contains the canonical physical-memory,
reservation, managed-range, address-space, mapping, per-frame-state, count,
and digest snapshot with no pointer authority. Its code, data, stack,
database, IPC buffers, and grant buffers are wired before it completes a
one-way handoff.

Every static `PROCESS_USER` frame is atomically staged `VM_WIRED` before the
launcher runs. VM independently validates the complete snapshot, then invokes
one exact VM-only summary operation. The kernel revalidates the scalar summary,
all roots, the allocator, and typed ownership while mutation is quiesced, then
installs every wired owner and the irreversible handed-off phase in one
non-failing pass. After that point, VM owns ordinary frame-allocation policy
while the kernel retains the allocator, typed-owner, and page-table mechanisms
as privileged validation mirrors.

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

The launcher releases VM first. VM's bootstrap ready call is accepted only
after the separate `VM_READY` summary has been validated and the ownership
phase has irreversibly changed to `HANDED_OFF`. Only then may the launcher
acknowledge VM readiness and release PM. The launcher does not perform or
infer the ownership commit.

Step 9 exposes no general post-handoff map, unmap, allocation, or page-fault
protocol. Step 11 adds one nonrepeatable exception: exact VM authority may
install the fixed UART device page into the held TTY root without allocating a
frame or page table. The device leaf is outside managed RAM, is tracked
separately from VM's handoff snapshot, and is rejected by managed-memory
lookup, IPC-buffer, grant, and inventory paths. Every ordinary static mapping
remains wired.

A VM-originated fault is fatal; non-VM fault delivery remains disabled until a
later reviewed mapping protocol defines suspension, exact mapping authority,
mutation rollback, and retry. The exact handoff contract is
[ADR-0045](../adr/0045-static-vm-bootstrap-and-handoff.md).

### Phase 5: core user services

The launcher releases PM, TTY, RAMFS, and VFS one at a time in the explicit
manifest order. Release atomically installs only the named profile, activates
the reserved endpoint, records one readiness deadline, and makes the prepared
thread runnable. Each service performs one versioned readiness `call`; the
launcher validates its exact endpoint generation and the kernel atomically
records the ready transition with the token-bound acknowledgment before any
dependent is released.

Before TTY release, the launcher invokes exact `console_handoff_begin`.
The kernel disables ordinary early-console output and notifies VM. VM invokes
one exact operation-12 command that writes the fixed UART PTE at
`0x7fffe000`, then the kernel notifies the launcher that mapping committed.
Only then may the launcher release TTY.

TTY initializes the NS16550A while PLIC source 10 remains disabled, drains
stale device state, and commits ownership through operation 14. The kernel
then installs the exact source route and enables supervisor-external delivery.
One manifest-bounded absolute deadline is armed at console begin and covers
mapping, release, initialization, commit, and readiness without being extended
at release. TTY's ordinary bootstrap readiness is accepted only after commit
and clears that deadline. The exact contract is
[ADR-0047](../adr/0047-tty-console-handoff-and-serial-protocol.md).

Malformed, foreign, early, duplicate, missing, or expired readiness is fatal.
There is no restart, alternate profile, skipped dependency, or recovery
fallback. After the last static service is ready, the launcher seals bootstrap
state, clears the exact controller binding, and holds its own thread. Its
endpoint and immutable profile remain attached but confer no bootstrap
authority in the sealed phase. Its wired process and root remain dormant until
later PM/VM teardown exists.

PM initializes a separate bounded application-lifecycle table before reporting
ready. Static manifest services remain an immutable bootstrap inventory rather
than ordinary PM children: they have no application PID, wait parent, zombie
state, or ordinary exit path before RS exists. The sealed profile table
contains exact `APPLICATION` profile ID 7, but no process receives it before
the later executable-preparation transition. When launcher completion seals
bootstrap, the kernel injects one exact `BOOTSTRAP_SEALED` event into the
manifest PM endpoint. That event enables PM's runtime process policy without
transferring or reviving launcher authority.

The initial PM outcome may reserve and abort one hidden empty kernel process
after that event. The reservation has no address space, thread, endpoint,
profile, scheduler state, grant, or process-owned frame. It proves PM's narrow
ordinary-runtime authority and rollback boundary without claiming that a
dynamic child can already run.

The exact initial PM contract is
[ADR-0046](../adr/0046-pm-process-lifecycle-and-spawn-metadata.md).

### Phase 6: first user environment

After launcher authority is sealed, PM, VFS, and VM cooperate through the
ordinary spawn transaction to load `init`. VFS creates one synthetic console
object bound to the fixed TTY endpoint and installs it as init descriptors 0,
1, and 2. Init is not a static manifest entry and its activation does not
restore launcher authority. Init starts the shell, and later spawn descriptor
actions duplicate the parent's console descriptors. The shell can run a small
built-in userland including at least `echo`, `cat`, `ls`, and `ps`.

The first successful internal PM transaction creates init as PID 1 with no
parent and designates it as the application reaper. Static services remain
outside that application parent tree.

## Runtime interactions

### IPC

Synchronous fixed-size IPC is the control plane. The kernel validates endpoint
generations, sender privilege profiles, receiver selection, reply tokens, and
blocking thread transitions. A call receives a kernel-generated one-shot reply
token so a future multithreaded process can have several concurrent calls
without reply ambiguity. Pointers inside a message have no cross-process
meaning.

One RISC-V `ecall` namespace exposes IPC operations 1 through 6 and reserves
grant create, revoke, copy-from, and copy-to as operations 7 through 10.
Existing IPC register layouts and results remain unchanged. Grant create
returns one nonnegative opaque token in `a0`; every failure is one stable
negative result. The kernel ABI remains independent from user-language
startup and wrapper policy.

### User-service runtime

[ADR-0042](../adr/0042-freestanding-user-service-runtime.md)
defines the implemented first freestanding user-side layer.

An initial service is a fixed-address, non-PIE ELF64 RISC-V executable whose
entry is `_start` at `0x40000000`. Page-separated load segments are RX, R, and
RW/NX. The external image owner copies file-backed bytes, zeroes every
remaining segment byte, maps a separate zero-filled stack ending at
`0x80000000`, performs instruction-fetch synchronization, and prepares the
thread context. The runtime does not relocate itself or clear BSS.

`_start` sets `gp` and `tp` to zero, preserves the supplied aligned stack, and
calls `void micros_service_main(void)`. It provides no arguments,
environment, constructors, TLS, heap, libc, or process-exit path. An
accidental return executes a labeled user breakpoint and cannot fall through
into arbitrary image bytes.

One raw RV64 function consumes `a0` through `a7`, executes `ecall`, and returns
the signed `a0` result. Stateless C wrappers expose operations 1 through 10,
return stable results directly without `errno`, and publish a created grant
token only after success. The only compiler support is `memcpy` and `memset`.

Launcher release, manifest privileges, readiness, VM handoff, service
initialization, and recovery remain above this boundary. The runtime itself
owns no lifecycle or protocol authority.

### Static bootstrap launcher

[ADR-0043](../adr/0043-static-bootstrap-launcher.md) defines the implemented
Step 8 contract.

The immutable manifest has a six-entry capacity for the launcher, VM, PM, TTY,
RAMFS, and VFS. Each active entry carries an exact service ID and name,
process slot, generated embedded-image ID, profile ID and name, user-page and
stack limits, explicit prerequisite mask, readiness timeout, and one narrowly
defined bootstrap role. Only the TTY role may carry the fixed UART page and
PLIC source required by ADR-0012.

The manifest never contains a pointer or mutable privilege mask. The kernel
resolves each image through an immutable checked image catalog and each
profile through the sealed profile table. No privilege or prerequisite is
inferred from service names, roles, source order, images, devices, or IRQs.
Each static image exports one fixed writable bootstrap-configuration object;
before context preparation, the kernel patches its exact service ID, own
endpoint generation, launcher endpoint, manifest version, and the bounded
static service-ID/endpoint table. The object is startup data, not authority;
reserved endpoints remain unresolved until release. Prepared non-launcher
threads remain inactive and scheduler-unassigned; their immutable intended
policy is stored in bootstrap state and scheduler admission occurs only in the
same commit that installs the profile and publishes the endpoint.

RISC-V syscall operation 11 is implemented for launcher-only
bootstrap control. It releases the exact next service, atomically accepts one
token-bound ready call, reports a fatal launcher-detected protocol failure, or
irreversibly completes bootstrap. The generic freestanding runtime remains
operations 1 through 10; launcher code owns a private wrapper.

The readiness request and acknowledgment are fixed 64-byte IPC messages. The
request's kernel-written source and reply token, not its payload, prove the
exact service generation. The kernel owns one absolute `time`-counter
deadline, so a missing response fails inside the guest without sleeps or
reliance on the host harness timeout.

### Bulk data

Direct memory grants are the initial data plane. A grant identifies a bounded
region, an authorized endpoint, and read/write permissions. Grants are
non-transitive: a grantee cannot forward the grant to another service. VFS
therefore uses resident page-sized bounce buffers for application-to-filesystem
and application-to-TTY transfers. The kernel performs checked copies; user
services never map arbitrary memory from another process.

TTY uses short `SUBMIT_READ`, `SUBMIT_WRITE`, `CANCEL`, and `COLLECT` calls
rather than retaining VFS's only thread for a canonical read. Completion is a
coalesced TTY-to-VFS notification. TTY retains only the exact VFS endpoint,
last accepted request ID, grant, offset, length, and one bounded request slot
per direction. Accepted IDs increase monotonically; a `BUSY` submission does
not advance that sequence and may be retried unchanged. Applications never
call TTY directly.

Write acceptance precedes physical drain. If a collected write still occupies
the resident transmit buffer, one armed writable event notifies VFS when that
buffer becomes reusable. VFS may retry the unaccepted ID unchanged unless a
higher ID has since been accepted, in which case the older ID is stale.

The grantor alone creates or revokes its grants. The exact grantee invokes
copy-from or copy-to with both the grantor endpoint and opaque token. No user
grant-table registration or user-visible inspect operation exists.

### Process creation

PM coordinates process creation. It first reserves one PM record and
never-reused PID, then one hidden kernel process object. The reserved kernel
process has no root, thread, endpoint, profile, or runnable state. VFS then
resolves and buffers the executable and exposes its resident contents to VM
through a read-only direct grant. VM creates the reserved process's address
space, maps destination child frames into a VM-only scratch window, copies and
validates each ELF segment, removes the scratch mappings, maps the frames into
the child, freezes the result, and returns a kernel-issued load-complete token
to PM. VM cannot publish or run the child.

PM is the sole caller of the kernel preparation transition. It presents the
reservation, load-complete token, and initial context. In one hidden
transition, the kernel creates the first thread and reserved endpoint,
installs only the immutable `APPLICATION` profile, performs the required local
`fence.i`, initializes the context, and seals the validated mapping generation.
VM mapping changes are then rejected. After PM and VFS commit their prepared
state, PM invokes final activation, which revalidates that generation before
making the endpoint visible and the thread runnable. Every earlier failure
releases opaque resources in reverse order. This directed transaction avoids
the PM/VFS/VM cycles found in MINIX.

PM separately owns exit and wait semantics. Accepted exit first makes the
thread permanently non-runnable and binds one exit transaction. After VFS
cleanup, a PM-only detach transition atomically closes endpoint, IPC, grants,
profile, scheduler/context, and thread state and leaves one exact
VM-release-pending process. VM presents the same transaction to release
mappings, root, and frames before PM releases the empty process. Only
afterward does the PM record become a zombie. A matching exact-child or
any-child wait consumes that status once; parent exit reparents children to
init.

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
before exact VM authority maps one user R/W, non-executable UART page into the
held TTY root. TTY initializes the UART, commits ownership through authorized
operation 14, and only then reports ready.

The kernel maps no PLIC page into user space. For each supervisor-external
interrupt it claims source 10, records the source in service, injects or
coalesces one source-`NONE` event to exact TTY, and does not complete the
source. TTY drains `IIR` to no-pending and invokes exact `IRQ_COMPLETE(10)`.
The kernel validates ownership and in-service state before writing the PLIC
completion register. A fatal kernel panic disables external delivery and may
seize UART permanently in polled mode.

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
- A PM spawn reservation names one hidden kernel process generation with no
  root, thread, endpoint, profile, grant, scheduler state, or process-owned
  frame.
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
- The sole post-handoff device exception is the exact TTY UART leaf at
  `0x7fffe000`; it names no managed frame and cannot back an IPC buffer, grant,
  PM output, or managed mapping inventory entry.
- A `VM_TRANSFERABLE` frame is not live user-mapping authority until a reviewed
  VM mapping transition binds it to an exact process generation.
- Generic execution-context preparation is unavailable after handoff; existing
  prepared threads may return, while new preparation requires the PM-only
  load-complete-token transition.
- Only VM may request user mapping changes after the handoff.
- VM's fault-handling working set is wired, and a VM-originated fault is fatal.
- Only PM publishes process lifecycle state.
- PM PIDs, PM record handles, kernel process handles, thread handles,
  endpoints, service IDs, and spawn tokens are distinct identities.
- PID zero is invalid, init is the sole application reaper, and a consumed PID
  is not reused in v0.1.
- A spawned application is invisible until final endpoint activation, and
  every earlier failure rolls back the exact acquired resources in reverse
  order.
- An exited application releases all kernel, VM, VFS, IPC, and grant resources
  before becoming a PM-only zombie; its PM record remains until one successful
  matching wait.
- PM runtime process creation remains disabled until the exact kernel-origin
  bootstrap-sealed event.
- A spawned process receives exactly one immutable application privilege
  profile before its endpoint becomes visible or its first thread runs.
- Only VFS publishes file-descriptor and namespace state.
- Init receives descriptors 0, 1, and 2 from one VFS-owned synthetic console
  object, and children receive them only through explicit descriptor actions.
- Console begin, exact VM mapping, TTY release, console commit, and TTY
  readiness occur in that order once.
- One absolute manifest-bounded TTY deadline covers that complete sequence and
  is not restarted at release.
- PLIC source 10 has one exact TTY endpoint-generation owner, and a claim is
  completed exactly once only after that owner drains UART state and
  acknowledges it.
- Direct application access to TTY, UART, PLIC, or IRQ completion is
  impossible under the immutable profiles.
- Bootstrap-only privileges become unavailable after their transition point.
- The immutable bootstrap manifest is versioned, pointer-free, bounded to six
  entries, and completely validated before manifest-directed service
  mutation.
- Service IDs, process slots, thread handles, image IDs, profile IDs, and
  endpoint generations remain distinct identities.
- Every static service image, stack, and execution context exists before the
  VM ownership handoff.
- A non-launcher endpoint remains reserved and hidden until exact profile
  installation, endpoint activation, starting state, deadline, and exact
  scheduler admission commit together.
- At most one static service is starting, and one exact endpoint generation
  may complete one ready transition.
- A service is not released until all development-DAG prerequisites have
  passed their readiness gate.
- VM readiness implies the ownership handoff has committed; TTY readiness
  implies console ownership has committed.
- Bootstrap failure after publication is fatal and never rolls a service back
  to a hidden endpoint.
- Sealed bootstrap state has no controller binding, runnable launcher thread,
  deadline, or operation that can restore authority; the retained launcher
  endpoint is source-only for already staged acknowledgments and rejects every
  new IPC destination.
- The initial user image has one fixed `_start`, no relocation or
  dynamic-loader state, and no allocatable page with both write and execute
  permission.
- The runtime does not retain syscall authority or results and does
  not initialize service readiness, process lifecycle, heap, TLS, arguments,
  or environment state.

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

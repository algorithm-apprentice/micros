# System Overview

## Purpose

This document explains the system, not its delivery history.
[The roadmap](../roadmap.md#current-status) owns current status; the linked
subsystem contracts own exact interfaces and acceptance.

`micros` is an educational operating system that uses a small privileged
kernel and isolated user-space services. It independently reimplements the
verified logical structures, algorithms, and behavior of MINIX 3 without
attempting source compatibility, binary compatibility, or a line-by-line
translation.

The first complete product increment is a shell-capable system running under
QEMU. It should be small enough that one developer can trace a process from a
user request through IPC, memory management, filesystem lookup, and back.

A complete POSIX-compatible operating system is the final product goal. The
shell increment is the first integrated checkpoint; omitted process, thread,
signal, credential, terminal, storage, networking, libc, shell-language, and
utility mechanisms remain staged future outcomes.

## Design principles

1. Keep policy outside supervisor mode unless moving it would create an
   unresolved bootstrap cycle.
2. Introduce one new dependency layer at a time.
3. Prefer explicit protocols and ownership over shared writable state.
4. Make stale identities, invalid transitions, and ownership violations fail
   immediately.
5. Reproduce the dependency-ready MINIX logical structures, algorithm family,
   complexity envelope, and behavior before selecting another mechanism.
6. Defer compatibility, migration, and recovery machinery until a working
   system creates a concrete need for them.
7. Keep implementation provenance clear: MINIX defines structural,
   algorithmic, and behavioral evidence, not source to copy.
8. Preserve explicit extension boundaries for foreseeable features without
   implementing those features before they are needed.
9. Optimize or redesign structures, algorithms, or semantics only after the
   corresponding baseline works end to end.
10. Organize source modules by cohesive authority, resource lifecycle,
    protocol layer, or algorithm rather than accumulating unrelated behavior
    in central core files.

The current structural, algorithmic, and behavioral research gate is defined
by [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md), which
supersedes the earlier behavioral-only rule in ADR-0025.
Accepted subsystem ADRs remain authoritative until explicitly superseded.
The source-organization rules and current VFS module map are defined by
[ADR-0051](../adr/0051-responsibility-based-source-modules.md) and
[Responsibility-Based Source Organization](source-organization.md).

## Target environment

The accepted v0.1 target is:

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

OpenSBI enters supervisor mode with the boot hart ID and FDT address.
The kernel establishes its boot stack, clears BSS, initializes the object
registry, registers the boot hart, installs primary/emergency trap stacks
and the direct-mode vector, then begins serial output.

### Phase 2: kernel bootstrap

The kernel validates the complete FDT memory/reservation map, excludes
firmware and all memory through `__kernel_end`, and reclaims the consumed FDT
blob. A bitmap allocator and typed ledger own every managed frame.
[Bootstrap allocation](bootstrap-allocation.md) uses retained search progress;
[kernel mapping](kernel-mapping-topology.md) selects the largest legal Sv39
leaf while preserving exact supervisor RX, R and RW/NX domains.
Only the required UART and PLIC pages are mapped.

Selected memory/object/scheduler operations retain operation-local checks;
complete diagnostics remain at their explicit owning boundaries. The exact
contracts are [frame ownership](selected-frame-ownership.md),
[address spaces](selected-user-address-space.md),
[release/handoff](selected-release-and-wired-handoff.md),
[objects/harts](selected-object-and-hart-validation.md) and
[common return](common-scheduler-return.md).

Every trap frame carries its registered hart. Outside dispatch `sscratch`
names that hart's stack anchor; during dispatch it is the nested-trap sentinel
and kernel `tp` identifies the same hart. Nested faults use the emergency
stack. OpenSBI timer state is embedded in the hart, not standalone globals.

### Phase 3: process, IPC, and static image preparation

Before mutation, the kernel validates the pointer-free manifest, profiles,
resource bounds, generated checked images and lowest-ID topological order.
It prepares every static process/root/image/stack/thread/context/endpoint
before VM handoff. Only the launcher starts active and runnable; other
endpoints remain reserved, profiles uninstalled and threads held.

The production manifest contains launcher, VM, PM, TTY, RAMFS and VFS.
Its seventh storage slot is for dependency-closed fixtures, not a seventh
production service. Readiness uses exact endpoint generations and one-shot
reply tokens; see [ADR-0043](../adr/0043-static-bootstrap-launcher.md).

### Phase 4: VM handoff

VM independently validates its fixed pointer-free boot database before
requesting the one-way ownership commit. The kernel completely preflights
the summary, roots, allocator and typed ownership while mutation is quiesced,
then commits the planned wired owners without partial publication.
VM readiness is accepted only after `HANDED_OFF`.

Static user mappings retain exact process-generation `VM_WIRED` ownership.
Only VM owns subsequent frame policy; privileged mechanisms remain checked
kernel mirrors. The sole device exception is the exact TTY UART leaf.
The separate token-bound executable-loading protocol owns VM's temporary
scratch mapping; handoff does not reopen generic bootstrap mutation or
execution-context preparation. VM-originated faults are fatal.

See [ADR-0045](../adr/0045-static-vm-bootstrap-and-handoff.md),
[the wired read boundary](../adr/0040-post-handoff-wired-address-space-resolution.md)
and [ADR-0050](../adr/0050-post-handoff-elf-spawn.md).

### Phase 5: core user services

The launcher releases PM, TTY, RAMFS and VFS sequentially. Release publishes
the exact profile, endpoint, guest-owned deadline and runnable state together.
Ready acknowledgment is generation- and token-bound. Malformed, foreign,
early, duplicate, missing or expired readiness is fatal, not recoverable
through a skipped dependency or alternative profile.

TTY release follows console begin and VM's exact UART mapping; readiness
follows console commit. One unextended deadline covers that whole sequence.
RAMFS validates its seed before readiness; VFS mounts it once and binds the
synthetic console. After the final acknowledgment the launcher is held,
its controller binding is cleared and bootstrap authority is irreversibly
sealed. The retained launcher endpoint/profile do not restore authority.

PM consumes the kernel-origin `BOOTSTRAP_SEALED` event before application
lifecycle work. Static services remain outside the application PID/parent/
zombie tree. [The coordinator](pm-spawn-coordinator.md),
[application runtime](application-runtime.md) and
[ELF-spawn integration](elf-spawn-integration.md) define the ordinary
post-seal spawn, rollback, exit and wait path.

### Phase 6: first user environment

Production node Q is still a later gated outcome, not the existing test PID 1.
Its [Accepted contract](init-shell.md) has PM create parentless init through
ordinary spawn, VFS supply console descriptors 0/1/2, and init persist as the
application reaper while restarting one foreground shell. The shell uses
bounded simple-command parsing and direct spawn/wait, not fork.
Separate `echo`, `cat`, `ls` and `ps` applications do not gain bootstrap
authority; `ps` observes PM-owned applications only.

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
the signed `a0` result. Stateless C wrappers expose operations 1 through 10
plus non-copying grant validation operation 15, return stable results directly
without `errno`, and publish a created grant token only after success. The only
compiler support is `memcpy` and `memset`.

Launcher release, manifest privileges, readiness, VM handoff, service
initialization, and recovery remain above this boundary. The runtime itself
owns no lifecycle or protocol authority.

### Static bootstrap launcher

The phase-3/5 sequence above is the bootstrap lifecycle.
[ADR-0043](../adr/0043-static-bootstrap-launcher.md) owns the exact manifest,
operation-11 and readiness formats. Configuration records are startup data,
not authority: the kernel resolves every image/profile and never derives
privileges from names, payload pointers or source order.

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
copies the canonical request, resolves and buffers the executable, and exposes
its resident executable and request snapshots to VM through separate read-only
direct grants. VM validates the bounded static ELF, creates the initial
argv/env/auxv stack, maps one selected destination frame at a time into a
VM-only scratch window, initializes it, removes the scratch alias, and asks
the kernel to atomically build and freeze the exact private child mappings.
The returned load token binds the mapping generation, entry point, and stack
pointer. VM cannot publish or run the child.

PM is the sole caller of the kernel preparation transition. It presents the
reservation and load-complete token. In one hidden transition, the kernel
creates the first thread and reserved endpoint, installs only the immutable
`APPLICATION` profile, performs the required local `fence.i`, initializes the
token-bound context, and seals the validated mapping generation. VM mapping
changes are then rejected. After PM and VFS commit their hidden state, PM
invokes final activation, which revalidates that generation before making the
endpoint visible and the thread runnable. VM commits the activated mapping
into its persistent process table and VFS releases its image transaction
before PM reports success. Activation consumes the singleton hidden-process
reservation, so the running child does not block a later sequential spawn.
Every earlier failure releases opaque resources in reverse order. This
directed transaction avoids the PM/VFS/VM cycles found in MINIX.

PM separately owns exit and wait semantics. Accepted exit first makes the
thread permanently non-runnable and binds one exit transaction. PM then moves
the record to `EXITING`, clears its wait state, and reparents every live,
exiting, or zombie child to init before VFS cleanup. A PM-only detach
transition atomically closes endpoint, IPC, grants, profile,
scheduler/context, and thread state and leaves one exact VM-release-pending
process. VM presents the same transaction to release mappings, root, and
frames before PM releases the empty process. Only afterward does the PM
record become a zombie. PM then independently reevaluates init's complete
adopted-child set and the exiting process's exact original parent's child set,
deduplicating the scan when both parents are init. Each matching any-child
wait selects the lowest PID in its own set, so one exit completion may stage
results for two distinct parents. Each selected status is consumed once.

Dynamic applications use a separate startup ABI from static services. Their
single initial stack page contains `argc`, `argv`, `envp`, and a bounded auxv
carrying kernel- and service-derived PM endpoint, VFS endpoint, PID, and page
size values. The aligned initial frame may occupy at most the upper 2,048
bytes, preserving at least 2,048 bytes below initial `sp` for the
downward-growing runtime stack. A normal `micros_application_main` return
becomes a PM `EXIT` call. Applications receive no bootstrap
service-configuration mapping and retain authority only to PM and VFS.

The v0.1 application thread is fixed to hart 0, priority 9, a
1,000,000-counter-tick quantum, and preemptible scheduling. Static services
remain at priority 8, so activation cannot preempt PM before the mandatory
post-activation commits, while any ready service can preempt application
work.

The complete bounded transaction, wire formats, ELF subset, stack ABI,
rollback, and normal-exit ordering are fixed by
[ADR-0050](../adr/0050-post-handoff-elf-spawn.md).

### File I/O

Applications call VFS. VFS owns descriptors and routes requests to RAMFS.
Because direct grants cannot be forwarded, each transfer uses two checked
grant hops through a resident VFS bounce buffer. A VFS-owned synthetic console
object routes descriptors 0, 1, and 2 to TTY without adding RAMFS device nodes
or dynamic driver discovery. RAMFS does not communicate with block drivers in
the shell MVP.

RAMFS owns one volatile rooted namespace, generation-safe node identities,
regular-file data, namespace links, and backend references. Its initial state
comes from a validated pointer-free seed blob but becomes writable in a fixed
BSS arena before readiness. Open and close are represented by referenced
lookup/create results and counted putnode release. Reads are short at EOF,
sparse holes read as zero, writes commit one complete page-bounded request,
and directory enumeration returns complete fixed backend records plus a
continuation cursor. VFS later translates those records and owns every
application-visible file position and descriptor.

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
The claimed report emits a [bounded current-kernel stack trace](panic-stack-trace.md)
before replaying diagnostic history; it never follows user or older trap epochs.

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
  active except for the exact token-bound VM scratch leaf; that transition
  runs with SIE clear and performs a local per-page invalidation. ASID-zero
  activation otherwise performs complete local invalidation.
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
- The sole live user `VM_TRANSFERABLE` exception is one exact VM-root scratch
  leaf at `0x7fffe000`, bound to one load token while VM fills the frame.
  Complete root validation recognizes it in VM's attached root whether that
  root is active or temporarily inactive, while ordinary user lookup, IPC,
  grants, PM output, and managed inventory reject it. The immutable
  supervisor direct map remains present but grants no user authority. No
  child mapping contains a transferable frame; frozen and active child leaves
  use exact process-generation `VM_WIRED` ownership.
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
- Exact VFS is the sole RAMFS client, and RAMFS accepts exactly one mount.
- A RAMFS node handle resolves only when its slot and nonzero generation
  match one live node.
- Every non-root RAMFS node has one live directory parent and one unique
  sibling name; directory link counts equal two plus live child directories.
- RAMFS reference counts never underflow, the root retains its mount pin, and
  directory records do not acquire references.
- Every RAMFS data block has at most one file owner; sparse holes read as zero,
  and a write publishes all requested bytes and metadata or none.
- RAMFS retains no VFS grant after a reply, and every grant direction matches
  the exact path, name, read, write, or getdents data flow.
- Every VFS descriptor belongs to one exact application process record and
  names one live shared open-file description.
- VFS local vnode references and accumulated RAMFS references remain separate;
  final local release balances the exact backend count while the root keeps
  its mount pin.
- Absolute application paths start at the process root, relative paths start
  at its working directory, and both remain confined to that root.
- Application file and terminal data cross two separately authorized grant
  hops through resident VFS pages; no application grant becomes backend
  authority.
- A consuming console read begins only after VFS validates the complete
  application destination grant and mapping.
- Application directory records contain no RAMFS node handle or pointer.
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
- The immutable bootstrap manifest is versioned, pointer-free, stores at most
  seven entries, and is completely validated before manifest-directed service
  mutation; the production manifest has exactly six active entries.
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
- The service runtime does not retain syscall authority or results and does
  not initialize service readiness, process lifecycle, heap, TLS, arguments,
  or environment state.

## Source layout

The current responsibility-oriented hierarchy is:

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

## Scope boundary for the first shell milestone

The shell MVP includes isolated address spaces, separate process/thread/hart
objects, preemption, generation-aware IPC, one-shot reply rights, privileges,
direct grants, VM, PM, TTY, RAMFS, VFS, executable loading, init, and a shell.

It excludes thread creation, `fork`, signals, persistent filesystems, dynamic
drivers, DS, user-space scheduling policy, RS recovery, networking, SMP, and
NetBSD userland compatibility.

Those exclusions describe v0.1 ordering only. Accepted ADR-0081 makes complete
POSIX compatibility the long-term goal and requires an explicit replacement
point for every deferred MINIX/POSIX mechanism. MINIX source/binary
compatibility and copied source remain permanent non-goals.

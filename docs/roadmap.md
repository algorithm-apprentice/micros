# Roadmap

## Delivery model

Development proceeds through sequential pull requests. A later milestone does
not begin until the previous pull request has been reviewed and accepted.
Architecture or scope changes are recorded in ADRs before implementation.
Implementation follows the repository's documentation-first, test-first,
independently reviewed AI-native workflow. Each dependency-ready subsystem
first reproduces the documented MINIX behavioral baseline; optional
optimization follows only after baseline integration passes.

Current Milestone 2 progress includes trap recovery, supervisor timer
interrupts, the bootstrap frame allocator, the Sv39 kernel address space, and
generation-safe process/thread/hart identity tables with per-hart trap and
timer ownership. A typed per-frame ledger now binds allocator geometry to exact
kernel/process-generation owners and provides the staged one-way VM-handoff
gate. Generation-safe per-process Sv39 roots now share immutable kernel
subtrees while owning private, typed user mappings. U-mode entry, saved
execution contexts, and thread-owned kernel stacks now complete the first real
user round trip. The portable MINIX-style queues and accounting model now drive
repeated two-address-space switching under real timer interrupts, including a
real no-runnable idle transition and timer wake. Endpoint privileges and
generation-safe lifecycle gates are complete. The fixed message ABI and
dormant IPC state are present, and sender/receiver queue topology is validated.
Failure-atomic held-thread enqueue and matching delivery commit are
implemented. Incoming held senders and receivers consume the first compatible
FIFO peer, preserve unmatched peers, stage the canonical message, retain call
senders in reply wait, and make ordinary participants runnable through the
scheduler. Consuming a queued ordinary sender also stages its canonical
no-message `OK` completion, while a consumed call remains completion-free in
reply wait. A failure-atomic portable helper stages canonical no-message
success or dead-endpoint completion for target integration. Portable ordinary
send and receive now enforce exact active
endpoints, operation and target-profile authorization, canonical snapshots,
immediate matching before blocking, and specific or `ANY` FIFO selection.
Portable call now allocates monotonic nonzero reply tokens from fixed kernel
state, binds each token to the exact caller thread and callee generation,
delivers it through immediate or queued request paths, and retains the caller's
reply buffer while only reply wait remains. Portable reply now resolves an
opaque token with a bounded thread scan, requires the exact active callee and
reply operation, stages a canonical token-zero response, consumes the
one-shot right, and wakes the caller only when no independent run-time flag
remains. Another thread owned by the callee endpoint may use the token only
after the token-bearing request completion returns; an early attempt preserves
all state and reports a reply-token error. Validation rejects duplicate staged
copies carrying one call token. The token bypasses only the ordinary
send-target mask. These operations currently consume scheduler-held
non-current callers. Portable
`reply_receive` now shares reply's complete token and message preflight,
requires its distinct operation authority, validates a specific source or
`ANY` plus the receive buffer, and commits the caller reply together with
either one immediate FIFO sender or one receiver-queue insertion. Scheduler
and queue failures preserve both the token and complete state. Portable
notification now requires a nonzero event mask, exact active source and
destination generations, and notify-profile authority. It never blocks:
the first compatible specific or `ANY` receiver outside reply wait is staged
and awakened immediately, while all other events OR-coalesce by source slot.
Ordinary receive and `reply_receive` select the lowest matching pending source
before FIFO senders, revalidate its live generation, and stage the canonical
kernel envelope with a zero token and payload tail. Validators preserve the
bitmap/event relation and prevent source reuse while an event remains
pending. Bounded v0.1 deadlock detection now runs only after matching fails and
before send, receive, call, or `reply_receive` blocking mutation. It follows
`SEND`, then `REPLY`, then specific `RECEIVE`, treats `ANY` as no dependency,
resolves exact endpoint/process generations through the checked sole-live-
thread projection, rejects candidate returns as deadlock, and treats repeated
intermediate threads as corruption. `reply_receive` checks the graph after
simulating reply completion. Endpoint close now preflights every exact-
generation queue, token, notification, and staged message reference before one
non-failing cancellation commit. It unlinks held closing-process threads,
wakes foreign senders, specific receivers, and reply waiters with
`DEAD_ENDPOINT`, cancels caller/callee reply rights, clears pending
notifications in both directions, and preserves unrelated `ANY` receivers.
An owned staged `DEAD_ENDPOINT` result from an earlier close is discarded when
its endpoint later closes, allowing cancellation to cascade. Successfully
staged messages, notifications, or no-message completions must instead be
drained before close; their rejection is failure-atomic. The final commit
finishes through the ADR-0029 lifecycle close without exposing a partial
transition. ADR-0030's portable
acceptance evidence now includes one persistent replayable 8,192-transition
model with three multithreaded model processes. It compares complete IPC and
scheduler state after every mixed operation, denial, malformed input, close,
and reuse. An isolated QEMU gate exercises the same production portable
mechanism with three exact process generations and trusted kernel-owned
messages, then restores the object, endpoint, and scheduler baseline. Target
endpoint lifecycle and IPC component paths now share one authoritative,
failure-atomic, one-shot registry runtime. Portable no-message, message, and
failure completion shapes are now canonical. One shared selected-thread return
path preflights and commits them across scheduler start, ordinary U-trap,
captured IPC return, and idle wake. It performs bounded user-buffer copy,
retains the exact prevalidated physical chunks through commit, patches stable
`a0` results, stores the exact selected context, clears the completion, and
only then commits scheduler selection after timer/accounting preparation.
Malformed residual non-pending completion state is an invariant failure.
Production U-mode `ecall` routing now captures the six stable operation
numbers, advances `sepc`, preflights target arguments and buffers, executes the
current-thread guard transaction, maps stable results, stages immediate
completion, and selects ordinary or captured return. A focused QEMU gate proves
real instruction entry, successful notify commit, unauthorized-send rollback,
upper endpoint rejection, register preservation, and baseline restoration.
The complete three-address-space syscall gate now executes all six operations,
blocking/wakeup, token routing, atomic reply/receive, notification coalescing
and reply-wait exclusion, cross-page request/reply buffers, priority wakeups,
stable negative results, deferred completion across a timer-selected peer,
exact register results, and full teardown. An isolated image revokes an
accepted return buffer and proves the
exact `invalid-bootstrap-ipc-buffer` panic before scheduler commit.
ADR-0037's kernel-origin IPC mechanism now injects source-`NONE` event masks,
wakes `ANY` receivers without a notifier thread, OR-coalesces deferred masks,
and returns them through the shared selected-thread completion path.
Arbitrary-address
generation-bound translation and bounded two-page IPC message snapshot/write
are now implemented for that target boundary. The scheduler now provides a
reversible current-thread IPC guard with exact ready-queue and trap-stack
rollback plus head-preserving immediate commit.
ADR-0038's generation-safe kernel grant registry and lifecycle are now
implemented with exact participant authority, revoke-time generation advance,
terminal quarantine, failure-atomic endpoint cancellation, an authoritative
target runtime, a 4,096-transition model, and endpoint QEMU evidence. Checked
copy is the next separate design-implementation pair. The user-runtime syscall
ABI remains a later reviewed task.
ADR-0039's one-page-bounded checked copies are now implemented using exact
grant participants, retained at-most-two-page physical plans, explicit
direction and bounds, failure-atomic commit, native/model evidence, and a
three-address-space QEMU grant gate. This implementation is
initially bootstrap-phase-only; the separate ADR-0040 outcome supplies its
post-handoff mapping authority. The user syscall/runtime ABI remains outside
this outcome.
ADR-0040's replacement is now implemented for the statically embedded service
set:
every live bootstrap leaf must be staged `VM_WIRED` before ownership commit,
and read-only root validation, lookup, translation, and activation then use
exact wired process-generation ownership. Transferable-frame mapping, VM fault
delivery, and mutation remain later VM work. Native owner classification and
an isolated QEMU gate prove failure-atomic handoff, handed-off IPC/grant access,
revoked mutation, bootstrap-only generic context preparation, and a real
handed-off user return.
ADR-0041's unified RISC-V `ecall` boundary is now implemented. IPC operations
1 through 6 remain unchanged, while grant create, revoke, copy-from, and
copy-to use operations 7 through 10 with fixed register layouts, stable
generic results, and exact current-process authority. Native ABI tests, a
three-process bootstrap cleanup gate, and retained-state handed-off
integration prove real ecalls, stable failures, register preservation,
generation reuse, and both copy directions.
ADR-0042's freestanding user-service runtime is now implemented. It supplies
one fixed standalone ELF at the existing user window, loader-owned BSS and
external stack initialization, a no-argument service entry, one raw RV64
`ecall` function, typed wrappers for operations 1 through 10, direct stable
results without `errno`, and only `memcpy`/`memset` compiler support. Native
raw-stub tests prove exact argument, result, output, and token preservation.
The repository-owned ELF checker rejects forbidden segments, sections,
relocations, undefined symbols, startup mutations, and noncanonical raw-stub
instructions. The isolated three-process QEMU image proves the checked ELF
loader contract, startup, stack/data/BSS/rodata behavior, blocking IPC,
register preservation, grant lifecycle and copies, accidental-return trapping,
and complete cleanup.
ADR-0043's static bootstrap launcher is now implemented. The kernel validates
the pointer-free manifest and generated images, prepares every static service
while held, publishes exact profiles/endpoints/scheduler state atomically,
enforces token-bound readiness and guest-owned deadlines through operation 11,
and seals launcher authority through exact `SOURCE_ONLY` state. Native models
and three isolated QEMU workflows prove success, timeout, cyclic-manifest
failure, and stable diagnostics.

ADR-0045's VM server and one-way ownership handoff core are now implemented.
The kernel constructs one fixed VM boot-information/frame database, stages
every static user frame wired, validates exact VM-only operation 12, publishes
ownership irreversibly before generic VM readiness, and retains wired IPC and
grant-copy authority. A real launcher/VM/probe QEMU workflow proves independent
kernel/VM database agreement, post-handoff return, launcher sealing, and a
bidirectional checked-copy grant exchange. Running- and sealed-phase VM
self-fault workflows prove exact identity/register diagnostics, bootstrap
service-fault ordering, and the post-seal no-bootstrap-failure path. This
completes the static Step 9 boundary. Dynamic mappings and non-VM page-fault
delivery remain outside that minimal boundary.
PM's later tokenized preparation transition remains the only post-handoff
context-creation replacement.

ADR-0046 defines the dependency-ready PM model boundary. PM owns a bounded
application table, monotonic semantic PIDs, parent/child and init-reaper
relationships, spawn metadata, exit, zombie, and wait state. Launcher sealing
delivers one exact kernel-origin event that enables runtime PM policy.
ADR-0050 now extends PM-only kernel control across hidden process reservation,
token-bound execution preparation and rollback, atomic activation, ordered
exit detach, and empty-process release. An isolated launcher/VM/PM QEMU fixture
loads and runs a synthetic four-page child and returns every child resource to
baseline. The production PM/VFS/VM coordinator now adds ordinary protocol
composition, exact rollback, immediate PM publication commits, and ordered
exit/wait completion with a separate mixed native reference model. Its
[implementation map](architecture/pm-spawn-coordinator.md) records ownership
and validation. The checked application runtime is merged as #127;
the final dynamic PID-1 spawn/wait fixture now composes the dependency-ready
Step 14 production mechanisms.

The application-runtime slice is merged after the separately
reviewed ELF-checker correction #126. Its
[implementation map](architecture/application-runtime.md) fixes the separate
startup/decoder/normal-return/image owners and native/component evidence
under existing ADR-0050. Code now proves checked application entry and normal
return through the existing PM component, plus standalone image/seed parity.
Implementation #127 passed the complete author gate and independent review
and merged with the identical reviewed tree. The
[final ELF spawn integration map](architecture/elf-spawn-integration.md)
records the implemented six-production-service dynamic PID-1 gate.
Implementation #128 runs real post-freeze descriptor rollback, inherited
I/O, child normal exit and one-shot wait, complete post-init resource
equality and physical UART drain. Its merge requires the current complete
author gate and clean independent review; production init/shell remain later.

ADR-0047's static TTY boundary is now implemented. VM installs one exact
post-handoff UART device leaf before TTY release; TTY then initializes the
NS16550A, commits console ownership through operation 14, and acknowledges
readiness under the original begin-time deadline. The kernel claims PLIC
source 10, notifies only the bound TTY generation, and retains the source in
service until TTY drains the device and explicitly completes it. The real TTY
service provides bounded canonical input, interrupt-driven output, strict
request identifiers, explicit completion collection, and exact-direction
VFS grants. A five-service QEMU gate injects serial input only after an exact
TTY marker and proves canonical erase/CR handling, checked grants, retained
claim/completion evidence, physical UART drain, and clean shutdown. This
completes the Step 11 boundary; production VFS descriptors, RAMFS, init, and
shell integration remain later steps.

ADR-0048 defines the dependency-ready RAMFS boundary. One exact VFS peer
mounts a validated build-generated seed into a fixed writable node and sparse
data-block arena, then uses versioned lookup, create, mkdir, read, write,
getdents, and putnode calls. Generation-safe node handles, separate link and
reference counts, root-confined whole-path traversal, exact-direction
one-page grants, all-or-error writes, and complete-record directory cursors
preserve the selected MINIX behavior without adding a block layer. The design
is implemented with native seed/state tests, a replayable 8,192-transition
model, one six-service QEMU scenario, a standalone image check, and fail-closed
validation ownership.

ADR-0049 defines the dependency-ready production VFS boundary. VFS mounts
RAMFS once, owns fixed per-process descriptors, shared open-file descriptions,
vnode/backend-reference aggregation, root and working-directory routing, and
one synthetic console object. Application open, close, read, write, getdents,
mkdir, and chdir calls use one application grant hop and one separately
authorized RAMFS or TTY hop through two resident pages. A non-copying
grant-range preflight protects consuming reads, and VFS translates backend
directory records into a stable application format. The design requires a
replayable 8,192-transition model, a bounded standalone image, and one isolated
seven-process QEMU application probe while the production manifest remains
six services. Capacity seven preserves a 128-byte service configuration and
moves the VM boot object to an exact 364,704-byte layout.

ADR-0051 defines responsibility-based source organization before the next VFS
lifecycle increment. The portable VFS core must be decomposed by state
validation, ownership, transfer, namespace, regular I/O, console, process, and
spawn authority before Step 14 exit cleanup is added. The refactor preserves
the accepted VFS behavior. The VFS decomposition is now merged, and the
[repository source-module audit](architecture/source-module-audit.md)
classifies the remaining in-scope implementation/test source review signals
into separate subsystem design and implementation pairs rather than one
repository-wide rewrite. ADR-0052 defines the first correction: preserve the
existing VFS test executions while separating fixture, core, spawn, service,
model, target, kernel-hook, and fixture-generation source ownership before
exit-cleanup coverage is added. That correction and the exit-cleanup slice
are merged. The ADR-0053 design and implementation are also merged, applying
the exact
[kernel address-space source-organization contract](architecture/kernel-address-space-source-organization.md).
ADR-0054 defined the frame-ownership correction: apply the exact
[frame-ownership source-organization contract](architecture/frame-ownership-source-organization.md).
That design and implementation are merged. ADR-0055 defined the following
correction: apply the exact
[kernel VM-handoff source-organization contract](architecture/kernel-vm-handoff-source-organization.md)
before later PM and VM coordination extends operation 12 and its lifecycle
evidence. That design and implementation are merged. ADR-0056 defined the
following correction: apply the exact
[kernel PM-control source-organization contract](architecture/kernel-pm-control-source-organization.md)
before the cross-service coordinator extends operation 13 and its lifecycle
evidence. That design and implementation are merged. ADR-0057 defined the
following correction: apply the exact
[VM service source-organization contract](architecture/vm-service-source-organization.md)
before the coordinator extends VM loading and release behavior. That design
and implementation are merged. ADR-0058 defines the PM correction:
apply the exact
[PM service source-organization contract](architecture/pm-service-source-organization.md)
before the PM/VFS/VM coordinator. Its reviewed design is Accepted and merged.
The implementation now separates the specified lifecycle, adapter, native
evidence, kernel report, and generator owners, with empty migrations and
fail-closed structural enforcement. It does not add the later coordinator.

The next shared-mechanism audit correction is the kernel IPC design in
Accepted
[ADR-0059](adr/0059-kernel-ipc-source-decomposition.md) and its exact
[source contract](architecture/kernel-ipc-source-organization.md).
It is a behavior-preserving maintenance outcome over the already implemented
Step 6/7 mechanisms, not a new runtime dependency node. Its independently
reviewed design is Accepted and merged. The implementation separates the
specified portable, captured-syscall, native/reference-model, and target
owners, with empty migrations and fail-closed structural enforcement. It
adds no new IPC behavior or execution.

The bootstrap maintenance correction follows Accepted
[ADR-0060](adr/0060-kernel-bootstrap-source-decomposition.md), with the exact
[bootstrap source contract](architecture/kernel-bootstrap-source-organization.md).
It separates immutable validation, privileged preparation/rollback,
diagnostics, captured control, boot entry, launcher and native evidence
without changing the runtime graph or bootstrap behavior. Its independently
reviewed design is Accepted and merged. The implementation now exposes the
specified source owners and semantic phases with empty migrations, preserved
evidence and fail-closed structural enforcement. It adds no bootstrap behavior,
application runtime or dynamic PID-1 integration.

The reviewed trap/syscall correction follows Accepted
[ADR-0061](adr/0061-trap-and-syscall-source-decomposition.md) and its exact
[trap/syscall source contract](architecture/trap-syscall-source-organization.md).
It preserves the current trap/call algorithm, register custody, and runtime
evidence while exposing named authority and task phases. Its independently
reviewed design is Accepted and merged as #121. The implementation now
separates the specified owners with empty fourteen-symbol/nine-function
migrations, unchanged architecture and runtime surfaces, and fail-closed
source/build/evidence enforcement. It adds no application runtime or dynamic
PID-1 behavior.

The next selected high-risk dependency-ready correction is Accepted
[ADR-0062](adr/0062-kernel-scheduler-source-decomposition.md) and its exact
[scheduler source contract](architecture/kernel-scheduler-source-organization.md).
The design separates queue, IPC transaction, validation, planning/commit,
accounting, target completion/return/interrupt/idle, and independent evidence
owners while retaining all current behavior. It names 38 C sources, six
private contracts, six source sets, 138 moving definitions, and twelve
semantic long-function splits. Its independently reviewed design merged as
#123. Implementation now separates all specified portable, target-runtime,
and evidence owners with empty owner/function migrations and retained
execution identities. Its complete author gate and independent read-only
implementation review passed, and the implementation merged as #124 with an
identical validated/reviewed squash tree.
This does not add application startup, dynamic PID-1 integration, or a new
runtime dependency.

The next selected tooling correction is Accepted
[ADR-0063](adr/0063-user-elf-checker-source-decomposition.md) and its exact
[user ELF checker source contract](architecture/user-elf-checker-source-organization.md).
It separates parsing, structure, service policy, instructions, CLI, fixtures,
and case discovery without changing accepted service images or diagnostics.
The correction precedes application-checker reuse in `elf-spawn-app-runtime`;
it is not a new runtime-DAG node or an application-runtime implementation.
Independent design review is clean after correction of one image-inheritance
gap, and the design merged separately as #125. Implementation now separates
all specified owners and both semantic phase roots with literal-empty
migrations, unchanged fixture/case identities, and added CLI contracts.
Implementation #126 passed full author validation and clean independent
implementation review and is merged. Application
startup is not part of this source-only outcome.

The selected RAMFS maintenance correction is Accepted
[ADR-0066](adr/0066-ramfs-source-decomposition.md), with the exact
[RAMFS source contract](architecture/ramfs-source-organization.md).
It exposes portable state/codec/ownership/namespace/I/O/directory work,
native and independent-model evidence, target peer/report, and generator
phases without changing Step 12 behavior. Design is documentation-only;
independent design and bounded correction review are clean. Implementation
remains pending the separate design merge. This correction adds no production
init, shell, command, or runtime-DAG node.

## v0.1 completion goal

The independent validation-efficiency task follows Accepted
[ADR-0064](adr/0064-fail-closed-validation-evidence-reuse.md) and its
[exact evidence contract](architecture/validation-evidence.md).
Original design #129 is merged. Implementation #130 was abandoned after a
concrete default-configuration closure omission. Corrective design #131 and
Accepted [ADR-0065](adr/0065-closed-tool-default-configuration-for-validation.md)
are merged; the fresh implementation follows both contracts and the canonical
source/evidence map.
It does not alter the runtime DAG, reduce mandatory coverage or authorize
receipt reuse before the separate implementation merges.

`micros` v0.1 is complete when a clean checkout builds with the documented
LLVM/LLD/CMake/Ninja toolchain, boots under RISC-V64 QEMU `virt` through
OpenSBI, and reaches a shell that runs `echo`, `cat`, `ls`, and `ps`.

The system must enforce user isolation and preemption and must include
generation-aware IPC, one-shot reply rights, privilege profiles, direct grants,
the static launcher, VM, PM, TTY, RAMFS, VFS, ELF spawn, init, and automated
native/QEMU/end-to-end tests.

Although v0.1 runs one user thread per process on one hart, process, thread,
endpoint, and hart state must remain distinct. No v0.1 implementation may make
multithreading or SMP require replacing the process identity, scheduler object
model, IPC reply model, or global current-execution representation.

## Milestone 0: design baseline

### Deliverables

- architecture overview;
- MINIX dependency analysis;
- development DAG;
- testing strategy;
- initial ADR set;
- repository documentation index.

### Exit criteria

- every initial ADR is reviewed and either Accepted, revised, or Rejected;
- the shell MVP boundary is unambiguous;
- the implementation order contains no dependency cycle;
- no implementation files are committed before this gate.

## Milestone 1: reproducible boot

### Deliverables

- RISC-V cross-compilation configuration;
- linker script and boot image;
- OpenSBI/QEMU launch command;
- bounded FDT parser for memory and reserved ranges;
- UART output;
- panic path;
- deterministic test shutdown.

### Exit criteria

- a clean checkout builds with one documented command;
- QEMU prints a versioned boot marker;
- an intentional panic prints location and machine state;
- the host gates distinguish success, explicit failure, panic, unexpected exit,
  and timeout.

## Milestone 2: privileged kernel mechanisms

### Deliverables

- trap entry and exception decoding;
- timer interrupts;
- bootstrap frame allocator;
- typed bootstrap frame ownership and atomic handoff classification;
- kernel and user page tables;
- user-mode entry;
- separate process, thread, endpoint, and hart objects;
- thread execution contexts;
- preemptive round-robin scheduler.

### Exit criteria

- allocator and typed-ownership invariants pass randomized host tests;
- expected exceptions return safely;
- U-mode cannot access kernel-only pages;
- two user threads can be preempted repeatedly without register corruption.

## Milestone 3: protected communication

### Deliverables

- process slots and generation-aware endpoints;
- immutable privilege profiles;
- one-shot reply tokens bound to caller threads;
- blocking send, receive, call, reply/receive, and notification;
- deadlock-chain detection;
- direct grants and safe-copy operations;
- unified user grant syscalls;
- wired post-handoff address resolution for initial services;
- one fixed standalone service ELF and freestanding user-service runtime.

### Exit criteria

- stale endpoints and unauthorized IPC are rejected;
- every blocking transition has a tested wakeup path;
- deadlock tests terminate deterministically;
- grant direction, bounds, overflow, endpoint, and lifetime checks pass;
- real U-mode grant create, revoke, and both copy directions preserve stable
  results and non-result registers;
- wired service roots retain activation, IPC-buffer, and checked-copy access
  after the irreversible ownership handoff;
- one independent user ELF has exact RX, R, and RW/NX load closure, no
  relocation or hosted-libc dependency, loader-zeroed BSS, and an external
  aligned stack;
- startup establishes the reviewed `gp`/`tp` policy, enters the no-argument C
  service function, and traps deterministically if that function returns;
- host-stub tests cover typed wrappers for operations 1 through 10, and one
  real QEMU image proves those production operations through the standalone
  runtime while preserving registers, stack state, initialized data, BSS, and
  read-only data;
- no protocol relies on raw pointers crossing an address space.

## Milestone 4: bootstrap and memory service

### Deliverables

- pointer-free, versioned embedded service manifest with seven-entry storage
  and an exact six-entry production chain;
- generated immutable references to already checked service images;
- deterministic explicit-prerequisite topology and cycle rejection;
- kernel preparation of every static root, image, stack, context, reserved
  endpoint, and held thread before launcher entry;
- bootstrap launcher with manifest-bound release authority only;
- atomic exact-profile installation, endpoint publication, and thread release;
- one exact-generation readiness call and token-bound acknowledgment;
- guest-owned readiness deadlines and stable fatal diagnostics;
- exact manifest privilege, UART-page, and IRQ-source assignments;
- irreversible launcher authority revocation after the last static service;
- VM server;
- one-way ownership handoff from bootstrap memory to VM.

### Exit criteria

- manifest validation rejects unknown versions, malformed entries, missing
  profiles or images, invalid resource fields, missing prerequisites, and
  cycles before manifest-directed service mutation;
- capacity-seven consumers agree on the exact 128-byte service configuration
  and 364,704-byte VM boot-information offsets, active counts, zero tails, and
  digest extent;
- launcher order is deterministic and independent from source array order;
- unreleased endpoints remain hidden and held services remain off ready queues;
- release installs only the exact named immutable profile and publishes one
  endpoint generation atomically with exact scheduler admission;
- malformed, foreign, early, duplicate, and stale readiness cannot release a
  dependent;
- a missing readiness response fails inside the guest with the exact service
  identity and no host-timeout dependence;
- failure before publication preserves or rolls back hidden state, while
  failure after publication is fatal with no recovery fallback;
- VM's generic ready transition follows the irreversible ownership handoff;
- TTY release and readiness retain the ADR-0012 begin/commit gates;
- final completion holds the launcher, clears the exact controller binding,
  and makes bootstrap authority impossible to reintroduce while its retained
  source-only endpoint rejects new destinations until later teardown;
- every usable physical frame has exactly one owner;
- user mapping changes after handoff require VM authority;
- VM's complete fault-handling working set remains wired;
- VM failure while launcher bootstrap is running is a fatal bootstrap error;
  after launcher sealing, VM self-fault uses its exact non-bootstrap fatal
  diagnostic and never recurses into VM.

## Milestone 5: core user-space services

### Deliverables

- PM with application identity, hidden process reservation, spawn metadata,
  exit, zombie, wait, and rollback;
- interrupt-driven TTY with early-console and PLIC handoff;
- RAMFS;
- VFS with descriptors, a synthetic console object, pathname routing, and a
  root mount.

### Exit criteria

- PM starts through the static launcher after VM handoff and receives one
  exact bootstrap-sealed runtime event;
- semantic PIDs, parent/child relationships, init reparenting, zombie
  retention, exact-child and any-child wait, and one-shot wait replies pass a
  deterministic native model;
- PM's initial kernel authority can reserve and abort only one empty hidden
  process, releases every live reservation resource without changing
  endpoint/scheduler/ownership state, and leaves exactly one consumed process
  generation and transaction stale;
- no Milestone 5 PM outcome claims a successful spawn or allocates post-handoff
  user memory before the reviewed VM mapping protocol;
- TTY release follows console begin and one exact VM-installed UART device
  leaf, while TTY readiness follows ownership commit;
- one guest-owned manifest deadline covers the complete console begin through
  readiness sequence without a pre-release timeout gap;
- PLIC source 10 remains in service from kernel claim through exact TTY
  acknowledgment after UART draining;
- deterministic QEMU input and output prove canonical CR/LF and erase
  processing, empty-to-nonempty transmit start, interrupt-driven
  receive/transmit, and no routine kernel UART output after begin;
- service protocols reject malformed types and payload lengths;
- client termination releases server-owned state;
- terminal data and file data use exact-direction grants;
- non-transitive data paths use bounded resident bounce buffers;
- init receives working descriptors 0, 1, and 2 without RAMFS device nodes;
- RAMFS validates a reproducible seed before readiness and accepts one exact
  VFS mount;
- RAMFS generation, parent/name, link/reference, block ownership, sparse-file,
  capacity, and failure-atomic invariants pass deterministic native tests;
- a replayable RAMFS model covers at least 8,192 mixed mount, namespace, I/O,
  directory-cursor, release, malformed, stale, capacity, and grant-failure
  transitions;
- one QEMU integration proves the real RAMFS service's seed lookup, writable
  sparse file, complete-record directory cursor, exact grants, balanced
  references, VFS readiness, launcher sealing, and TTY-routed pass marker;
- VFS resolves absolute and relative paths and enforces exact process-local
  descriptor ownership;
- shared open-file descriptions own regular-file offsets and directory
  cursors, while vnode-local and RAMFS reference counts remain balanced;
- application read destinations are completely prevalidated before a
  consuming TTY hop;
- non-copying grant validation preserves checked-copy error precedence, and
  global second-hop grant exhaustion returns `NO_SPACE` without backend
  submission;
- file and terminal data cross application-to-VFS and VFS-to-backend grants
  through resident pages without transitive authority;
- complete RAMFS directory records become complete fixed application records
  with no exposed backend handle;
- pending console reads, completions, cancellation, and writable retry leave
  the VFS receive loop available, including exact
  `CANCEL/REQUEST -> COLLECT/OK` cleanup;
- a replayable VFS model covers at least 8,192 mixed lifecycle, descriptor,
  path, I/O, directory, console, malformed, capacity, and injected-failure
  transitions; and
- one isolated QEMU application probe proves both grant hops, descriptor
  access, absolute/relative routing, translated directories, canonical console
  input, TTY output, a no-authority resident-output drain barrier, zero live
  grants, and clean launcher sealing without claiming production spawn.

## Milestone 6: shell MVP

Milestone 6 is internally gated. The ELF spawn path is implemented and
reviewed first with an isolated dynamic PID-1 fixture and child application.
Production init, shell policy, and commands begin only after that fixture
proves failed-spawn rollback, successful activation, normal exit, wait, and
complete child-resource release.

### Deliverables

- ELF-spawn gate:
  - bounded static RISC-V ELF loading;
  - post-handoff VM allocation, scratch initialization, mapping, freeze,
    rollback, and release for one reserved child;
  - target-proven VM scratch preemption, inactive-root validation,
    reactivation, and stale-translation removal;
  - PM/VFS/VM spawn transaction with explicit descriptor duplication and
    complete preactivation rollback;
  - PM-only thread, endpoint, application-profile, context, and activation
    transitions;
  - separate argv/env application startup and normal-return exit;
  - RISC-V instruction-fetch synchronization before a child runs; and
  - deterministic dynamic-parent/child QEMU resource-baseline proof;
- shell gate:
  - production init;
  - interactive shell;
  - `echo`, `cat`, `ls`, and `ps`; and
  - automated serial end-to-end scenario.

### Exit criteria

- the ELF-spawn gate proves one post-freeze failed spawn leaves no resource,
  one valid child receives exact argv/env and descriptors, and child
  exit/wait returns to the post-init baseline;
- a QEMU component preempts VM with the scratch leaf present, activates a
  distinct Sv39 root, reactivates VM through the ordinary scheduler path, and
  proves `SCRATCH_COMMIT` removes both the PTE and usable translation;
- the complete system boots from a clean build;
- the required command scenario succeeds without manual timing assumptions;
- process exit and wait leave no process, frame, descriptor, or inode leak;
- repeated scenario runs produce the same externally visible result.

## Milestone 7: resilient control plane

### Deliverables

- DS endpoint publication;
- user-space scheduler policy;
- RS lifecycle management;
- service crash injection and restart.

### Exit criteria

- service identity changes invalidate stale endpoints;
- dependencies observe a controlled restart notification;
- injected restart tests preserve or explicitly discard state according to the
  service contract;
- bootstrap-only authority is not reintroduced.

## Later milestones

These require separate ADRs and are not part of the shell MVP:

- `exec` completion and `fork`;
- multiple threads per process;
- copy-on-write;
- signals;
- VirtIO block devices;
- a persistent filesystem;
- file-backed memory mappings;
- POSIX conformance expansion;
- NetBSD userland port evaluation;
- networking;
- SMP;
- physical RISC-V hardware.

## Major risks

| Risk | Consequence | Mitigation |
| --- | --- | --- |
| Hidden bootstrap cycle | A service cannot start without one of its successors | Validate explicit manifest prerequisites, reject cycles, and release in one deterministic topological order |
| ABI churn | Every service changes at once | Accept endpoint, message, and grant ADRs before service code |
| Assembly bugs | Corruption appears far from the cause | Dedicated QEMU context and trap tests with known register patterns |
| Memory ownership ambiguity | Leaks or double allocation | Central ownership ledger and assertions before/after VM handoff |
| Slow feedback | Kernel bugs become expensive to isolate | Keep pure logic host-testable and QEMU smoke tests short |
| Unbounded source-level MINIX fidelity | MVP imports unrelated architecture, compatibility, or service breadth | Reproduce documented behavior and authority only, while keeping the DAG and non-goals |
| Future concurrency requires redesign | Thread or SMP work replaces process and IPC foundations | Separate process/thread/hart objects and reply rights in v0.1 |
| Silent service failure | Boot hangs without a diagnosis | Use one guest-owned counter deadline, exact service identity, and a stable fatal bootstrap record |
| Host-only assumptions | macOS build works but CI or target behavior differs | Separate host and target toolchains and test on Linux CI |

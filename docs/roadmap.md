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
and returns them through the shared selected-thread completion path. PLIC
routing, manifest authority, `irq_complete`, console handoff, and TTY remain
separate later work. Arbitrary-address
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
generation reuse, and both copy directions. Runtime wrappers and startup
remain the next separately reviewed user-service-runtime outcome.
ADR-0042 now supplies the accepted design: one fixed
standalone ELF at the existing user window, loader-owned BSS and external
stack initialization, a no-argument service entry, one raw RV64 `ecall`
function, typed wrappers for operations 1 through 10, direct stable results
without `errno`, and only `memcpy`/`memset` compiler support. It remains
unimplemented and cannot unblock the static launcher until the ADR is
accepted and the separate implementation outcome passes native, static, and
QEMU runtime acceptance.
PM's later tokenized preparation transition remains the only post-handoff
context-creation replacement.

## v0.1 completion goal

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

- embedded service manifest;
- bootstrap launcher;
- service readiness protocol;
- exact manifest privilege and device assignments;
- VM server;
- one-way ownership handoff from bootstrap memory to VM.

### Exit criteria

- launcher order is deterministic;
- a missing readiness response fails with a useful diagnostic;
- every usable physical frame has exactly one owner;
- user mapping changes after handoff require VM authority;
- VM's complete fault-handling working set remains wired;
- VM failure is reported as a fatal bootstrap error.

## Milestone 5: core user-space services

### Deliverables

- PM with spawn metadata, exit, and wait;
- application privilege installation during spawn;
- interrupt-driven TTY with early-console and PLIC handoff;
- RAMFS;
- VFS with descriptors, a synthetic console object, pathname routing, and a
  root mount.

### Exit criteria

- service protocols reject malformed types and payload lengths;
- client termination releases server-owned state;
- terminal data and file data use grants;
- non-transitive data paths use bounded resident bounce buffers;
- init receives working descriptors 0, 1, and 2 without RAMFS device nodes;
- RAMFS operations pass native model tests and QEMU integration tests;
- VFS resolves absolute and relative paths and enforces descriptor ownership.

## Milestone 6: shell MVP

### Deliverables

- ELF loading;
- PM/VFS/VM spawn transaction with rollback;
- RISC-V instruction-fetch synchronization before a child runs;
- init;
- interactive shell;
- `echo`, `cat`, `ls`, and `ps`;
- automated serial end-to-end scenario.

### Exit criteria

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
| Hidden bootstrap cycle | A service cannot start without one of its successors | Keep bootstrap interfaces explicit and one-way |
| ABI churn | Every service changes at once | Accept endpoint, message, and grant ADRs before service code |
| Assembly bugs | Corruption appears far from the cause | Dedicated QEMU context and trap tests with known register patterns |
| Memory ownership ambiguity | Leaks or double allocation | Central ownership ledger and assertions before/after VM handoff |
| Slow feedback | Kernel bugs become expensive to isolate | Keep pure logic host-testable and QEMU smoke tests short |
| Unbounded source-level MINIX fidelity | MVP imports unrelated architecture, compatibility, or service breadth | Reproduce documented behavior and authority only, while keeping the DAG and non-goals |
| Future concurrency requires redesign | Thread or SMP work replaces process and IPC foundations | Separate process/thread/hart objects and reply rights in v0.1 |
| Silent service failure | Boot hangs without a diagnosis | Readiness timeouts and structured serial events |
| Host-only assumptions | macOS build works but CI or target behavior differs | Separate host and target toolchains and test on Linux CI |

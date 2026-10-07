# Testing Strategy

## Objective

No test suite can prove that a general-purpose kernel is free of defects. The
`micros` strategy builds confidence through independent layers:

- compiler and static checks;
- fast native unit and property tests;
- QEMU kernel component tests;
- multi-service integration tests;
- end-to-end shell scenarios;
- assertions and ownership invariants;
- deterministic stress and fuzz testing.

Formal models may later be used for small critical protocols. Full-system
formal verification is outside the MVP.

## Test-first development protocol

Behavior changes use Red-Green-Refactor at the layer that can measure the real
requirement:

1. write a failing native, QEMU, integration, or end-to-end test;
2. confirm that it fails for the expected missing behavior rather than a broken
   harness;
3. implement the smallest production change that makes it pass;
4. refactor while the relevant suite remains green;
5. retain the test as regression coverage.

Privileged and hardware behavior is not forced into a host unit test. Its Red
step is a failing QEMU component or acceptance test. Documentation-only changes
do not require an invented runtime test. Exploratory spikes are not merged.

The full task and review loop is defined in
[the AI-native development workflow](development/ai-native-workflow.md).

## Test layers

| Layer | Environment | Primary purpose | Expected cadence |
| --- | --- | --- | --- |
| Compile checks | Host | Warnings, ABI assertions, freestanding violations | Every build |
| Unit tests | Native host process | Algorithms, parsers, state machines | Every local change |
| Kernel component tests | QEMU test image | Traps, MMU, contexts, IRQ, IPC, grants | Every applicable pull request |
| Integration tests | Full QEMU image | Server protocols and transactions | Every applicable pull request once all participating services exist |
| End-to-end tests | Full QEMU image | Boot and shell-visible behavior | Every applicable pull request after shell exists |
| Stress and fuzz tests | Host and QEMU | Long sequences, malformed data, rare transitions | Scheduled or explicit |
| Hardware tests | Physical target | Behavior not represented by QEMU | Post-MVP |

## Planned test commands

The build system should expose stable intent-based targets:

```text
ninja test-unit
ninja test-qemu-smoke
ninja test-qemu-integration
ninja test-stress
```

Performance budgets are:

- incremental `test-unit-fast`: normally below 6 seconds;
- persistent native models: isolated from the fast loop and run in the PR or
  milestone tier;
- QEMU smoke suite: normally below 10 seconds;
- QEMU integration suite: kept short enough for every applicable pull request;
- stress suite: unconstrained by the fast feedback budget and run separately.

Budgets are review signals, not reasons to hide necessary coverage.

The native validation tiers are `test-unit-fast`, `test-ipc-model`, and the
complete `test-unit` gate. The implemented QEMU targets are `test-qemu-smoke`,
`test-qemu-panic`, `test-qemu-trap`, `test-qemu-timer`,
`test-qemu-frame-allocator`, `test-qemu-trap-panic`, `test-qemu-mmu`,
`test-qemu-object-model`, `test-qemu-endpoint`,
`test-qemu-ipc`, `test-qemu-ipc-ecall-core`,
`test-qemu-ipc-syscall`, `test-qemu-ipc-syscall-panic`,
`test-qemu-nested-trap`, and
`test-qemu-frame-ownership`, `test-qemu-user-address-space`, and
`test-qemu-user-execution`, `test-qemu-scheduler`,
`test-qemu-scheduler-invalid-outgoing`, and
`test-qemu-scheduler-invalid-next`. From a clean checkout,
the implemented configure, build, and execution gates are:

```bash
cmake --workflow --preset test-unit-fast
cmake --workflow --preset test-ipc-model
cmake --workflow --preset test-unit
cmake --workflow --preset test-qemu-smoke
cmake --workflow --preset test-qemu-panic
cmake --workflow --preset test-qemu-trap
cmake --workflow --preset test-qemu-timer
cmake --workflow --preset test-qemu-frame-allocator
cmake --workflow --preset test-qemu-trap-panic
cmake --workflow --preset test-qemu-mmu
cmake --workflow --preset test-qemu-object-model
cmake --workflow --preset test-qemu-endpoint
cmake --workflow --preset test-qemu-ipc
cmake --workflow --preset test-qemu-ipc-ecall-core
cmake --workflow --preset test-qemu-ipc-syscall
cmake --workflow --preset test-qemu-ipc-syscall-panic
cmake --workflow --preset test-qemu-nested-trap
cmake --workflow --preset test-qemu-frame-ownership
cmake --workflow --preset test-qemu-user-address-space
cmake --workflow --preset test-qemu-user-execution
cmake --workflow --preset test-qemu-scheduler
cmake --workflow --preset test-qemu-scheduler-invalid-outgoing
cmake --workflow --preset test-qemu-scheduler-invalid-next
```

`test-unit-fast` runs the FDT parser corpus, portable frame allocator,
typed frame-ownership ledger, Sv39 encoding, kernel-object lifecycle/model
tests, and deterministic scheduler admission, RTS, priority-queue, policy,
current-selection, preemption-repair, return-plan, and separate
thread/kernel/idle accounting tests plus a replayable 4,096-step two-hart
reference model. They also cover reversible current-thread IPC detach,
idle/thread trap-stack anchor transitions, exact rollback, same-priority head
preservation, and higher-priority wakeup selection. Endpoint tests cover
endpoint encoding, immutable privilege-profile tables,
process-bound lifecycle, stale-generation rejection, relationship validation,
authorization, and deterministic endpoint/IPC regressions under ASan and
UBSan. It also verifies the exact 64-byte IPC message ABI and zero-state
invariants for dormant thread and endpoint IPC storage. The native host tests
also cover generation-safe sender/receiver FIFO topology, exact queue
membership, stale links, cycles, duplicate membership, and close rejection
while queues or exact-source/reply waiters remain attached. Canonical queued
call and queue-less reply-wait shapes, unique token values, reserved message
types, and impossible already-matchable queue pairs are validated too.
Failure-atomic enqueue tests prove canonical source/token overwrite, FIFO tail
append, duplicate-token rejection, and detection of a matching peer before
blocking. Staged-delivery tests require an exact active source generation,
user message type, aligned retained receive buffer, runnable scheduler state,
and no residual queue or reply authority. Matching-delivery regressions cover
head, middle, and tail dequeue, specific and `ANY` selection, unmatched FIFO
preservation, ordinary versus call sender wake behavior, exact staged message
bytes, canonical no-message `OK` completion for consumed ordinary senders, no
premature call completion, ready-queue placement, stale and corrupt queues,
no-match preservation, and byte-exact state and output preservation on every
failure. Direct completion tests require zero message/buffer state and
duplicate-staging atomicity. Authorized
send/receive operation tests add exact active source and destination
resolution, receive/send operation and target-profile enforcement, one
canonical message snapshot, immediate matching before blocked enqueue,
specific and `ANY` FIFO selection, explicit scheduler-held non-current caller
requirements, and complete state preservation on every rejected transition.
Authorized call tests additionally prove fixed-state monotonic nonzero token
allocation, low-level mint/reuse rejection, no wrap at exhaustion, exact
live-caller/source/callee binding for staged nonzero tokens, rejection of
queued token bindings, preservation of independent receiver run-time flags,
canonical token delivery through immediate and queued paths, reply-only wait,
retained reply buffers, and byte-exact failure preservation including
scheduler-transition rejection.
Authorized reply tests prove bounded opaque-token lookup, exact caller-thread
and callee-generation matching, rejection before queued calls enter reply
wait or before the token-bearing request completion returns, active endpoint
and reply-operation enforcement, token-authorized bypass of an absent ordinary
send-target permission, canonical replying source and zero delivered token,
one-shot consumption, preservation of independent caller run-time flags,
wakeup only at the zero boundary, validator rejection of noncanonical or
duplicate staged call tokens, and complete state/token preservation on every
failure including a late scheduler preflight rejection. The seeded endpoint
model also exercises allowed and denied reply-operation authorization.
Authorized reply/receive tests prove that the combined operation has distinct
authority, reuses the complete reply token and message preflight, accepts a
specific active source or `ANY`, preserves unmatched FIFO senders, retains a
consumed queued call in reply wait, and atomically chooses immediate delivery
or receiver blocking. They also exercise canonical replies and requests,
validator rejection of corrupted final states, later delivery to the blocked
receiver, and byte-exact token/state preservation for invalid receive
arguments and late caller or sender scheduler failures. The seeded endpoint
model exercises allowed and denied reply/receive-operation authorization.
Authorized notification tests prove nonzero-mask validation, exact active
source and destination generations, operation and target-profile authority,
immediate first-compatible receiver wakeup, call-reply exclusion, nonblocking
notifier return, per-source OR coalescing, and bitmap/event agreement. Receive
and reply/receive tests require lowest-slot pending selection before blocked
senders, exact source-generation revalidation, and a canonical reserved kernel
message containing the little-endian 64-bit event mask with a zero token and
zero payload tail. Corruption, pending-source close, stale-source, and late
scheduler failures preserve all state. A replayable 4,096-step notification
model compares coalescing and specific/`ANY` selection with a compact
reference.
Deterministic deadlock tests exhaust every realizable chain length across the
64 endpoint slots and independently exercise the
`MICROS_THREAD_CAPACITY`-step reference bound. They cover `SEND` before
`REPLY` before specific `RECEIVE`, `ANY` termination, exact generation
resolution, zero/multiple-thread projection rejection, candidate-return
cycles, repeated intermediate corruption, and the post-reply graph used by
`reply_receive`. Every deadlock or corruption result preserves the complete
registry, object state, tokens, messages, and outputs.
Endpoint-close cancellation tests combine closing-owned send, receive, call,
notification, and staged-delivery state with foreign queued senders, specific
receivers, reply waits, pending notifications, and already staged messages.
They require one atomic transition to detach the closing process, wake exact
foreign dependents with `DEAD_ENDPOINT`, preserve unrelated `ANY` receivers,
cancel caller/callee tokens, and clear notification state in both directions;
an owned staged `DEAD_ENDPOINT` result is discarded by a later cascading
close, while successfully staged messages, notifications, and no-message
completions reject close before mutation until drained. Separate stale
queue-generation, token-generation, notification-source, staged-source,
held-thread, and late scheduler failures prove byte-exact preservation. A
deterministic seeded 512-case scenario sweep creates a fresh fixture for each
selected cancellation class and checks staged-delivery preflight plus
unrelated-state preservation.
The final portable acceptance model retains one fixture for 8,192 mixed
transitions. It uses seven process generations, including three processes with
two model threads each, and independently compares complete run-time flags,
sender/receiver queues, reply tokens, notification masks, staged messages and
results, plus ready-queue/current scheduler state after every transition.
Its trace covers all IPC operations, close/reuse, profile denial, malformed
input, ordinary-send completion drain, reply-before-request-return rejection,
and deadlock, and prints the replayable seed plus a trace hash on success and
the recent complete operation trace on failure.
The Python host tests include ELF allocatable-section closure, legacy-global
rejection, and machine-readable QEMU record regressions.
`test-ipc-model` contains the replayable endpoint lifecycle, notification,
close-cancellation, and persistent 8,192-transition IPC models. `test-unit`
combines both tiers and remains the complete native gate.

`tools/validation_plan.py` maps committed, staged, unstaged, and untracked
paths to `fast`, `pr`, or `full` execution plans. Documentation-only changes
avoid target builds. Code PR plans always include complete `test-unit`.
Unknown non-documentation paths, mixed mapped/unmapped changes, and shared
toolchain, linker, target-entry, conditional multi-image, post-link, public
header, or QEMU-harness changes run the full tier. Explicit paths are unioned
with Git discovery; rename discovery classifies both paths. Execution rejects
remaining untracked files. Every plan ends with separate index, worktree, and
branch-range diff checks.
`test-qemu-smoke` verifies the real OpenSBI handoff, exact object/trap
readiness, FDT memory discovery, agreement between decoded range counts and
emitted range events, a nonempty firmware reservation result, allocator and
MMU readiness, exact agreement between reachable kernel page tables and typed
ownership, and clean SBI shutdown. The remaining stable targets are added when
their dependency-DAG layers become implementation-ready.

`test-qemu-panic` builds an isolated test image and verifies ordered source,
hart, and machine-state diagnostics plus clean fatal shutdown. Panic output
does not turn a timeout, explicit failure, missing FDT evidence, or malformed
diagnostic stream into a pass.

`test-qemu-trap` verifies the real direct-mode entry and `sret` paths with two
distinct x1-x31 register patterns, two stacks, an observable status change, and
an exact expected illegal-instruction label. `test-qemu-timer` verifies
expired-only return preparation, failure-atomic timer programming, three
accepted supervisor timer expirations, two successful interrupt rearms, final
disarm, and the SIE-clear wait handshake.
`test-qemu-frame-allocator` verifies production FDT/linker reservation
integration, deterministic allocation and release from the live post-MMU
baseline, preservation of retained table bits, exact free-count restoration,
and runtime memory sizing by booting one ELF at 128 MiB and 256 MiB.
`test-qemu-trap-panic` verifies that an unarmed exception retains a separately
captured trap frame and that its `sepc` equals the fault symbol in the built
ELF. `test-qemu-mmu` verifies exact recovery from a store page fault against RX
text and an instruction page fault from RW/NX kernel data.
`test-qemu-object-model` exercises the production process/thread/hart registry,
including generation advance, stale rejection, the one-thread policy, and
hart-local current-thread state. `test-qemu-endpoint` exercises reserved versus
active visibility, immutable profile installation, asymmetric
call/send/notify authorization, bound-process release rejection, stale
endpoint rejection after generation advance, failed runtime-initialization
preservation, and complete baseline restoration through the authoritative
target registry. `test-qemu-ipc` uses that same target registry with three
exact production process generations,
three distinct generation-bound Sv39 roots, saved integer contexts, complete
thread-owned kernel-stack patterns, scheduler state, and trusted kernel-owned
messages before the syscall ABI exists. It proves immediate and blocked
send/receive, canonical no-message completion for consumed queued senders,
exact call/reply token routing and one-shot use, atomic `reply_receive`,
notification coalescing and call-reply exclusion, deadlock rejection, close
cancellation, stale generation rejection after reuse, authorization denial,
and final endpoint/object/hart/frame baseline restoration while roots,
contexts, stacks, scheduler state, and messages remain preserved.
`test-qemu-ipc-ecall-core` executes the production U-mode ecall route. It
proves stable operation/result numbering, exact `sepc + 4`, preserved
non-result registers, target-buffer snapshot before guard begin, immediate
notify completion through captured return, unauthorized-send rollback through
ordinary return, upper endpoint-bit rejection before mutation, and complete
endpoint/object/address-space/frame baseline restoration.
`test-qemu-ipc-syscall` runs three real address spaces through all six
operations. It proves blocked and immediate send/receive, two tokenized call
round trips, atomic `reply_receive` plus later sender wake, notification
coalescing while the destination is in reply wait, pending notification
delivery, page-crossing request/reply buffers, higher-priority wake selection,
stable `-3`, `-1`, and `-6` results, deferred completion across a timer-selected
peer, exact `sepc + 4`, preserved non-result registers, and complete
endpoint/thread/root/frame cleanup.
`test-qemu-ipc-syscall-panic` releases a previously accepted receive-buffer
page after sender commit but before selected-thread return preflight. It
requires the exact `invalid-bootstrap-ipc-buffer` panic and trap context.
The ADR-0037 kernel-origin notification slice extends the native notification
model and `test-qemu-ipc`. It must prove source-`NONE` immediate delivery,
pending-mask coalescing, `ANY`-only consumption, reply-wait exclusion,
state-preserving failures, selected-thread completion return, and complete
baseline restoration. That evidence does not claim PLIC routing,
`irq_complete`, console handoff, or TTY behavior.
`test-qemu-nested-trap`
injects a second fault
after the per-hart `sscratch` sentinel is armed and proves that the registered
emergency stack is selected without trusting interrupted `tp`.
`test-qemu-frame-ownership` exercises the production ledger and object
registry together: stale and cross-process owners are rejected, process
release remains blocked while exact owners exist, forbidden handoff plans are
failure-atomic, and one complete plan seals bootstrap mutation in a single
transition.
`test-qemu-user-address-space` constructs two generation-bound roots, maps the
same virtual page to distinct typed frames, switches ASID-zero roots with exact
fences, checks SUM-gated hardware access, proves full-page zeroing on reuse,
rejects active/stale/corrupt roots without mutation, atomically tears roots
down, and verifies every process-root API is revoked after ownership handoff.
It also translates arbitrary aligned user addresses and proves failure-atomic
64-byte IPC snapshot/write across a page boundary, including range, alignment,
mapping, and read/write permission rejection.
`test-qemu-user-execution` enters a relocation-free payload in U-mode, proves
user-stack access and kernel-page isolation, captures every integer register
on a thread-owned supervisor stack, uses scheduler-owned admission/current
selection and accounting, resumes a modified user frame through the common
return plan, and returns through an interrupt-disabled supervisor continuation
while preserving caller state.
`test-qemu-scheduler` runs two private address spaces under real
supervisor timer delivery. It proves failure-atomic timer start,
return-boundary timer preparation, queue-reachable current ownership, separate
thread/kernel/idle accounting, repeated equal-priority alternation, complete
integer-register preservation, a forced spurious idle iteration, and a later
real timer wake. The same gate now proves shared IPC completion preflight and
commit across scheduler start, ordinary U-return, captured IPC return, and idle
wake; deferred message copy through a validated user buffer; stable success and
dead-endpoint `a0` results; retained prevalidated physical chunks with no
post-timer address-space revalidation; rejection of malformed residual
non-pending state; timer-before-completion ordering; and exact completion
clearing.
The two isolated invalid-context gates require a U-origin timer panic with
exact diagnostics proving the outgoing or selected context failed before any
return-plan mutation.

## Native unit tests

Code should run natively when its correctness does not depend on actual
privilege levels or device behavior. Target and host builds use the same
implementation source where practical.

Initial native test subjects include:

- intrusive lists, queues, bitmaps, and ring buffers;
- endpoint packing, generation changes, and special endpoint rejection;
- process/thread ownership and one-thread-per-process enforcement;
- reply-token allocation, matching, one-shot use, and cancellation;
- scheduler queue selection;
- physical range insertion, allocation, release, and coalescing;
- FDT memory-node iteration, multiple `reg` tuples, reservations, and malformed
  bounds;
- IPC transition and deadlock-detection models;
- permission masks;
- grant bounds, direction, lifetime, and overflow checks;
- page-table index and flag calculations;
- allocatable ELF section closure against linker permission ranges;
- ELF header validation;
- RAMFS directories, inode lifetime, and path traversal;
- protocol message validation.

Unit tests do not replace target tests for trap assembly, page-table activation,
TLB behavior, context switching, or interrupt delivery.

## Property and model-based tests

Examples of required properties:

- allocated physical ranges never overlap;
- free plus allocated frames equals the managed frame total;
- each runnable context occurs in exactly one run queue;
- every thread has exactly one owning process;
- every current-thread pointer belongs to one hart-local object;
- a stale generation never resolves to a reused process slot;
- a stale generation never validates, activates, mutates, or destroys a reused
  process address space;
- a stale thread generation never prepares, inspects, detaches, or enters a
  reused execution-context slot;
- every running thread is current on exactly one hart, while every inactive
  thread is current on none;
- failed object-table operations preserve both registry state and output
  arguments;
- an allocator bit is set exactly when the corresponding typed owner is not
  free;
- stale process generations cannot release, adopt, or reclassify frames;
- a failed ownership or handoff operation preserves allocator, ledger, plan,
  counts, and output arguments;
- the ownership handoff changes every planned class and the phase atomically;
- a reply token wakes exactly one blocked caller thread and cannot be reused;
- an IPC transition preserves exactly one blocked or runnable state;
- a grant cannot authorize bytes outside its declared range;
- RAMFS link and open-reference counts never become negative;
- failed spawn steps restore every resource acquired by earlier steps.

Randomized tests use explicit seeds. A failure prints the seed and operation
sequence so the exact case can be replayed.

## QEMU kernel component tests

These tests execute the real RISC-V entry, privilege, and MMU paths. They cover:

- trap entry and return with known register patterns;
- expected and unexpected exception handling;
- user attempts to access supervisor pages;
- timer interrupt and preemption;
- repeated context switching;
- page-table activation and invalidation;
- exact agreement between the reachable page-table tree, allocator bits, and
  typed frame owners;
- same virtual addresses in distinct process roots resolve distinct exact
  user-frame owners;
- inactive-root mutation plus complete ASID-zero activation flushes prevent
  stale cross-root translations;
- every attached thread stack is pairwise disjoint, bound to its exact slot,
  and selected only for that hart's exact current thread;
- user return status, PC, and stack mappings are revalidated before every
  `sret`;
- store rejection on kernel text and instruction-fetch rejection on writable
  kernel data;
- executable-frame reuse with different code after `fence.i`;
- synchronous IPC blocking and wakeup;
- specific-source and any-source receives;
- concurrent-call reply matching in a model with several threads per process;
- notification coalescing;
- stale and unauthorized endpoint use;
- deadlock-chain rejection;
- safe-copy across distinct address spaces;
- invalid grant and unmapped-buffer rejection.

Most component tests may run in one test kernel to avoid repeated QEMU startup.
Tests expected to panic or corrupt their own address space use isolated images.

## Integration tests

Integration tests exercise stable protocols rather than internal functions.
Before every participating service is dependency-ready, the earlier component
uses a native protocol/transition model and does not pull its successor into
the current task. The first pull request or milestone gate where all peers
exist must add the corresponding QEMU integration scenario.

Integration scenarios include:

- launcher readiness and failure paths;
- manifest and application privilege-profile installation;
- VM ownership handoff;
- fatal VM-originated fault handling;
- PM exit/wait behavior;
- PM/VFS/VM spawn success and rollback;
- load-complete token binding, stale-token rejection, and PM-only executable
  preparation/activation authority;
- rejection of mapping mutation while prepared, generation revalidation at
  activation, and abort/reprepare recovery;
- UART interrupt delivery to TTY;
- two-phase early-console ownership transfer and deferred PLIC completion;
- init console descriptors and child descriptor-duplication actions;
- VFS/RAMFS open, read, write, close, and directory operations;
- non-transitive application/VFS/backend bounce-buffer paths;
- client termination during an outstanding request;
- malformed message type, endpoint, grant, and request identifier;
- later, service restart and endpoint replacement.

Every blocking scenario has a host-side timeout. A timeout is a test failure
with the latest structured serial events attached.

## End-to-end tests

The shell MVP test boots a release-like image and drives the serial console. It
verifies externally visible behavior rather than internal implementation
order. At minimum it:

1. waits for versioned init and shell readiness markers;
2. runs `echo`;
3. reads a seeded RAMFS file;
4. runs `ls`;
5. runs `ps`;
6. starts a child, waits for it, and checks the reported status;
7. requests a clean shutdown.

Later POSIX work may use differential tests against MINIX or another reference
system. Differential tests compare documented results, return values, and
`errno`; they do not require identical scheduling or IPC traces.

## Machine-readable output

Test images emit TAP-compatible serial output:

```text
TAP version 13
ok 1 - user cannot read a supervisor page
ok 2 - stale endpoint is rejected
not ok 3 - write-only grant accepted a read
```

Boot and service diagnostics use stable event names plus human-readable data.
The host harness must distinguish:

- successful completion;
- explicit test failure;
- kernel panic;
- unexpected QEMU exit;
- timeout.

QEMU exits through a documented test-only mechanism or SBI system reset after
the final result is flushed.

## Assertions and observability

Debug builds continuously verify:

- run-queue and IPC-queue membership;
- process/thread ownership and hart-local current-thread state;
- process state transitions;
- endpoint slot/generation consistency;
- physical frame ownership and reference counts;
- disjoint kernel and VM frame pools;
- grant owner, grantee, permission, offset, and lifetime;
- request/reply identifier matching;
- one-shot reply-token lifetime;
- VFS descriptor and RAMFS inode reference counts.

An invariant failure stops immediately. Panic output includes at least:

- build identifier;
- hart and privilege state;
- exception cause and fault address;
- program counter and stack pointer;
- current process slot and endpoint;
- current IPC peer and message type where applicable;
- source file and line for explicit assertions.

Broad catches, silent retries, and success-shaped fallback results are not
acceptable.

## Sanitizers, fuzzing, and static checks

Native tests should use compiler warnings as errors and support ASan and UBSan.
Freestanding target builds use only sanitizer checks that are explicitly
supported without a hosted runtime.

Host fuzz targets should cover:

- ELF parsing;
- protocol decoders;
- path parsing;
- RAMFS image parsing;
- IPC and grant transition sequences.

Static analysis is introduced only when its configuration is reproducible and
its findings are actionable. A large unaudited warning baseline is not an
acceptable substitute for enforcement.

## Pull request gate

Each implementation pull request must provide:

- native tests for portable logic;
- QEMU coverage for changed privilege or hardware behavior;
- native protocol/transition coverage for changed cross-service behavior when
  a peer is not yet dependency-ready;
- QEMU integration coverage in the first applicable pull request where all
  participating services exist;
- explicit negative tests for permissions and invalid input;
- updated documentation when an interface or invariant changes.

Documentation-only changes do not require a target build unless documentation
tests are later introduced.

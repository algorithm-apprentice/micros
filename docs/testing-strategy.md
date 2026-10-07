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

Initial performance budgets are:

- native unit suite: normally below 2 seconds;
- QEMU smoke suite: normally below 10 seconds;
- QEMU integration suite: kept short enough for every applicable pull request;
- stress suite: unconstrained by the fast feedback budget and run separately.

Budgets are review signals, not reasons to hide necessary coverage.

The implemented fast targets are `test-unit`, `test-qemu-smoke`,
`test-qemu-panic`, `test-qemu-trap`, `test-qemu-timer`,
`test-qemu-frame-allocator`, `test-qemu-trap-panic`, `test-qemu-mmu`,
`test-qemu-object-model`, `test-qemu-endpoint`,
`test-qemu-nested-trap`, and
`test-qemu-frame-ownership`, `test-qemu-user-address-space`, and
`test-qemu-user-execution`, `test-qemu-scheduler`,
`test-qemu-scheduler-invalid-outgoing`, and
`test-qemu-scheduler-invalid-next`. From a clean checkout,
the implemented configure, build, and execution gates are:

```bash
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
cmake --workflow --preset test-qemu-nested-trap
cmake --workflow --preset test-qemu-frame-ownership
cmake --workflow --preset test-qemu-user-address-space
cmake --workflow --preset test-qemu-user-execution
cmake --workflow --preset test-qemu-scheduler
cmake --workflow --preset test-qemu-scheduler-invalid-outgoing
cmake --workflow --preset test-qemu-scheduler-invalid-next
```

`test-unit` currently runs the FDT parser corpus, portable frame allocator,
typed frame-ownership ledger, Sv39 encoding, kernel-object lifecycle/model
tests, and deterministic scheduler admission, RTS, priority-queue, policy,
current-selection, preemption-repair, return-plan, and separate
thread/kernel/idle accounting tests plus a replayable 4,096-step two-hart
reference model, endpoint encoding, immutable privilege-profile tables,
process-bound lifecycle, stale-generation rejection, relationship validation,
authorization, and a replayable 4,096-step lifecycle model under ASan and
UBSan. They also verify the exact 64-byte IPC message ABI and zero-state
invariants for dormant thread and endpoint IPC storage. The Python host tests
also cover generation-safe sender/receiver FIFO topology, exact queue
membership, stale links, cycles, duplicate membership, and close rejection
while queues or exact-source/reply waiters remain attached. Canonical queued
call and queue-less reply-wait shapes, unique token values, reserved message
types, and impossible already-matchable queue pairs are validated too. The
Python host tests include ELF allocatable-section closure, legacy-global
rejection, and machine-readable QEMU record regressions.
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
endpoint rejection after generation advance, and complete baseline
restoration. `test-qemu-nested-trap` injects a second fault
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
real timer wake.
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

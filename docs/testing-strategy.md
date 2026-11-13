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

## Current validation policy

[ADR-0123](adr/0123-current-contract-validation.md) and
[current-contract validation](architecture/current-contract-validation.md)
define the current tooling boundary. Validate actual current code and its
contracts, not accumulated source-retirement history. Complete host discovery
has no skip list; ordinary/observed agreement, independent expected results,
source/product identity and closing input checks remain mandatory.

The [build guide](development/building.md) owns tool setup, invocation and
standalone-image commands. This document owns evidence policy, layer selection
and the planner-checked QEMU/model command matrices. Subsystem ADRs and
contracts own exact cases, error precedence, ABI/resource bounds and witnesses;
the original PRs own historical execution and review records.

| Layer | Establishes | Required use |
| --- | --- | --- |
| Compiler/static checks | Warnings, ABI, freestanding and artifact closure | Every applicable build |
| Native unit/property tests | Portable algorithms, state, authority, bounds and failure atomicity | Smallest affected development loop |
| Persistent independent models | Complete mixed transition/state agreement with replayable seeds | Applicable model work and complete native acceptance |
| Current structural/work checks | Ownership, private dependencies, complete primitive/site coverage, work and storage bounds | Applicable source/algorithm change and complete native acceptance |
| QEMU components | Real traps, privilege, MMU/fences, registers, interrupts and context switches | Applicable hardware/mechanism change |
| Cross-service integration | Composed IPC/grants, publication, rollback and resource lifetime | Native protocol model before peers exist; real QEMU integration once ready |
| Stack evidence | Compiler/machine upper bounds plus authenticated target high-water observations | Every declared current scope and configuration |
| End-to-end | User-visible complete system behavior | The integrated shell/POSIX milestone, not an earlier component |
| Stress/fuzz and physical hardware | Long or malformed sequences; later platform gaps | Explicit/scheduled stress and post-MVP hardware work |

A host test cannot certify target hardware. A component cannot certify a
missing service, and a passing guest cannot waive an unknown stack edge or
algorithmic divergence. Existing QEMU configurations and scenarios remain
distinct; consolidation of this guide does not combine or remove them.

## Structural and algorithmic baseline evidence

For MINIX-derived work, follow [ADR-0080](adr/0080-minix-structural-and-algorithmic-baseline.md).
Before implementation, trace the fixed reference's behavior, authority,
logical data structures, mutation order, average/worst/amortized work where
relevant, auxiliary space, stack and hot paths. Classify every adaptation.

Deterministic evidence includes topology/membership, primitive comparisons,
probes, traversals and movement, boundary/adversarial inputs, independent
models and storage bounds. Analyze the logical input/capacity; a fixed array
does not make a linear or quadratic algorithm constant-time.
Elapsed time and model action coverage are not operation-count oracles.

[Operation-count evidence](architecture/operation-count-evidence.md) owns
passive test-only annotations, exact placement, complete composition,
overflow failure and ordinary collector absence. Current reference fields
retain their fixed independent provenance; candidate output cannot define
its own expected behavior or loosen a work bound.

[Stack-bound evidence](architecture/stack-bound-evidence.md) owns actual
compiler/object/linked frames and call closure, finite indirect/fatal
qualifications, assembly epochs, metadata passivity, unchanged budgets and
authenticated observations. An observed low-water mark is not a static
upper bound. Every declared ordinary/metadata/observed variant remains
required, including standalone production images; B13-E007 stays explicitly
blocked until production node Q and `STACK13` exist.
The required aggregate command is `cmake --workflow --preset test-stack-evidence`;
the [build guide](development/building.md#stack-evidence) owns its execution
and focused-development instructions.

Current source checks enforce module/state/fixture ownership, public ABI,
private dependency direction, model independence and actual observation/
machine contracts. Completed whole-body/header/AST transport and migration
tables are historical, not ordinary acceptance. Unrelated ordinary prototypes
or callback-free helpers do not need a metadata prerequisite. Real new
indirect calls still fail closed until their complete targets are proved.
Source/function size limits are [review signals](architecture/source-organization.md#review-signals),
not reasons to freeze source spelling or manufacture a runtime refactor.

## Validation tiers and cadence

| Intent | Required coverage |
| --- | --- |
| Focused iteration | Smallest relevant unit/model/source or QEMU check; not final PR evidence |
| Fast | Nonslow native CTests and the explicit small Python set |
| PR | Planner-selected current coverage; implementation changes include complete native validation, with target ownership and shared/unknown-input escalation |
| Full/milestone | Complete native discovery, all selected current QEMU/image workflows, stack gate, documentation and diff checks |
| Documentation only | Documentation/structure plus staged, worktree and branch-range checks |

Use the authoritative planner's union of committed, staged, unstaged and
untracked changes; explicit paths may not hide a changed input.
Actual Red and focused Green precede one sufficient final plan.
That same actual result can satisfy an affected audit unit: do not run a
second B01-B12/B14 retirement transaction solely to duplicate it.
After a correction, carry forward only demonstrably ownership-disjoint
evidence under [ADR-0034](adr/0034-author-validation-before-review.md).

Default/`--fresh` execution never reuses prior success and does not collect
optional reusable profiles or publish new receipts. It still invalidates an
old receipt before a fresh attempt and rejects execution, source-drift,
log-finalization and publication failures. Explicit reuse/producer/recording
follow the [closed-profile contract](architecture/validation-evidence.md);
they cannot lower selected coverage or substitute for independent review.
Ordinary runtime work does not need a separate receipt ceremony.

Feedback budgets remain review signals: incremental `test-unit-fast` normally
below six seconds, QEMU smoke normally below ten seconds, and integrations
short enough for applicable PRs. Persistent models stay outside the fast
loop without reducing their seeds, transition counts or PR coverage.
[Measured costs](research/current-validation-overhead.md) are diagnostic;
do not hide coverage, sleep, retry a failed guest or raise a deadline to
manufacture a timing pass.

### Persistent model commands

Run a selected persistent model separately when it is the affected layer:

```bash
cmake --workflow --preset test-ipc-model
cmake --workflow --preset test-tty-model
cmake --workflow --preset test-ramfs-model
cmake --workflow --preset test-vfs-model
cmake --workflow --preset test-elf-spawn-model
```

These named models keep their complete transition counts and seeds.
They are also included by `test-unit`; selecting one is development
evidence, not a substitute for the PR plan.

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
- immutable bootstrap-manifest parsing, profile/image resolution, explicit
  prerequisite topology, and launcher lifecycle transitions;
- permission masks;
- grant bounds, direction, lifetime, and overflow checks;
- user-runtime wrapper register marshalling, stable result propagation, and
  grant-token output preservation through a host raw-syscall stub;
- freestanding `memcpy` and `memset` semantics without a hosted libc;
- page-table index and flag calculations;
- allocatable ELF section closure against linker permission ranges;
- kernel and standalone user ELF header, program-header, relocation, symbol,
  and section-permission validation;
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
- a bootstrap service is hidden before release, has exactly one starting and
  ready transition, and cannot release a dependent before every prerequisite
  is ready;
- a failed prepublication bootstrap transition preserves complete manifest,
  profile, endpoint, scheduler, deadline, and launcher state;
- sealed bootstrap authority cannot be reintroduced;
- a grant cannot authorize bytes outside its declared range;
- RAMFS link and open-reference counts never become negative;
- failed spawn steps restore every resource acquired by earlier steps.

Randomized tests use explicit seeds. A failure prints the seed and operation
sequence so the exact case can be replayed.

## QEMU kernel component tests

Use actual RISC-V execution for register-preserving trap/return, timer and
preemption, distinct user roots, privilege/isolation, executable permissions,
translation invalidation, `fence.i`, stack ownership, nested emergency entry,
IPC blocking/wakeup and checked grant copies. Panic/corruption scenarios use
their isolated configurations. Native multithreaded/hart models retain the
policy-separation checks that the one-hart target cannot demonstrate.

All ordinary guests require the shared ordered boot/object/trap/FDT/allocator/
MMU/ownership records before the expected pass or panic, complete scenario
witnesses and clean firmware termination. Exact markers, ordering, memory-size
variants and timeout arguments are executable contracts in
[CMake](../CMakeLists.txt) and the [runner owners](architecture/qemu-runner-source-organization.md).
Missing, duplicate, malformed or partial evidence is not success.

The [current-evidence scopes](architecture/current-contract-validation.md#required-target-evidence-has-an-ordinary-owner)
belong to their existing MMU, address-space, handoff, scheduler, user-runtime,
IPC-syscall, bootstrap-launcher, TTY and seeded-panic workflows. They inspect the same owned guest result and
actual artifacts rather than launching a duplicate guest. Terminal and trace
controllers retain every twelve/eighteen-case execution, private debugger
authority, complete captures, current input identity and atomic publication.

### Required QEMU matrix

The planner checks this complete matrix against the registered workflows.
Run a row with the [build guide's tool environment](development/building.md#tool-discovery).
Each command configures, builds and executes its own target; do not borrow
another configuration's result. The actual recipe and exact markers remain
in [CMakePresets.json](../CMakePresets.json) and [CMakeLists.txt](../CMakeLists.txt).

| Command | Evidence |
| --- | --- |
| `cmake --workflow --preset test-qemu-smoke` | Ordered boot and clean firmware shutdown |
| `cmake --workflow --preset test-qemu-panic` | Intentional complete panic, seeded history and actual BSS storage |
| `cmake --workflow --preset test-qemu-trap` | Register-preserving expected exception return |
| `cmake --workflow --preset test-qemu-timer` | Accepted delivery, rearm/disarm and interrupt-state preservation |
| `cmake --workflow --preset test-qemu-uart-console` | UART drain, two-phase ownership, real PLIC claim/completion and panic seizure |
| `cmake --workflow --preset test-qemu-terminal-failure` | All twelve bounded polling/entry/reset/repair cases |
| `cmake --workflow --preset test-qemu-panic-trace` | All eighteen frame/epoch/recursive/fault trace cases |
| `cmake --workflow --preset test-qemu-frame-allocator` | Real FDT allocation/release at both required RAM sizes |
| `cmake --workflow --preset test-qemu-trap-panic` | Unexpected exception with captured context |
| `cmake --workflow --preset test-qemu-mmu` | Permissions/faults, large leaves, table identities and load probes at both RAM sizes |
| `cmake --workflow --preset test-qemu-object-model` | Generation-safe object reuse and per-hart identity |
| `cmake --workflow --preset test-qemu-endpoint` | Publication, privileges, generations and baseline cleanup |
| `cmake --workflow --preset test-qemu-grant` | Exact participants, direction, cross-page copy and atomic denial |
| `cmake --workflow --preset test-qemu-grant-syscall` | Actual operations 7-10, stable results, registers and teardown |
| `cmake --workflow --preset test-qemu-user-runtime` | Independent checked ELF/startup/raw ecall/wrappers and actual linked memory transfers |
| `cmake --workflow --preset test-qemu-bootstrap-launcher` | Held preparation, ordered release/readiness and sealed authority |
| `cmake --workflow --preset test-qemu-vm-handoff` | Independent database validation, irreversible handoff and wired grant exchange |
| `cmake --workflow --preset test-qemu-pm-service` | Reserve/abort/prepare/activate/exit/release, checked application and no leaked resources |
| `cmake --workflow --preset test-qemu-vm-scratch-preemption` | Real root switch/reactivation and stale scratch-load fault after commit |
| `cmake --workflow --preset test-qemu-tty` | Marker-paced canonical input, deferred IRQs, grant I/O, diagnostic forwarding and physical drain |
| `cmake --workflow --preset test-qemu-ramfs` | Seed, mount, sparse I/O, cursor, balanced references and zero live grants |
| `cmake --workflow --preset test-qemu-vfs` | Application descriptors, namespace, translated directories and two-hop file/console I/O |
| `cmake --workflow --preset test-qemu-elf-spawn` | Real dynamic test PID 1/child, post-freeze rollback, argv/env/I/O, exit/wait and resource equality |
| `cmake --workflow --preset test-qemu-vm-ready-early` | Exact pre-handoff readiness rejection |
| `cmake --workflow --preset test-qemu-vm-self-fault` | Running-phase VM fault and fatal bootstrap ordering |
| `cmake --workflow --preset test-qemu-vm-self-fault-sealed` | Sealed-phase VM fault without revived bootstrap failure |
| `cmake --workflow --preset test-qemu-bootstrap-ready-timeout` | Guest-owned missing-ready deadline |
| `cmake --workflow --preset test-qemu-bootstrap-manifest-panic` | Invalid manifest rejection before publication |
| `cmake --workflow --preset test-qemu-ipc` | Blocking, tokens, notify, deadlock, close and baseline restoration |
| `cmake --workflow --preset test-qemu-ipc-ecall-core` | Real trap routing, guard commit/rollback and registers |
| `cmake --workflow --preset test-qemu-ipc-syscall` | Complete six-operation cross-address-space acceptance and actual selected syscall/completion work |
| `cmake --workflow --preset test-qemu-ipc-syscall-panic` | Revoked accepted receive mapping fails before return commit |
| `cmake --workflow --preset test-qemu-nested-trap` | Per-hart emergency-stack route |
| `cmake --workflow --preset test-qemu-frame-ownership` | Typed generation authority and failure-atomic handoff |
| `cmake --workflow --preset test-qemu-user-address-space` | Isolation, selected paths, release/rollback and complete cleanup |
| `cmake --workflow --preset test-qemu-address-space-handoff` | Wired/index obligations and handed-off grant/return behavior |
| `cmake --workflow --preset test-qemu-user-execution` | Real U-mode round trip, isolation, registers and stack ownership |
| `cmake --workflow --preset test-qemu-scheduler` | Queue/accounting/preemption/idle behavior and exact common-return work witnesses |
| `cmake --workflow --preset test-qemu-scheduler-invalid-outgoing` | Invalid outgoing context panics before plan mutation |
| `cmake --workflow --preset test-qemu-scheduler-invalid-next` | Invalid selected context panics before plan mutation |

## Integration tests

Before every participating service is dependency-ready, use a native protocol/
transition model without pulling a successor into the task. The first
applicable PR or milestone where all peers exist requires real QEMU
integration. Keep generation/permission failures, rollback and cleanup at
their actual authority boundaries.

| Boundary | Required evidence and detailed contract |
| --- | --- |
| Bootstrap and VM | Exact manifest/profile release, ready ordering, irreversible wired handoff, early-ready rejection, running/sealed VM fault classification and launcher sealing: [ADR-0043](adr/0043-static-bootstrap-launcher.md), [ADR-0045](adr/0045-static-vm-bootstrap-and-handoff.md) |
| PM | Hidden reservation, stale generations/tokens, byte-preserving rejection, reverse rollback, exit/zombie/wait/reparenting; the initial empty reserve/abort fixture is not successful spawn: [ADR-0046](adr/0046-pm-process-lifecycle-and-spawn-metadata.md), [coordinator](architecture/pm-spawn-coordinator.md) |
| TTY | Console begin -> exact VM mapping -> release -> commit -> ready, one unextended guest deadline, retained source-10 claim and explicit completion, grant-backed canonical input/output and physical drain: [ADR-0047](adr/0047-tty-console-handoff-and-serial-protocol.md) |
| RAMFS | Seed/ELF budget, mount, generation-safe namespace, sparse grant-backed I/O, complete-record cursor, balanced references and zero live grants: [ADR-0048](adr/0048-ramfs-service-and-vfs-filesystem-protocol.md) |
| VFS | Process/descriptor/open-file/vnode ownership, preflight before consuming reads, independent grant hops, translated directory records, asynchronous TTY completion/cancel/retry and resource cleanup: [ADR-0049](adr/0049-vfs-service-and-application-io-protocol.md) |
| Executable lifecycle | Real PM/VFS/VM/kernel forward and reverse transactions, prepared mapping seal, lower-priority activation, source-only exit detach, release and wait: [ADR-0050](adr/0050-post-handoff-elf-spawn.md), [integration map](architecture/elf-spawn-integration.md) |
| VM scratch preemption | Real timer switch to another root, inactive scratch validity, ASID-zero reactivation, commit and exact stale-load page fault: [ADR-0050](adr/0050-post-handoff-elf-spawn.md) |
| Application runtime | Checked ELF, canonical argv/env/auxv, independent startup bytes, non-result registers and normal-return PM exit: [runtime map](architecture/application-runtime.md) |
| Complete validators | Retain boundary/corruption and Clang-AST work-shape controls, including every inactive field, exact bitmap bounds and count domination: [ADR-0076](adr/0076-bounded-complete-frame-ownership-validation.md), [ADR-0077](adr/0077-bounded-complete-vm-boot-database-validation.md), [valid VM cases](adr/0078-valid-vm-boot-database-boundary-evidence.md) |

The actual current VM boot object has seven-address-space capacity and
364,704 bytes; the service configuration remains 128 bytes and the production
manifest remains six services. Earlier six-slot sizes are historical, not
alternative accepted current layouts.

Serial input is driven by exact ready lines/prompts, never sleeps.
Unrelated workflows keep stdin disconnected. Guests retain their own
readiness deadlines and the host absolute timeout. Timeout, premature exit,
host-kill or a pass marker without the final drain/resource/firmware boundary
is failure. Later service restart and endpoint replacement remain separate
future integration requirements.

## End-to-end tests

The future node-Q shell MVP test must boot a release-like image and drive the
serial console. It must verify externally visible behavior rather than
internal implementation order. At minimum it:

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

Production node Q is not implemented by the existing dynamic test PID 1.
Its exact [shell contract](architecture/init-shell.md) retains the prompt-paced
five-command script, contiguous transcript, expected application PIDs,
shell reaping/restart, resource equality, bounded TTY drain and clean shutdown.
The [correction register](research/minix-complete-baseline-findings-and-correction-dag.md)
owns `BASE01`, `NODE00`-`NODE03`, `STACK13` and `RECERT01` ordering.

## POSIX conformance evidence

[ADR-0081](adr/0081-complete-posix-compatible-system-goal.md) requires a
versioned POSIX.1-2024 capability matrix. Every unconditional mandatory row,
every applicable conditionally mandatory row and every claimed optional/XSI
group needs executable evidence. Conditional inapplicability must cite the
standard's exact condition, not an internal MVP profile.

Complete compatibility also requires the shell/mandatory utilities and
representative applications without project-private source changes.
The [staged-omission manifest](research/posix-staged-omission-manifest.md)
keeps every future replacement obligation explicit. Current native,
integration or shell progress is not a complete-POSIX claim.

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

Debug assertions and explicit diagnostics verify the invariants below.
Selected ordinary-path checks and complete-audit boundaries follow their
Accepted subsystem contracts; this does not restore global scans to every
operation:

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

Pure documentation changes run the documented documentation/structure and
diff checks. They do not require native suites, target builds or invented
runtime Reds. Exact current author evidence and independent review remain
mandatory at the applicable phase.

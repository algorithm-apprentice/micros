# ADR-0056: Kernel PM-Control Source Decomposition

- Status: Accepted
- Date: 2026-10-12
- Related:
  [ADR-0025](0025-minix-behavioral-baseline-before-optimization.md),
  [ADR-0032](0032-fail-closed-change-aware-validation.md),
  [ADR-0033](0033-reviewed-design-before-implementation.md),
  [ADR-0034](0034-author-validation-before-review.md),
  [ADR-0046](0046-pm-process-lifecycle-and-spawn-metadata.md),
  [ADR-0050](0050-post-handoff-elf-spawn.md),
  [ADR-0051](0051-responsibility-based-source-modules.md),
  [ADR-0053](0053-kernel-address-space-source-decomposition.md),
  [ADR-0054](0054-frame-ownership-source-decomposition.md),
  [ADR-0055](0055-kernel-vm-handoff-source-decomposition.md),
  [source-module audit](../architecture/source-module-audit.md),
  [source organization](../architecture/source-organization.md),
  [kernel PM-control source organization](../architecture/kernel-pm-control-source-organization.md),
  [MINIX baseline](../research/minix-kernel-pm-control-source-organization.md)

## Context

ADR-0051 requires cohesive responsibility owners, explicit private contracts,
acyclic dependencies, and semantic function boundaries. The source-module
audit identifies kernel PM control as the next correction after kernel VM
handoff.

At reviewed base
`6753fdabba31e663c68234e97b5ac104558b0ed4`, four signal files contain 5,216
physical lines and 77 functions:

```text
kernel/pm_control_core.c
kernel/pm_control_syscall.c
tests/host/pm_control_test.c
tests/host/pm_control_execution_test.c
```

Fifteen functions exceed ADR-0051's 120-line review signal. More importantly,
the sources mix:

- operation-13 ABI decoding and complete PM-control validation;
- empty-process reservation and abort;
- execution preparation, prepared rollback, and final activation;
- exit begin, endpoint/grant/thread detach, and empty-process release;
- common syscall authority/runtime/output handling and every command adapter;
- native ABI, authority, reservation, VM-agreement, preparation, activation,
  token, and exit evidence; and
- multiple mutable fixture owners hidden in monolithic test files.

The behavior is already specified and tested. This decision changes source
ownership only. It preserves every stable header, command, state, token,
endpoint, profile, scheduler policy, error, output boundary, commit order,
case order, marker, target, CTest, workflow, and timeout.

The fixed MINIX baseline separates generic kernel-call dispatch from process
creation, execution installation, privilege control, scheduling, stop/resume,
and final clear. `micros` retains that downward mechanism direction while
preserving required and accepted adaptations: direct spawn instead of fork,
RISC-V U-mode context, unprivileged VM policy, immutable profiles,
generation-safe objects, exact lifecycle tokens, retained output plans, and
non-failing commits.

## Decision

Decompose the kernel PM-control production and native evidence sources
according to
[the exact architecture contract](../architecture/kernel-pm-control-source-organization.md).
The extraction is behavior-preserving and introduces no new supported
interface.

### Stable interfaces

The following headers remain byte-for-byte stable:

```text
include/micros/pm.h
kernel/pm_control_core.h
kernel/pm_control_runtime.h
kernel/pm_control_runtime_core.h
kernel/pm_control_syscall.h
kernel/pm_control_syscall_core.h
```

No operation-13 command, register shape, structure, enum, field, size,
alignment, token, phase, result, error, or public function signature changes.

### Portable state owners

The final portable layout separates:

- common predicates and state initialization;
- register ABI decoding;
- complete scalar/object validation;
- empty-process reservation and abort;
- execution preparation and prepared rollback;
- final activation; and
- exit begin, execution detach, and empty-process release.

One declarations-only `pm_control_internal.h` carries only helpers required by
at least two accepted owners. Complete validation is read-only. Transition
modules depend on common predicates and validation but never call sibling
transition modules.

The existing `pm_control_runtime_core.c` remains the cohesive read-only
PM/VM/frame agreement owner. The existing `pm_control_syscall_core.c` remains
the cohesive portable caller-authority and retained-output-plan owner.

### Syscall adapter owners

The public captured-`ecall` facade retains:

- trusted argument checks;
- operation-13 decode;
- exact PM caller authority;
- authoritative runtime acquisition and complete validation;
- sealed/handoff phase gating; and
- command dispatch.

Separate private modules own:

- shared fatal/result/output-translation runtime;
- reserve and abort-reserved orchestration;
- execution prepare and abort-prepared orchestration;
- activation orchestration; and
- exit begin, detach, and process-release orchestration.

Operation adapters do not call one another. They depend downward on the
matching portable transition and existing kernel mechanisms. Portable
PM-control modules include no syscall-private header.

### Failure and publication boundaries

The split preserves:

- exact decode, caller-authority, runtime-validation, and phase precedence;
- unchanged outputs on every rejected decode, translation, or preflight;
- complete fixed-output physical plans before mutation;
- exact process/thread generation and nonzero token capacity behavior;
- prepare ordering across execution preflight, context commit, PM-control
  commit, local `fence.i`, and output publication;
- activation as the sole endpoint and runnable publication point;
- exit ordering across source-only stop, VFS cleanup, endpoint/IPC/grant/
  execution detach, VM release, and empty-process release;
- fatal classification for runtime or invariant corruption;
- stable recoverable results for invalid input, authorization, phase, memory,
  or capacity; and
- no fallible work after an accepted non-failing commit begins.

### Native evidence owners

The state/ABI/reservation test keeps one suite entry and separates:

- shared fixture and retained-output adaptation;
- ABI and decode;
- syscall authority and output planning;
- state preparation, reservation, abort, capacity, and invariant behavior;
  and
- exact PM/VM/frame reservation agreement.

The execution test keeps one suite entry and separates:

- one mutable fixture and child-freeze/activation/exit-call setup;
- preparation, prepared rollback, activation, and corruption evidence;
- token exhaustion; and
- exit begin, detach, VM release, and process release.

Each family has exactly one mutable context owner. Case order and all
assertions remain exact.

### Build and validation ownership

CMake defines exact ordered source sets for portable PM-control sources,
target runtime/adapters, state/ABI/reservation host evidence, and execution/
exit host evidence. `pm_test` and `micros` expand only the required sets once.

The validation planner gives every new path the exact classification of its
current monolithic owner. The structural test itself is host-unit-only. The
split creates no new behavioral executable, CTest, workflow, marker, timeout,
or duplicate execution.

### Structural Red gate

Implementation begins with:

```text
tests/host/test_kernel_pm_control_source_layout.py
```

The fail-closed gate verifies exact family paths, ordered source sets, consumer
expansion, no direct raw split-source listing, declarations-only private
headers, dependency hygiene, stable-header hashes, final function owners,
unique target/test mutable state, recursive include-closure long-function
scanning, stable evidence identities, validation-plan inheritance, and
migration completion.

The initial migration collections contain:

- 19 moving production public symbols;
- 13 moving native test-case functions; and
- 15 current over-limit functions.

The 32-entry owner collection and 15-entry long-function collection may only
shrink and must be empty in the final implementation. Context and singleton
ownership assertions are unconditional, and the exact family inventory
rejects additional matching implementation or header files.

### Implementation order

The implementation proceeds as seven green slices:

1. structural gate, source-set skeleton, private headers, and migration maps;
2. ABI, validation, reservation, and matching native evidence;
3. execution preparation, activation, fixture, and matching native evidence;
4. exit lifecycle and matching token/exit evidence;
5. syscall runtime and responsibility-specific adapters;
6. empty migration collections and one authoritative pull-request validation
   execution; and
7. independent implementation review.

Each slice runs only its narrow existing evidence. The final pull-request plan
is authoritative and selected workflows are not rerun separately.

## Consequences

### Positive

- Operation-13 source names expose the accepted reservation, preparation,
  publication, and exit tasks.
- Complete validation becomes a visible lower-level owner rather than being
  interleaved with every transition.
- The 453-line captured-`ecall` handler becomes a readable common facade over
  isolated operation adapters.
- Native evidence gains one explicit mutable context per family and preserves
  exact case order without mirroring a production monolith.
- Future PM-control changes have narrow production and evidence homes before
  the cross-service coordinator extends surrounding PM and VM behavior.

### Costs

- CMake and validation-plan path ownership become more explicit.
- The implementation moves more than five thousand source lines while
  preserving behavior.
- Private fixture and adapter declarations must remain narrow enough not to
  replace the monolith with a broad internal API.
- Extraction must preserve exact output timing, token consumption, endpoint/
  scheduler publication, grant/IPC detach, and target evidence identities.

### Risks and mitigations

- **Authority drift:** stable headers and exact symbol-owner gates preserve the
  PM-only caller and fixed application policy.
- **Commit reordering:** command-specific adapter contracts and required
  function dispositions preserve preflight and non-failing commit order.
- **State duplication:** the structural gate enforces one target state owner
  and one mutable context owner per native family.
- **Dependency cycles:** validation remains below transitions, portable state
  remains below adapters, and adapters cannot call one another.
- **Coverage dilution:** stable native/QEMU identities and fail-closed path
  inheritance preserve current evidence.
- **Structural bypass:** exact family inventory and recursive include-closure
  scanning prevent moved or newly hidden long functions and contexts from
  escaping the gate.
- **Mechanical splitting:** every long function has a required named task
  outline; arbitrary wrappers do not satisfy the design.

## Alternatives considered

### Leave PM control intact until the coordinator

Rejected. The coordinator would add more PM/VM/VFS lifecycle behavior around
the same mixed state, adapter, and evidence sources.

### Split only the syscall dispatcher

Rejected. Reservation, execution, activation, exit, and both native evidence
families would remain mixed.

### Keep preparation and activation in one portable module

Rejected. Preparation creates hidden held objects and may be rolled back;
activation is the irreversible publication point and changes for a different
reason.

### Create one file per command

Rejected. Preflight and commit halves of one lifecycle responsibility belong
together. Eight command files would over-fragment shared reservation,
preparation, and exit invariants.

### Mirror MINIX kernel-call files exactly

Rejected. MINIX copies a parent, performs destructive exec, has general
privilege/scheduler calls, supports signals and tracing, and clears processes
under a different VM model. The reusable baseline is responsibility
direction, not its exact file list.

### Change PM-control behavior while moving code

Rejected. Any ABI, token, authority, state, error, output, mapping, scheduler,
IPC, grant, or exit-order change requires its own reviewed design and
regression evidence.

## Non-goals

This ADR does not authorize:

- public API, ABI, command, structure, state, token, result, error, or behavior
  changes;
- concurrent lifecycle transactions, multiple application threads, SMP,
  signals, fault-to-exit conversion, recovery, migration, or compatibility;
- changes to PM service, VM service, VFS, scheduler, endpoint, IPC, grant,
  object, frame, address-space, or execution behavior;
- decomposition outside the four audited signal files and their direct build
  and validation ownership;
- new generated fixtures, behavioral tests, workflows, markers, timeouts, or
  CI jobs; or
- function splitting by line count instead of task cohesion.

# Kernel PM-Control Source Organization

This document defines the exact behavior-preserving source decomposition for
kernel PM-control state, operation-13 syscall adaptation, and the two native
PM-control evidence families. It applies ADR-0051 to the fifth correction
identified by the repository source-module audit.

Implementation must not begin until
[ADR-0056](../adr/0056-kernel-pm-control-source-decomposition.md) is Accepted
and its design pull request is merged.

## Reviewed base

The reviewed source base is:

```text
6753fdabba31e663c68234e97b5ac104558b0ed4
```

The fixed local MINIX baseline and required `micros` adaptations are recorded
in
[the research note](../research/minix-kernel-pm-control-source-organization.md).

## Current inventory

The audit signal covers four files:

| File | Physical lines | Functions | Longest function |
| --- | ---: | ---: | --- |
| `kernel/pm_control_core.c` | 1,864 | 42 | `micros_pm_control_state_validate_objects`, 157 |
| `kernel/pm_control_syscall.c` | 587 | 6 | `micros_pm_control_handle_captured_user_ecall`, 453 |
| `tests/host/pm_control_test.c` | 1,588 | 16 | `test_runtime_accepts_exact_frozen_vm_reservation`, 343 |
| `tests/host/pm_control_execution_test.c` | 1,177 | 13 | `test_exit_begin_detach_release`, 245 |
| **Total** | **5,216** | **77** | **15 functions above 120 lines** |

The numeric values are review signals. The boundaries below follow state
validation, reservation, execution preparation, activation, exit, privileged
adaptation, fixture, and scenario ownership.

Three existing support modules are already cohesive and remain separate:

| File | Existing responsibility |
| --- | --- |
| `kernel/pm_control_runtime.c` | Sole authoritative target PM-control state, bootstrap binding, reset, and complete target-runtime validation |
| `kernel/pm_control_runtime_core.c` | Read-only PM reservation agreement with VM mapping and frame-ownership state |
| `kernel/pm_control_syscall_core.c` | Portable PM caller authority classification and retained output-plan construction/publication |

The correction may place these files in reusable source sets, but it does not
move their functions or broaden their responsibilities.

## Behavioral preservation

The decomposition preserves all Accepted behavior in ADR-0046 and ADR-0050.

### Stable interfaces

These headers remain byte-for-byte stable:

```text
include/micros/pm.h
kernel/pm_control_core.h
kernel/pm_control_runtime.h
kernel/pm_control_runtime_core.h
kernel/pm_control_syscall.h
kernel/pm_control_syscall_core.h
```

No syscall number, command, enum, structure, field order, size, alignment,
token, endpoint, phase, result, error, public declaration, or test entry
changes.

### State and authority

- The kernel remains the sole owner of process, thread, endpoint, immutable
  application profile, scheduler, context, instruction-synchronization, and
  publication mechanisms.
- PM remains the sole caller authorized to reserve, prepare, activate, stop,
  detach, and release a dynamic application through operation 13.
- VM remains the authority for load and mapping policy. PM control validates
  exact VM load tokens and mapping generations but does not select frames or
  edit page tables.
- VFS cleanup still precedes kernel exit detach. The kernel does not invent or
  consume a VFS proof token.
- One target-owned `micros_pm_control_state` remains in
  `kernel/pm_control_runtime.c`. Portable modules introduce no mutable global
  state.

### ABI and failure ordering

The public captured-`ecall` path retains this common order:

1. reject null trusted arguments fatally;
2. decode the operation-13 register shape;
3. resolve the authoritative PM, bootstrap, endpoint, object, VM, ownership,
   and grant runtime state;
4. classify exact caller and immutable profile authority;
5. validate complete runtime invariants;
6. require sealed bootstrap plus handed-off VM and frame ownership;
7. dispatch the decoded command; and
8. return only the existing stable syscall result or the existing fatal
   diagnostic.

Operation adapters retain their exact command-specific order:

- `RESERVE`: VM-load/reservation phase preflight, complete output translation,
  process-capacity preflight, non-failing reserve commit, then output
  publication.
- `ABORT_RESERVED`: VM-load/reservation phase preflight, exact reservation
  preflight, then non-failing abort commit.
- `PREPARE_EXECUTION`: complete output translation, PM/VM/object preflight,
  post-handoff execution preflight, execution commit, PM-control commit,
  local `fence.i`, then output publication.
- `ABORT_PREPARED`: exact prepared-shape preflight, then non-failing rollback
  to the frozen mapping shape.
- `ACTIVATE`: exact prepared-shape and mapping-generation preflight, then one
  non-failing endpoint-publication and scheduler-admission commit.
- `EXIT_BEGIN`: complete output translation, exact running blocked-call
  preflight, endpoint source-only commit, permanent thread stop, then output
  publication.
- `EXIT_DETACH`: exact stopped-exit preflight, complete grant-cancellation
  preflight, endpoint/IPC close, grant cancellation, then thread/context/
  scheduler detach and VM exit-detached publication.
- `RELEASE_EXITED_PROCESS`: exact detached, rootless, frameless, threadless,
  endpointless process preflight, then process release and exit-state clear.

Every returning failure preserves authoritative state and caller output bytes.
Capacity and recoverable phase failures retain their existing stable result.
Runtime, ownership, object, scheduler, endpoint, IPC, grant, VM, or token
disagreement remains fatal. No adapter adds fallible work after its accepted
non-failing commit begins.

## Portable PM-control layout

| Path | Owned responsibility |
| --- | --- |
| `kernel/pm_control_core.c` | Common byte/handle/endpoint predicates and PM-control state initialization |
| `kernel/pm_control_abi.c` | Operation-13 command recognition and register decoding |
| `kernel/pm_control_validation.c` | Complete scalar/object state validation and shared read-only application-profile checks |
| `kernel/pm_control_reservation.c` | Empty-process reserve and reserved-process abort preflight/commit |
| `kernel/pm_control_execution.c` | Frozen-mapping execution preparation and prepared-execution abort preflight/commit |
| `kernel/pm_control_activation.c` | Prepared-child revalidation and final endpoint/scheduler activation |
| `kernel/pm_control_exit.c` | Running-child resolution, exit begin, execution detach, and empty-process release |
| `kernel/pm_control_internal.h` | Sole private portable PM-control contract |

`pm_control_internal.h` contains declarations, shared private types, and
constants only. It contains no function body or mutable definition. A private
helper is declared there only when at least two accepted owners need it;
otherwise it remains file-local.

Complete validation is read-only. Reservation, execution, activation, and exit
modules may depend on common predicates and validation, but no transition
module calls another transition module.

## Stable public-symbol ownership

| Owner | Public symbols |
| --- | --- |
| `pm_control_core.c` | `micros_pm_control_state_prepare` |
| `pm_control_abi.c` | `micros_pm_control_decode` |
| `pm_control_validation.c` | `micros_pm_control_state_validate`, `micros_pm_control_state_validate_objects` |
| `pm_control_reservation.c` | `micros_pm_control_reserve_preflight`, `micros_pm_control_reserve_commit_prevalidated`, `micros_pm_control_abort_preflight`, `micros_pm_control_abort_commit_prevalidated` |
| `pm_control_execution.c` | `micros_pm_control_prepare_preflight`, `micros_pm_control_prepare_commit_prevalidated`, `micros_pm_control_abort_prepared_preflight`, `micros_pm_control_abort_prepared_commit_prevalidated` |
| `pm_control_activation.c` | `micros_pm_control_activate_preflight`, `micros_pm_control_activate_commit_prevalidated` |
| `pm_control_exit.c` | `micros_pm_control_exit_begin_preflight`, `micros_pm_control_exit_begin_commit_prevalidated`, `micros_pm_control_exit_detach_preflight`, `micros_pm_control_exit_detach_commit_prevalidated`, `micros_pm_control_release_exited_preflight`, `micros_pm_control_release_exited_commit_prevalidated` |
| `pm_control_syscall.c` | `micros_pm_control_handle_captured_user_ecall` |

The existing public declarations stay in their current stable headers. The
split does not create another supported API.

## Privileged syscall-adapter layout

| Path | Owned responsibility |
| --- | --- |
| `kernel/pm_control_syscall.c` | Public captured-`ecall` facade, common decode/authority/runtime/phase order, and command dispatch |
| `kernel/pm_control_syscall_runtime.c` | Fatal diagnostics, stable result publication, target output translation, and shared authoritative runtime acquisition |
| `kernel/pm_control_syscall_reservation.c` | `RESERVE` and `ABORT_RESERVED` target orchestration |
| `kernel/pm_control_syscall_execution.c` | `PREPARE_EXECUTION` and `ABORT_PREPARED` target orchestration |
| `kernel/pm_control_syscall_activation.c` | `ACTIVATE` target orchestration |
| `kernel/pm_control_syscall_exit.c` | `EXIT_BEGIN`, `EXIT_DETACH`, and `RELEASE_EXITED_PROCESS` target orchestration |
| `kernel/pm_control_syscall_internal.h` | Sole private target-adapter contract and immutable per-call runtime view |

The operation adapters do not call one another. They depend downward on the
existing portable authority/output helpers, the matching portable transition,
and the existing endpoint, IPC, grant, execution, VM, and ownership
mechanisms.

`pm_control_syscall_runtime.c` owns no lifecycle transition. Its per-call view
contains references to already authoritative state; it does not cache them in
mutable global storage.

## Dependency direction

The permitted dependency direction is:

```text
pm_control_abi -> pm_control_core
pm_control_validation -> pm_control_core
pm_control_reservation -> pm_control_validation + pm_control_core
pm_control_execution -> pm_control_validation + pm_control_core
pm_control_activation -> pm_control_validation + pm_control_core
pm_control_exit -> pm_control_validation + pm_control_core

pm_control_runtime_core -> public portable PM-control + VM/ownership validation
pm_control_runtime -> pm_control_runtime_core + public portable PM-control
pm_control_syscall_core -> public PM/bootstrap/endpoint/object contracts
pm_control_syscall_runtime -> runtime owners + pm_control_syscall_core
operation syscall adapters -> pm_control_syscall_runtime/core
                              + matching portable transition
pm_control_syscall facade -> runtime + operation syscall adapters
```

Portable PM-control state never includes a syscall-private header. Validation
does not invoke a transition. The target facade and adapters do not become PM,
VM, VFS, scheduler, endpoint, IPC, grant, or address-space policy owners.

## Native PM-control evidence layout

### State, ABI, authority, and reservation family

The existing `micros_pm_control_test_run()` entry remains in
`tests/host/pm_control_test.c` and keeps this exact case order:

1. operation-13 constants, result layouts, and decode behavior;
2. direct caller and immutable-profile authority classification;
3. authority resolution from authoritative bootstrap/object state;
4. sealed/handoff phase classification and retained output planning;
5. PM-control state preparation;
6. reserve/abort lifecycle;
7. process-generation capacity and quarantine;
8. invariant rejection; and
9. exact loading/frozen/activated-pending VM reservation agreement.

The final sources are:

| Path | Owned responsibility |
| --- | --- |
| `tests/host/pm_control_test.c` | Sole suite entry and exact case order |
| `tests/host/pm_control_test_fixture.c` | Shared fixture construction, endpoint encoding, retained-output test adapter, and sole mutable family context |
| `tests/host/pm_control_abi_test.c` | ABI constants, result layouts, successful decodes, and rejection preservation |
| `tests/host/pm_control_syscall_test.c` | Caller/authority resolution, phase gating, and retained output-plan evidence |
| `tests/host/pm_control_reservation_test.c` | State preparation, reserve/abort, capacity/quarantine, and invariant evidence |
| `tests/host/pm_control_runtime_test.c` | Exact PM/VM/frame reservation-agreement lifecycle evidence |
| `tests/host/pm_control_test_internal.h` | Declarations-only private fixture and case contract |

Exactly one mutable `pm_control_test_context` is defined in
`pm_control_test_fixture.c` and passed explicitly to the cases that need it.
No test module defines another allocator, ownership ledger, or VM state
singleton.

### Execution and exit family

The existing `micros_pm_control_execution_test_run()` entry remains in
`tests/host/pm_control_execution_test.c` and keeps this exact case order:

1. prepare, abort, reprepare, and activate;
2. prepared-child corruption rejection;
3. prepared-token and exit-token exhaustion; and
4. exit begin, detach, VM release, and process release.

The final sources are:

| Path | Owned responsibility |
| --- | --- |
| `tests/host/pm_control_execution_test.c` | Sole suite entry and exact case order |
| `tests/host/pm_control_execution_test_fixture.c` | Sole mutable execution context, object/endpoint/VM/ownership setup, child freeze, activation, and blocked-exit-call helpers |
| `tests/host/pm_control_execution_prepare_test.c` | Prepare/abort/activate behavior and prepared-shape corruption evidence |
| `tests/host/pm_control_execution_token_test.c` | Prepared-token and exit-token exhaustion preservation |
| `tests/host/pm_control_execution_exit_test.c` | Exit begin/detach/release transaction and cleanup evidence |
| `tests/host/pm_control_execution_test_internal.h` | Declarations-only private fixture and case contract |

Exactly one mutable `pm_control_execution_test_context` is defined in
`pm_control_execution_test_fixture.c`. Snapshot copies are local test values,
not a second global context.

The current `pm_test` executable and `pm-lifecycle` CTest remain the only
native behavioral execution. `tests/host/pm_test.c` retains the current order
of the two PM-control suites before the PM-service lifecycle cases.

## Exact test-case owner migration

The structural migration map contains these 13 case functions:

| Final owner | Test cases |
| --- | --- |
| `pm_control_abi_test.c` | `test_contract_and_decode` |
| `pm_control_syscall_test.c` | `test_syscall_authority`, `test_syscall_authority_resolution`, `test_syscall_phase_and_retained_output` |
| `pm_control_reservation_test.c` | `test_state_preparation`, `test_reserve_abort_lifecycle`, `test_capacity_and_quarantine`, `test_invariant_rejection` |
| `pm_control_runtime_test.c` | `test_runtime_accepts_exact_frozen_vm_reservation` |
| `pm_control_execution_prepare_test.c` | `test_prepare_abort_activate`, `test_prepared_corruption_is_invariant` |
| `pm_control_execution_token_test.c` | `test_token_exhaustion_is_invariant` |
| `pm_control_execution_exit_test.c` | `test_exit_begin_detach_release` |

Together with the 19 moving production public symbols, the implementation
begins with an exact 32-entry function-owner migration map. Both suite entry
symbols, `micros_pm_control_state_prepare`, and the public captured-`ecall`
entry remain in their current owners.

## Current long-function disposition

Every current over-limit function is a required semantic split. None receives
an algorithmic exception.

| Function and current lines | Required task outline |
| --- | --- |
| `micros_pm_control_state_validate_objects` (157) | Common state/object validation; stopped/detached exit shape; empty reservation shape; prepared execution shape |
| `micros_pm_control_decode` (129) | Common aligned syscall preflight; reserve/abort decoding; prepare/activate decoding; exit decoding; success-only publication |
| `micros_pm_control_prepare_preflight` (129) | Complete invariant preflight; frozen mapping validation; hart/endpoint availability; thread-generation selection; candidate/result construction |
| `running_application_resolve` (124) | Active endpoint/process/mapping resolution; sole-thread discovery; fixed scheduler/context shape; blocked PM-call IPC shape; token publication |
| `micros_pm_control_handle_captured_user_ecall` (453) | Common decode; authority/runtime acquisition; phase gate; responsibility-specific command adapter dispatch |
| `test_contract_and_decode` (248) | Constant/layout contract; successful command-family decodes; reserved-register/alignment/unknown-command rejection matrix |
| `test_syscall_authority` (187) | Authorized baseline; caller corruption; endpoint/binding mismatch; PM-role/profile mismatch |
| `test_syscall_authority_resolution` (121) | Authoritative fixture; authorized resolution; foreign caller; corrupted owner; oversized/absent manifest state |
| `test_syscall_phase_and_retained_output` (158) | Phase matrix; one/two-chunk publication; translation fault/invariant preservation; preflight-failure output preservation |
| `test_reserve_abort_lifecycle` (136) | Reserve candidate/commit; duplicate reserve preservation; stale/exact abort; released-generation reuse |
| `test_runtime_accepts_exact_frozen_vm_reservation` (343) | Bootstrap ownership/VM setup; load begin; frame claim and scratch completion; freeze; corruption rejection; activated-pending classification |
| `freeze_child` (126) | Load begin; data-frame claim; page-plan construction; table-frame freeze; VM/ownership commit and validation |
| `initialize_fixture` in `pm_control_execution_test.c` (143) | Object/hart/ownership setup; PM endpoint/profile setup; reservation; VM handoff; child freeze |
| `test_prepare_abort_activate` (217) | Initial prepare; prepared-shape assertions; abort; reprepare; activation; complete final validation |
| `test_exit_begin_detach_release` (245) | Active blocked child; exit begin; endpoint/grant/thread detach; ownership corruption check; VM release; empty-process release |

The final implementation must remove all 15 entries from the migration
allowlist. Renaming or moving an unchanged long function does not satisfy the
gate. Each retained top-level function becomes a readable ordered outline of
named domain phases.

## Build ownership

`CMakeLists.txt` defines these exact ordered reusable source sets:

```text
MICROS_PM_CONTROL_CORE_SOURCES
MICROS_PM_CONTROL_TARGET_SOURCES
MICROS_PM_CONTROL_HOST_TEST_SOURCES
MICROS_PM_CONTROL_EXECUTION_HOST_TEST_SOURCES
```

`MICROS_PM_CONTROL_CORE_SOURCES` contains the seven portable owners plus the
existing `pm_control_runtime_core.c` and `pm_control_syscall_core.c`.

```text
kernel/pm_control_core.c
kernel/pm_control_abi.c
kernel/pm_control_validation.c
kernel/pm_control_reservation.c
kernel/pm_control_execution.c
kernel/pm_control_activation.c
kernel/pm_control_exit.c
kernel/pm_control_runtime_core.c
kernel/pm_control_syscall_core.c
```

`MICROS_PM_CONTROL_TARGET_SOURCES` contains `pm_control_runtime.c`, the public
syscall facade, shared syscall runtime, and four responsibility-specific
syscall adapters.

```text
kernel/pm_control_runtime.c
kernel/pm_control_syscall.c
kernel/pm_control_syscall_runtime.c
kernel/pm_control_syscall_reservation.c
kernel/pm_control_syscall_execution.c
kernel/pm_control_syscall_activation.c
kernel/pm_control_syscall_exit.c
```

The two host source sets contain their exact suite entry, fixture, and case
sources. The existing consumers are:

```text
MICROS_PM_CONTROL_HOST_TEST_SOURCES
  tests/host/pm_control_test.c
  tests/host/pm_control_test_fixture.c
  tests/host/pm_control_abi_test.c
  tests/host/pm_control_syscall_test.c
  tests/host/pm_control_reservation_test.c
  tests/host/pm_control_runtime_test.c

MICROS_PM_CONTROL_EXECUTION_HOST_TEST_SOURCES
  tests/host/pm_control_execution_test.c
  tests/host/pm_control_execution_test_fixture.c
  tests/host/pm_control_execution_prepare_test.c
  tests/host/pm_control_execution_token_test.c
  tests/host/pm_control_execution_exit_test.c
```

| Consumer | Required expansions |
| --- | --- |
| `pm_test` | Core plus both host-test source sets, each exactly once |
| `micros` | Core plus target source sets, each exactly once |

No split source is listed directly in a consumer in parallel with its source
set. No current target, executable, or CTest is renamed.

## Validation ownership

Every new production, test, and private-header path inherits the exact
validation classification of its current monolithic owner through
`tools/validation_plan.py`.

- Portable-core paths inherit `kernel/pm_control_core.c`.
- Target syscall paths inherit `kernel/pm_control_syscall.c`.
- State/ABI/reservation test paths inherit
  `tests/host/pm_control_test.c`.
- Execution/exit test paths inherit
  `tests/host/pm_control_execution_test.c`.
- The structural test itself is host-unit-only.

The implementation does not opportunistically change the current ownership
of either original test path. In particular, an existing fail-closed full-tier
fallback remains full-tier unless a separate reviewed validation task changes
it.

## Stable evidence identities

The implementation preserves:

- native executable `pm_test`;
- CTest `pm-lifecycle`;
- native suite entries `micros_pm_control_test_run` and
  `micros_pm_control_execution_test_run`;
- workflow `test-qemu-pm-service`;
- the exact PM-service marker and 45-second timeout;
- workflow `test-qemu-vm-scratch-preemption`;
- the existing downstream TTY, RAMFS, and VFS workflows that compile the same
  PM-control production sources;
- every current generated fixture, service image, test hook, panic reason,
  and shutdown behavior; and
- current validation-plan and CI workflow identities.

No new behavioral executable, CTest, custom target, workflow, marker, timeout,
or duplicate test run is introduced merely because sources are split.

## Structural Red gate

Implementation begins with:

```text
tests/host/test_kernel_pm_control_source_layout.py
```

The gate is fail-closed and verifies:

- the exact accepted `kernel/pm_control*.{c,h}` and
  `tests/host/pm_control*.{c,h}` family inventories;
- rejection of any additional unapproved matching family path;
- exact ordered source-set definitions and one accepted expansion per
  consumer;
- absence of direct raw split-source listings in those consumers;
- no textual C inclusion and no production inclusion of test-private
  headers;
- declarations-only private headers;
- byte-identical stable headers;
- exact public and test-case function owners;
- exactly one target PM-control state owner and one mutable context owner per
  split native family;
- recursive repository-local quoted-header closure from every accepted family
  source/header;
- no function above 120 lines anywhere in that complete closure;
- stable executable, CTest, workflow, marker, timeout, and suite-entry
  identities;
- exact validation-plan inheritance for every new path; and
- empty final 32-function owner migration and 15-function long migration
  collections.

Context and singleton ownership assertions are unconditional. Detection counts
every matching static declaration prefix across the recursive closure rather
than enumerating initializer or declarator suffix forms.

## Implementation sequence

The implementation proceeds as small green commits:

1. add the structural Red gate, exact source sets, private-header skeletons,
   and complete migration collections;
2. split ABI, complete validation, reservation, and matching state/ABI/
   reservation evidence;
3. split execution preparation, activation, fixture setup, and matching
   execution evidence;
4. split exit lifecycle and matching exit/token evidence;
5. split syscall runtime plus reservation, execution, activation, and exit
   adapters while retaining the public facade;
6. empty both migration collections and run one authoritative pull-request
   validation plan; and
7. obtain independent implementation review.

Each intermediate commit is behaviorally green and runs only the narrow
existing evidence for its active slice. The final
`tools/validation_plan.py --tier pr --base <review-base> --execute` result is
authoritative. Workflows already selected by that plan are not rerun
separately.

## Non-goals

This source correction does not authorize:

- any operation-13 ABI, command, result, state, token, endpoint, profile,
  scheduler, mapping, IPC, grant, or frame-ownership behavior change;
- PM, VM, VFS, scheduler, endpoint, IPC, grant, object, address-space, or
  execution-policy changes;
- concurrent spawn or exit transactions, multiple application threads, SMP,
  signals, fault-to-exit conversion, recovery, migration, or compatibility
  machinery;
- decomposition of the PM service, VM service, PM QEMU scenario, scheduler,
  endpoint, IPC, grant, or object signal files assigned to later audit units;
- new generated fixtures, behavioral tests, workflows, markers, timeouts, or
  CI jobs; or
- mechanical line-count splitting without a cohesive task boundary.

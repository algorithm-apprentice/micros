# PM Service Source Organization

This document defines the behavior-preserving decomposition of the PM
portable lifecycle, service adapter, canonical spawn decoder and layout
planner, native evidence, kernel report, and fixture generator. It applies
ADR-0051 to the final PM/VM correction required before the Step 14 coordinator.

Implementation is blocked until
[ADR-0058](../adr/0058-pm-service-source-decomposition.md) is Accepted and
its design pull request is merged.

## Reviewed base and reference

The reviewed source base is the merged VM decomposition:

```text
6fa6984a2491bafa005feb205438a3c4c63508b6
```

The fixed local MINIX trace and classifications are recorded in
[the research note](../research/minix-pm-service-source-organization.md).
Accepted ADR-0046 and its ADR-0050 refinements remain the behavior authority.

## Current inventory

| Signal file | Physical lines | Functions | Longest function |
| --- | ---: | ---: | --- |
| `servers/pm/pm_core.c` | 2,179 | 62 | `micros_pm_table_validate`, 349 |
| `servers/pm/pm_service.c` | 844 | 20 | `run_self_test`, 164 |
| `lib/spawn/spawn_request.c` | 410 | 9 | `micros_spawn_request_decode`, 160 |
| `tests/host/pm_test.c` | 2,687 | 42 | `test_replayable_transition_model`, 537 |
| `tests/host/spawn_abi_test.c` | 1,576 | 23 | `test_spawn_request_rejections`, 406 |
| `kernel/pm_service_test.c` | 454 | 5 | `micros_pm_service_test_handle_trap`, 164 |
| `tools/generate_pm_service_fixture.py` | 199 | 3 | `render_fixture`, 149 |
| **Total** | **8,349** | **164** | **16 functions above 120 lines** |

These are review signals, not mechanical splitting rules. The boundaries
follow identity, validation, wire parsing, forward and reverse transactions,
wait ownership, service adaptation, evidence, and rendering.

## Preserved contracts

The following headers remain byte-for-byte stable:

```text
include/micros/pm.h
include/micros/spawn.h
lib/spawn/spawn_request.h
servers/pm/pm_core.h
servers/pm/pm_control.h
kernel/pm_service_test.h
kernel/pm_service_test_fixture.h
tests/qemu/pm_service_protocol.h
```

No public declaration, structure, capacity, result, phase, token, protocol,
ELF policy, stack layout, privilege, scheduling policy, or syscall changes.
`servers/pm/pm_control.c` remains an unchanged, cohesive syscall wrapper.

ADR-0050's existing PM budget must be enforced for every service variant.
The current common user-ELF check has no PM-specific resident limit. Extend
that existing invocation inside `micros_add_bootstrap_user`, only when its
target is `micros_pm_service`, with `--resident-page-limit 32` and
`--stack-page-count 1`. Do not add a second ELF-check execution or a new
workflow. Other targets retain their existing checks unchanged.

### Authority

PM alone owns semantic PIDs, PM record generations, parent/reaper relationships,
hidden lifecycle metadata, publication decisions, and wait status. Kernel
process/thread/endpoint identities, VM mapping authority, and VFS descriptors
remain distinct. Static services are not PM application records.

Portable code continues to receive explicit table and output parameters.
Helpers retain the existing argument, invariant, stale-generation, state,
capacity, and success-only output order. Validation remains read-only.

### Lifecycle ordering

Preserve the exact current behavior:

- one nonreused PID and one generation-safe record per reservation;
- one global spawning record and the exact forward stage/token sequence;
- no published endpoint before final activation;
- exact reverse resource order, acknowledgment identity, and parent-loss
  reply suppression during rollback;
- normal exit rejects a hidden spawning child with `BUSY` until its rollback
  has completed, as refined by ADR-0050;
- exit clears the exiting parent's wait, reparents running/exiting/zombie
  children, and does not select an adopted zombie yet;
- detach, VM release, process release, then zombie publication;
- after publication, init's complete wait set is scanned first, followed by
  the original parent's set only when the handles differ;
- at most two ordered wait outcomes, independently accepted or restored; and
- no record release before successful wait-reply acceptance.

The current target service still rejects ordinary successful application
operations. Its isolated self-test exercises the existing synthetic kernel
lifecycle, not production PM/VFS/VM coordination or PID 1. This decomposition
does not implement that later coordinator.

## Portable PM owners

| Path | Responsibility |
| --- | --- |
| `servers/pm/pm_common.c` | Shared byte, identity, record-resolution, reset/release, and child-query primitives |
| `servers/pm/pm_core.c` | Table initialization, bootstrap-sealed runtime gate, public record resolution, and endpoint lookup |
| `servers/pm/pm_protocol.c` | Public request decoding and result construction |
| `servers/pm/pm_validation.c` | Complete read-only table, record, relationship, wait, and global-shape validation |
| `servers/pm/pm_spawn.c` | Allocation, PID consumption, forward stage updates, and activation |
| `servers/pm/pm_rollback.c` | Reverse resource/action construction and exact acknowledgment transitions |
| `servers/pm/pm_exit.c` | Exit-begin/reparent, detach, VM-release acknowledgment, and zombie publication |
| `servers/pm/pm_wait.c` | Child selection, wait staging, resume, and reply completion |
| `servers/pm/pm_internal.h` | Sole declarations-only portable private contract |

The common owner is a leaf. It exists separately from the facade because
runtime enable and public lookups call complete validation, while validation
needs lower-level identity and record queries. Keeping both in one owner
would create a facade/validation dependency cycle.

The original shared helpers move into `pm_common.c` with private
`pm_common_*` names:

```text
zero_bytes
bytes_are_zero
read_u64_le
handle_is_none
handles_equal
endpoint_is_concrete
endpoint_is_published_elsewhere
clear_record
record_handle
resolve_record
resolve_spawning_record
release_record
clear_wait
wait_matches_pid
record_is_child_of
has_matching_child
```

Other original helpers remain local to their cohesive owners:

| Owner | Original helper names |
| --- | --- |
| Protocol | `read_u32_le`, `write_u32_le`, `write_u64_le`, `result_is_defined`, `exit_kind_is_defined` |
| Validation | `spawn_resources_are_valid`, `spawn_is_clear`, `exit_is_clear`, `wait_is_clear` |
| Spawn | `resolve_forward_spawning_record`, `table_has_spawning_record`, `find_free_slot`, `allocate_record` |
| Rollback | `rollback_resource_for_stage`, `populate_rollback_action`, `populate_completed_rollback` |
| Exit | `clear_wait_outcomes` |
| Wait | `clear_wait_outcome`, `find_lowest_matching_zombie`, `populate_wait_outcome`, `stage_wait_result` |

`stage_wait_result` gains one narrow private declaration for exit's existing
post-zombie staging. It is not a public operation. Other helpers become
external only when another accepted owner requires them.

### Public symbol owners

| Owner | Stable public functions |
| --- | --- |
| Core | `micros_pm_table_initialize`, `micros_pm_enable_runtime`, `micros_pm_process_resolve`, `micros_pm_find_running_by_endpoint` |
| Protocol | `micros_pm_decode_request`, `micros_pm_build_result` |
| Validation | `micros_pm_table_validate` |
| Spawn | `micros_pm_begin_init`, `micros_pm_begin_spawn`, `micros_pm_spawn_reserve_kernel`, `micros_pm_spawn_prepare_image`, `micros_pm_spawn_freeze_mappings`, `micros_pm_spawn_prepare_descriptors`, `micros_pm_spawn_prepare_execution`, `micros_pm_spawn_commit_hidden`, `micros_pm_spawn_activate` |
| Rollback | `micros_pm_spawn_rollback_begin`, `micros_pm_spawn_rollback_ack` |
| Exit | `micros_pm_exit_begin`, `micros_pm_exit_detach`, `micros_pm_exit_release_vm`, `micros_pm_exit_release_process` |
| Wait | `micros_pm_wait_begin`, `micros_pm_wait_resume`, `micros_pm_wait_reply_complete` |

### Validation phases

The 349-line validator becomes an orchestration over named tasks:

1. table magic, reserved fields, PID-exhaustion, and init-history checks;
2. per-record reserved fields and free/quarantined shapes;
3. live identity and pairwise PID/endpoint uniqueness;
4. spawning, running/waiting, exiting, and zombie shapes;
5. exact parent, pending-child, and reap-pending relationships; and
6. accumulated spawning, reaper, active, and non-init counts.

Preserve original traversal and check order. Resolvers validate bounds and
generations before dereference. No phase invokes a lifecycle transition or
constructs a second process table.

## Canonical spawn request and layout

| Path | Responsibility |
| --- | --- |
| `lib/spawn/spawn_request.c` | Header, string references, canonical bytes, descriptor actions, and success-only decoded output |
| `lib/spawn/spawn_stack_layout.c` | Checked size/alignment primitives and initial frame occupancy planning |

Both include the unchanged public `spawn_request.h`. There is no shared
private helper between them, so no new private header is needed.

The decoder is split into header/count, ordered path/argv/env references and
unused tails, exact terminal cursor, descriptor actions/unused tail, and
final publication tasks. Preserve its current error namespace and order:
argument errors precede malformed input; output changes only on success.

The layout planner retains its existing bounded, checked arithmetic, offsets,
16-byte alignment, 2,048-byte occupancy limit, and output-publication timing.
It does not call the decoder; the decoder does not call the planner. Stack
construction and ELF planning remain in the already merged VM image owners.

## Service adapter

| Path | Responsibility |
| --- | --- |
| `servers/pm/pm_service.c` | Sole entry, startup, notification/request branch selection, and receive/reply-receive loop |
| `servers/pm/pm_service_fixture.c` | Bootstrap configuration, sole mutable service context, byte helpers, and configuration validation |
| `servers/pm/pm_service_bootstrap.c` | Token-bound launcher readiness |
| `servers/pm/pm_service_requests.c` | Existing ordinary request classification, response building, and preservation checks |
| `servers/pm/pm_service_self_test.c` | Existing synthetic prepare/abort/activate/exit scenario and its VM exchanges |
| `servers/pm/pm_service_scratch.c` | Existing scratch driver's reserve/reply/peer-hold/peer-run/block path |
| `servers/pm/pm_service_internal.h` | Declarations-only adapter/fixture contract |

One `pm_service_context` owns the original `process_table`, volatile initialized
data sentinel, and conditional `initial_table` snapshot. The required exported
`micros_bootstrap_service_config` remains a separate exact ABI object in the
fixture owner. All service helpers receive explicit context where they use
its state.

Preserve initialization and validation before readiness, the initial receive,
kernel-notification-first dispatch, exact one-shot runtime enable, self-test
entry only under its existing guard, and ordinary atomic reply/receive.

Move the original `pm_service_*` byte helpers and `validate_configuration`
to the fixture, `send_ready` to bootstrap, `protocol_result`/`serve_request`
to requests, the three VM exchange helpers and `run_self_test` to self-test,
and `run_scratch_preemption_test` to scratch. Retain narrow private
`pm_service_*` names.

`run_self_test` becomes named output-initialization, reserve/load, prepare/
stale-token/abort/re-prepare, activate/commit/child-exit capture, exit-begin
preservation, and detach/VM-release/process-release/report phases. Every
original check and collaborator order remains unchanged.

Scenario adapters do not call siblings or include the portable private header.
PM-only syscall wrappers remain in unchanged `pm_control.c`; no new caller
authority or lifecycle handler is introduced.

## Native PM evidence

The existing `pm_test` executable and `pm-lifecycle` CTest remain intact.
Its two existing kernel-control suite entries run first, then these exact
portable cases:

```text
test_protocol_contract
test_runtime_gate_and_spawn
test_reverse_rollback_and_identity
test_rollback_acknowledgment_identity
test_generation_and_pid_exhaustion
test_failed_init_is_terminal
test_endpoint_lookup_and_failure_preservation
test_orphaned_lifecycle_is_invalid
test_exit_wait_and_reply_restore
test_wait_selection
test_parent_exit_reparents_and_aborts
test_nested_parent_exit_stages_independent_waits
test_replayable_transition_model
```

| Path | Responsibility |
| --- | --- |
| `tests/host/pm_test.c` | Sole `main` and exact suite/case order |
| `tests/host/pm_test_fixture.c` | Stateless codecs, request builders, complete-spawn/init/exit setup, and rollback-driving helpers |
| `tests/host/pm_protocol_test.c` | Protocol contract case |
| `tests/host/pm_spawn_test.c` | Runtime/spawn, reverse rollback, acknowledgment identity, exhaustion, and failed-init cases |
| `tests/host/pm_validation_test.c` | Endpoint/failure preservation and orphan rejection |
| `tests/host/pm_exit_wait_test.c` | Exit/wait restore, selection, reparenting, and independent-parent outcomes |
| `tests/host/pm_model_test.c` | Model initialization, seeded loop/dispatch, per-step checks, and replay diagnostics |
| `tests/host/pm_model_reference.c` | Independent reference queries, wait-result staging, and existing comparison |
| `tests/host/pm_model_operations.c` | Eight operation handlers and cohesive forward/exit/wait subphases |
| `tests/host/pm_test_internal.h` | Declarations-only shared fixture/case contract |
| `tests/host/pm_model_test_internal.h` | Declarations-only automatic model-context/reference contract |

PM cases already own automatic tables and snapshots; keep them local. Do not
invent a mutable global fixture table. Shared setup helpers continue receiving
explicit tables and handles. The unchanged kernel-control evidence retains
its separately owned contexts and source sets.

The model retains exactly 4,096 steps, seed `0x4d4943524f53504d`, one random
draw per step, original operation/slot/flag trace encoding, operation order,
initialization checks, reference state, and comparisons after each operation.
One automatic model context may carry its existing local table, reference,
init handle, seed/RNG, step, and trace across helpers. No heap, global model,
new CTest, model workflow, additional random draw, or oracle change.

## Native spawn-ABI evidence

The existing `spawn_abi_test` executable, `spawn-abi` CTest, and final
`spawn ABI tests passed` output remain intact, with this exact case order:

```text
test_spawn_request_contract
test_spawn_request_boundaries
test_spawn_request_rejections
test_application_stack
test_application_stack_boundaries
test_elf_plan
test_elf_header_rejections
test_elf_segment_rejections
```

| Path | Responsibility |
| --- | --- |
| `tests/host/spawn_abi_test.c` | Sole `main`, exact order, and output |
| `tests/host/spawn_abi_test_fixture.c` | Sole mutable buffer context, byte/string/request/occupancy/ELF builders |
| `tests/host/spawn_request_test.c` | Three canonical-request cases and rejection helper |
| `tests/host/application_stack_test.c` | Two stack cases |
| `tests/host/vm_elf_test.c` | Three ELF cases and unchanged-output rejection helper |
| `tests/host/spawn_abi_test_internal.h` | Declarations-only fixture/case contract |

One `spawn_abi_test_context` owns all seven original mutable static buffers:
request and ELF bytes; boundary path, argument, and environment storage; and
the separate occupancy argument/environment storage. Preserve their existing
sizes and separation, including the executable-limit-plus-one buffer.
Cases and stateful builders receive explicit context. Plans and sentinels
remain automatic values.

## Kernel PM report

| Path | Responsibility |
| --- | --- |
| `kernel/pm_service_test.c` | Public launch facade |
| `kernel/pm_service_test_fixture.c` | Immutable expected services, profiles, policies, and runtime configuration |
| `kernel/pm_service_test_report.c` | Public trap/report hook, panic boundary, authority/fault/current/readiness checks, final resource/generation/token report |
| `kernel/pm_service_test_internal.h` | Declarations-only private configuration contract |

`micros_pm_service_test_handle_trap` moves to the report owner; launch remains
in the facade. The existing scenario has no mutable global state. Retain
automatic authority/current/consumed-handle/output values rather than adding
a context, cache, or copied authoritative registry.

The report is split into authority acquisition, exact breakpoint/PC/magic and
sealed phases, transaction/mapping cleanup, process/thread/endpoint/frame/
grant/generation baseline, runtime validation, current PM/readiness, and
marker/shutdown tasks. Preserve short-circuit order, sentinels, exact consumed
identities, marker, and timeout. Assembly and probe sources remain unchanged.

The retained `tests/host/test_kernel_pm_control_source_layout.py` is a direct
consumer of this report: it currently reads the facade for the PM marker.
In the same green slice that moves the report, redirect those assertions to
`kernel/pm_service_test_report.c`. Preserve their exact single-marker count,
required fields, CMake marker, and 45-second timeout checks. This adapts only
the report consumer; kernel PM-control ownership and behavior stay unchanged.

## Fixture generator

`tools/generate_pm_service_fixture.py` remains one CLI. Its renderer separates
validated descriptors/report symbol, resident limits, images, exact manifest,
and report-address rendering. Preserve arguments, error prefix, output include,
IDs 101 through 104, manifest entry order, limits, report symbol/address,
and every output byte for identical checked ELF inputs.

No framework, second generator, fixture format, or reusable prototype is
introduced. Existing generator tests gain an exact pre-extraction synthetic
output baseline during implementation.

## Dependency direction

```text
pm_common -> no PM responsibility owner
pm_core -> pm_common + pm_validation
pm_protocol -> pm_common
pm_validation -> pm_common
pm_spawn -> pm_common + pm_validation
pm_rollback -> pm_common + pm_validation
pm_wait -> pm_common + pm_validation
pm_exit -> pm_common + pm_validation + private wait staging
```

Complete validation calls no transition. Spawn, rollback, and exit do not
call one another. Wait does not call exit. The exit-to-wait edge preserves
the existing post-zombie staging and introduces no reverse edge.

The service facade calls shared fixture/bootstrap and its selected adapters;
adapters call shared fixture and supported public PM/runtime/syscall APIs,
not siblings or `pm_internal.h`. Native tests/models use public APIs and their
own private evidence contracts. Reference-model decisions never depend on
production private helpers.

## Exact CMake source sets

```text
MICROS_PM_CORE_SOURCES
  servers/pm/pm_common.c
  servers/pm/pm_core.c
  servers/pm/pm_protocol.c
  servers/pm/pm_validation.c
  servers/pm/pm_spawn.c
  servers/pm/pm_rollback.c
  servers/pm/pm_exit.c
  servers/pm/pm_wait.c

MICROS_SPAWN_REQUEST_SOURCES
  lib/spawn/spawn_request.c
  lib/spawn/spawn_stack_layout.c

MICROS_PM_SERVICE_SOURCES
  servers/pm/pm_service.c
  servers/pm/pm_service_fixture.c
  servers/pm/pm_service_bootstrap.c
  servers/pm/pm_service_requests.c
  servers/pm/pm_service_self_test.c
  servers/pm/pm_service_scratch.c

MICROS_PM_CORE_HOST_TEST_SOURCES
  tests/host/pm_test.c
  tests/host/pm_test_fixture.c
  tests/host/pm_protocol_test.c
  tests/host/pm_spawn_test.c
  tests/host/pm_validation_test.c
  tests/host/pm_exit_wait_test.c
  tests/host/pm_model_test.c
  tests/host/pm_model_reference.c
  tests/host/pm_model_operations.c

MICROS_SPAWN_ABI_HOST_TEST_SOURCES
  tests/host/spawn_abi_test.c
  tests/host/spawn_abi_test_fixture.c
  tests/host/spawn_request_test.c
  tests/host/application_stack_test.c
  tests/host/vm_elf_test.c

MICROS_PM_SERVICE_QEMU_TEST_SOURCES
  kernel/pm_service_test.c
  kernel/pm_service_test_fixture.c
  kernel/pm_service_test_report.c
```

Every current consumer expands each required set exactly once:

| Consumer | Required new expansions |
| --- | --- |
| `pm_test` | PM core and PM native-test sets |
| `spawn_abi_test` | Spawn-request and spawn-ABI native-test sets |
| `vm_core_test`, `vfs_test`, `vfs_model_test`, `vfs_service_test` | Spawn-request set |
| `micros_vm_server`, `micros_vfs_service` | Spawn-request set |
| Every existing `micros_pm_service` branch | PM core and service sets |
| `MICROS_BUILD_PM_SERVICE_TEST` target sources | Kernel PM-report set |

Existing VM, VFS, frame, and kernel PM-control sets remain unchanged and are
still expanded as before. `pm_control.c`, the two existing PM assembly paths,
probe, generated fixture, and resident limits stay with their current
consumers. No raw split-source listing is left beside its new source set.

## Validation and structural gate

New paths inherit their exact monolithic classification: portable PM from
`pm_core.c`; service adapters from `pm_service.c`; layout from
`spawn_request.c`; PM cases/model from `pm_test.c`; spawn cases/fixture from
`spawn_abi_test.c`; kernel fixture/report from `pm_service_test.c`. Private
headers inherit the corresponding owner. Existing unknown/full-tier
classification is preserved, not narrowed. The new structural test alone is
host-unit-only.

Implementation begins with
`tests/host/test_pm_service_source_layout.py`. It verifies exact family paths,
ordered source sets/consumers, stable hashes, declarations-only private
headers, no textual source inclusion, public/case/helper ownership,
recursive dependency direction, global/static-local state ownership, case
and scenario order, model seed/steps/trace, generator/evidence identities,
validation inheritance, and C/Python long functions.
It also verifies that all five current PM service target branches use the
common helper and that its single PM ELF-check invocation receives the exact
32-page/one-stack-page options. Retained report consumers are adapted rather
than bypassed or weakened.

The original owner migration contains exactly 44 functions: 21 moving
portable PM functions, one stack-layout function, one kernel hook, 13 PM
cases, and eight spawn-ABI cases. The original long-function migration
contains exactly the 16 functions in the following table. Both collections
may only shrink and must be empty in the final implementation.

| Original long function | Required semantic tasks |
| --- | --- |
| `micros_pm_table_validate` | Header; record shape/uniqueness; state-specific checks; relationships/waits; global counts |
| `run_self_test` | Output setup; reserve/load; prepare/abort/re-prepare; activate/child capture; exit preservation; release/report |
| `micros_spawn_request_decode` | Header/counts; ordered references/tails; cursor; descriptor actions/tail; publication |
| `test_protocol_contract` | Valid decoding; malformed/version/type/token preservation; valid result bytes; invalid-result preservation |
| `test_rollback_acknowledgment_identity` | Prepared setup; bad begin/ack preservation; exact reverse walk; final identity |
| `test_endpoint_lookup_and_failure_preservation` | Lookup identity; rollback/stage preservation; endpoint/exit/wait rejection |
| `test_exit_wait_and_reply_restore` | Pending/busy wait; staged exit/release; failed reply restore; successful collection |
| `test_parent_exit_reparents_and_aborts` | Child/wait setup; busy hidden-child preservation; rollback/reparent; combined wait/collection |
| `test_nested_parent_exit_stages_independent_waits` | Nested setup; reparent without early selection; two ordered outcomes; independent restore/collection |
| `test_replayable_transition_model` | Initialization; one draw/trace per step; eight operation handlers; complete existing per-step checks |
| `test_spawn_request_rejections` | Header/reserved/counts; references/tails; strings; descriptor shape/duplicates |
| `test_application_stack` | Initial plan; pointers/strings; auxv; zero gaps/tails; path exclusion |
| `test_elf_header_rejections` | Truncation/identity; fixed header/entry; allowed flags; ignored section metadata |
| `test_elf_segment_rejections` | Type/permissions/alignment; file/VA geometry; size/overflow; overlap and scratch separation |
| `micros_pm_service_test_handle_trap` | Authority/fault; cleanup baseline; runtime validation; current/readiness; report |
| `render_fixture` | Descriptors/symbol; limits; images; manifest; exported report address |

Do not satisfy a migration by moving or renaming an unchanged long function.
State/inventory assertions are unconditional. Shared structural scan helpers
should be reused where suitable; initializer, array, co-declaration, nested
include, and static-local forms must not evade ownership.

## Evidence and implementation sequence

Preserve `pm_test`/`pm-lifecycle`, `spawn_abi_test`/`spawn-abi`, the real
`micros_pm_service` ELF and 32-page limit, `test-qemu-pm-service`,
`test-qemu-vm-scratch-preemption`, and the TTY/RAMFS/VFS consumers. Preserve
all current markers, timeouts, compile-time guards, labels, seeds, and
generator/assembly symbols. No split creates a new behavioral execution.

After this design is reviewed and merged, use small green slices:

1. structural Red, exact source sets, private skeletons, and migrations;
2. common/facade/protocol/validation owners and matching PM evidence;
3. spawn/rollback/exit/wait owners and native case/model phases;
4. canonical decoder/layout and spawn-ABI fixture/case phases;
5. service fixture/bootstrap/request/self-test/scratch owners, with the
   common single-invocation PM budget check covering every variant;
6. kernel immutable fixture/report, its retained structural consumer, and
   generator phases;
7. empty migrations and one authoritative PR validation; and
8. independent implementation review.

Each extraction runs only its narrow existing gate. The final
`python3 tools/validation_plan.py --tier pr --base <review-base> --execute`
result is authoritative; selected workflows are not rerun separately.
Reviewers inspect author evidence rather than repeating suites.

## Non-goals

No coordinator, successful application spawn, init/runtime/shell, new rollback
or exit policy, signal/fault delivery, PID reuse, compatibility/migration,
credentials/process groups, recovery, concurrency, kernel directory move,
or unrelated audit correction. No API, wire, allocation, authority, scheduling,
test-strength, executable, workflow, marker, timeout, or oracle change.

# Kernel Endpoint Source Organization

## Status, base, and goal

This is the Accepted design contract for
[ADR-0067](../adr/0067-kernel-endpoint-source-decomposition.md).
Independent read-only design review is clean. The paired
`kernel-endpoint-decomposition-implementation` starts only after this
separate design merges.

```text
reviewed micros base: 7e107f5189cbf6f3aa44dfc05652d041e803c60c
fixed MINIX base:     4db99f4012570a577414fe2a43697b2f239b699e
```

The four audited files are `kernel/endpoint.c`,
`tests/host/endpoint_test.c`, `tests/host/endpoint_model_test.c`, and
`kernel/endpoint_test.c`: 5,457 lines, 98 definitions, ten over-limit
functions. The final map has 25 C sources, four new private headers, four
source sets, 90 original-definition moves, and no long-function exception.
Five portable definitions and the three evidence entry points stay at their
existing roots.

The [fixed MINIX organization study](../research/minix-kernel-endpoint-source-organization.md)
supplements, not replaces, the existing behavioral ledger.
Accepted endpoint, IPC, grant, bootstrap, and PM decisions remain authoritative.
This correction changes no runtime-DAG edge.

## Preserved authority and state

- Process generations remain the sole endpoint lifetime authority. Preserve
  unsigned slot/generation packing, `ANY`/`NONE`, quarantine, and exact
  process/endpoint agreement.
- A process may consume its endpoint lifecycle once. Close clears primary
  endpoint/profile and the endpoint record but retains
  `endpoint_lifecycle_consumed` until process release.
- Registry initialization is one-shot from zero storage. Invalid arguments,
  dirty storage, repeated initialization, bad capacity, canonical profile,
  duplicate ID/name, and target-mask failures retain their existing order.
  Candidate profiles are automatic and no registry byte publishes early.
- Profile IDs, printable/NUL-terminated names, padding, operation masks,
  target masks, and kernel-operation bits remain exact. Profile queries keep
  complete registry validation and their existing argument precedence.
- Ordinary resolution and authorization accept only active endpoints.
  Internal resolution may accept reserved or source-only exact generations.
  A source-only destination returns `CLOSING` without publishing an output.
- Strict launcher sealing still rejects local IPC, pending-source and foreign
  receiver/token dependencies, and requires held/clear owner threads.
  PM teardown's distinct preflight permits the exact active endpoint before
  the caller commits stopped reply-wait teardown.
- Complete source-only validation retains both canonical inactive/clear
  threads and `INACTIVE | IPC_REPLY` with its exact valid reply wait.
  Existing callee/source-only token and staged readiness-ack rules remain.
- One original automatic queued-thread witness is shared by both endpoint
  queue walks and final thread closure. Preserve bounded walks, exact
  generation/owner/flag checks, notification sources, matching-work rejection,
  staged completion shapes, token uniqueness, and `last_reply_token` bounds.
- Prevalidated publication/source-only/close helpers remain void and
  non-failing. Their external SIE-clear transaction is the authority boundary;
  do not insert a recoverable check or observer into an intermediate state.

No validation repairs corruption. No new error, fallback, policy, privilege,
capacity, endpoint, thread, grant, queue, or recovery mechanism is authorized.

## Exact final source sets

The separate implementation adds these four ordered lists:

```text
MICROS_ENDPOINT_CORE_SOURCES (10)
  kernel/endpoint.c
  kernel/endpoint_codec.c
  kernel/endpoint_identity.c
  kernel/endpoint_registry.c
  kernel/endpoint_privilege.c
  kernel/endpoint_completion_validation.c
  kernel/endpoint_queue_validation.c
  kernel/endpoint_object_validation.c
  kernel/endpoint_publication.c
  kernel/endpoint_close.c

MICROS_ENDPOINT_HOST_TEST_SOURCES (7)
  tests/host/endpoint_test.c
  tests/host/endpoint_test_fixture.c
  tests/host/endpoint_identity_test.c
  tests/host/endpoint_profile_test.c
  tests/host/endpoint_validation_test.c
  tests/host/endpoint_lifecycle_test.c
  tests/host/endpoint_source_only_test.c

MICROS_ENDPOINT_MODEL_HOST_TEST_SOURCES (5)
  tests/host/endpoint_model_test.c
  tests/host/endpoint_model_fixture.c
  tests/host/endpoint_model_reference.c
  tests/host/endpoint_model_projection.c
  tests/host/endpoint_model_operations.c

MICROS_ENDPOINT_QEMU_TEST_SOURCES (3)
  kernel/endpoint_test.c
  kernel/endpoint_test_fixture.c
  kernel/endpoint_test_scenarios.c
```

New private headers are:

```text
kernel/endpoint_private.h
tests/host/endpoint_test_internal.h
tests/host/endpoint_model_internal.h
kernel/endpoint_test_internal.h
```

The existing `include/micros/endpoint.h` and
`kernel/endpoint_internal.h` remain byte-exact. The latter is the existing
six-function inter-subsystem prevalidated interface, not the new family-private
helper contract. Publication/close owners include it for their existing
definitions. Other callers do not acquire the new private helper header.

## Every original portable definition

The following partition contains all 67 definitions exactly once:

| Owner | Original definitions |
| --- | --- |
| `endpoint.c` | `micros_endpoint_resolve_internal`, `micros_endpoint_resolve_active`, `micros_endpoint_authorize_operation`, `micros_endpoint_authorize_target`, `micros_endpoint_authorize_kernel_operation`. |
| `endpoint_codec.c` | `storage_is_zero`, `clear_bytes`, `copy_bytes`, `bytes_equal`, `bytes_are_zero`, `names_equal`, `profile_is_zero`, `endpoint_record_is_zero`, `endpoint_ipc_state_is_zero`, `thread_handle_is_zero`, `thread_handle_is_shallow_valid`, `queue_pair_is_shallow_valid`, `pending_notification_state_is_shallow_valid`, `clear_endpoint_record`, `canonicalize_profile`. |
| `endpoint_identity.c` | `process_handles_equal`, `thread_handles_equal`, `require_initialized`, `micros_endpoint_pack`, `micros_endpoint_unpack`, `process_resolve_error`, `endpoint_record_resolve_validated`, `process_has_current_thread`, `process_threads_are_held`, `process_threads_have_clear_ipc_state`. |
| `endpoint_registry.c` | `micros_endpoint_registry_initialize`, `micros_endpoint_registry_validate`. |
| `endpoint_privilege.c` | `micros_privilege_profile_resolve`, `micros_privilege_profile_allows_operation`, `micros_privilege_profile_allows_target`, `micros_privilege_profile_allows_kernel_operation`. |
| `endpoint_completion_validation.c` | `delivery_state_is_clear`, `reply_wait_state_is_valid`, `source_only_process_threads_are_valid`, `staged_reply_token_binding_is_valid`, `notification_message_is_valid`, `read_u32_le`, `source_only_message_is_valid`, `staged_delivery_state_is_valid`, `staged_error_state_is_valid`, `staged_no_message_success_state_is_valid`. |
| `endpoint_queue_validation.c` | `sender_queue_state_is_valid`, `receiver_queue_state_is_valid`, `endpoint_queue_is_valid`, `pending_notification_state_is_valid`, `receiver_matches_pending_notification`, `endpoint_has_matchable_work`. |
| `endpoint_object_validation.c` | `thread_has_no_ipc_flags`, `micros_endpoint_registry_validate_objects`. |
| `endpoint_publication.c` | `micros_endpoint_reserve`, `micros_endpoint_install_profile`, `micros_endpoint_activate`, `micros_endpoint_preflight_publish`, `micros_endpoint_commit_publish_prevalidated`. |
| `endpoint_close.c` | `micros_endpoint_preflight_source_only_for_teardown`, `micros_endpoint_preflight_source_only`, `micros_endpoint_commit_source_only_prevalidated`, `micros_endpoint_seal_source_only`, `endpoint_has_foreign_waiters`, `endpoint_is_pending_notification_source`, `micros_endpoint_close_commit_prevalidated`, `micros_endpoint_close`. |

Canonical representation is a leaf. Its byte loops and canonicalizers are
not merged or optimized during movement. Identity/ownership queries retain
their validated-input preconditions and do not call whole-state validation.
The existing three representation `_Static_assert`s belong with identity.
The existing registry-magic definition moves, unchanged, into the new private
header because initialization and initialized-state checks both need it.

Dependency direction is exact:

```text
codec, identity: leaves
registry -> codec, identity
privilege -> registry
completion validation -> codec, identity
queue validation -> codec, identity, completion validation
object validation -> registry, privilege, completion validation, queue validation
facade -> identity, object validation, privilege
publication -> codec, identity, object validation, privilege
close -> codec, identity, object validation
```

These are endpoint-family edges; existing public kernel-object functions and
bootstrap message constants remain external dependencies. Only completion
validation needs the existing bootstrap acknowledgment constants.
No owner calls IPC mutation, grants, a runtime singleton, or architecture
operations. In particular, canonical profile construction must not move into
`endpoint_privilege.c`: checked queries call registry validation, which calls
canonical construction.

Exactly these 31 existing static helpers need cross-file declarations:

```text
bytes_are_zero, bytes_equal, canonicalize_profile, clear_bytes,
clear_endpoint_record, copy_bytes, delivery_state_is_clear,
endpoint_has_matchable_work, endpoint_ipc_state_is_zero,
endpoint_queue_is_valid, endpoint_record_is_zero,
endpoint_record_resolve_validated, names_equal,
pending_notification_state_is_shallow_valid,
pending_notification_state_is_valid, process_handles_equal,
process_has_current_thread, process_resolve_error,
process_threads_are_held, process_threads_have_clear_ipc_state,
profile_is_zero, queue_pair_is_shallow_valid, reply_wait_state_is_valid,
require_initialized, source_only_process_threads_are_valid,
staged_delivery_state_is_valid, staged_error_state_is_valid,
staged_no_message_success_state_is_valid, storage_is_zero,
thread_handle_is_zero, thread_handles_equal
```

Use `endpoint_` private names, without doubling an existing prefix.
Unshared helpers stay static. The private header admits those exact original
types, the include guard and existing magic, not a helper implementation,
mutable object, runtime getter, or new public authority.

## Native, model, and target ownership

The deterministic family partitions all fifteen original definitions:

| Owner | Original responsibility |
| --- | --- |
| `endpoint_test.c` | `main`, its exact 25-case table, external case declarations, ordered run/print/first-failure loop. |
| `endpoint_test_fixture.c` | `profile`, `context_pattern`, `write_u32_le`, `setup_lifecycle_fixture`; sole native fixture storage. |
| `endpoint_identity_test.c` | `test_endpoint_encoding_boundaries`, `test_resolution_rejects_unimplemented_slots`. |
| `endpoint_profile_test.c` | `test_profile_table_is_immutable_and_specific`, `test_invalid_profile_tables_preserve_storage`. |
| `endpoint_validation_test.c` | `test_validator_rejects_corruption`, `test_relationship_validator_rejects_corruption`. |
| `endpoint_lifecycle_test.c` | `test_endpoint_lifecycle_and_authorization`, `test_profile_install_and_close_require_held_threads`, `test_endpoint_reuse_rejects_stale_generation`. |
| `endpoint_source_only_test.c` | `test_source_only_seal_preserves_staged_source`. |

The model family partitions its fifteen original definitions:

| Owner | Original responsibility |
| --- | --- |
| `endpoint_model_test.c` | `micros_endpoint_model_test_run`: setup, exact draw/script outline, operation dispatch, record/compare, cleanup, coverage and terminal result. |
| `endpoint_model_fixture.c` | `record_transition`, `model_fail`, `setup_fixture`; sole model fixture and readonly operation names. |
| `endpoint_model_reference.c` | `phase_has_endpoint`, `find_slot`, `slot_is_live`, `slot_has_endpoint`, `slot_has_nonactive_endpoint`, `slot_is_releasable`, `slot_has_stale_endpoint`, `find_active_profile`, `target_is_allowed`: independent expected selection and authority. |
| `endpoint_model_projection.c` | `validate_model`: complete production/reference and count comparison. |
| `endpoint_model_operations.c` | `next_random`, and the newly outlined eight operation cases and authorization subtasks. |

Both actual model link consumers retain the same source set. Existing
`tests/host/ipc_model_main.c` remains byte-exact, including its call to
`micros_endpoint_model_test_run`. Do not add a new model CTest or silently
change whether an existing driver executes the model.

The target has one original definition, retained in `kernel/endpoint_test.c`:
`micros_endpoint_runtime_run_self_test`. It owns the original automatic work,
single IRQ-save/restore scope, first failure to `done`, and final result.
`kernel/endpoint_test_fixture.c` owns the exact two readonly profile objects
and cold/one-shot runtime preparation tasks.
`kernel/endpoint_test_scenarios.c` owns the outlined visibility/authorization,
grant lifecycle/cancellation, generation reuse, stale denial and complete
cleanup tasks. These are new phase definitions, not invented original moves.

Native/model/target helper prefixes are respectively `endpoint_test_`,
`endpoint_model_`, and `endpoint_qemu_`. Headers contain only needed owned
types, declarations and admitted existing assertion macros. Include and
call checks account for address-taken test cases and predicates, not merely
direct calls. Neither expected-state code nor ordinary evidence helpers
imports `kernel/endpoint_private.h`.

## Storage and exact lifetimes

| Family | Original mutable storage | Required owner |
| --- | --- | --- |
| Native | `registry`, `objects`, `processes[2]`, `threads[2]`, `hart` | One static five-field native fixture, borrowed by case roots and passed explicitly to setup/phases. |
| Model | `registry`, `objects`, `hart`, `trace[32]`, `trace_count` | A separate static five-field model fixture; no shared native/model registry or trace. |
| Target | Existing borrowed object/endpoint/grant pointers and automatic cancellation plan, record, handles, grant tokens, baseline counts and result locals | One original entry lifetime, optionally transported through the exact typed automatic work described below; no new mutable static target context. |

Reset only at original sites. Native setup clears registry and objects, not
the entire fixture. Model setup clears registry, objects and trace and sets
the original trace count to zero; it does not blanket-zero the hart or new
work. Keep one-shot runtime probes and failed-initialization observations.

Automatic snapshots, rejected tables, user contexts and candidate profile
arrays stay automatic. Ordinary helpers receive explicit original state,
not another fixture lookup or mutable cached result.

Permitted automatic case-work fields are exactly the original declarations:
the invalid-profile table and snapshot; lifecycle endpoint pair/stale endpoint/
stale target/record pointer; held-thread endpoint/context; and source-only
endpoint pair/record/receiver/index. Preserve only original initializers,
including the zero endpoint pair, NULL records, and context-pattern call.
No blanket initialization, duplicate snapshot, copied work, or retained
work pointer is permitted.

Model run work may group only `model[4]`, `coverage`, `random_state`, and
`step`; the original constant automatic seed stays in the entry.
Per-iteration work may group only `random`, `operation`, `slot`, and `value`.
Initialize them in the original order; preserve the two original explicit
model/coverage clears and random-state assignment, not an aggregate zero.
Authorization-only locals retain their case scope and original initializers.

Target work contains only its original automatic declarations. The saved IRQ
status and `passed = false` remain in entry. The same `done` path restores
that saved status exactly once after every failure or success. Helpers do
not restore interrupts, retain work or allocate another registry.
Resource cleanup remains the original success path, not a newly invented
rollback after a failed target assertion.

Readonly static objects retain exact type, initializer, duration and unique
storage: the native 25-case table, resolution boundary slots, model's eight
operation names, its 17-operation/value scripts, its three authorization
operations, its profile table, and the target profile table/invalid profile.
Hoisting a local static object to file scope in the same owner is permitted
only with raw actual-object verification before reconstruction. Do not copy
masked declaration text or substitute metadata for current literal bytes.

Existing assertion and local corruption macros retain format, conditions,
snapshot/reset/action/comparison order and first failure. Their actual source
file/line and stringized expressions follow the new real call sites; quoted
case labels, diagnostics and model trace formats remain unchanged.

## Required semantic disposition

Every resulting function is at most 120 physical lines; statements are not
compressed to satisfy the signal.

| Original function | Lines | Required ordered task outline |
| --- | ---: | --- |
| `micros_endpoint_registry_validate` | 136 | Initialized/header checks; canonical complete profile table and mask closure; shallow complete endpoint records; original success. |
| `micros_endpoint_registry_validate_objects` | 196 | Argument/registry/object validation; original queued-thread witness initialization; complete process/endpoint/queue/notification pass; complete thread/completion/token-uniqueness pass; success. |
| native `test_invalid_profile_tables_preserve_storage` | 135 | Original profile setup; duplicate ID/name cases; operation/ID/name failures; target-mask failures; zero/exact-capacity rejection and complete preservation. |
| native `test_endpoint_lifecycle_and_authorization` | 221 | Hidden reservation/output; bound-release denial; profile/publication; exact asymmetric call/send/notify matrix; stale-versus-argument precedence; reply/kernel authority; final relationship proof. |
| native `test_profile_install_and_close_require_held_threads` | 126 | Context/admission and installation denial; hold/remove/install/activate; runnable-close denial; held close and consumed-lifecycle denial; scheduler/context cleanup. |
| native `test_source_only_seal_preserves_staged_source` | 150 | Activate exact peers; stage canonical ready acknowledgment; seal and accept the exact source; reject/restore wrong message; internal/closing/authorization/output checks; refuse close until staged source clears; final close. |
| native `main` | 123 | Move the exact local static 25-case table to file scope in the same runner; retain the one cohesive ordered run/print/first-failure loop. No case regrouping or new execution. |
| model `validate_model` | 158 | Existing production validation/token check; every free/live process, thread and endpoint phase projection; exact aggregate counts. |
| model `micros_endpoint_model_test_run` | 659 | Original setup; two base draws and scripted override; eight operation cases with separate authorization subtasks; trace then whole comparison; exact teardown/baseline; all nineteen coverage checks. |
| target `micros_endpoint_runtime_run_self_test` | 428 | IRQ/baseline and cold runtimes; one-shot preparation/client reservation; held-release and visibility/profile/authorization; grant create/inspect/denial/revoke/cancel-plan; close/commit/reuse; stale grant/endpoint and full cleanup; one common IRQ restoration. |

Registry initialization's existing 97-line function also exposes candidate
construction and duplicate checks, closed target-mask validation, then
non-failing registry copy/count/magic publication. One original automatic
candidate array remains authoritative. Do not add an early registry store,
another candidate array, or a new post-publication failure.

Existing bounded publication/close functions already use distinct validation
and prevalidated commits; keep those ordered boundaries instead of wrapping
individual field assignments. Completion validation retains its named
notification/source-only/token checks. Its false/true exits must not change
which caller loop, operation or entry returns.

## Model identity and failure order

Preserve exactly:

```text
seed: 0xe029cafe
slots: 4
steps: 4096
trace ring: 32
LCG: state * 1664525 + 1013904223
operations: create,reserve,install,activate,authorize,close,release,stale
scripted prefix: 17 exact original operation/value pairs
coverage: all 19 original fields must be nonzero
```

Every iteration draws `random` and then `value`, even when a scripted pair
overrides them. Authorization draws `target_random` and the initial operation
selection before source lookup and before reply/reply-receive/no-slot exits.
Preserve those two additional draws even when that selected operation later
does nothing.

Successful or no-op operation completion records the final slot/value, then
compares complete state. A transition failure reports before recording that
attempt; a projection failure reports after its record. A helper's early
success replaces only the original switch-case `break`, never the outer
loop's trace/comparison or whole-model completion.

Selection retains `(random >> 16) % 4`, wraparound scanning, the no-slot
sentinel, generation advance, exact profile expectations, and independent
target-mask math. Teardown closes/retires each live model slot in order, then
proves zero processes/threads and one hart before coverage succeeds.
No extra draw, transition, trace hash, success log or cached comparison is added.

## Build consumers and validation inheritance

| Source set | Exact existing consumers |
| --- | --- |
| Core | `endpoint_test`, `ipc_model_test`, `bootstrap_control_test`, `pm_test`, `micros`, once each. |
| Native | `endpoint_test`, once. |
| Model | `endpoint_test` and `ipc_model_test`, once each. |
| Target | `micros` under its existing `MICROS_BUILD_ENDPOINT_TEST` source guard, once. |

Keep all existing C17, warnings, sanitizers, include paths, labels, targets,
profiles, linkers, budgets, presets, drivers and workflow names unchanged.
No unrelated source-set membership changes. The retained bootstrap gate's
literal endpoint operand check becomes an exact complete-core-set check for
the same consumer, not a weaker global filename search.

Every alias inherits these four independent original dimensions:

| Original | Full-tier fallback | QEMU metadata | Separate model | Image metadata |
| --- | --- | --- | --- | --- |
| `kernel/endpoint.c` | false | The twenty gates listed below | `test-ipc-model` | empty |
| `tests/host/endpoint_test.c` | true | empty | empty | empty |
| `tests/host/endpoint_model_test.c` | false | empty | `test-ipc-model` | empty |
| `kernel/endpoint_test.c` | false | `test-qemu-endpoint` | empty | empty |

The twenty core workflow names are:

```text
test-qemu-endpoint
test-qemu-grant-syscall
test-qemu-user-runtime
test-qemu-bootstrap-launcher
test-qemu-vm-handoff
test-qemu-pm-service
test-qemu-vm-scratch-preemption
test-qemu-tty
test-qemu-ramfs
test-qemu-vfs
test-qemu-elf-spawn
test-qemu-vm-ready-early
test-qemu-vm-self-fault
test-qemu-vm-self-fault-sealed
test-qemu-bootstrap-ready-timeout
test-qemu-bootstrap-manifest-panic
test-qemu-ipc
test-qemu-ipc-syscall
test-qemu-ipc-syscall-panic
test-qemu-address-space-handoff
```

Add exactly four original-to-new-source/header alias rows and the one new
host-unit-only `tests/host/test_endpoint_source_layout.py` path.
Preserve full fallback for native aliases despite empty separate workflow
metadata. Preserve the existing planner AST, unknown-path escalation, Git
discovery, union, command construction and execution algorithms after only
those exact declaration additions are projected out.

## Complete recipe and closed-profile maintenance

The reviewed base's complete `CMakeLists.txt` SHA-256 is:

```text
cd615eaa75141a5626a024de1ad7772e254c47129ed34829a9b01a472da28dd6
```

Independently invert exactly the four ordered set declarations and their nine
consumer operands above. All other recipe bytes, guards, flags, commands and
memberships must match that complete base. Only after this proof may the one
admitted CMake literal in `tools/validation_context.py` change to the actual
new complete recipe digest.

The prior RAMFS complete inverse/hash and retained trap/syscall complete
recipe/planner oracle remain independently enforced. They may additionally
project only this exact four-set/nine-operand delta before their original
projections; do not replace their immutable expected hashes with current
output. Keep helper imports acyclic and actual expected-value declarations
independent from production metadata.

Unknown recipe bytes still reject before build-cache metadata collection.
No new reusable workflow, root, tool family, configuration admission,
normalization, closure algorithm or old-HEAD receipt equivalence is allowed.
Actual current-candidate native, smoke and TTY-image eligibility must remain
real; silent profile disablement is not success.

## Structural and runtime acceptance

The structural Red freezes all 98 original bodies and exact owners, 90
moves, ten long-function entries, source/header/set/consumer maps, storage
schemas, macro/include scope, readonly data, case/script/trace identity,
coverage metadata and complete recipes from the immutable base.
Both active migration collections become literal empty tuples before review.

Finite typed projections reconstruct each complete original body under only
known private naming, fixture transport, readonly relocation and outlined
phases. They preserve literal bytes and caller bindings, automatic lifetime,
predicate/effect/order, loop and return scope. Independent small positive and
negative fixtures reject wrong owner/profile/generation authority, weakened
canonical/queue/token checks, duplicate witnesses, early publication, lost
snapshot/clear/revoke/IRQ restoration, changed draws/trace order, and
callee-local success that skips caller work. This is not a general C
interpreter or an alternative to review.

At minimum, keep byte-exact public and legacy prevalidated headers, the IPC
and grant runtimes/internal interfaces, kernel objects/public object header,
grant registry/public grant header, `tests/host/ipc_model_main.c`,
`CMakePresets.json`, and `tools/run_qemu_smoke.py`.

Narrow green source moves use the existing `endpoint-profile` and `ipc-model`
executions at the affected layer; target integration uses actual
`test-qemu-endpoint`. Its exact pass marker remains:

```text
MICROS_ENDPOINT_TEST_PASS generation=validated profiles=immutable visibility=staged authorization=separate grants=generation-safe
```

The existing timeout is eight seconds. Preserve every cold-runtime,
failed-initialization, output-sentinel, grant prepare/IPC-close/commit,
generation reuse and complete object/endpoint/grant/hart baseline proof.

Before implementation review, stage the complete candidate, prove clean
tracked/index state and no untracked task files, record HEAD/tree and exact
commands/results, and run one authoritative current PR plan. Shared CMake
ownership currently requires all 47 fresh commands: complete native tests,
38 QEMU workflows, four images, docs and three diff checks.
Do not separately duplicate workflows already selected by that plan.

Additional bounded actual producer/reuse acceptance must prove native,
smoke and TTY-image receipts refer to the same current candidate and original
producer, while unsupported workflows remain fresh. This is additional
profile-coupling evidence, never a replacement for the full origin/main
author plan. Squash invalidates old-HEAD receipts even for an identical tree.

## Implementation sequence and design barrier

### Current implementation progress

Design #135 is merged as `b99a035`. The first implementation slice exposes
the fifteen original codec definitions and fourteen necessary cross-owner
prototypes, with exact complete-body reconstruction. All 98 original bodies
and fifteen stable neighboring artifacts are frozen from the immutable base.
The definition inspector now rejects earlier call sites and prototypes,
with an independent regression; its one initial endpoint fingerprint was
corrected from the unchanged Git baseline before any C movement.
The existing literal-preserving token utility moved unchanged to the shared
lexical helper owner, with its complete original AST enforced.

All 25 final source paths, four private headers, four source sets and nine
consumer operands are wired once. Not-yet-extracted owners are explicitly
declarations-only compilation units; they do not duplicate fixture storage or
claim completed ownership. This keeps the entire authorized recipe inverse
provable while subsequent green commits move real definitions.
The resulting complete CMake digest is
`50cac89bbc8dfc0246b97d36be3585ff53f7e86c3dbc772539a6f8352e05db7d`;
only its admitted literal is maintained after restoring the whole base
`cd615eaa75141a5626a024de1ad7772e254c47129ed34829a9b01a472da28dd6`.
Existing native include search roots are unchanged; private includes resolve
relative to their actual source owners.

All 369 source-layout checks, native endpoint/profile and combined IPC-model
executions, and the actual endpoint/grant QEMU scenario pass for this slice.
The following slice also exposes identity/ownership queries, registry
initialization/validation, and checked profile-query owners: 31 original
definitions now have their required owners, with 22 necessary private
prototypes and unchanged complete bodies and recipe.
Portable source ownership is now complete: all 67 original definitions occupy
their ten required owners, with exactly 31 necessary private declarations.
Completion/queue/object validation and publication/close remain acyclic,
while the five supported checked resolution/authorization entry points form
the small facade. The legacy six-function prevalidated interface and every
original body remain unchanged.
The deterministic native family now also has its seven required owners and
one exact five-field static fixture. Its nine stateful case roots borrow the
same fixture once; setup receives that pointer, pure helpers and encoding
do not gain ambient context, and original resets/snapshots/macros/25-case
runner remain exact under typed reconstruction.
The independent model now also exposes its five owners and separate
five-field production/trace fixture. Expected slots, coverage, random state
and loop work retain their original automatic lifetime. Only the entry
borrows the fixture; setup/trace mutation and readonly failure/projection
helpers receive exact typed pointers. Raw operation-name bytes, model types,
all fifteen original model bodies, draw/trace/comparison order and resets
remain exact. The active 90-owner migration is now a literal empty tuple.
The public validators now have 26/34-line outlines and four 66/65/108/80-line
profile, record, process and thread passes. Exact whole-body projection
preserves original counter bounds, header/error precedence, every invariant,
and one caller-owned queued-thread witness shared across process and thread
closure; helpers do not introduce early success or another witness.
The shorter initializer now exposes candidate preparation, closed-target
proof and non-failing profile publication. The original automatic array and
mask remain caller-owned; exact projection preserves dirty/initialized/
capacity/duplicate/target precedence and copy/count/magic order. Publication
uses the frozen full registry-array extent rather than the decayed parameter
size. Independent mask-binding, extent and early-publication mutants reject
drift. Eight original long-function migrations remain pending, including the
target entry.
Complete current final-author/profile-reuse
acceptance and independent implementation review remain required; this is
not a completed endpoint correction.

### Reviewed sequence

1. Structural Red and immutable source/body/storage/execution/metadata facts.
2. Portable representation/identity, registry/privilege, complete validation,
   publication/close and facade owners; narrow native evidence.
3. Deterministic fixture/cases and exact readonly runner; typed case phases.
4. Independent model fixture/reference/projection/operation outlines; exact
   original draw, failure, trace, comparison, coverage and cleanup schedule.
5. Target fixture/scenario/entry outlines; real authoritative runtimes,
   original IRQ scope and QEMU proof.
6. Empty migrations, complete recipe proof/literal maintenance, one current
   full author plan and bounded real reuse acceptance.
7. Independent read-only implementation review, justified corrections,
   exact reviewed-head squash/tree verification and obsolete-branch cleanup.

This design authors documentation only. It creates no executable source,
test, model, harness, build scaffold, dependency or prototype.
Kernel-object, grant data-plane, QEMU-runner and directory-layout corrections,
and later production init/shell/commands, remain separate documented work.

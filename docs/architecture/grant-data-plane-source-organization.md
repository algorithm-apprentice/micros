# Grant Data-Plane Source Organization

## Status and scope

This is the exact Accepted design for
[ADR-0069](../adr/0069-grant-data-plane-source-decomposition.md).
Independent complete design review is clean. The paired implementation
starts from separately merged design #139,
`a71253e88b367d9cc4fe494395b76c7f16d39540`. Current progress below separates
actual definition ownership from once-only future source wiring.

```text
reviewed micros base: 6269d9247de6beafed2448aa0ffd6904de5e8833
fixed MINIX reference: 4db99f4012570a577414fe2a43697b2f239b699e
design task: grant-data-plane-decomposition-design
implementation task: grant-data-plane-decomposition-implementation
```

Kernel-object #138 is independently reviewed and merged with identical
validated/reviewed squash tree and exact branch cleanup. This is the next
single maintenance outcome, not another runtime-DAG node.

| Reviewed source | Lines | Definitions | Above 120 |
| --- | ---: | ---: | ---: |
| `kernel/grant_copy.c` | 453 | 6 | 1 |
| `kernel/grant_syscall.c` | 190 | 2 | 1 |
| `kernel/grant_test.c` | 1,186 | 10 | 1 |
| `kernel/grant_syscall_test.c` | 3,092 | 42 | 4 |
| `tests/host/grant_test.c` | 2,200 | 24 | 5 |
| `tests/host/grant_copy_test.c` | 497 | 8 | 1 |
| `tests/host/grant_syscall_core_test.c` | 400 | 3 | 1 |
| **Total** | **8,018** | **95** | **14** |

Names below define final owners. Source wiring alone is not a claim that
their actual tasks or semantic outlines are complete.

## Preserved authority

ADRs 0005/0038/0039/0040/0041/0047/0049/0050 define behavior.

- One exact grantor generation creates or revokes its own opaque direct
  grant; one exact grantee may copy or validate the remote range.
- Tokens, revoke-time generations, quarantine, endpoint cancellation and
  kernel-managed registry ownership remain unchanged.
- At most one page is copied, with at most two chunks per participant.
  Authority and phase checks precede zero-length success.
- Validate complete remote and local mappings before any copy. Prepare
  both plans before classifying errors: invariant, then phase, then fault.
  Do not short-circuit an ordinary first-side fault around second-side
  structural validation.
- The TTY UART device leaf and VM scratch leaf never become ordinary grant
  authority or retained physical chunks. Keep the range planner's existing
  device-span validation, advancement and deferred fault.
- Compare every source/destination physical interval before the bounded
  non-failing commit; never copy incrementally while discovering mappings.
- Operation 15 validates no local range, copies no bytes and grants no
  lease. It retains its distinct exact remote-only sequence.
- Captured syscall runtime validation precedes decode. Preserve operations
  7-10/15, full-width scalars, shape errors, fatal invariant reasons,
  create preparation/device exclusion/publication, and success-only `a0`.
- Preserve every existing IRQ, scheduler, register, root and resource
  lifetime. No new retry, rollback, cleanup, normalization or validation is
  introduced by outlining.

The [fixed source study](../research/minix-grant-data-plane-source-organization.md)
classifies existing differences; it does not supersede those ADRs.

## Exact source ownership

Exactly 22 C owners are in scope. The original seven remain; fifteen are
added. The cohesive registry `kernel/grant.c` is not an eighth signal or a
new decomposition target.

| Owner | Responsibility |
| --- | --- |
| `kernel/grant_copy.c` | Public from/to/validate facade, copy admission, IRQ lifetime, authority/phase/plan classification and retained-plan commit orchestration. |
| `kernel/grant_copy_range.c` | Address-error mapping and one complete page-bounded range, including device-leaf exclusion. |
| `kernel/grant_syscall.c` | Captured runtime admission, decode, operation dispatch/create publication and stable result/panic mapping. |
| `kernel/grant_test.c` | One real mechanism fixture and ordered copy/denial/reuse/cleanup scenarios under root-owned IRQ/report scope. |
| `kernel/grant_syscall_test.c` | Original entry and finish, root profiles, actual payload/assembly custody and setup/report orchestration. |
| `kernel/grant_syscall_test_fixture.c` | Sole syscall fixture, byte/address/context/snapshot/observer primitives and raw arming/control leaves. |
| `kernel/grant_syscall_test_commands.c` | Original command dispatcher, exact command-binding rows and fourteen non-copy arming templates. |
| `kernel/grant_syscall_test_copy_commands.c` | Twenty-four original copy, unknown-operation and unavailable-control arming templates. |
| `kernel/grant_syscall_test_transitions.c` | Exact post-return command/actor transitions and original switch/reuse/finish scheduling. |
| `kernel/grant_syscall_test_effects.c` | Original notification/token/byte/revoke/reuse effect checks. |
| `kernel/grant_syscall_test_hooks.c` | Original before-ecall/after-dispatch/after-return and supervisor-control boundaries. |
| `kernel/grant_syscall_test_lifecycle.c` | Process preparation, thread/root/endpoint release, grantor reuse and baseline queries. |
| `tests/host/grant_test.c` | Exact static eight-case deterministic runner. |
| `tests/host/grant_test_fixture.c` | Sole registry/model fixture, setup/recreate and original unchanged-create helper. |
| `tests/host/grant_lifecycle_test.c` | Token, create/inspect/revoke/capacity/quarantine/cancel and failed-IPC-close cases. |
| `tests/host/grant_validation_test.c` | Error precedence/output and independent corruption cases. |
| `tests/host/grant_model_test.c` | Boundary case, actual seeded runner, draw/dispatch/trace/comparison/coverage order and public model entry. |
| `tests/host/grant_model_reference.c` | Independent comparisons, plan-reference checks, LCG, operation names and exact trace/error reporting. |
| `tests/host/grant_model_operations.c` | Original create/revoke/inspect/cancel/close-reuse/malformed/foreign-plan/invalid-create cases. |
| `tests/host/grant_model_copy.c` | Original five-label authorization group and its selection, expectation, failure-output and byte-commit tasks. |
| `tests/host/grant_copy_test.c` | Separate original copy fixture and authority/bounds/physical-plan cases. |
| `tests/host/grant_syscall_core_test.c` | Original ABI numbering/mapping/decode cases and static mapping data. |

Three new private headers:

```text
kernel/grant_copy_internal.h
kernel/grant_syscall_test_internal.h
tests/host/grant_test_internal.h
```

All new ordinary domain phases are static unless their named responsibility
crosses the required owners. No public header gains extraction machinery.
Native copy and target mechanism work remain local to their existing files;
they need no additional private header or source set.

## All original definitions and moves

The following is a complete partition of the 95 originals. Preserve original
public/test entry names; private names receive their family prefix.

| Owner | Original definitions |
| --- | --- |
| Copy facade | `copy_grant`, `micros_grant_validate_range`, `micros_grant_copy_from`, `micros_grant_copy_to`. |
| Range | `map_address_error`, `plan_range`. |
| Syscall adapter | `panic_grant_syscall`, `micros_grant_handle_captured_user_ecall`. |
| Mechanism target | `copy_storage`, `storage_matches`, `snapshot_failure_state`, `failure_state_matches`, `fill_bytes`, `physical_for`, `write_pattern`, `pattern_matches`, `range_is_value`, `micros_grant_runtime_run_self_test`. |
| Syscall root | `micros_grant_syscall_runtime_run_self_test`, `micros_grant_syscall_test_finish`. |
| Syscall fixture | `copy_bytes`, `bytes_equal`, `fill_bytes`, `read_satp`, `user_address_of`, `expected_satp`, `abi_result`, `physical_for`, `write_pattern`, `pattern_matches`, `range_is_value`, `fill_context_pattern`, `current_actor`, `test_mismatch`, `normalize_objects`, `capture_page_tables`, `snapshot_user_bytes`, `user_bytes_match`, `snapshot_failure_state`, `failure_state_matches`, `scheduler_state_valid`, `arm_raw`, `arm_control`. |
| Syscall command | `arm_command`. |
| Syscall transitions | `schedule_next`, `schedule_reuse`, `schedule_finish`, `transition_after_return`. |
| Syscall effects | `validate_command_effect`. |
| Syscall hooks | `micros_grant_syscall_test_before_ecall`, `micros_grant_syscall_test_after_dispatch`, `micros_grant_syscall_test_after_return`, `micros_grant_syscall_test_handle_control_trap`. |
| Syscall lifecycle | `release_address_space`, `release_thread`, `close_endpoint`, `reuse_grantor`, `prepare_process`, `endpoint_registry_is_empty`, `scheduler_is_at_idle_baseline`. |
| Native root | `micros_grant_test_run`. |
| Native fixture | `setup_fixture`, `recreate_process`, `expect_create_failure_unchanged`. |
| Native lifecycle | `context_pattern`, `test_token_boundaries`, `test_create_inspect_revoke_and_reuse`, `test_create_failures_and_capacity`, `test_terminal_generation_quarantines`, `test_endpoint_cancellation_and_reuse`, `test_failed_ipc_close_preserves_cancel_plan`. |
| Native validation | `test_error_precedence_and_output_preservation`, `test_validator_rejects_corruption`. |
| Native model root | `test_grant_model_boundaries`, `test_grant_model`, `micros_grant_model_test_run`. |
| Native reference | `process_handles_equal`, `records_equal`, `registries_equal`, `model_next`, `model_operation_name`, `record_model_trace`, `model_fail`, `cancel_plan_matches_reference`. |
| Native copy | `process_handles_equal`, `authority_equal`, `setup_fixture`, `create_grant`, `expect_authority_failure_unchanged`, `test_copy_authority_and_bounds`, `test_copy_plan_and_commit`, `micros_grant_copy_test_run`. |
| Native ABI | `request_matches`, `expect_decode_failure`, `micros_grant_syscall_core_test_run`. |

Exactly 65 originals move: two range, forty syscall-target and twenty-three
native registry/model definitions. The other thirty remain in their owners.
The two new operation owners and copy-command owner initially have no
original definitions; they must contain their actual outlined tasks before
review, not declarations-only placeholders.

## Necessary visibility and acyclic direction

Exactly 48 formerly static originals need cross-owner declarations:

- Production: only `plan_range`, privately named `grant_copy_plan_range`;
  `map_address_error` remains static in range.
- Native: the three comparisons; setup, recreate and unchanged-create;
  eight deterministic cases; LCG, trace recording, model failure and
  cancellation-plan reference checking. Eighteen total. Context pattern and
  operation-name lookup stay static in their owner.
- Syscall target: copy/compare/fill; CSR/address/result/physical/pattern/
  context/actor/mismatch queries; failure snapshot/compare and scheduler
  observation; raw arm, command arm, control arm and transition; effect
  checking; all seven lifecycle helpers. Twenty-nine total. Normalization,
  table capture, user snapshot/compare, and the three scheduling helpers
  stay static.

Preserve all complete original result/parameter types. A fixture pointer is
the only additional original-helper state parameter; pure helpers gain none.
Native registry/model private names use `grant_test_`; syscall-target
private names use `grant_syscall_test_`; new production copy phases use
`grant_copy_`. Public/test entries keep their exact original names. Local
native-copy and mechanism helpers need no new external namespace.
New necessary cross-owner interfaces are the two non-resetting fixture
getters, eight lifecycle-model case helpers, one authorization-group helper,
and twenty-four copy-command arming helpers. Other new phases stay static.

Native direction:

```text
reference -> no operation/root/fixture function
fixture -> reference
lifecycle/validation -> fixture, reference
model operations/copy -> fixture, reference
model root -> fixture, reference, operation/copy owners
deterministic root -> lifecycle, validation
```

Syscall-target direction:

```text
fixture/raw arm/control -> no command/transition/hook/lifecycle/root
copy-command arming -> fixture
command dispatcher/non-copy arming -> fixture, copy-command arming
transitions -> fixture, command dispatcher
effects -> fixture
lifecycle -> fixture
hooks -> fixture, commands, transitions, effects, lifecycle
root -> fixture, commands, lifecycle
```

Raw arming must not live with command dispatch: otherwise cross-owner
copy-command callbacks create a cycle. Fixture code receives explicit
state; its original runtime reads stay at their original sites, without
additional ambient borrowing.

## Storage, constants and scopes

Maintain four separate original evidence families; never merge their state.

| Family | Existing mutable storage |
| --- | --- |
| Native registry/model | Nine objects: three registries, three process/thread/endpoint arrays, hart, trace array and trace count. One original fixture owns all nine. |
| Native copy | Six objects: three registries and three process/thread/endpoint arrays. Remains a separate local fixture. |
| Mechanism target | Physical-address array `[3][3]` plus grant/endpoint/object failure snapshots. All four remain in this family. |
| Syscall target | Thirty-eight static mutable objects, including exact actor/script/live/physical arrays, runtime pointers, grant tokens, control/dispatch/baseline fields and original snapshot/observed buffers. One fixture owns them. |

The three externally named
`micros_grant_syscall_test_saved_sp/gp/tp` objects stay separately defined in
the syscall root with their exact names/types. They are assembly custody,
not fields that can be renamed or moved into the fixture.

The entry's `fence.i`, inline `sp/gp/tp` capture and
`micros_scheduler_test_enter_without_timer` remain in the actual root
function, not merely in a reconstruction. Capturing a helper's stack
pointer would break the supervisor resume boundary. Keep setup-failure IRQ
restoration only on its original failure path; successful user entry does
not invent a restore. The mechanism's returning root still restores IRQ
state on every exit. Helpers add no IRQ scope.

The syscall fixture retains the sole explicit static initializer
`dispatch_actor = GRANT_SYSCALL_PROCESS_COUNT`; other static fields retain
implicit zero storage. A grouped static initializer may express exactly
that original field, never another reset. Native setup does not reset hart,
model trace or counters at new sites. Getters return the original family
without resetting it.

Seven syscall address/control constants, the command/control enums, script
type/flags, status masks, payload symbols and all array extents are frozen.
Cross-owner constants have one private readonly owner; no mutable cached
derivative is added.

Optional automatic work contains only each original root's mutable
declarations, types, array extents and actual explicit initializers. It is
never blanket-zeroed, copied or retained. Keep saved IRQ status, result/
success/failure reporting and root-owned static data in their original
root lifetime. Preserve block-local cancellation plans, case-local model
handles, context/permission temporaries and nested loop locals.

Native test data matters:

- target profiles retain their original block-static `const` declarations;
  every profile extent is computed from the actual root array, not a decayed
  parameter; payload-size calculation and validation remain original;
- deterministic eight-case table remains block-static `const`, unlike the
  object family's automatic table;
- ABI nine-row error/result mapping remains block-static `const` in its
  original caller, with exact entries and `sizeof` extent bound to the
  actual array;
- an optional named local mapping-record type may expose only its original
  two fields; it is not a new public protocol;
- assertion formats, sentinels, complete snapshots, macro restore/mutation/
  validation order and first failure remain exact.

## Required semantic dispositions

All resulting C functions are at most 120 physical lines. No original long
function receives an algorithmic exception.

| Original function | Lines | Required ordered outline |
| --- | ---: | --- |
| `copy_grant` | 172 | Early shape/local-range rejection; root IRQ save; exact authority; ledger/phase; zero length; device authority; both range plans and precedence; direction/physical validation; original commit; common IRQ restore. |
| `micros_grant_handle_captured_user_ecall` | 162 | Original argument/runtime invariants; decode/shape result; create prepare/device/commit or selected original operation; exact success/error/fatal publication. |
| `micros_grant_runtime_run_self_test` | 971 | Runtime/baseline/profile capture; three processes/pages; original grants; cross/local directional successes; cross-page denial/canaries; authority/range/permission/zero denials; reversible phase probe; stale token/endpoint; cancel/close/reuse; success-path cleanup/baseline; one root failure-stage report and IRQ restore. |
| `arm_command` | 851 | Original stale-endpoint scalar; exact command dispatch; original non-copy or copy arming template; original success/`NONE`/unknown failure. |
| `transition_after_return` | 324 | Exact command-to-actor/next-command schedule; original reuse control; original finish control; `NONE`/unknown false. |
| `validate_command_effect` | 149 | Notification state; five token publications/distinctness; four byte/canary cases; revoke result; reused endpoint; original default true. |
| `micros_grant_syscall_runtime_run_self_test` | 144 | Root profiles/IRQ; runtime/baseline capture; actual payload extent; three prepared actors; first real command; original validators; root `fence.i`, saved assembly state and real user entry; original fatal failure path. |
| `test_create_inspect_revoke_and_reuse` | 135 | Setup/null-output preservation; create/record proof; foreign inspect/revoke preservation; revoke/stale/reuse token proof. |
| `test_endpoint_cancellation_and_reuse` | 170 | Three grants; source-only seal/plan; corrupted and foreign plan denials; close/non-failing commit/unrelated survivor/one-use denial; process generation reuse/stale authority. |
| `test_error_precedence_and_output_preservation` | 149 | Setup; range before corruption; record corruption/sentinel; stale/malformed token boundaries; cancellation shape/foreign plan; closed endpoint result/output. |
| `test_grant_model_boundaries` | 132 | Original fill/compare loop; exact capacity snapshot/output denial block; independent reset/terminal slot setup; allocate/revoke/quarantine/reference closure. |
| `test_grant_model` | 778 | Original run setup; one draw/selection/forced prefix; exact operation; coverage/trace; complete registry/participant comparison; final coverage and original report. |
| `test_copy_authority_and_bounds` | 191 | Separate fixture/three directional grants; successful authority/end-zero output; direction/participant/bounds/shape denials; revoke/stale/overflow/zero precedence. |
| `micros_grant_syscall_core_test_run` | 338 | Root static mapping data; ABI numbering; recoverable/fatal mapping and sentinels; valid full-width shapes; operation-15/revoke/copy malformed streams; final null/unknown cases. |

Five shorter mixed tasks also require outlines: `plan_range` (94), remote-only
`micros_grant_validate_range` (98), `test_copy_plan_and_commit` (101),
`micros_grant_syscall_test_handle_control_trap` (114) and
`micros_grant_syscall_test_finish` (99). Their candidate/device/translation/
publication, range-only, chunk/commit/negative, switch/reuse/supervisor and
cleanup/baseline/report boundaries remain explicit in the original order.
Other bounded primitives and atomic observations retain complete bodies.

## Script and model identity

The syscall command enum has 47 identities, including `NONE`, and exactly
46 non-`NONE` arming/transition labels. Arming has 38 actual `arm_raw`
templates: fourteen non-copy templates and twenty-four copy/control-error
templates. Preserve every grouping, expression, actor, offset, address,
length, upper-bit/unused-register value, expected result, token flag,
preservation flag and `NORMAL`/`CAPTURED` selector.

A readonly command binding table may map the exact original labels to those
actual templates. Non-copy bodies are static beside dispatch; twenty-four
cross-owner copy bodies have necessary private declarations. A transient
arming input may contain only original actor/context/command inputs and the
original stale-endpoint scalar, computed before dispatch at its original
site. No pointer to that transient input survives a call.

A readonly transition table may represent the exact original label order
and current/fixed actor, next command and existing control action. It must
not infer next steps from enum adjacency. Keep the original switch to
grantee/wrong/grantor, same-actor steps, reuse and finish. Unknown/NONE
behavior and effect-check default true remain distinct.

Verify actual enum/table/helper data before finite expansion to the original
complete bodies. Reintroducing an unchecked old command body, ignoring
unknown fields, recomputing expected arguments from production metadata or
merely hashing names is not an oracle.

The native model retains:

```text
seed 0x38a110c5; LCG multiplier1664525/increment1013904223
steps4096; operations13; forced first13 operations despite one draw each
trace ring32; exact trace fields/formats and chronology
three actor generations/endpoints; four original64-byte data arrays
```

Only the original eleven mutable run objects and ten iteration objects may
be grouped. Keep the seed constant in root, the original expected-registry
copy and byte/coverage/trace initialization sites, and otherwise uninitialized
locals. The five-label authorization block receives its complete original
case-local work; expected denials still complete the attempt and reach
coverage, trace and full comparison. Case failure stops before those steps.

The authorization group exposes selected-reference lookup, original
participant/bounds/direction expectation, actual authority/output checks,
and success-only physical-plan/byte work. Preserve all early breaks,
sentinels, contexts, array `sizeof`, nested scopes and complete byte checks.
Do not derive expectations through production private helpers. No new
script, trace field, coverage counter, teardown or empty-registry proof is
added.

## Build and inherited validation

Exactly three ordered sets:

```text
MICROS_GRANT_COPY_TARGET_SOURCES:
  kernel/grant_copy.c
  kernel/grant_copy_range.c

MICROS_GRANT_HOST_TEST_SOURCES:
  tests/host/grant_test.c
  tests/host/grant_test_fixture.c
  tests/host/grant_lifecycle_test.c
  tests/host/grant_validation_test.c
  tests/host/grant_model_test.c
  tests/host/grant_model_reference.c
  tests/host/grant_model_operations.c
  tests/host/grant_model_copy.c

MICROS_GRANT_SYSCALL_QEMU_TEST_SOURCES:
  kernel/grant_syscall_test.c
  kernel/grant_syscall_test_fixture.c
  kernel/grant_syscall_test_commands.c
  kernel/grant_syscall_test_copy_commands.c
  kernel/grant_syscall_test_transitions.c
  kernel/grant_syscall_test_effects.c
  kernel/grant_syscall_test_hooks.c
  kernel/grant_syscall_test_lifecycle.c
```

Four operands: copy set once in `micros`, native set once each in
`endpoint_test`/`ipc_model_test`, and syscall-target set once under the
existing `MICROS_BUILD_GRANT_SYSCALL_TEST` guard. Keep the original assembly
operand and all other grant/ABI/copy/native/mechanism operands and guards.
No new executable, CTest, model invocation, workflow, image or slow label.

Three aliases independently inherit:

| Baseline path/future family | Full tier | QEMU | Separate model | Images |
| --- | --- | --- | --- | --- |
| `kernel/grant_copy.c`, range owner/private header | false | Original eleven gates below | `test-ipc-model` | empty |
| `kernel/grant_syscall_test.c`, syscall target owners/private header | false | Grant-syscall and address-space-handoff | empty | empty |
| `tests/host/grant_test.c`, native family/private header | false | empty | `test-ipc-model` | empty |
| New structural gate | false | empty | empty | empty |

The eleven copy gates are grant, grant-syscall, user-runtime, IPC ecall-core,
IPC syscall, IPC syscall panic, frame-ownership, user-address-space,
address-space-handoff, user-execution and scheduler, with their existing
`test-qemu-` names and order.

Unmoved syscall production retains its original seven gates and model.
Those gates are grant-syscall, user-runtime, IPC ecall-core, IPC syscall,
IPC syscall panic, user-address-space and address-space-handoff, with their
existing `test-qemu-` names.
Unmoved native copy retains model selection. Unmoved native ABI retains
grant-syscall/handoff plus model. Do not collapse those independent rows
into a shared least-common subset.

The complete reviewed recipe digest is
`44d8a8615e5465ad199ed916b30e72af05643ef00ae5ac9a7df00a0dc4cad2c1`.
Invert only the actual three new sets and four operands before maintaining
the one admitted new digest. Earlier object/endpoint/scheduler/RAMFS recipe
and planner expected values stay enforced under only this exact delta.
Use the unchanged shared C-source inspector; the new grant gate imports
leaves, never the object/scheduler gate that consumes its recipe inverse.

## Evidence and completion barrier

Design commits documentation only and runs docs/structure and three diff
checks. Implementation starts with structural Red and immutable Git-base
inventories, not rewritten-output expectations.

Freeze all 95 original bodies/signatures, 65 owner moves, fourteen long and
five shorter dispositions, four fixture families, actual macros/constants/
tables/script/model data, and every unchanged neighbor. In particular,
these 25 neighbors remain byte-exact:

```text
kernel/grant.c
kernel/grant_internal.h
kernel/grant_copy_core.c
kernel/grant_syscall_core.c
kernel/grant_abi.c
kernel/grant_runtime.c
kernel/grant_runtime_internal.h
kernel/grant_syscall.h
kernel/grant_test.h
kernel/grant_syscall_test.h
include/micros/grant.h
include/micros/grant_copy.h
include/micros/grant_runtime.h
include/micros/grant_syscall_core.h
include/micros/syscall_abi.h
include/micros/ipc_abi.h
kernel/syscall.c
kernel/ipc_syscall.c
arch/riscv64/grant_syscall_test.S
kernel/boot_component_tests.c
kernel/trap_test_hooks.c
tests/host/endpoint_test.c
tests/host/ipc_model_main.c
CMakePresets.json
tools/run_qemu_smoke.py
```

All entries are required, not an allowlist from which implementation may
select a subset.
The shared source inspector remains unchanged as well.
Actual typed/scoped data/statement expansion needs independent positive and
negative fixtures for scalar/array extent, initializers, pointer binding,
return scope, snapshots, normalization, command values, transition order,
model draw/trace/coverage and IRQ/cleanup boundaries.

Narrow native work uses the existing endpoint-profile or IPC model entry;
target mechanism uses real `test-qemu-grant`; target syscall uses real
`test-qemu-grant-syscall`. Production range/syscall work also retains handed-off,
user-runtime and other selected gates. Preserve original eight-second grant
and fifteen-second grant-syscall budgets and exact markers.

Before review, all 22 owners are real and both original owner/task migrations
are literal empty tuples; every current C definition is bounded. Stage all
task files, prove clean tracked/index state and no untracked output, record
HEAD/tree and the complete current origin/main author plan. Shared recipe
ownership currently selects 47 fresh commands. Verify actual eligibility of
all three supported profiles before recording; canonical configure/build
steps may close normal generated inputs but collectors never build, install
or disable admission. Additional bounded actual native/smoke/TTY-image
reuse must match the same candidate and original full producer/attempt/log.
Old-HEAD receipts remain historical after squash.

Independent review reads the complete current change and exact evidence
without routine gate reruns. Only a minimal targeted concrete reproduction
is permitted. Fix justified findings, re-review to clean, squash only that
head, verify identical tree, fast-forward main and delete its exact obsolete
local/remote branch. No later TTY/runner/layout/init/shell work is folded in.

## Current implementation progress

The initial structural Red found `plan_range` still in its facade rather
than the required range owner. Before any C rewrite, all 95 complete bodies,
seven signature families, original declaration/data inventories, fourteen
long functions, 25 stable neighbors, complete recipe and shared/tool AST
facts were frozen from immutable Git `6269d924`, not rewritten output.

The first green slice moves only the two original range definitions.
`grant_copy_plan_range` is the sole necessary private production export;
the address-error mapper remains file-local in that leaf. All 95 complete
bodies/types and 25 stable artifacts remain exact. Sixty-three original
owner moves and all nineteen semantic dispositions remain pending.

All 22 future C paths, three private headers/sets and four consumer operands
are wired once. Fourteen future native/syscall units are explicitly
declarations-only migration owners, not duplicate state or completed tasks.
Three family aliases and one host-only structural input retain all four
classification dimensions; planner/context algorithms remain unchanged.
The actual complete grant recipe inverse restores `44d8a861...` before
maintaining only the admitted current digest
`4e370ba5578b0b114a51d2d5ae738320ce7503a3d51bd7c2e6fb2623284981b3`.
Earlier source/recipe/planner expected values remain unchanged under only
that finite delta. This is not final author/reuse or review completion.

The next semantic slice outlines copy execution into a 115-line root,
19-line local-range admission and 54-line complete participant-plan phase.
Its caller-owned work contains exactly the original eleven objects, with
only the two original plan initializers. Saved IRQ status stays separate
in root; authority/phase/zero/device order, both plans before classification,
direction/physical validation/commit and the common restoration remain
exact. Independent typed-work/binding/return/initializer/IRQ mutants and
full original-body reconstruction protect that outline. Sixty-three owner
moves and eighteen semantic dispositions remain pending.

Range planning now has a 78-line page-walk/publication root and a 40-line
device-span task. Its four original initialized range objects and five
original loop locals remain in their exact caller lifetime. Typed borrowed
lvalues preserve device validation, phase/invariant precedence, bounded
advancement, deferred fault and original `continue`; the candidate still
publishes only after complete ordinary planning and the final device-fault
check. Independent pointer/switch/success/fault mutants and complete-body
reconstruction retain the original range behavior without another IRQ
scope or mutable state. Seventeen semantic dispositions remain pending.

Remote-only validation now has a 72-line root and a 34-line device/remote-plan
phase. Its six original objects and sole plan initializer retain caller
lifetime; early shape, authority, phase and zero-length order still precede
device lookup and remote translation. A checked void-phase expansion
preserves the caller-owned result and original common IRQ exit without local
range, physical overlap or byte-copy work. Shared typed automatic-work
inspection retains the copy execution contract unchanged. Sixteen semantic
dispositions and sixty-three original owner moves remain pending.

The captured syscall adapter now has a 54-line runtime/decode/result root,
63-line operation dispatcher and 55-line create preparation/device/publication
phase. Its ten original objects retain their exact types, declaration order
and sole `MICROS_GRANT_NONE` initializer. Checked void returns expand only
the original create-case breaks, so preparation failure still precedes device
lookup and device denial still precedes publication. Complete original-body
reconstruction and independent binding, initializer, case-exit and ordering
fixtures preserve the public entry, invariant panics and success-only result.
No IRQ scope or current-process lookup is added. Fifteen semantic dispositions
and sixty-three original owner moves remain pending.

All twenty-three native registry/model definitions now have their required
owners. One fixture owns exactly the original nine static fields and a
non-resetting getter. Stateful case roots borrow it once; ordinary stateful
helpers receive it explicitly, while token/comparison/LCG/name helpers retain
their original pure signatures. The root's original block-static eight-case
table remains unchanged. Complete-body and signature inverses retain all
twenty-four native originals, exact array extents, macros, trace chronology
and reset sites. The two future model-operation owners still contain no
outlined tasks. Forty original syscall-target moves and fifteen semantic
dispositions remain pending.

The create/revoke, endpoint-cancellation, error-precedence and model-boundary
cases now expose their ordered domain phases with roots of 18, 19, 19 and
16 lines. Their eighteen phases retain the original typed automatic objects,
initializers, case-local capacity snapshot/sentinel block and first-failure
returns. The capacity-only phase borrows the fixture without an unused work
parameter. Actual complete-body reconstruction retains the original fourteen
long and five shorter inventories unchanged. Forty target moves and eleven
semantic dispositions remain pending.

Separate native copy evidence now has a 15-line authority/bounds root and
14-line chunk/commit root. Seven local phases keep the six-field copy fixture
separate from registry/model evidence, preserve the three actual 16-byte
arrays and their `sizeof`, initialize only the original two plans, and retain
the complete original byte, overflow, overlap and precedence checks.
Independent array/type/binding/initializer fixtures protect the shared typed
work inverse. Forty original target moves and nine semantic dispositions
remain pending.

Native ABI evidence now has a 66-line root and eight bounded numbering,
mapping, full-width and malformed-shape phases. The actual anonymous
block-static nine-row mapping array and its `sizeof`-bounded loop remain
lexically in root. The mapping leaf receives each original readonly error/
signed-result scalar and the original output work; typed binding and
independent actual-row/sentinel mutants reconstruct the complete old body.
The four original automatic fields and sole arguments initializer remain
exact. Forty target moves and eight semantic dispositions remain pending.

The seeded model now has a 60-line root, eight actual lifecycle-operation
helpers and one authorization-group owner with seven bounded phases.
Every native owner is real. Root retains the constant seed, original
coverage-array initializer and step lifetime; nine other run objects, ten
iteration objects and thirteen case-local authorization objects retain their
exact types, extents, initializers and nested scopes. Expected denials finish
the original attempt and still reach coverage, trace and full comparison;
failed checks stop before those actions. The complete original model
reconstructs from actual typed helpers and case-success exits, with independent
draw/forced-prefix/inner-loop/coverage/trace/denial/array mutants. No teardown,
blanket work initialization or new model state is added. Forty original
target moves and seven target semantic dispositions remain pending.

The real mechanism now has an 85-line root, twelve ordered domain phases
and three typed denial predicates; all 25 definitions are at most 111 lines.
Its thirty grouped automatic fields preserve original types, actual process/
endpoint arrays and the sole failure-stage initializer. Saved IRQ status,
initialized pass control, static profile, runtime admission and common report/
restoration remain in actual root. Denial predicates preserve the original
left-to-right short-circuit calls and all previously captured canaries;
phase reversal, stale authority, success-path cancellation/reuse/cleanup and
baseline checks retain their complete original order. The four original
static mechanism objects remain separate and unchanged. Forty syscall-target
owner moves and six syscall-target semantic dispositions remain pending.

All forty syscall-target originals now have their required owners, completing
all sixty-five original moves. The owner migration is a literal empty tuple.
One fixture owns exactly the original thirty-eight fields and sole dispatch-
actor initializer; seven readonly constants, complete enums/masks/script data
and payload declarations retain their frozen values and types. Six stateful
public roots borrow once, stateful ordinary helpers receive the fixture
explicitly, and pure raw-control/observer helpers keep their original inputs.
The three assembly save definitions, actual `fence.i`, stack/register capture
and scheduler entry remain in root, with typed private extern declarations
for cross-owner resume use. Actual private owner directions are acyclic;
raw arming/control are fixture leaves. All forty-two target bodies/signatures
reconstruct completely. Six semantic dispositions remain pending, and the
future copy-command file is still declarations-only until its real templates
are outlined.

Setup, finish, supervisor-control and effect roots now have 115, 80, 62 and
74 lines. Nine file-local phases separate actor preparation, endpoint closure,
resource release, switch/reuse control and four byte/canary observations.
Borrowed original actor/stage scalars and the root expected-context object
retain their exact types, lvalues, lifetimes, failure labels and enum returns.
Actual root profiles/`sizeof`, payload checks, IRQ/fatal paths, assembly
capture/entry and supervisor stack/status restoration remain in root.
Notification, five token publications, revoke/reuse checks and effect-default
`true` remain exact. Complete-body reconstruction and independent borrowed-
lvalue/context/exit fixtures preserve these four outlines. Only command
arming and post-return transitions remain as semantic migrations.

Post-return dispatch now has a 37-line root and an actual readonly table of
all forty-six original ordered rows. Typed row expansion retains current-
actor versus the four fixed-actor transitions, exact next commands, reuse/
finish calls and original `NONE`/unknown `false`; no enum-adjacency rule is
introduced. The three original scheduling helpers keep complete bodies and
types. Independent order/actor/command/action/unused-field mutants protect
the table and root's actual array-bound lookup. Only command arming remains
as a semantic migration.

Command arming now has a 26-line root, fourteen static non-copy templates
and twenty-four cross-owner copy/control-error templates, all at most 45
lines. Its actual readonly forty-six-row binding table retains all original
groups, the two scoped local declaration blocks, all thirty-eight raw calls,
byte preparation, scalar expressions, flags and result/return selectors.
The original full-width stale-endpoint scalar is still computed before
dispatch; the readonly transient input contains only that scalar and the
original actor/context/command inputs and is never retained. Typed actual
row/helper expansion reconstructs the complete original arming body.

The source outcome now has all 22 real C owners and 217 definitions, with
at most 25 definitions per owner and 115 physical lines per function. All
65 moves and nineteen semantic dispositions are implemented; both migration
collections are literal empty tuples, while the immutable 95-body/signature,
fourteen-long/five-shorter and 25-stable-artifact inventories remain intact.
Actual function and readonly callback-table dependencies follow the Accepted
acyclic directions. Source/recipe/classification oracles and all original
native/model/target identities remain protected. Narrow native and real
QEMU checks are green. The complete current author plan, original-producer
reuse acceptance and independent implementation review form the separate
final acceptance barrier. The implementation pull request records those
results; source closure alone does not claim them.

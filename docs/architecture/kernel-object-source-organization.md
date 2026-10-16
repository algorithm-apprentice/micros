# Kernel Object Source Organization

## Status, base, and goal

This is the exact design contract for the three kernel-object signal files
in the [source-module audit](source-module-audit.md).
[ADR-0068](../adr/0068-kernel-object-source-decomposition.md) is Accepted
after clean independent complete and acceptance-only design reviews.
Design #137 merged as `4a7e49764533fbbef1e2d10188f837e7f092817e`;
implementation now proceeds on a fresh branch from that outcome.

```text
reviewed micros base: 4eb1c4d68de3830b888a0d239a2874bd79792244
fixed MINIX reference: 4db99f4012570a577414fe2a43697b2f239b699e
design task: kernel-object-decomposition-design
implementation task: kernel-object-decomposition-implementation
```

Endpoint #136 is complete, including full fresh author acceptance, actual
three-profile reuse, clean independent review and identical squash tree.
This is the next sequential maintenance outcome, not a new runtime-DAG node.

| Current source | Physical lines | Original definitions | Above 120 lines |
| --- | ---: | ---: | ---: |
| `kernel/kernel_objects.c` | 1,900 | 48 | 2 |
| `tests/host/kernel_objects_test.c` | 2,355 | 27 | 5 |
| `kernel/object_model_test.c` | 171 | 1 | 1 |
| **Total** | **4,426** | **76** | **8** |

Expose representation, identity, range geometry, process/thread/context/hart
lifetime, base validation, independent expected-state logic and target
scenario tasks. Names below are future ownership requirements, not claims
that implementation files already exist.

No identity, generation, quarantine, policy, capacity, supported API, layout,
scheduler, IPC, endpoint, grant, mapping, runtime, clock, deadline, model,
recovery, caching, heap, concurrency, directory or validation optimization
is introduced. Other audit pairs and production init/shell/commands remain
separate.

## Preserved authority and failure boundaries

Accepted ADRs 0011/0019/0021/0022/0023/0026/0029/0030 and the later lifecycle
decisions define behavior. In particular:

- Process/thread/hart slot state and full typed generations remain distinct.
  Process generations retain the 20-bit endpoint bound and reserved-value
  exclusions; thread/hart generations retain their existing 32-bit behavior.
- Preserve lowest-free versus exact-slot creation, terminal quarantine,
  release prerequisites, every error precedence and unchanged output.
  `process_next_generation` retains its 12-bit ABI slot range independently
  of the 64-entry registry bound.
- Address-space roots remain opaque object-layer values. Do not add Sv39
  alignment, mapping, privilege or preparation checks to these primitives.
- Preserve context byte copies, half-open stack geometry, thread/thread and
  thread/hart disjointness, current-thread uniqueness and exact selected
  stack binding. RISC-V context validity remains outside portable objects.
- Retain independent runtime flags and scheduler-assigned metadata. There
  is no revived `RUNNING` state or process-as-thread shortcut.
- Initialization still checks argument, policy, existing magic and zero
  storage in that order. Its original non-failing stores start with magic,
  then policy and the process endpoint loop; do not silently turn that into
  another publication algorithm.
- The production wrapper still owns its one registry, boot-hart handle and
  readiness flag, with explicit existing IRQ scopes. Ordinary helpers gain
  no ambient runtime access; target capture retains only its three original
  borrows within the entry-owned IRQ scope.

The [fixed MINIX trace](../research/minix-kernel-object-source-organization.md)
classifies the existing adaptations. It does not supersede those ADRs.

## Exact ordered source sets

Define exactly these three complete sets; every member appears once.

```text
MICROS_KERNEL_OBJECT_CORE_SOURCES
  kernel/kernel_objects.c
  kernel/kernel_object_state.c
  kernel/kernel_object_identity.c
  kernel/kernel_object_stack.c
  kernel/kernel_object_process.c
  kernel/kernel_object_thread.c
  kernel/kernel_object_context.c
  kernel/kernel_object_hart.c
  kernel/kernel_object_validation.c

MICROS_KERNEL_OBJECT_HOST_TEST_SOURCES
  tests/host/kernel_objects_test.c
  tests/host/kernel_objects_test_fixture.c
  tests/host/kernel_objects_initialization_test.c
  tests/host/kernel_objects_process_test.c
  tests/host/kernel_objects_thread_test.c
  tests/host/kernel_objects_hart_test.c
  tests/host/kernel_objects_identity_test.c
  tests/host/kernel_objects_validation_test.c
  tests/host/kernel_objects_model_test.c
  tests/host/kernel_objects_model_reference.c
  tests/host/kernel_objects_model_projection.c
  tests/host/kernel_objects_model_operations.c

MICROS_KERNEL_OBJECT_QEMU_TEST_SOURCES
  kernel/object_model_test.c
  kernel/object_model_test_scenarios.c
```

These are 23 C sources. Introduce exactly three private headers:

```text
kernel/kernel_object_private.h
tests/host/kernel_objects_test_internal.h
kernel/object_model_test_internal.h
```

Headers contain only necessary types, constants, prototypes and the existing
native assertion macros. No helper implementation, inline algorithm,
textual C include, new executable macro or mutable object definition.
Keep existing include search paths; local private includes resolve from
their actual owners, including native consumers that have only `include/`.

## Every original portable definition

The following original identities partition all 48 definitions. Public
spellings/types remain exact.

| Owner | Original definitions |
| --- | --- |
| `kernel_objects.c` | `micros_kernel_objects_initialize`, `micros_kernel_objects_validate`. |
| `kernel_object_state.c` | `bytes_are_zero`, `clear_bytes`, `storage_is_zero`, `thread_scheduler_metadata_is_zero`, `clear_thread_scheduler_metadata`, `micros_thread_ipc_state_is_clear`, `clear_thread_ipc_state`, `copy_user_context`, `clear_user_context`, `user_context_is_zero`, `trap_anchor_is_zero`, `timer_state_is_initial`. |
| `kernel_object_identity.c` | `process_handle_is_valid`, `thread_handle_is_valid`, `hart_handle_is_valid`, `thread_handles_equal`, `thread_handle_is_null`, `hart_handle_is_null`, `require_initialized`, `resolve_process_mutable`, `resolve_thread_mutable`, `resolve_hart_mutable`, `micros_process_next_generation`, `micros_thread_next_generation`. |
| `kernel_object_stack.c` | `stack_range_is_valid`, `ranges_overlap`, `trap_anchor_stacks_are_valid`. |
| `kernel_object_process.c` | `micros_process_resolve`, `micros_process_create`, `micros_process_create_at`, `micros_process_release`, `micros_process_attach_address_space`, `micros_process_detach_address_space`. |
| `kernel_object_thread.c` | `micros_thread_resolve`, `micros_thread_create`, `micros_thread_release`. |
| `kernel_object_context.c` | `micros_thread_attach_execution_context`, `micros_thread_capture_execution_context`, `micros_thread_inspect_execution_context`, `micros_thread_detach_execution_context`. |
| `kernel_object_hart.c` | `micros_hart_resolve`, `micros_hart_register`, `micros_hart_resolve_context`, `micros_hart_install_trap_stacks`, `micros_hart_current_thread`. |
| `kernel_object_validation.c` | `micros_kernel_objects_validate_base` and its new file-local validation tasks. |

Forty-six portable definitions move. The initializer and six-line public
combined validator remain the small registry facade. Initialization is one
bounded, non-failing publication after its existing admission checks; do not
manufacture wrappers around its individual stores.

### Necessary private interface and source direction

Only these 22 original static helpers require cross-owner visibility:

```text
storage_is_zero
process_handle_is_valid
thread_handle_is_valid
hart_handle_is_valid
thread_handles_equal
thread_handle_is_null
hart_handle_is_null
thread_scheduler_metadata_is_zero
clear_thread_scheduler_metadata
clear_thread_ipc_state
stack_range_is_valid
ranges_overlap
copy_user_context
clear_user_context
user_context_is_zero
trap_anchor_is_zero
trap_anchor_stacks_are_valid
require_initialized
resolve_process_mutable
resolve_thread_mutable
resolve_hart_mutable
timer_state_is_initial
```

Give them `kernel_object_` private names, remove only `static`, and preserve
their complete original result/parameter types. `bytes_are_zero` and
`clear_bytes` remain static. Other new production phases remain static in
their specified owner. The header admits those declarations, its guard, and
the unchanged `MICROS_KERNEL_OBJECTS_MAGIC` value
`UINT64_C(0x4d4943524f534f42)`, not another authority API.

Mutable resolution assumes its caller's existing initialized-registry
boundary. Do not add a whole-state check or make it a supported external
operation.

Actual dependencies under this partition are:

| Caller owner | Allowed object owners |
| --- | --- |
| Facade | State; existing external scheduler validator |
| Identity | None |
| Stack geometry | None |
| State | Identity's null-handle queries |
| Process | Identity |
| Thread | State, identity |
| Context | State, identity, geometry, thread's supported const resolver |
| Hart | Identity, geometry, thread's supported const resolver |
| Base validation | State, identity, geometry |

Public `micros_kernel_objects_validate` continues to call
`micros_scheduler_core_validate`, which calls
`micros_kernel_objects_validate_base`. Base validation must not call back
into the facade/scheduler or any lifecycle mutator.
`kernel/scheduler_core_internal.h` remains the byte-exact inter-subsystem
declaration owner for that base entry, not the new extraction header.

## Every original native/model definition

These owners partition all 27 original definitions. Every entry except
`main` moves. Private host names use `kernel_object_test_`; new file-local
phases use the same prefix.

| Owner | Original definitions/responsibility |
| --- | --- |
| `kernel_objects_test.c` | `main`, its exact fifteen-case automatic `const` array, ordered run/print/first-failure loop. |
| `kernel_objects_test_fixture.c` | `reset_objects`, `context_pattern`, `contexts_equal`; the sole original static registry and one new non-resetting registry getter. |
| `kernel_objects_initialization_test.c` | `test_initialization_is_checked_and_one_shot`, `test_uninitialized_registry_rejects_every_operation`, `test_fixed_capacities_report_exhaustion_atomically`. |
| `kernel_objects_process_test.c` | `test_process_slots_reuse_with_new_generations`, `test_process_exact_slot_creation_is_isolated`, `test_process_address_space_lifecycle_is_exact`. |
| `kernel_objects_thread_test.c` | `test_thread_execution_context_lifecycle_is_exact`, `test_thread_slots_reuse_with_new_generations`, `test_thread_ownership_and_production_limit`, `test_model_policy_supports_multiple_threads`. |
| `kernel_objects_hart_test.c` | `test_hart_policy_and_stack_validation_are_atomic`. |
| `kernel_objects_identity_test.c` | `test_generation_boundaries_and_quarantine`, `test_invalid_arguments_and_stale_handles_preserve_outputs`. |
| `kernel_objects_validation_test.c` | `test_validator_rejects_corrupt_relationships` and its original corruption macro. |
| `kernel_objects_model_test.c` | `test_seeded_lifecycle_model`: original setup, draw, dispatch, complete comparison and result. |
| `kernel_objects_model_reference.c` | `process_handles_equal`, `model_lowest_free_process`, `model_lowest_free_thread`, `model_process_handle`, `model_thread_handle`, `model_reports_expected_error`: independent mathematics and existing failure reporting. |
| `kernel_objects_model_projection.c` | `lifecycle_model_matches_registry`: complete process/thread/hart projection and one mismatch diagnostic. |
| `kernel_objects_model_operations.c` | `model_random` and the eight newly outlined operation cases. |

The model root calls operation and projection owners, both of which may use
pure reference/fixture primitives. Reference code calls no operation, root,
registry getter or production private helper. Putting the model error
reporter in the root would create a root/operation back-edge; it stays with
independent reference comparison.

There is one host source set and one existing `kernel-objects` CTest, not a
new separate model executable, slow label or workflow.

## Storage, data, and exact lifetimes

### Portable and native authority

The production registry remains caller-provided. Preserve the cohesive
runtime's three existing static objects and all its build guards verbatim.

The native fixture owns exactly the one original
`static struct micros_kernel_objects objects`. Do not add a one-field
wrapper, second model registry or cached relationship state. Each of the
fifteen case roots, including the seeded model, borrows the registry once;
ordinary helpers receive the exact typed pointer. Pure helpers gain no
fixture parameter.

The getter only returns that registry's address. `reset_objects` receives an
explicit registry and performs exactly the original whole-registry clear.
Original full snapshots remain legitimate; a rule copied from a multi-field
fixture must not accidentally ban these original registry-value operations.
No new reset or copy is permitted elsewhere.

`EXPECT_TRUE`, `EXPECT_ERROR`, and `EXPECT_ERROR_UNCHANGED` preserve their
actual formats, conditions, enum casts and first failure. The last macro
still creates its original automatic `expected_objects` snapshot before
evaluating the operation and comparing the complete registry. Do not move
that snapshot into shared work or evaluate the operation as an eager helper
argument.

The local corruption macro still restores the one case snapshot, executes
the mutation statement, and calls the combined validator, in that order.
Its actual definition/undef lifetime is verified if it must move to file
scope in the same owner. Keep all 21 mutations in their original order and
leave the final registry exactly as the original case leaves it.

### Permitted automatic case work

Grouping is optional. If used, case work contains only the listed original
objects and initializers; constants and nested scopes remain as specified.
There is no blanket work zeroing, new snapshot, copied work or retained
work pointer.

| Case | Original permitted work |
| --- | --- |
| Uninitialized registry | Fourteen objects: process/thread/hart input and output handles, three resolved pointers, input/output/before contexts, and two stack outputs. Preserve all `{0,1}`, MAX, pointer-one and context-pattern initializers and their order. |
| Capacity | Three capacity-sized handle arrays, three MAX output handles and the original index. Preserve the three independent reset/policy/fill/denial sequences. |
| Address-space lifecycle | Process, stale process, thread and snapshot; only process/thread start at zero. The original constant opaque root remains automatic in the root and is passed explicitly. |
| Execution-context lifecycle | Thirteen mutable objects: process, first/second/stale thread, first/second hart, four contexts, two observed bounds and snapshot. Keep the eight original constant stack bounds in the root. Preserve only original zero handles, three pattern contexts, `unchanged = observed`, and MAX bounds. The two block-local scheduler plans stay in their distinct original blocks, never in shared work. |
| Generation/quarantine | Original `next`, three process handles, three thread handles and two resolved pointers; preserve feed-face, zero-handle and NULL initializers. Pure generation checks precede the original reset sites. |
| Corruption | Owner, first/second thread, two harts and snapshot; only the five original handles begin at zero. The single case snapshot remains authoritative and each mutation restores it without another cached copy. |

Other bounded cases keep their complete bodies under only the approved
namespace and explicit registry transport.

### Model identity

Preserve the three original reference struct definitions and all their
fields/types in the host private header. No production private header is
imported by reference or evidence code.

```text
seed: 0x7a31d4c9
steps: 4096
draw: one uint32_t xorshift value per iteration, shifts 13/17/5
dispatch: value % 8, original cases 0 through 7, no new default
policies: max_threads_per_process=3, max_harts=2
initial harts: exact 0 and 1
```

Run work may group only original `model`, `harts[2]`, `random_state` and
`step`. Only `model` gets its original aggregate-zero initialization;
random state starts at the original literal. Per-iteration work may group
only `value`, `slot`, `expected` and `actual`, with the original one draw and
otherwise uninitialized locals. Keep the local `MODEL_STEPS` enum.

Each operation retains its original case-local handles, MAX sentinels,
temporary roots, context-pattern construction, expected/actual computation,
output checks and reference updates. In particular, context construction
does not move past its original selection/checks merely because a path may
be denied.

Helper success replaces only the original case `break`; the caller still
compares complete state after every completed attempt, including expected
denials. A failed status or output check still stops before comparison.
Projection helpers return to the root's one original `mismatch` label and
diagnostic, not another print/return domain.

There is no scripted prefix, trace ring, coverage counter, final teardown
or final empty-registry proof in this model. Do not add any. Its live state
after the last iteration remains until the next original case reset.

### Target ownership

`kernel/object_model_test.c` retains the one original entry and its two
block-static resources:

- `thread_stack[MICROS_THREAD_KERNEL_STACK_SIZE]`, with its original
  `MICROS_TRAP_STACK_ALIGNMENT` attribute and implicit zero storage;
- the original static mutable `micros_user_context context`.

Do not make them automatic, const, duplicated, independently reset, or part
of a new global context. Root owns runtime capture and the IRQ lifetime;
the scenario owner receives explicit existing state.

Optional target work contains exactly eleven original objects: objects and
hart pointers, hart handle, first/replacement process, thread, unchanged
thread, current handle, return plan, resolved-process pointer, and error.
Only the unchanged-thread MAX sentinel and NULL resolved pointer have their
original explicit initializers. Saved status and `passed = false` stay in
entry.

The attach/select task receives the actual bottom and top computed from
the root-owned array plus its existing context pointer. `sizeof` remains
bound to the actual array in root, never a decayed helper parameter.
No new runtime getter, registry, stack pool or implicit reset is required.

Every failure and success reaches the same root `done` restoration exactly
once. Keep the existing success-path retirement and reuse sequence;
failure adds no rollback.

## Required semantic dispositions

All resulting C functions are at most 120 physical lines. None of the
eight original long functions receives an algorithmic exception. Do not
compress statements to meet a signal.

| Original function | Lines | Required ordered tasks |
| --- | ---: | --- |
| `micros_hart_install_trap_stacks` | 137 | Original argument/init/exact-hart/already-installed checks; candidate geometry; complete other-hart conflicts; attached-thread conflicts; original non-failing anchor/idle/install publication. |
| `micros_kernel_objects_validate_base` | 476 | Original header/policy and witness initialization; process slots; thread slots/context relationships; process-thread count closure; hart shape/identity/stack/timer/current uniqueness; final three counts and hart policy. |
| `test_uninitialized_registry_rejects_every_operation` | 175 | Original declarations/reset; process/root operations and sentinels; thread/context operations and sentinels; hart operations and exact unchanged-registry evidence. Preserve all actual calls rather than adding APIs implied by the title. |
| `test_thread_execution_context_lifecycle_is_exact` | 338 | Fixture and two-thread/two-hart preparation; first attach/inspect/input boundaries; peer/idle/emergency conflicts; admit/plan/commit and captured context; hold/idle/remove/detach/reuse and stale-output preservation; final cleanup. |
| `test_generation_boundaries_and_quarantine` | 175 | Pure process/thread generation and reserved-value boundaries; thread maximum/reuse/stale/quarantine; process maximum/reuse/stale/quarantine and next-slot proof. |
| `lifecycle_model_matches_registry` | 140 | Original header counts; every process; every thread/context; every hart; one final combined validator and original mismatch diagnostic. |
| `test_seeded_lifecycle_model` | 374 | Original fixture/model/hart setup; one draw; eight ordered case tasks; complete comparison; original return with no added cleanup. |
| `micros_kernel_object_runtime_run_self_test` | 161 | Root IRQ/runtime capture; process/thread and production-limit proof; actual stack attach/admit/plan/commit/current/release denial; hold/idle/remove/detach/release; process reuse/stale/sentinel and baseline; common IRQ restore. |

Four shorter mixed tasks also require explicit outlines:

| Original function | Lines | Required ordered tasks |
| --- | ---: | --- |
| `micros_thread_attach_execution_context` | 99 | Original admission/exact thread and candidate geometry; attached peer-thread conflicts; installed hart idle/emergency conflicts; original context publication. |
| `test_fixed_capacities_report_exhaustion_atomically` | 115 | Process fill/refusal; independent thread fill/refusal; independent hart fill/refusal and final validation, with resets at their original boundaries. |
| `test_process_address_space_lifecycle_is_exact` | 105 | Original preparation/argument rejection; attach and duplicate/mismatch/release denial; live-thread detach denial and retirement; generation reuse/stale snapshot proof. |
| `test_validator_rejects_corrupt_relationships` | 118 | Fixture/snapshot; original mutations 1-8 for relationships/counts/policies; 9-14 for generations/context; 15-21 for released-slot payload and hart closure. Never regroup or reorder the actual mutation stream by type. |

Other short original operations are bounded cohesive lookup, generation,
allocation/release, snapshot or one-property cases. Keep their original
bodies rather than adding single-expression wrappers.

### Base-validation witnesses and return scope

Retain exactly one automatic
`process_thread_counts[MICROS_PROCESS_CAPACITY]` and one automatic
`current_threads[MICROS_THREAD_CAPACITY]` in the caller lifetime, plus the
three original observed counts. Initialize them in the original order.
Thread checks update the former; process-count closure consumes it. Hart
current binding uses the latter to reject duplicate current ownership.

Keep every old slot-state predicate, short circuit, pairwise loop bound,
seven-way hart-stack overlap ordering, free-hart `continue`, and count
increment position. Longer thread/hart passes may expose subordinate
shape/context/stack/current tasks inside the same validation owner.

No helper copies or reinitializes a witness per slot, caches a relationship,
dereferences an owner before its original bounds check, publishes state or
repairs corruption. Base validation does not add the scheduler's final
running/queue/accounting closure. Helpers map failure only to the original
caller invariant error; success does not bypass later slots or counts.

## Stable neighbors and original source oracles

Freeze these eighteen artifacts from the immutable reviewed Git base:

```text
include/micros/kernel_objects.h
include/micros/kernel_object_runtime.h
kernel/kernel_object_runtime.c
kernel/kernel_object_runtime_internal.h
include/micros/user_context.h
include/micros/scheduler_core.h
kernel/scheduler_core_internal.h
kernel/scheduler_core_validate.c
include/micros/endpoint.h
kernel/endpoint_internal.h
arch/riscv64/trap_context.h
arch/riscv64/scheduler_context.h
arch/riscv64/trap.S
arch/riscv64/user_entry.S
arch/riscv64/scheduler.S
kernel/boot_component_tests.c
CMakePresets.json
tools/run_qemu_smoke.py
```

The three original complete source digests are:

```text
kernel/kernel_objects.c
453c3cfbfb02d1c014226a315106a157676a7b195be06f561bb3abbda042db4a
tests/host/kernel_objects_test.c
d8876ac913fea8cdd7e4237db03644ec888f501ad1001d417b16d01b2a5fbe95
kernel/object_model_test.c
ce707a2a16d4ee8a57db165f4abb0f8b6155ef60e8d26d232740bfb8d3e22f99
```

Freeze all 76 bodies/signatures, 72 owner moves, eight original long entries,
four shorter task dispositions, storage/data and real execution identities
before movement. A finite typed inverse reconstructs complete bodies under
only known private names, registry/work transport and named task outlines.
It verifies actual types, initializers, caller arguments, source data and
callee exits before restoring their original form.

The scheduler and endpoint gates also require the complete portable source's
original byte hash. Their expected
`453c3cfbfb02d1c014226a315106a157676a7b195be06f561bb3abbda042db4a`
must remain independently enforced. Restore the complete original bytes
from current actual owners/signatures, constant and outlined bodies, with
only the exact inverse admitted by this design.

Do not replace the hash with current output, weaken it to per-body tokens,
load the old Git blob as a success-shaped answer, or reinsert unchecked old
executable text. Raw-body/format preservation is required where that
complete inverse needs it. A changed predicate, effect, binding, literal,
declaration or unrecognized owner must reject.

Independent positive/negative fixtures cover first failure, array extent,
scalar-output binding, null/member qualification, snapshot-before-action,
macro/reset order, separate block-local plans, shared witnesses, one model
draw, helper-local break/continue/mismatch scope, target IRQ restoration and
success-only cleanup. This is a bounded inverse, not a general C interpreter.

### Shared inspection without import cycles

The future host-only `tests/host/c_source_records.py` takes exactly the
existing scheduler `compact`, `matching`, `matching_lexical`, `cpp_guards`,
`Function`, and `function_records` definitions unchanged. Its dependencies
are the existing frame lexical/signature primitives and VM declaration
inspection, plus standard-library imports.

Scheduler re-exports those imported names; the object gate imports the
shared owner directly. The shared owner imports no scheduler, endpoint or
object gate. This permits retained gates to use the new exact object
inverse without a scheduler/object back-edge.

Freeze every moved node's whole reviewed-base AST, including decorators,
before the move. The class and record-function AST digests are:

```text
Function
312fdebc83fa138ed1822691b7a540be4d0f3330a2be8de5164fa74f9ec86eef
function_records
068cc6a2b9f64e99a0d937ac5c64a60291c23608ef1f637f170c54e851f5cf72
```

No parser, literal-token, declaration, CPP-guard or inspection behavior
change is authorized by that relocation. Retain the existing independent
definition-selection and literal-preserving token regressions.

## Build consumers and four-dimensional inheritance

Core replaces the existing literal operand once in each exact consumer:

```text
kernel_objects_test
frame_ownership_test
scheduler_test
endpoint_test
ipc_model_test
bootstrap_control_test
pm_test
micros
```

Host replaces its one operand in `kernel_objects_test`. Target replaces its
one operand in `micros`, inside the existing
`MICROS_BUILD_OBJECT_MODEL_TEST` guard. These are ten operand replacements;
no consumer lists split members beside the set. Existing C17, warnings,
sanitizers, include roots, linkers, labels, drivers, budgets and guards stay
unchanged.

| Original path | Full fallback | QEMU | Separate model | Image |
| --- | --- | --- | --- | --- |
| `kernel/kernel_objects.c` | false | Exact 23 gates below | `test-ipc-model` | empty |
| `tests/host/kernel_objects_test.c` | true | empty | empty | empty |
| `kernel/object_model_test.c` | false | `test-qemu-object-model` | empty | empty |

The exact ordered core QEMU list is:

```text
test-qemu-object-model
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
test-qemu-frame-ownership
test-qemu-address-space-handoff
test-qemu-scheduler
```

Add exactly three original-to-new-source/header alias rows. All four
dimensions are checked independently; empty host QEMU metadata never removes
its original full fallback.

Add only the new structural gate
`tests/host/test_kernel_object_source_layout.py` and shared
`tests/host/c_source_records.py` to host-unit-only inputs. After those two
declarations and three alias rows are projected out, existing planner
algorithms/AST, unknown-path escalation and required command construction
remain unchanged.

### Complete recipe and closed profiles

The complete current recipe SHA-256 is:

```text
50cac89bbc8dfc0246b97d36be3585ff53f7e86c3dbc772539a6f8352e05db7d
```

Independently reverse exactly the three ordered set additions and ten
operands to that whole recipe before maintaining the one admitted CMake
literal. Retained endpoint/scheduler/RAMFS/trap complete recipe/planner
oracles may additionally project only this exact delta; their original
expected values remain unchanged.

The bootstrap consumer guard changes its literal-object operand expectation
to the exact complete core-set membership for the same consumer, not a
weaker filename search. Keep helper imports acyclic and expected declarations
independent of production metadata.

No profile admission, closure/execution algorithm, tool policy, receipt
schema, reusable inventory or cross-HEAD equivalence changes. Unknown recipe
bytes still reject before cache metadata collection. Canonical configure
presets may refresh generated build graphs before recording; changed
pre/post inputs still reject, never silently disable eligible profiles.

## Acceptance and sequence

Implementation starts with structural Red and frozen facts from this Git
base. Original definition and semantic-task migration inventories are
explicit; the latter contains the eight long and four shorter dispositions.
Keep the original eight-long inventory distinct and immutable. Both active
owner/task collections become literal empty tuples before review.

Each green slice uses the smallest existing layer. Portable closure builds
all seven native consumers and the real target; focused existing CTests
include `kernel-objects`, `frame-ownership`, `scheduler-core`,
`endpoint-profile`, `ipc-model`, `bootstrap-control`, and `pm-lifecycle`
when their common core changes. Host-only evidence extraction runs
`kernel-objects`; target extraction runs real `test-qemu-object-model`.
Shared-inspector/oracle maintenance also runs retained source-layout guards.
Do not duplicate an unaffected model or QEMU run for a host-only move.

Before implementation review, stage the complete candidate, prove clean
tracked/index state and empty untracked task output, record HEAD/tree and
exact results, and execute the complete current authoritative PR plan.
Shared CMake currently selects all 47 fresh commands. Additional bounded
actual reuse acceptance proves native, smoke and TTY-image receipts refer to
the same candidate and original producer while unsupported workflows remain
fresh. It never substitutes for the origin/main author plan.

Retain `kernel_objects_test` / `kernel-objects` and the real target gate's
exact marker and eight-second budget:

```text
MICROS_OBJECT_MODEL_TEST_PASS process-generation=advanced stale=rejected thread-limit=enforced hart-local=preserved
```

No new workflow, native/model invocation, marker, timeout or policy replaces
that evidence. Reviewers inspect exact author results without routine
reruns. After a clean independent implementation review, squash only the
reviewed head, verify identical merge tree, fast-forward main and delete
the exact obsolete local/remote branches.

This design itself changes documentation only. It authors no executable
source, test, model, harness, build scaffold or reusable prototype.

### Current implementation progress

The initial structural Red exposed the registry-storage helper in its old
monolithic owner. The first implementation slice moves the original twelve
representation and twelve identity definitions to their required owners.
Their complete bodies and types remain frozen; the current necessary
portable private surface is nineteen helpers, growing only to the final
twenty-two as remaining owners move.

All 76 original bodies/signatures and eighteen neighboring artifacts are
frozen from immutable `4eb1c4d`, not rewritten output. The six shared
inspection definitions move unchanged with whole-AST proof and compatible
scheduler re-exports. Its existing cache/lexing fixture follows the real
new helper owner without changing its assertions.

All 23 final C paths, three headers/sets and ten source operands are wired
once. Still-unextracted owners are explicitly declarations-only units, not
duplicate state or claimed completed tasks. The following portable slice
completes all forty-six core moves: all 48 original portable definitions
now occupy nine owners and the exact necessary private surface is
twenty-two helpers. The facade contains only the original initializer and
combined scheduler-delegating validator. The existing lower base-validator
declaration and all original portable bytes remain independently enforced.
The native/model slice now completes all twenty-six original host moves
across its twelve owners. Cases and reference comparison receive the one
original registry explicitly; the new getter returns it without resetting.
Actual assertion macros, reference types, complete bodies/signatures and
the fifteen-case automatic `const` table reconstruct exactly. The active
seventy-two-owner migration is now a literal empty tuple.

All twelve semantic task dispositions remain pending, including the target
entry. Only the target scenario unit remains declarations-only; no completed
semantic outline is claimed by source movement alone.

The complete current portable source restores the original
`453c3cfbfb02d1c014226a315106a157676a7b195be06f561bb3abbda042db4a`
bytes. The exact complete recipe inverse restores `50cac89...` before
maintaining only the admitted current digest
`44d8a8615e5465ad199ed916b30e72af05643ef00ae5ac9a7df00a0dc4cad2c1`.
Prior scheduler/endpoint source and recipe/planner expected values remain
unchanged under only these verified inverses.

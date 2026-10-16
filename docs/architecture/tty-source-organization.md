# TTY Source Organization

## Status and scope

This is the Accepted exact contract for
[ADR-0070](../adr/0070-tty-function-and-evidence-decomposition.md).
Independent complete read-only design review is clean and design #141 is
separately merged. Original implementation #142 was blocked and abandoned
after the model graph omission described by Accepted
[ADR-0071](../adr/0071-tty-model-validation-reference-dependency.md).
Corrective design #143 is independently reviewed and separately merged as
`caa7573b89ef7501abf82a1ad89f3237c003a237`; its actual squash tree equals
the reviewed tree and its obsolete branches are deleted. Implementation
has restarted test-first on a fresh branch from that current `main`.
Abandoned patches are reference only, not wholesale cherry-picked or
accepted as current validation evidence.
Accepted ADR-0047 remains the authority
for every existing TTY, UART, PLIC, mapping, grant and request transition.

```text
reviewed micros base: ac94a21141b86c2e9b661cf90f500cca82e0c0c4
fixed MINIX reference: 4db99f4012570a577414fe2a43697b2f239b699e
design task: tty-function-decomposition-design
implementation task: tty-function-decomposition-implementation
corrective design task: tty-model-validation-design-correction
```

Grant design #139 and implementation #140 are independently reviewed and
merged. Implementation's actual squash tree equals its validated/reviewed
tree; exact obsolete local and remote branches are deleted. This next
maintenance outcome adds no runtime-DAG node.

| Reviewed signal | Physical lines | Definitions | Above 120 |
| --- | ---: | ---: | ---: |
| `kernel/tty_handoff_runtime.c` | 835 | 35 | 1 |
| `kernel/uart_console_test.c` | 242 | 2 | 1 |
| `servers/tty/tty_service_core.c` | 877 | 20 | 1 |
| `tests/host/tty_model_test.c` | 1,859 | 38 | 1 |
| `tests/host/tty_handoff_test.c` | 949 | 16 | 2 |
| `tests/host/tty_control_syscall_test.c` | 682 | 38 | 0 |
| `tests/host/tty_control_test.c` | 359 | 7 | 1 |
| **C total** | **5,803** | **156** | **7** |
| `tools/generate_tty_service_fixture.py` | 256 | 3 | 1 |

The Python signal is `render_fixture` at 195 lines; `parse_arguments` and
`main` are 11 and 24 lines. Counts describe the immutable reviewed base, not
newly generated output.

## Preserved behavior

The [fixed MINIX responsibility study](../research/minix-tty-source-organization.md)
classifies inherited parity and required adaptations. Nothing here imports
MINIX's multiple devices, termios, job control, legacy hooks or recovery.

- One exact manifest-bound TTY process/thread/endpoint generation owns one
  UART leaf and PLIC source 10. Public state layouts and authority stay exact.
- Runtime identity, prepared/active object relationships, endpoint/profile/
  scheduler properties, console/route phases and the original deadline remain
  one complete invariant.
- Candidate preparation copies original state, applies the existing pure
  transition and publishes only after success. Prevalidated commits retain
  their original exact fields; begin deadline and begin phase remain separate.
- State precedes delivery enable. A PLIC claim remains in service until
  explicit completion after draining. Panic seizure remains terminal.
- Service decode/shape precedes caller, ownership, ID, slot/grant and complete
  invariant checks in their original order. Do not hoist validation.
- A busy write does not consume its ID. An uncollected completion differs
  from a resident-only buffer: only the latter arms the original writable
  hint, at its original point.
- Grant copy rejection cannot publish a write or consume read input. Preserve
  original stage/discard/commit and completion publication order.
- A collect reply carries an exact release action; completion is cleared only
  by the original reply-commit boundary, not while preparing the reply.
- Preserve every real MMIO access, fence, interrupt-bit observation, retained
  claim probe, bounded claim-attempt count, panic reason and test marker.
- No allocation, clock/deadline change, IRQ scope, state reset, retry, rollback,
  extra validation, inferred peer, alternate device or cleanup is added.

## Exact owners

Exactly 26 C owners are in scope. Seven original paths remain, nineteen are
added. Other TTY/core/driver/control/trap/service modules remain unchanged.

| Owner | Responsibility |
| --- | --- |
| `kernel/tty_handoff_runtime.c` | State preparation/binding/reset/public getters, begin facade, deadline and panic. |
| `kernel/tty_handoff_runtime_state.c` | Sole two-object runtime context, non-resetting private getter and original byte primitives. |
| `kernel/tty_handoff_runtime_validate.c` | Original identity, manifest/live binding, complete validation, readiness and device authority. |
| `kernel/tty_handoff_runtime_transitions.c` | Original candidate prepare and bounded prevalidated commit functions. |
| `kernel/uart_console_test.c` | Real UART/PLIC scenario with original root reporting and ordered mechanism phases. |
| `servers/tty/tty_service_core.c` | Service initialization, ownership commit and decoded call dispatch. |
| `servers/tty/tty_service_validate.c` | Complete service/request/completion shape and ownership invariant. |
| `servers/tty/tty_service_read.c` | Pending-read submission, stage/copy/commit and read-completion publication. |
| `servers/tty/tty_service_write.c` | Busy/retry distinction, grant preflight/copy and resident write/completion publication. |
| `servers/tty/tty_service_request.c` | Shared byte/reply/copy/submit primitives, exact cancellation/collection and reply-commit ownership. |
| `tests/host/tty_model_test.c` | Actual automatic model root, scripted/random dispatch, request-ID selection, final drain and report. |
| `tests/host/tty_model_common.c` | Original readonly peer constants, failure/trace, byte codec and xorshift primitives. |
| `tests/host/tty_model_reference.c` | Independent canonical input, echo, output and write-retry reference transitions. |
| `tests/host/tty_model_io.c` | Actual fake grant and UART callbacks with original effects/observations. |
| `tests/host/tty_model_protocol.c` | Original request construction, exact reply comparison and accepted-reply/UART effects. |
| `tests/host/tty_model_operations.c` | Original read/write/cancel/collect/input-IRQ/transmit-IRQ/malformed operations. |
| `tests/host/tty_model_validate.c` | Ordered complete production/reference/observation comparisons. |
| `tests/host/tty_handoff_test.c` | Original bounded handoff/deadline/panic cases and deterministic runner. |
| `tests/host/tty_handoff_test_fixture.c` | Sole five-object native runtime fixture and original resolver/profile test doubles. |
| `tests/host/tty_handoff_device_test.c` | Exact device-leaf/range classification case. |
| `tests/host/tty_handoff_runtime_test.c` | Actual prepared-to-ready runtime lifecycle/corruption and panic case. |
| `tests/host/tty_control_syscall_test.c` | Five original captured-control cases, both actual `setjmp` sites and entry. |
| `tests/host/tty_control_syscall_test_fixture.c` | Sole twenty-object fixture, argument constructors, reset and invocation. |
| `tests/host/tty_control_syscall_test_observe.c` | Sixteen original authority/runtime/flag/mapping observation doubles. |
| `tests/host/tty_control_syscall_test_controller.c` | Twelve original ordered hardware/publication/fault/panic doubles. |
| `tests/host/tty_control_test.c` | Original operation-14 decode/shape/authority cases and runner. |

The generator remains one file. Its new ordinary rendering/validation phases
are file-local Python functions, not another CLI or shared framework.

Five new private headers:

```text
kernel/tty_handoff_runtime_internal.h
servers/tty/tty_service_internal.h
tests/host/tty_model_internal.h
tests/host/tty_handoff_test_internal.h
tests/host/tty_control_syscall_test_internal.h
```

Keep existing public/internal-supported headers byte-exact. No facade gains
extraction-only supported API. Existing externally named test doubles keep
their exact names and complete result/parameter types.

## Complete partition of original definitions

Names below are the original names before family-private prefixing.
Every original is required, including functions that do not move.

### Runtime: 35 originals

Runtime root, eight:

```text
micros_tty_handoff_runtime_prepare
micros_tty_handoff_runtime_bind_bootstrap
micros_tty_handoff_runtime_reset
micros_tty_handoff_runtime_state
micros_tty_handoff_runtime_authoritative_state
micros_tty_handoff_runtime_begin
micros_tty_handoff_runtime_deadline_expired
micros_tty_handoff_runtime_panic
```

State leaf, two: `clear_bytes` and `copy_bytes`. The two original static
objects and new non-resetting private context getter have this one owner.
Putting these primitives beside the facade would create a validation/facade
import cycle; the explicit state leaf prevents it.

Validation, ten:

```text
process_handles_equal thread_handles_equal endpoint_matches_process
identity_is_valid runtime_entry binding_identity_matches live_binding_matches
micros_tty_handoff_runtime_role_ready
micros_tty_handoff_runtime_device_authority
micros_tty_handoff_runtime_validate
```

Transitions, fifteen:

```text
micros_tty_handoff_runtime_prepare_begin
micros_tty_handoff_runtime_commit_begin_deadline_prevalidated
micros_tty_handoff_runtime_commit_begin_phase_prevalidated
micros_tty_handoff_runtime_prepare_mapped
micros_tty_handoff_runtime_commit_mapped_prevalidated
micros_tty_handoff_runtime_prepare_release
micros_tty_handoff_runtime_commit_release_prevalidated
micros_tty_handoff_runtime_prepare_commit
micros_tty_handoff_runtime_commit_console_prevalidated
micros_tty_handoff_runtime_prepare_claim
micros_tty_handoff_runtime_commit_claim_prevalidated
micros_tty_handoff_runtime_prepare_complete
micros_tty_handoff_runtime_commit_complete_prevalidated
micros_tty_handoff_runtime_prepare_ready
micros_tty_handoff_runtime_commit_ready_prevalidated
```

### Service: 20 originals

| Owner | Originals |
| --- | --- |
| Root | `micros_tty_service_initialize`, `micros_tty_service_commit_ownership`, `micros_tty_service_handle_call`. |
| Validation | `endpoint_is_valid`, `request_is_clear`, `completion_is_clear`, `completion_is_valid`, `micros_tty_service_validate`. |
| Read | `publish_read_completion`, `micros_tty_service_complete_read`, `handle_submit_read`. |
| Write | `handle_submit_write`. |
| Request | `clear_bytes`, `prepare_reply`, `prepare_protocol_reply`, `copy_grant`, `validate_submit`, `handle_cancel`, `handle_collect`, `micros_tty_service_commit_reply`. |

### Mixed model: 38 originals

| Owner | Originals |
| --- | --- |
| Root | `model_collect_request_id`, `model_random_transition`, `model_scripted_transition`, `test_replayable_mixed_transition_model`, `main`. |
| Common | `model_failure`, `write_u32_le`, `write_u64_le`, `read_u32_le`, `read_u64_le`, `model_random`, `model_record`. |
| Reference | `reference_input_index`, `reference_echo_index`, `reference_output_pending`, `reference_publish_output`, `reference_enqueue_echo`, `reference_receive_byte`, `reference_first_line_length`, `reference_consume_input`, `reference_write_accept`, `reference_write_arm_retry`, `reference_output_take`. |
| I/O | `model_copy_grant`, `model_bus_read`, `model_bus_write`. |
| Protocol | `submit_request`, `identity_request`, `model_check_reply`, `model_commit_reply`. |
| Operations | `model_submit_read`, `model_submit_write`, `model_cancel`, `model_collect`, `model_input_irq`, `model_transmit_irq`, `model_malformed_submit`. |
| Validation | `model_validate`. |

### Handoff evidence: 16 originals

| Owner | Originals |
| --- | --- |
| Root | `test_complete_handoff`, `test_deadline_covers_every_pre_ready_phase`, `test_ready_preserves_retained_route`, `test_uart_console_ownership`, `test_failure_preservation`, `advance_to_phase`, `test_terminal_panic`, `test_invariant_rejection`, `micros_tty_handoff_test_run`. |
| Fixture | `micros_bootstrap_control_find_binding`, `micros_process_resolve`, `micros_thread_resolve`, `micros_endpoint_resolve_internal`, `micros_privilege_profile_allows_kernel_operation`. |
| Device | `test_device_leaf_classification`. |
| Runtime | `test_runtime_lifecycle_validation`. |

### Captured control evidence: 38 originals

| Owner | Originals |
| --- | --- |
| Root | `test_commit_orders_state_before_delivery`, `test_completion_orders_hardware_before_idle`, `test_returned_failures_preserve_state`, `test_invariant_failure_is_fatal_before_mutation`, `test_hardware_corruption_precedes_tuple_failure`, `main`. |
| Fixture | `commit_arguments`, `complete_arguments`, `reset_fixture`, `invoke`. |
| Observation | `micros_test_irq_is_enabled`, `micros_test_timer_interrupt_is_enabled`, `micros_test_external_interrupt_is_enabled`, `micros_bootstrap_runtime_state`, `micros_bootstrap_runtime_authoritative_state`, `micros_bootstrap_runtime_validate`, `micros_ipc_runtime_registry`, `micros_ipc_runtime_authoritative_registry`, `micros_kernel_object_runtime_registry`, `micros_tty_handoff_runtime_state`, `micros_tty_handoff_runtime_authoritative_state`, `micros_tty_control_syscall_authority_resolve`, `micros_tty_handoff_runtime_validate`, `micros_plic_validate`, `uart_console_handoff_is_active`, `micros_user_address_space_validate_tty_uart_mapping`. |
| Controller | `micros_test_external_interrupt_enable`, `micros_bootstrap_runtime_fail`, `micros_tty_handoff_runtime_prepare_commit`, `micros_tty_handoff_runtime_commit_console_prevalidated`, `micros_tty_handoff_runtime_prepare_complete`, `micros_tty_handoff_runtime_commit_complete_prevalidated`, `micros_plic_prepare_tty_enable`, `micros_plic_commit_tty_prepare_prevalidated`, `micros_plic_commit_tty_enable_prevalidated`, `micros_plic_complete`, `micros_tty_owner_fault_record`, `micros_trap_panic_entry`. |

The two UART originals remain `uart_registers` and
`micros_uart_console_runtime_run_self_test`. The seven native control
originals remain `decode_failure_preserves_request`, `test_contract_and_decode`,
`test_commit_width_rejection`, `test_complete_shape_rejection`,
`test_syscall_authority`, `test_no_console_authority` and
`micros_tty_control_test_run`.

Exactly 116 originals move: runtime 27, service 17, model 33, handoff seven
and captured-control 32. The other 40 stay in their original owners.

## Private directions and necessary visibility

Private namespaces are `tty_handoff_`, `tty_service_`, `tty_model_`,
`tty_handoff_test_` and `tty_control_syscall_test_`. Public/test entry and
externally named double symbols retain their original names.

Only a required cross-owner use makes an original or new helper external.
Preserve complete existing parameter/result types; the explicit original
fixture pointer is the only new state input. Pure primitives gain no fixture.

Exactly fifty formerly static originals require private cross-owner
declarations:

| Family | Exact original helpers |
| --- | --- |
| Runtime: five | `clear_bytes`, `copy_bytes`, `endpoint_matches_process`, `identity_is_valid`, `binding_identity_matches`. |
| Service: ten | `clear_bytes`, `copy_grant`, `endpoint_is_valid`, `handle_cancel`, `handle_collect`, `handle_submit_read`, `handle_submit_write`, `prepare_protocol_reply`, `prepare_reply`, `validate_submit`. |
| Model: twenty-nine | `identity_request`, `model_bus_read`, `model_bus_write`, `model_cancel`, `model_check_reply`, `model_collect`, `model_commit_reply`, `model_copy_grant`, `model_failure`, `model_input_irq`, `model_malformed_submit`, `model_random`, `model_record`, `model_submit_read`, `model_submit_write`, `model_transmit_irq`, `model_validate`, `read_u32_le`, `read_u64_le`, `reference_consume_input`, `reference_first_line_length`, `reference_output_pending`, `reference_output_take`, `reference_receive_byte`, `reference_write_accept`, `reference_write_arm_retry`, `submit_request`, `write_u32_le`, `write_u64_le`. |
| Handoff: two | `test_device_leaf_classification`, `test_runtime_lifecycle_validation`. |
| Captured control: four | `commit_arguments`, `complete_arguments`, `invoke`, `reset_fixture`. |

`model_failure` is also a cross-owner dependency of the original
`MODEL_EXPECT` macro. Its declaration is not optional just because the
unexpanded C body contains the macro name rather than that callee.

Only three new non-resetting context getters cross owners: runtime state,
handoff native fixture and captured-control fixture. The model's two
original readonly endpoint constants have one common owner and necessary
private readonly declarations. Other new ordinary phase helpers remain
file-local. Existing public APIs and externally named doubles need no
extraction-only supported declarations.

```text
runtime state leaf -> no other runtime owner
runtime validation -> state leaf
runtime transitions -> state leaf
runtime root -> state leaf, validation, existing transition public contract

service validation -> no request/read/write/facade helper
service request -> validation
service read/write -> request, validation
service facade -> request, validation, read, write

model common -> no other model owner
model reference -> no operation/protocol/I/O/root/validation owner
model I/O -> reference
model protocol -> common, I/O
model operations -> common, reference, I/O, protocol
model validation -> common, reference
model root -> common, reference, I/O, protocol, operations, validation

handoff fixture -> existing bounded production substrate only
handoff device -> no fixture helper unless its actual original fields require it
handoff runtime case -> fixture
handoff root -> device and runtime cases

control fixture -> original production adapter under test
control observation/controller doubles -> non-resetting fixture
control root -> fixture
```

The validation -> reference edge is the narrowly Accepted ADR-0071
correction, separately merged as #143.
It preserves the original final `reference_output_pending` call after the
CR/LF comparison. The predicate remains in the reference owner with its
existing required declaration; it does not call back into validation.
No other private direction or complete original body changes.

The existing adapter calls externally named doubles; that test wiring does
not authorize private callbacks from a double into a returned case/helper.
Runtime public functions may call other existing public functions exactly
as before. No new validation/transition recursion or shared ambient current
process is introduced.

## Storage and lifetime contracts

| Family | Required original storage |
| --- | --- |
| Kernel runtime | `tty_handoff_state` and `tty_bootstrap_state`, with their original static zero state and reset/prepare/bind sites. |
| Handoff native doubles | Five original static objects: process, thread, endpoint and their two handles. |
| Captured-control doubles | Twenty original static objects, including bootstrap/registry/object/TTY/context records; authority, PLIC and enable/validity flags; stage/counters; `jmp_buf`; and panic-armed flag. |
| Mixed model | One actual automatic root `struct tty_model`; no static replacement, new getter or allocation. |

The first three families may group only their original complete fields.
Getters return the one original family without clearing or rebinding it.
Existing public double signatures cannot gain fixture parameters: stateful
doubles borrow once; ordinary helpers receive explicit state.

Both `setjmp(panic_target)` sites stay in their actual case roots. Their
`longjmp` callbacks retain the original static environment, panic flag and
abort fallback. No helper that returns before invocation may own the saved
environment.

Preserve static readonly UART binding/manifest entries, masks, constants,
native phase arrays and every exact array `sizeof`. Optional automatic work
contains only original mutable declarations, full types/extents and exact
explicit initializers; never blanket-zero, copy, retain or substitute it.
Derived pointers must still address the actual original enclosing object,
not a copied bootstrap state, profile or plan.

Native runtime lifecycle evidence keeps its four root constants and actual
bootstrap/binding/entry/transition, registry/object/context and candidate
scopes. Its before/after device-authority sentinel, prepared/active scheduler
mutations, deadline 150 and route/ready/panic chronology remain exact.

## Required domain outlines

No original over-limit C function receives an algorithmic exception. Every
current resulting C or Python function is at most 120 physical lines.

| Original | Lines | Ordered outline |
| --- | ---: | --- |
| `live_binding_matches` | 122 | Resolve original records/identity/root relations; original prepared and active predicates; original console-phase/deadline selection. |
| `micros_uart_console_runtime_run_self_test` | 194 | Handoff candidate setup/release; early/IER observation and quiesce; state-before-route enable and independent interrupt-bit observations; actual bounded claim/retention; explicit completion; terminal panic seizure; original root reporting/reset-return panic. |
| `handle_submit_write` | 128 | Submit shape/ID; uncollected-completion busy; resident-only retry arm; zero-length authority preflight; invariant validation; full grant copy; resident acceptance; ID/completion/reply/UART-event publication. |
| `model_validate` | 168 | Production validity and callback/interrupt observations; complete input/echo comparison; write/cursor/retry comparison; request/completion/rejected-read and CR/LF closure. |
| `test_device_leaf_classification` | 141 | Required exact device/range and boundary exclusion; invalid executable/alias/foreign leaves; physical substitution and ordinary managed classification; out-of-phase output preservation. |
| `test_runtime_lifecycle_validation` | 354 | Exact original mock/bootstrap setup; absent/unexpected authority; prepare/bind/invariant and device-none; begin deadline/phase; map/device sentinel; release/live scheduling; commit/claim/ready retention/completion; source-only corruption and terminal panic. |
| `test_syscall_authority` | 139 | Root original constants/typed fixture; authorized tuple; foreign current-thread rejection; free-thread corruption; profile-operation corruption; manifest-source corruption. |
| Python `render_fixture` | 195 | Five descriptor validations; VM-only metadata/report/TTY bound rejection; five quotas and actual total; segment bytes; image list; exact manifest entry order; actual report address and final join. |

Other bounded original primitives, request operations, model operations and
cases retain complete bodies. Moving them to a named owner is not a license
to rewrite their algorithms or acceptance observations.

## Model and generator identity

The model retains:

```text
seed = 0x4d4943524f535454
xorshift = left13, right7, left17
random step count = 8192
trace capacity = random step count + 32
scripted prefix = 20 actual transitions, no outer random draw
additional write-byte random draws = original one per generated byte
initial output hash = 1469598103934665603
output hash multiplier = 1099511628211
```

Preserve the complete reference, callback bus/copy state, all nine progress
counters, `MODEL_EXPECT` first failure, full operation trace and final
`TTY_MODEL_PASS` formatting. The random and scripted tables are actual
original data, not enum-adjacency rules or generated reference expectations.
The final resident-output drain and original `expect_lf` closure stay real.

The generator keeps original descriptor roles and validation order
launcher/VM/PM/TTY/VFS, image IDs 101 through 105, actual segment bytes,
quota adjustments `+2,+1,+1,+1,+1`, and exact total. Its manifest emits VFS,
launcher, TTY, PM, VM in that order. Preserve report-symbol lookup and address,
the fixed UART-bound test, formatting/newlines, CLI flags, UTF-8 write,
exception boundary and failure prefix. No change to shared ELF rendering.

## Exact source sets and consumers

| Ordered set | Exact owners | Existing consumer operands |
| --- | --- | --- |
| `MICROS_TTY_HANDOFF_RUNTIME_SOURCES` | Runtime root, state, validation, transitions. | `micros`, `tty_test`: two. |
| `MICROS_TTY_SERVICE_CORE_SOURCES` | Service root, validation, read, write, request. | `tty_service_test`, `tty_model_test`, `micros_add_bootstrap_user(micros_tty_service ...)`: three. |
| `MICROS_TTY_MODEL_TEST_SOURCES` | Model root, common, reference, I/O, protocol, operations, validation. | `tty_model_test`: one. |
| `MICROS_TTY_HANDOFF_HOST_TEST_SOURCES` | Handoff root, fixture, device, runtime. | `tty_test`: one. |
| `MICROS_TTY_CONTROL_SYSCALL_HOST_TEST_SOURCES` | Control root, fixture, observation, controller. | `tty_control_syscall_test`: one. |

Exactly five sets and eight operands. Existing guards, target definitions,
other source operands, native entry/CTest names, image embedding and Python
generator dependencies stay unchanged. No source glob or source-selection
policy is introduced.

The whole recipe delta must invert to reviewed current digest
`4e370ba5578b0b114a51d2d5ae738320ce7503a3d51bd7c2e6fb2623284981b3`
before updating only the admitted current recipe literal. Earlier Grant and
composed Object/Endpoint/Scheduler/Trap/RAMFS recipe/source oracles retain
their expected values under this one additional finite delta.

## Independent validation dimensions

Five exact aliases inherit their original representative in all dimensions:

| Family | Full tier | QEMU | Separate slow model | Images |
| --- | --- | --- | --- | --- |
| Runtime C/header | No | UART console, TTY, RAMFS, VFS, ELF spawn | None | None |
| Service C/header | No | TTY, RAMFS, VFS, ELF spawn | TTY model | TTY service image |
| Model C/header | No | None | TTY model | None |
| Handoff native C/header | No | UART console, TTY, RAMFS, VFS, ELF spawn | None | None |
| Captured-control native C/header | No | UART console, TTY, RAMFS, VFS, ELF spawn | None | None |

Unmoved UART component retains UART-console-only ownership; unmoved native
control retains the existing five QEMU workflows and no separate model/image.
The unchanged generator path retains four TTY/RAMFS/VFS/ELF workflows and
no separate model/image. The new structural gate is host-unit-only.

Only those five declarative aliases, the host-only path and one recipe pin
may change tool metadata. Preserve entire prior planner/context algorithms
and their retained source/AST oracles through the finite added projections.
No supported profile, cache rule, execution, receipt, environment, timeout or
cross-HEAD equivalence change is authorized.

## Implementation and acceptance barrier

The implementation adds the host-only structural contract gate
`tests/host/test_tty_source_layout.py`; this design does not create it.
The following directly coupled neighbors remain byte-exact:

```text
include/micros/tty.h
servers/tty/tty_core.c
servers/tty/tty_core.h
servers/tty/tty_service_core.h
servers/tty/tty_uart.c
servers/tty/tty_uart.h
servers/tty/tty_service.c
servers/tty/tty_control.c
servers/tty/tty_control.h
kernel/tty_handoff_core.c
kernel/tty_handoff_core.h
kernel/tty_handoff_runtime.h
kernel/tty_control_core.c
kernel/tty_control_core.h
kernel/tty_control_syscall_core.c
kernel/tty_control_syscall_core.h
kernel/tty_control_syscall.c
kernel/tty_control_syscall.h
kernel/tty_interrupt.c
kernel/tty_interrupt.h
kernel/tty_fault.c
kernel/tty_fault.h
kernel/plic.c
kernel/plic_core.c
kernel/plic.h
kernel/uart_console_core.c
kernel/uart_console_core.h
kernel/uart_console_test.h
kernel/tty_service_test.c
kernel/tty_service_test.h
kernel/tty_service_test_fixture.h
tests/qemu/tty_service_vfs.c
tests/qemu/tty_service_claim_wait.S
tools/check_user_elf.py
tools/generate_bootstrap_fixture.py
tools/run_qemu_smoke.py
CMakePresets.json
```

These are required retained artifacts, not optional examples. Other
unrelated files remain outside scope. Prior structural gates may gain only
the exact added recipe/metadata composition; their expected source/AST/hash
values remain unchanged.

Freeze immutable reviewed Git inventories before any implementation rewrite:
156 complete C bodies and whole-family signatures/orders, 116 owner moves,
seven original long C tasks, the three complete Python functions and actual
render data, static/automatic storage, initializer and callback/jump lifetimes,
real consumers, full recipe and all independent validation dimensions.

Typed actual source/AST/data expansion needs independent positive and negative
fixtures for field types/extents, original initializers and pointers, enum
and error ordering, root jumps/IRQ/report scope, model draws/trace/callbacks,
exact generated bytes and all recipe/classification composition. Do not
reinsert unchecked old bodies or regenerate expected values from rewritten
output. The shared C function inspector remains unchanged.

Use small green slices and only their narrow existing native/model or real
target gates. Both owner/task migrations must finally be literal empty;
all 26 C owners must be real and all functions bounded. Stage the complete
change and record current HEAD/tree, clean tracked/index state and empty
untracked output before final acceptance.

Canonical configure/build steps close ordinary generated dependencies and
all three supported reusable profiles before final recording. Mere file
existence or one successful collector call is not a substitute for a stable
normal build closure. Collectors never build, install, normalize the
environment or waive admission. Run the complete current authoritative fresh
PR plan and additional bounded actual native/smoke/TTY-image reuse from the
same candidate and original full producer, with exact complete-log provenance.

Independent review reads the complete current change and exact author
evidence without routine suite/model/QEMU/doc reruns. Correct only justified
findings and review to clean. Squash only the reviewed head, verify identical
tree, fast-forward main and delete the exact obsolete local/remote branch.
Old-head receipts remain historical after squash.

## Non-goals

No terminal/line discipline, request, reply, grant, UART/PLIC, mapping,
ownership, readiness, deadline, timer, interrupt, model, topology or output
change. No new public contract, allocation, concurrency, recovery, worker,
retry loop, rollback, generalized framework, path migration, QEMU tooling,
production application startup or shell work is included.

## Current implementation progress

The restarted foundation first reproduced structural Red with both byte
primitives still in the original runtime facade. Independent exact recipe
and metadata inverse fixtures passed before reintroducing the bounded
source slice. The immutable reviewed `ac94a211` inventories remain the
source of all 156 complete bodies/seven signature families, original
orders/storage, seven long C tasks, three Python ASTs/data, 37 stable
neighbors, complete recipe and shared/tool facts.

Only the two complete byte primitives move to the state leaf in this slice;
the original authoritative objects still remain in the facade. All 26 paths,
five private headers/source sets and eight consumer operands are wired once,
with eighteen future C units explicitly declarations-only. Five family
aliases and one host-only gate input preserve all four validation dimensions.
The complete recipe inverse restores `4e370ba5...` before the sole current
digest `730a6dd6e29ff12cf191d3040ae607026da790be7945c94a6966c24f03c42fab`.

Current narrow evidence is newly executed: 227 source/tool contract tests,
all four native TTY consumers with the original model seed/steps/trace,
and both actual UART and TTY guests pass. There are 114 original moves and
all eight semantic dispositions remaining. Complete current author/reuse
and independent implementation review remain final barriers.

The next restarted green slice completes all 27 runtime original moves.
One state-leaf context owns exactly the original two objects; 26 public
entries borrow it once, and only two stateful private helpers gain its
explicit pointer. Pure signatures remain unchanged. All 35 complete original
bodies/types, acyclic owner directions, native runtime cases and both actual
UART/TTY guests pass on this candidate. There are 89 original moves and
eight semantic dispositions remaining.

Live binding now has the original 89-line resolution/selector root and a
40-line void prepared/active phase. The five original uninitialized locals
remain caller-owned with exact types; actual original-body restoration and
independent ordering/binding/lifetime fixtures precede the outline.
Current native and both real target paths pass. There are 89 moves, sixteen
declarations-only future C owners and seven semantic dispositions remaining.

All seventeen service moves are now reintroduced through fresh ownership
Red and independent complete private-signature fixtures. Twenty original
bodies/types occupy the five stateless owners; ten private exports, one
original magic and the acyclic graph are exact. The request byte helper's
private spelling avoids the retained entry's ELF symbol. Current service,
model and real TTY gates pass. There are 72 moves, twelve declarations-only
future C owners and seven semantic dispositions remaining.

The write-publication outline is now reintroduced through its own fresh Red
and independent order/type/binding/initialization fixtures. All rejection,
retry, grant and acceptance checks remain in the 106-line root; a 29-line
void phase performs only original accepted publication, using exactly the
original three uninitialized caller locals. Current whole-body proofs,
native service/model and real TTY pass. There are 72 original moves and
six semantic dispositions remaining.

All 33 model moves now have their seven exact owners. One automatic root,
its real bus/copy self-pointers and callback bindings, all 38 complete bodies
and signatures, original types/enums/macro and two readonly common-owner
endpoint values remain exact. Independent graph fixtures admit only the
original final reference-output predicate under merged ADR-0071 and reject
reverse or mutation dependencies. The current native model retains the
original seed, 8,194 recorded steps and `0xd962296c803b3115` output hash.
There are 39 original moves, six declarations-only C owners and six
semantic dispositions remaining.

Model validation now has a 117-line root and a 60-line file-local resident
input/echo/write comparison phase. Caller work retains the original two
typed pointer initializers and uninitialized loop index. A plain boolean
guard propagates failure without adding a second `MODEL_EXPECT` diagnostic;
production/callback checks precede the phase and original request, CR/LF and
final reference-output observations follow it. Complete original-body
restoration and independent scope/order/failure fixtures precede C changes.
Current model trace is unchanged. There are 39 moves and five semantic
dispositions remaining.

All seven native handoff moves now have their four exact owners. The fixture
leaf groups only the original five static objects, with a non-resetting
getter. Only the three stateful resolver doubles and runtime case borrow it;
pure doubles and the device case gain no ambient state. All sixteen complete
original bodies/types and the exact failure macro reconstruct, and actual
native device/lifecycle/deadline/panic cases pass. There are 32 original
moves, three declarations-only owners and five semantic dispositions remaining.

Device classification now has a 79-line exact/range root and a 79-line
terminal exclusion phase. The four original readonly constants remain in
root and pass by their complete typed values. Only the original authority
initializer and uninitialized classification occupy caller work; executable,
alias, foreign, physical-substitution, managed and out-of-phase sentinel
observations retain their order. Full original-body/type restoration and
independent fixtures precede the outline; actual native cases pass.
There are 32 original moves and four semantic dispositions remaining.

The native runtime case now has a 50-line root and six bounded phases:
record seeding, initial authority, begin/mapping, release/commit,
claim/readiness and completion/corruption/panic. Four original constants
remain in root; caller work has exactly eighteen original typed locals and
eight original initializers, with binding/entry/transition/context pointers
still addressing their actual enclosing objects. The sole fixture borrow,
deadline 150, before/after sentinel and every original failure/commit order
reconstruct from actual source before C publication. All current native
cases pass. There are 32 original moves and three semantic dispositions left.

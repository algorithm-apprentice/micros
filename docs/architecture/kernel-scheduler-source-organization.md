# Kernel Scheduler Source Organization

This is the exact design contract for the six scheduler sources in
[the source-module audit](source-module-audit.md).
[ADR-0062](../adr/0062-kernel-scheduler-source-decomposition.md) is Accepted
after a clean independent design review. Its separate design merged as PR #123,
commit `dac947c839455900264c338c5c09952f68e147c7`. Implementation now exposes all
specified owners and phases on a fresh branch from that merged outcome.

## Base, outcome, and non-goals

```text
reviewed micros base: e5b4da6e2fd33f752cfdadc7beb77f0ebf9b025b
fixed MINIX reference: 4db99f4012570a577414fe2a43697b2f239b699e
design task: kernel-scheduler-decomposition-design
implementation task: kernel-scheduler-decomposition-implementation
```

The source-audit predecessor is complete; the current base also contains the
reviewed trap/syscall implementation, PR #122. This is one dependency-ready
maintenance outcome, not a new runtime DAG node.

Expose existing queue, transaction, validation, selection, accounting, target,
fixture, reference, and projection work. The
[fixed MINIX trace](../research/minix-kernel-scheduler-source-organization.md)
defines responsibility direction. Accepted ADRs
[0026](../adr/0026-minix-baseline-kernel-scheduler.md),
[0027](../adr/0027-return-boundary-timer-rearming.md), and
[0035](../adr/0035-riscv-ipc-syscall-and-bootstrap-buffers.md)
continue to define behavior.

No policy, priority, quantum, timer, IPC, identity/layout, mapping, privilege,
fault, allocation, reset, cache, retry, recovery, concurrency, migration,
SCHED server, directory, validation-reuse, image, protocol, or test-execution
change is allowed. Application startup, dynamic PID-1 integration, and the
other audit corrections remain separate tasks.

At the reviewed base:

| Source | Physical lines | Definitions | Functions above 120 lines |
| --- | ---: | ---: | ---: |
| `kernel/scheduler_core.c` | 2,396 | 60 | 3 |
| `kernel/scheduler.c` | 1,208 | 34 | 1 |
| `kernel/scheduler_test.c` | 1,394 | 28 | 3 |
| `kernel/scheduler_invalid_test.c` | 441 | 11 | 1 |
| `tests/host/scheduler_test.c` | 1,125 | 13 | 1 |
| `tests/host/scheduler_model_test.c` | 1,337 | 17 | 3 |
| **Total** | **7,901** | **163** | **12** |

Counts use the existing lexical function-definition helper, including
`main` and the complete macro-containing 253-line model prelude.

## Stable artifacts and interfaces

Keep these seventeen artifacts byte-for-byte unchanged:

```text
include/micros/scheduler_core.h
include/micros/scheduler.h
include/micros/kernel_objects.h
include/micros/user_execution.h
include/micros/timer.h
kernel/scheduler_core_internal.h
kernel/kernel_object_runtime_internal.h
kernel/ipc_buffer_internal.h
kernel/kernel_objects.c
kernel/timer.c
tests/host/ipc_current_guard_test.c
arch/riscv64/scheduler_context.h
arch/riscv64/trap_context.h
arch/riscv64/scheduler.S
arch/riscv64/scheduler_test.S
arch/riscv64/user_entry.S
arch/riscv64/trap.S
```

The first implementation gate records reviewed-base hashes, never hashes
derived from rewritten sources. Preserve every supported declaration,
enum/value, capacity, field layout, compile-time guard, assembly symbol,
instruction label, and delegated IPC/buffer/context/root interface.
The existing `scheduler_core_internal.h` is a neighbor contract, not the
place for new extraction-only declarations.

Proposed [ADR-0068](../adr/0068-kernel-object-source-decomposition.md) defines
the next object correction. Only after its independent design acceptance
and separate merge may the physical `kernel/kernel_objects.c` freeze above
use the exact current-source inverse from the
[object contract](kernel-object-source-organization.md). Its original
`453c3cfbfb02d1c014226a315106a157676a7b195be06f561bb3abbda042db4a`
hash remains independently enforced; every other artifact above stays
byte-exact. No implementation is authorized by the Proposed record.

Panic/assertion source locations follow their actual owners. Do not spoof
`__FILE__`, `__LINE__`, or instruction labels. Any directly coupled
source-bound expectation must follow the real moved call site without
weakening reason, record, origin, register, or timeout evidence.

## Exact ownership and build sets

Define the following six complete ordered sets. Each listed C source appears
once in its set, and no consumer lists a split member beside the set.

```text
MICROS_SCHEDULER_CORE_SOURCES
  kernel/scheduler_core.c
  kernel/scheduler_core_queue.c
  kernel/scheduler_core_validate.c
  kernel/scheduler_core_policy.c
  kernel/scheduler_core_ipc.c
  kernel/scheduler_core_ipc_transition.c
  kernel/scheduler_core_return_plan.c
  kernel/scheduler_core_return.c
  kernel/scheduler_core_accounting.c

MICROS_SCHEDULER_RUNTIME_SOURCES
  kernel/scheduler.c
  kernel/scheduler_context.c
  kernel/scheduler_completion.c
  kernel/scheduler_return.c
  kernel/scheduler_interrupt.c
  kernel/scheduler_idle.c
  kernel/scheduler_test_support.c

MICROS_SCHEDULER_QEMU_TEST_SOURCES
  kernel/scheduler_test.c
  kernel/scheduler_test_state.c
  kernel/scheduler_test_fixture.c
  kernel/scheduler_test_completion.c
  kernel/scheduler_test_interrupt.c
  kernel/scheduler_test_trap.c
  kernel/scheduler_test_finish.c

MICROS_SCHEDULER_INVALID_QEMU_TEST_SOURCES
  kernel/scheduler_invalid_test.c
  kernel/scheduler_invalid_test_state.c
  kernel/scheduler_invalid_test_fixture.c
  kernel/scheduler_invalid_test_observation.c

MICROS_SCHEDULER_HOST_TEST_SOURCES
  tests/host/scheduler_test.c
  tests/host/scheduler_test_fixture.c
  tests/host/scheduler_queue_test.c
  tests/host/scheduler_return_test.c
  tests/host/scheduler_accounting_test.c

MICROS_SCHEDULER_MODEL_HOST_TEST_SOURCES
  tests/host/scheduler_model_test.c
  tests/host/scheduler_model_fixture.c
  tests/host/scheduler_model_reference.c
  tests/host/scheduler_model_projection.c
  tests/host/scheduler_model_prelude.c
  tests/host/scheduler_model_operations.c
```

These are exactly 38 C sources. Use exactly six new declarations-only private
headers:

```text
kernel/scheduler_core_private.h
kernel/scheduler_runtime_internal.h
kernel/scheduler_test_internal.h
kernel/scheduler_invalid_test_internal.h
tests/host/scheduler_test_internal.h
tests/host/scheduler_model_internal.h
```

Use directory-qualified includes to distinguish target and native test
headers. Headers contain types, constants, prototypes, and only the existing
native assertion/failure macros. They contain no object definitions,
functions, inline algorithms, textual C includes, or new executable macros.
Cross-file helpers gain their family prefix and one private declaration;
local helpers stay static. No public forwarding-only abstraction is needed.

### Portable responsibility disposition

The following original identities determine owners. Family prefixes may
change private spelling, not public signatures or logical ownership.

| Final suffix after `kernel/scheduler_core` | Original functions and responsibility |
| --- | --- |
| `.c` | `thread_handles_equal`, `hart_handles_equal`, `thread_handle_is_null`, `hart_handle_is_null`, `null_thread_handle`, `resolve_thread_mutable`, `resolve_hart_mutable`, `thread_is_current`, `hart_has_assigned_thread`, `add_overflows`, `bitmap_clear`, `bitmap_set`, `bitmap_test`: typed identity, membership queries, and bounded scalar/bitmap primitives. |
| `_queue.c` | `queue_dequeue`, `queue_enqueue_tail_raw`, `queue_enqueue_tail`, `queue_pick`, `preflight_tail_enqueue`, `queue_enqueue_tail_prevalidated`, `queue_enqueue_head_prevalidated`: ready-link mechanics and their preflight/commit boundary. |
| `_validate.c` | `micros_scheduler_core_validate` and its named whole-state validation phases. |
| `_policy.c` | `micros_thread_scheduler_admit_preflight`, `micros_thread_scheduler_admit`, `micros_thread_scheduler_hold`, `micros_thread_scheduler_remove`, `micros_thread_runtime_flags_set`, `micros_thread_runtime_flags_unset`, `micros_thread_install_policy`, `micros_hart_pick_ready`: existing checked admission, flags, policy, and direct selection. |
| `_ipc.c` | `current_ipc_guard_is_zero`, `clear_current_ipc_guard`, `ipc_blocked_flags_are_valid`, `micros_scheduler_preflight_current_ipc`, `micros_scheduler_begin_current_ipc`, `micros_scheduler_rollback_current_ipc`, `micros_scheduler_commit_current_ipc`: reversible current-thread custody. |
| `_ipc_transition.c` | `ipc_transition_clear_flags_are_valid`, `ipc_transition_set_flags_are_valid`, `micros_scheduler_commit_ipc_transitions`, `micros_scheduler_prepare_ipc_wake`, `micros_scheduler_commit_ipc_wake_prevalidated`, `micros_scheduler_commit_ipc_wake`, `micros_scheduler_commit_ipc_wake_pair`, `micros_scheduler_commit_ipc_call_delivery`, `micros_scheduler_commit_ipc_reply_receive_wait`, `micros_scheduler_commit_ipc_reply_receive_delivery`: all-record preflight and input-order commits. |
| `_return_plan.c` | `scratch_load`, `scratch_remove`, `scratch_head`, `scratch_tail`, `scratch_pick`, `micros_hart_plan_user_return`: the sole existing workspace, repair, renewal, selection, and complete candidate publication. |
| `_return.c` | `plans_equal`, `micros_hart_commit_user_return`, `micros_scheduler_apply_return_plan`: exact recomputation/comparison and bounded prevalidated application. |
| `_accounting.c` | `micros_scheduler_accounting_initialize`, `micros_scheduler_account_user_trap`, `micros_scheduler_account_idle_trap`, `micros_scheduler_account_enter_thread`, `micros_scheduler_account_enter_idle`: owner/counter transitions. |

Identity primitives are below queues and validation. Queue primitives may use
identity queries but never policy, IPC, planning, or full validation.
Validation calls `micros_kernel_objects_validate_base`, not the recursive
combined object validator or an operation handler. Policy and IPC use
validation/queue leaves; planning uses validation/identity and its private
workspace; checked return uses planning; apply uses only prevalidated stores.
Accounting uses complete validation only where the current implementation
does; user/idle trap charging retains its minimal resolve route.

### Target-runtime disposition

| Final owner | Original functions and responsibility |
| --- | --- |
| `kernel/scheduler.c` | Retain `micros_scheduler_initialize`, `micros_scheduler_is_initialized`, `micros_scheduler_admit`, `micros_scheduler_prepare_start_timer`, `micros_scheduler_start`: initialization, admission, first-entry orchestration. |
| `kernel/scheduler_context.c` | Sole seven-field state; `map_object_error`, `authoritative_objects`, `copy_user_context`, `clear_bytes`, `validate_selected_thread`, `load_selected_context`, `preflight_kernel_interval`, `activate_prevalidated_root`, `enter_selected_thread`, `panic_scheduler_invariant`. |
| `kernel/scheduler_completion.c` | `preflight_selected_completion`, `commit_selected_completion`, `panic_completion`, `preflight_completion_or_panic`, `commit_completion_or_panic`, guarded `micros_scheduler_test_rejects_malformed_completion`. |
| `kernel/scheduler_return.c` | `panic_invalid_context`, `select_user_return`, `micros_scheduler_select_user_return`, `micros_scheduler_select_captured_user_return`: outgoing custody and selected/idle return phases. |
| `kernel/scheduler_interrupt.c` | `micros_scheduler_user_trap_enter`, `handle_timer_result`, `micros_scheduler_handle_user_timer`, `micros_scheduler_enter_supervisor_interrupt`, `micros_scheduler_handle_supervisor_timer`: origin/accounting/timer adaptation, not selection. |
| `kernel/scheduler_idle.c` | `micros_scheduler_idle_select`, `micros_scheduler_idle_accounting_panic`: post-wait idle/selected return and unchanged fatal boundary. |
| `kernel/scheduler_test_support.c` | Guarded `micros_scheduler_test_enter_without_timer`, `micros_scheduler_test_prepare_supervisor_return`, with every existing build exclusion/inclusion. |

State/context primitives are below completion and orchestration. Completion
does not call start, selection, or idle. Start/return/idle use common context
and completion primitives, not one another's higher-level handler.
Interrupt adaptation does not select a thread. Existing conditional public
test hooks remain at the same phases; they do not introduce production
dependencies on evidence-private headers.

### Target evidence disposition

| Final owner | Original functions and responsibility |
| --- | --- |
| `kernel/scheduler_test.c` | Retain `micros_scheduler_runtime_run_self_test`, assembly callback `micros_scheduler_test_start_production`, and the two unchanged C idle-bypass ABI objects; entry/failure orchestration. |
| `kernel/scheduler_test_state.c` | Sole normal-test context, constants, `thread_handles_equal`, `copy_bytes`, `bytes_equal`, `clear_bytes`, `read_satp`, `fill_context_pattern`, `user_address_of`, `current_thread_index`. |
| `kernel/scheduler_test_fixture.c` | Setup phases; `snapshot_start_state`, `start_state_matches`; exact initialization, two process/image/context preparations, malformed completion and failed-start proof. |
| `kernel/scheduler_test_completion.c` | `completion_state_is_clear`, `observe_completion`, `expect_no_message_completion`, `expect_message_completion`, `completion_test_mismatch`, `user_registers_match`, `micros_scheduler_test_after_start`, `micros_scheduler_test_after_user_return`. |
| `kernel/scheduler_test_interrupt.c` | `test_uart_interrupt_enable`, `arm_external_interrupt`, `run_supervisor_external_test`, `micros_scheduler_test_handle_external_interrupt`, `micros_scheduler_test_note_selector_entry`, `micros_scheduler_test_handle_idle_timer`. |
| `kernel/scheduler_test_trap.c` | `micros_scheduler_test_handle_user_trap` and its ordered ecall, captured-completion, timer, and idle-request phases. |
| `kernel/scheduler_test_finish.c` | `finish_test`: final interrupt/proof checks, per-process teardown, resource reconciliation, exact output/shutdown. |
| `kernel/scheduler_invalid_test.c` | Retain `micros_scheduler_invalid_runtime_run_self_test` and `micros_scheduler_test_start_production`; entry/failure orchestration only. |
| `kernel/scheduler_invalid_test_state.c` | Sole invalid-test context, constants, `copy_bytes`, `bytes_equal`, `read_satp`, `user_address_of`, `fill_context_pattern`. |
| `kernel/scheduler_invalid_test_fixture.c` | Existing two-process/image/context setup, scheduler admission, and next-context corruption phases. |
| `kernel/scheduler_invalid_test_observation.c` | `micros_scheduler_invalid_test_before_user_timer`, `micros_scheduler_invalid_test_after_user_timer`, `micros_scheduler_invalid_test_handle_user_trap`, `micros_scheduler_invalid_test_report`. |

Each fixture/context is below its setup, observation, action, and finish
owners. Normal trap/interrupt actions may stage completions through the
completion owner; completion observes but never invokes those actions.
Finish may invoke the existing supervisor-external proof, never setup.
Invalid evidence does not borrow or include the normal evidence context.

### Native evidence disposition

| Final owner | Original functions and responsibility |
| --- | --- |
| `tests/host/scheduler_test.c` | Retain `main`, case descriptors, and output/short-circuit loop. |
| `tests/host/scheduler_test_fixture.c` | Sole functional fixture; `handles_equal`, `context_pattern`, `setup_fixture`. |
| `tests/host/scheduler_queue_test.c` | `test_priority_queues_and_tail_order`, `test_runtime_flag_zero_boundary`, `test_policy_replacement_is_atomic`, `test_corrupt_queue_state_is_rejected`. |
| `tests/host/scheduler_return_test.c` | `test_current_selection_and_preemption_repair`, `test_quantum_rotation_and_nonpreemptible_current`, `test_current_block_and_stale_plan_are_atomic`. |
| `tests/host/scheduler_accounting_test.c` | `test_accounting_owners_are_separate`, `test_trap_accounting_uses_minimal_route`. |
| `tests/host/scheduler_model_test.c` | Retain `next_random` and `micros_scheduler_model_test_run`; seed/draw/projection/coverage orchestration. |
| `tests/host/scheduler_model_fixture.c` | Sole separate native model fixture; `context_pattern`, `setup_fixture`, `initialize_model`. |
| `tests/host/scheduler_model_reference.c` | `handles_equal`, `handle_is_null`, `queue_find`, `queue_remove`, `queue_head`, `queue_tail_without_preemption`, `queue_tail`, `pick_ready`, `admit`, `plan_and_commit`: independent expected transitions and existing native/reference actions. |
| `tests/host/scheduler_model_projection.c` | `model_matches`: complete validation, per-hart accounting/current/queue and per-thread state/policy/link projection. |
| `tests/host/scheduler_model_prelude.c` | `run_required_prelude` and its exact ordered deterministic phases. |
| `tests/host/scheduler_model_operations.c` | The existing ten mixed-operation arms, their owner/state/error guards, reference updates, trace labels, and counter increments. |

Native cases depend on their fixture, never sibling cases. The model fixture
is below reference, projection, prelude, operations, and driver. Projection
uses only read-only reference queries (`handle_is_null`, `handles_equal`,
`queue_find`); reference transitions do not call projection. Prelude and
operations use reference transitions; only the driver invokes projection
after prelude and every step. No model includes production-private or
functional-fixture headers or derives expected state from production links.

State/fixture leaves do not contain the higher-level entry, main, or model
driver. This prevents an entry-to-action-to-fixture back-edge into the entry
module. Existing public evidence hooks and native case entries obtain only
the matching leaf's borrowed context; ordinary phase helpers receive it
explicitly.

### Consumer preservation

Every existing `kernel/scheduler_core.c` consumer expands the complete core
set once, including all native object/endpoint/IPC/grant/lifecycle consumers.
`micros` expands both core and runtime sets once alongside unchanged assembly.
Its current normal-scheduler branch alone adds the normal evidence set;
the two invalid branches share only the invalid evidence set and existing
assembly under their current mutually exclusive image selection.

The two `micros_scheduler_test_start_production` definitions remain exclusive
to their existing image variants; do not combine the normal and invalid sets.

The existing `scheduler_test` executable retains this relative composition:
`kernel/kernel_objects.c`, core set, unchanged
`tests/host/ipc_current_guard_test.c`, model set, functional set.
Preserve C17, repository/include search roots, warnings, profile definitions,
optional ASan/UBSan/frame-pointer compile/link flags, CTest `scheduler-core`,
and its existing fast/full tier membership. No object library, new target,
CTest, preset, or run is introduced.

## Storage and lifetime inventory

There are five consolidations, not five new stores of authority:

| Owner | Existing mutable private fields | Required lifetime |
| --- | ---: | --- |
| `scheduler_context.c` | 7 | One static target-runtime context. |
| `scheduler_test_state.c` | 48 | One static normal-target evidence context; `last_running` starts at 2. |
| `scheduler_invalid_test_state.c` | 11 | One static invalid-context evidence context. |
| native `scheduler_test_fixture.c` | 4 | One static functional fixture, reset at the exact existing calls. |
| native `scheduler_model_fixture.c` | 4 | One separate static native fixture, reset once per model invocation. |

The runtime fields are exactly `scheduler_initialized`,
`scheduler_timer_enabled`, `scheduler_start_timer_prepared`,
`scheduler_preemption_interval`, `selected_context`, `selected_root`, and
`scheduler_kernel_root`. Current identity, queues, accounting, and timers
remain in the authoritative object registry, not in this context.

The normal target fields are exactly:

```text
processes[2], threads[2], endpoints[2], registry, prepared_contexts[2]
code_physical[2], counter_physical[2], stack_physical[2]
kernel_stack_bottom[2], kernel_stack_top[2], baseline_owned, baseline_free
previous_counter[2], ecall_count[2], timer_trap_count, switch_count
last_running, supervisor_returned, start_boundary_observed
start_completion_observed, idle_requested, idle_woke_thread
spurious_idle_bypassed, selector_entry_count, selector_entries_before_spurious
kernel_ticks_before_start, start_snapshot, start_satp_snapshot
start_sie_snapshot, start_stie_snapshot, start_timer_attempts
expected_completion[2], expected_completion_result[2], expected_message
message_completion_deferred, message_completion_observed
captured_completion_observed, error_completion_observed
error_completion_staged, idle_completion_observed, external_origin
user_external_started, user_external_observed, user_external_return_observed
supervisor_external_observed, idle_external_started, idle_external_observed
idle_external_completion_observed
```

Preserve every original type/extent and `external_origin`'s volatile
qualification. `registry` remains a borrowed pointer to the authoritative IPC
registry. `start_snapshot` is evidence, not a second live registry.

The invalid fields are exactly `processes[2]`, `threads[2]`,
`code_physical[2]`, `counter_physical[2]`, `stack_physical[2]`,
`kernel_stack_bottom[2]`, `kernel_stack_top[2]`, `timer_objects_snapshot`,
`scheduler_snapshot_candidate`, `timer_frame_snapshot`, and
`timer_satp_snapshot`, with original types/zero initialization.

Each native fixture contains only its original `objects`, `process`,
`threads[8]`, and `harts[2]`. Keep the two fixture builders and context-pattern
policies separate; their context bases differ. Do not merge them merely
because their shapes match.

`plan_scratch` remains the sole static `scheduler_plan_scratch`, now in
`scheduler_core_return_plan.c`, with exactly the existing priority-by-capacity
`uint16_t slots` and `size_t counts` arrays. It is serialized scratch, not
authoritative queues, a cache, or a second registry. Do not copy it onto the
16 KiB supervisor stack or add a second workspace/reset.

These ABI definitions remain separate and unchanged:

```text
C owner kernel/scheduler_test.c:
  volatile uint64_t micros_scheduler_test_idle_bypass_once
  volatile uint64_t micros_scheduler_test_idle_bypass_observed
assembly owner:
  micros_scheduler_test_saved_state[17]       (136 bytes)
  micros_scheduler_test_restored_state[17]    (136 bytes)
  micros_scheduler_test_immediate_status      (8 bytes)
```

Both non-returning C entry roots retain their automatic saved IRQ status and
the call to `micros_scheduler_test_enter`. The normal supervisor continuation
must return into that same live invocation before finish. Do not move capture
into a helper whose frame has already returned.

Automatic validation `seen`/`current_seen` arrays, the IPC transition candidate
array of thread capacity, return plans/candidates, completion/accounting
records, outgoing context, full `EXPECT_UNCHANGED` snapshots, accounting
`plan`/`rejected_plan`, model/reference arrays, seed/PRNG/step state, prelude
slot lists, projection cursors/counts/expected links, and operation locals
retain automatic duration. Private work records may name/transport these
existing values by reference; they do not duplicate full registries/plans,
retain pointers after return, or become mutable globals.

## Required state, ordering, and failure preservation

Portable operations preserve exact typed-generation resolution and current
validation precedence. Runnable means zero RTS flags and ready membership,
including current at its own priority head. First blocking set dequeues;
last clear tail-enqueues. Generic flag operations retain their current
PREEMPTED/IPC exclusions and current/accounting restrictions.

Current IPC begin validates completely, dequeues, sets INACTIVE, clears
current, selects the idle anchor, and records one guard. Rollback restores
head/current/thread anchor and consumes it. Successful commit leaves current
null, restores a runnable immediate caller to its original queue head,
validates blocked flags, and consumes it without selecting a user return.

Batch IPC preserves shape/duplicate checks before full validation, resolves
and preflights every candidate before any commit, then applies records in
input order with bounded stores. No sorting, partial rollback, late
fallible lookup, or early first-record mutation is introduced.

Whole-state validation preserves the base-object call, accounting-owner
checks, bounded generation-checked queue walks, unique current witnesses,
exact stack/hart attachment, unassigned canonical metadata, policy/remaining
quantum bounds, and final membership closure. Preserve the existing allowed
unscheduled-current bootstrap shape; do not tighten it during extraction.

Return planning clears the complete automatic candidate byte representation,
validates before workspace use, repairs PREEMPTED only in the candidate,
distinguishes remaining-time head restoration from exhausted tail placement,
and keeps the bounded renewal loop and non-preemptible rule. It publishes the
complete candidate only on success, including zeroed padding; aggregate
initialization is not a replacement for the current explicit byte clear/copy.
Checked commit recomputes and compares all existing plan fields before apply.
Prevalidated apply remains void, bounded, and non-failing.

User/idle trap charging does not add a full validator, context copy, queue
scan, mapping lookup, or timer operation. Preserve unsigned counter subtraction,
remaining-quantum saturation, exact overflow checks, and no mutation on
rejection. Kernel/idle time never deducts the selected thread's quantum.

Target return preserves these distinct paths:

1. Normal return validates/stores the outgoing frame; captured return requires
   both current-handle fields zero and does not capture/store again.
2. Plan without authoritative mutation, then either prepare idle or validate
   selected thread/root, preflight completion, and load/validate the exact
   selected context image.
3. Prepare the timer only after complete relevant preflight, then sample and
   preflight kernel accounting before externally visible return commit.
4. Commit bounded message chunks if present, patch that same image's `a0`,
   store it, clear completion, apply plan, activate root, install image, and
   publish thread accounting in the existing order.

No-message completion performs no user-memory lookup. A deferred completion
belongs to its thread until selected. Lost accepted mapping remains
`invalid-bootstrap-ipc-buffer`; malformed completion remains invariant
panic. No translation, recoverable error, or revalidation is added after the
first commit mutation.

First start retains its pre-timer accounting check and second fresh sample
after timer work: the earlier failure restores IRQ state and returns; the
later impossible failure is `scheduler-start-accounting` panic. Preserve
prepared versus inactive timer error distinctions and the after-start hook.

Idle selection preserves its different panic boundaries and empty-return
behavior. Before the initial idle pivot, install the kernel root, idle anchor,
and IDLE accounting; unchanged assembly rearms `sscratch` before stack pivot.
The wait executes `wfi`, enables then disables SIE, skips selection while
still IDLE, and calls C only after accounting changes to KERNEL.
S-origin timer/external handling does not replace the interrupted S-frame.
Bootstrap deadline checks and reschedule/test-hook conditions keep their
current placement.

## Twelve required long-function splits

| Original function | Lines | Required ordered named phases |
| --- | ---: | --- |
| core `micros_scheduler_commit_ipc_transitions` | 128 | Shape/duplicates; complete-state validation; all candidate preflight; input-order non-failing commit. |
| core `micros_scheduler_core_validate` | 272 | Base objects; automatic witnesses; per-hart accounting and queue topology; exact current/stack witness; per-thread canonical policy/flags/membership closure. |
| core `micros_hart_plan_user_return` | 185 | Complete candidate clear and preflight; workspace load; early-preemption repair; bounded selection/renewal; queue projection; success-only complete publication. |
| runtime `select_user_return` | 137 | Arguments/outgoing custody; plan; separate idle return; selected context/completion preflight; timer/accounting; bounded commit/install. |
| normal target `micros_scheduler_test_handle_user_trap` | 232 | Common context/counter proof; ordered ecall/idle/external phases; message then captured completion; timer/error completion; external start; two-thread hold/spurious-idle request. |
| normal target `finish_test` | 144 | Supervisor-external proof; complete accumulated evidence; per-process exact teardown; ledger/validator baseline; unchanged markers/shutdown/failure. |
| normal target `micros_scheduler_runtime_run_self_test` | 204 | IRQ/baseline/IPC setup; two process/image/context preparations; fence/admission; malformed completion; failed-start preservation/delay; original-frame assembly entry; finish/failure. |
| invalid target `micros_scheduler_invalid_runtime_run_self_test` | 125 | IRQ/objects/payload; per-process image/context; fence/admission; guarded next corruption; original-frame entry; unchanged fatal setup path. |
| native `test_accounting_owners_are_separate` | 176 | Fixture/admission/first selection; thread-owned rejection and unchanged-plan proof; two thread intervals; idle interval; exact fixture reset and unsigned-wrap interval. |
| model `model_matches` | 181 | Complete native validation; per-hart accounting/current; each priority queue's exact order/tail/termination; each thread's linkage, state, and policy. |
| model `run_required_prelude` | 253 | Hart-0 initial execution/preemption; hart-1 same/lower/higher placement; held/runnable policy and flag boundaries; exhaustion/rotation; hart-1 hold/idle/wake. |
| model `micros_scheduler_model_test_run` | 416 | Fixture/reference init; exact prelude/projection; one draw/decode per step; existing ten operation handlers; common complete projection; all eighteen coverage checks. |

No unchanged long-function exception is granted. Every resulting function is
at most 120 physical lines; renaming/moving/compressing alone is not a split.
The shorter first-start and idle-select roots also expose named
preflight/timer/accounting/commit work rather than hiding those separate
failure boundaries in a new monolith.

Preserve return values, goto targets, break/continue semantics, short-circuit
order, macro failure exits, and caller/callee scope. A helper returning false
or an error is not a successful caller continuation. A successful idle pivot
or user entry does not return.

## Native cases and replay contract

`main` retains these exact eleven case labels and order:

```text
priority queues preserve tail order
runtime flags follow zero boundary
policy replacement is atomic
current selection repairs preemption
quantum rotation retains nonpreemptible current
current block and stale plan are atomic
accounting owners are separate
trap accounting uses minimal route
corrupt queue state is rejected
seeded two-hart model
current IPC scheduler guard
```

Preserve first-failure short-circuit, `ok %zu - %s` stdout,
`not ok %zu - %s` stderr, and exit status. The last case remains the unchanged
neighbor source; the model is not a separate executable or CTest.

The accounting case retains both automatic plans and its exact reset:
initialize at 100, enter at 120, trap at 170, enter at 200, trap at 300,
enter idle at 330, trap idle at 380; then reset the same fixture, initialize
at `UINT64_MAX - 29`, enter at `UINT64_MAX - 9`, trap at 10.
Keep all intermediate totals, saturation, unchanged snapshots, and rejected
plan-byte checks, not merely final totals.

The model retains eight threads, two harts, 4,096 steps, and seed
`0x5c4ed026`. PRNG state is automatic and the exact recurrence is:

```text
state = (state * 1664525 + 1013904223) modulo 2^32
```

Draw once at each step 1 through 4,096, returning the updated state. Decode
slot as `(value >> 8) % 8`, hart from assigned reference state or
`(value >> 16) & 1`, operation as `value % 10`, policy priority as
`(value >> 20) & 15`, and quantum as `32 + (value & 63)`.
Operations remain hold, wake, set-NO_QUANTUM, clear-NO_QUANTUM, return,
enter-thread, user-trap, enter-idle, idle-trap, policy in that order.
Counter deltas remain `1 + (value & 15)`, `1 + (value & 31)`, and
`1 + (value & 7)` for the existing respective accounting arms.
Keep label assignment points, owner guards, rejected-operation errors,
skipped operations, reference-update timing, and draw count.

The required prelude remains ordered: admit 0/1/2 to hart 0 at priority
10/quantum 60; plan; counter +10/enter 0; +15/trap; admit 3 at priority
3/quantum 60; plan; admit 4/5 to hart 1 at priority 8/quantum 50 and 6
at priority 12; plan; admit 7 at priority 1/quantum 50; plan hart 1 then
hart 0; hold 1, set NO_QUANTUM, install blocked priority 10/quantum 70,
wake 1; install runnable 2 at priority 3/quantum 80; +5/enter 3,
+100/trap, plan hart 0; hold 4/5/6/7 in the existing local-slot loop;
plan hart 1 idle; +5/enter idle; +7/idle trap; wake 4/5/6/7 in order.
No PRNG draw occurs in this prelude.

Project complete native/reference state at step 0 after prelude and after
every randomized step, including skip/rejection. Preserve complete validator,
accounting/current, head/tail/order/termination, thread assignment/flags,
priority/quantum/remaining/hart, and ready-link comparisons. The projection's
per-hart cursors/counts and per-thread expected links remain automatic.

All eighteen final counters must remain nonzero, in their current check order:

```text
preempt_higher, enqueue_same, enqueue_lower, remove_middle
flag_first, flag_additional, flag_partial_clear, flag_final_clear
return_keep, return_switch, return_idle, quantum_renew
thread_to_kernel, kernel_to_thread, kernel_to_idle, idle_to_kernel
policy_runnable, policy_blocked
```

Preserve the exact failure payload:

```text
scheduler model seed=0x5c4ed026 step=%zu operation=%s value=0x%08x: %s
```

Keep `scheduler model coverage incomplete`, immediate false propagation,
independent expected transition math, and the two distinct fixture policies.
Do not replace the model with copied production logic or a new dispatcher
framework.

## Target acceptance preservation

Normal evidence keeps two roots, every register/stack/root and queue-head
check, complete completion-state clearing, first-start timer failure snapshot,
one programming-attempt increment, delayed-start accounting, six-switch
threshold, monotonic nonzero private counters, and all ecall/selector counts.
Preserve message deferral, captured guard begin/wake/commit, dead-endpoint
result, user/supervisor/idle external interrupts, forced spurious bypass,
timer wake, external-idle wake, exact supervisor restore, and teardown order.

Invalid evidence snapshots after user accounting and before timer handling.
It normalizes only the hart timer field when comparing complete objects;
frame, SATP, queue/current/stack ownership, accounting KERNEL, and outgoing
versus next identity remain exact. Do not permit other state changes merely
to make the invalid-context gate pass.

Retain these markers and each workflow's eight-second timeout:

```text
MICROS_IPC_RETURN completions=shared paths=start,user,captured,idle buffers=bounded
MICROS_SCHEDULER_TEST_PASS queues=minix-priority current=reachable accounting=separate switches=alternating idle=resumed registers=preserved
MICROS_TTY_TRAP_TEST_PASS user=cause9-scheduled supervisor=cause9-direct idle=cause9-selected
MICROS_SCHEDULER_INVALID_CONTEXT state=outgoing ownership=preserved accounting=kernel
MICROS_SCHEDULER_INVALID_CONTEXT state=next ownership=preserved accounting=kernel
MICROS_PANIC reason=invalid-bootstrap-user-context
```

Normal/invalid setup, completion-stage, start-returned, shutdown, and panic
failure records retain their values and terminal behavior. Do not relax the
QEMU parser or replace hardware evidence with a native proxy.

## Migrations, enforcement, and validation

The original-function disposition above moves exactly 138 definitions:

| Original source family | Moving definitions |
| --- | ---: |
| portable core | 47 |
| target runtime | 29 |
| normal target evidence | 26 |
| invalid target evidence | 9 |
| native functional cases | 12 |
| native model | 15 |
| **Total** | **138** |

The other 25 original definitions remain in their specified root owners.
New phase/borrow helpers do not count as original moves. The initial
structural Red records all 138 owner obligations and twelve long-function
obligations. During green extraction only the explicit pending entries may
retain their exact reviewed-base bodies/owners. Private renaming and verified
typed existing-context transport may be normalized; predicates, calls,
arguments, writes, return values, and control exits are not discarded.
Both active migration collections are literal empty collections at completion.

`tests/host/test_scheduler_source_layout.py` reuses existing
lexical/CMake helpers. It checks the exact 38/six/six inventory and every
consumer; stable hashes and supported definitions; all original owners;
private declarations/recursive dependencies; exact context fields/qualifiers/
initializers, scratch and ABI objects, automatic lifetimes; twelve semantic
outlines and short-root boundaries; case/model/script identities; CPP guards;
and validation inheritance.

All six source sets and private headers are integrated. Both active migration
collections are literal empty tuples; their frozen inventories retain all
138 original moves and twelve semantic splits. The largest C function is
111 lines. The five contexts, sole workspace, separate ABI objects, automatic
witnesses, and original live supervisor-return invocations retain their
specified ownership and duration.

The model driver now exposes ten explicit operations, checked dispatch,
common per-step projection, and final coverage. Its six guarded switch skips
become verified callee-local success returns: the dispatcher still leaves only
its selected case, and the driver still performs common projection. The gate
checks this scope/continuation separately from C predicates and writes, just
as it checks CPP guards and case labels separately from C statement facts.
Expected inventories remain derived from the immutable reviewed source, not
from the outlined implementation.

Run its focused implementation loop with:

```bash
python3 -m unittest tests.host.test_scheduler_source_layout
```

Use bounded concrete source checks for the critical minimal-accounting,
normal/captured, all-record-preflight, timer-before-commit, same-image
completion, full candidate publication, and caller error/exit obligations.
Include small independently expected positive/negative fixtures for these
checks, especially selector-mode flips, reordered commit, dropped failure
propagation, and local-return versus caller-exit scope. Do not derive a
fixture's expected behavior from the changing monolith, require Git/network
at test runtime, or build a general C interpreter. Structural evidence
supplements, never replaces, existing semantic evidence and full review.

Preserve all four current metadata dimensions independently:

| Original owner | Full tier | QEMU workflows | Separate slow / image metadata |
| --- | --- | --- | --- |
| `kernel/scheduler_core.c` | yes | All current 37 | `test-ipc-model` / empty |
| `kernel/scheduler.c` | yes | All current 37 | `test-ipc-model` / empty |
| `kernel/scheduler_test.c` | no | `test-qemu-scheduler` | empty / empty |
| `kernel/scheduler_invalid_test.c` | no | `test-qemu-scheduler-invalid-outgoing`, `test-qemu-scheduler-invalid-next` | empty / empty |
| `tests/host/scheduler_test.c` | yes | empty | empty / empty |
| `tests/host/scheduler_model_test.c` | yes | empty | empty / empty |

Every split source/header inherits its family's exact tuple. In particular,
recognizing a previously unknown native path must not narrow its full-tier
classification. The structural test itself is explicitly host-unit-only.
Do not add an image/slow execution merely because a native model is split.

Narrow extraction evidence uses existing `scheduler-core` and relevant
object/endpoint/IPC native cases, then existing scheduler, invalid-context,
timer, user-execution, captured-IPC, and buffer-loss-panic QEMU workflows as
appropriate. Run only gates invalidated by the current extraction.

The focused portable build and combined native selectors are:

```bash
cmake --preset host-debug
cmake --build build/host-debug --target kernel_objects_test scheduler_test endpoint_test
ctest --test-dir build/host-debug --output-on-failure -R '^(kernel-objects|scheduler-core|endpoint-profile)$'
```

Before independent implementation review, stage the complete change, prove
clean tracked/untracked task state, record HEAD/tree/exact commands/results,
and execute one authoritative PR plan. Shared CMake/production ownership
currently requires complete native validation, all 37 QEMU workflows, three
existing service images, documentation/structure, and three diff checks.
Do not separately repeat selected plan workflows. Reviewers inspect exact
author evidence rather than routinely rerun suites/builds/models/QEMU/docs.
Fix justified findings and rerun only invalidated checks before bounded
re-review.

## Implementation sequence

After the separate design merge, use a fresh implementation branch:

1. Structural Red, exact sets/private declarations, inherited classification,
   and initial 138/twelve migrations; smallest green integration.
2. Portable identity/queue/validation/policy/IPC/planner/commit/accounting
   owners, with automatic witnesses and the sole planning workspace.
3. Target context/completion, first start, normal/captured return, interrupt,
   idle, and guarded support owners with explicit failure/commit boundaries.
4. Normal and invalid target evidence contexts/phases while retaining entry,
   assembly ABI objects, snapshots, and full acceptance.
5. Native case and independent-model ownership, deterministic phase splits,
   exact resets/trace/projection/coverage.
6. Empty migrations, one complete current author plan, independent
   implementation review, and justified bounded corrections.

This design phase contains documentation only, not source skeletons or
executable prototypes.

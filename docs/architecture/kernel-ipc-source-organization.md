# Kernel IPC Source Organization

This is the behavior-preserving design contract for the eleven IPC correction
files identified by the source-module audit. It applies ADR-0051 to portable
IPC, captured syscall adaptation, native cases/reference models, and three
target evidence families.

The implementation follows Accepted
[ADR-0059](../adr/0059-kernel-ipc-source-decomposition.md), whose independently
reviewed design merged in
[PR #116](https://github.com/algorithm-apprentice/micros/pull/116).
The pre-extraction inventory below describes the reviewed base; the exact
owners, source sets, private contracts, and structural gate are implemented.

## Base, goal, and readiness

```text
reviewed micros base: 11c1c43a3fff4c3104eac3f5eb656ae27185a61a
fixed MINIX reference: 4db99f4012570a577414fe2a43697b2f239b699e
design task: kernel-ipc-decomposition-design
implementation task: kernel-ipc-decomposition-implementation
```

The [local baseline trace](../research/minix-kernel-ipc-source-organization.md)
classifies every relevant adaptation. Accepted ADR-0030/0035/0036/0037,
their later bootstrap/console/exit consumers, and the distinct-object and
scheduler contracts remain authoritative.

The outcome is explicit, acyclic responsibility ownership and named task
phases with unchanged behavior. This correction is not a new runtime DAG
node and does not make IPC consume a later service. Other source-audit and
kernel-directory tasks remain separate.

## Reviewed inventory

| Signal file | Physical lines | Functions | Longest function |
| --- | ---: | ---: | --- |
| `kernel/ipc.c` | 3,001 | 67 | `micros_ipc_reply_receive`, 173 |
| `kernel/ipc_syscall.c` | 422 | 8 | `micros_ipc_handle_captured_user_ecall`, 162 |
| `kernel/ipc_test.c` | 1,478 | 23 | `micros_ipc_runtime_run_self_test`, 984 |
| `kernel/ipc_syscall_test.c` | 2,069 | 35 | `transition_after_return`, 477 |
| `kernel/ipc_ecall_test.c` | 475 | 7 | `micros_ipc_ecall_core_runtime_run_self_test`, 182 |
| `tests/host/ipc_model_test.c` | 4,706 | 88 | `run_model_prelude`, 365 |
| `tests/host/ipc_notify_test.c` | 2,768 | 40 | `test_seeded_notify_model`, 329 |
| `tests/host/ipc_reply_test.c` | 2,209 | 29 | `test_reply_routes_exact_tokens_without_send_target`, 224 |
| `tests/host/ipc_close_test.c` | 1,792 | 28 | `test_seeded_close_scenarios`, 343 |
| `tests/host/ipc_call_test.c` | 1,214 | 20 | `test_call_rejections_are_atomic`, 195 |
| `tests/host/ipc_operation_test.c` | 932 | 18 | `test_receive_specific_and_any_fifo`, 143 |
| **Total** | **21,066** | **363** | **42 functions above 120 lines** |

These are review signals. Module/function boundaries follow authority,
preflight/commit, independent reference decisions, fixture, and scenario
responsibilities rather than line count.

## Implemented preservation evidence

Both migration collections are empty: all 70 public/case/hook owner moves
and all 42 long-function semantic splits are complete. The structural gate
checks the exact 105-file family, all eleven source sets and consumers,
private types/members/declarations/macros, recursive dependencies, every
file-scope/static-local storage owner, original cases/models, target script,
capture frame, markers, timeouts, and validation inheritance.

The existing acceptance-model execution retained seed `0x30accee5`, 8,192
transitions, and trace `0x65c2014aad08f70b` after the complete extraction.
All original production transition/validation call sites and all 93 checked
forced-prelude actions were retained. Notification and close retain their
original draw schedules, reference comparisons, and coverage. No additional
behavioral execution was introduced.

All three existing QEMU scenarios passed their narrow extraction gates.
Supervisor register capture remains in each original non-returning entry
frame, and setup-failure helpers remain in the setup owner so the facade
does not acquire a reverse report dependency.

`ipc_model_test_actions.c` has 32 functions after exposing the thirteen
selection classes. It remains one cohesive action-construction/selection
module: it performs no reference transition or production operation, and
its individual tasks stay below the function review signal. This is a
function-count review disposition, not an exception for an unchanged long
algorithm.

The focused structural command is:

```bash
python3 -m unittest tests.host.test_kernel_ipc_source_layout
```

The authoritative final PR plan remains the gate described below.

## Stable surfaces and non-goals

Keep these eleven headers byte-identical:

```text
include/micros/ipc.h
include/micros/ipc_core.h
include/micros/ipc_abi.h
include/micros/ipc_runtime.h
include/micros/ipc_buffer.h
kernel/ipc_syscall.h
kernel/ipc_syscall_test.h
kernel/ipc_ecall_test.h
kernel/ipc_syscall_panic_test.h
kernel/ipc_runtime_internal.h
kernel/ipc_buffer_internal.h
```

The following already cohesive implementations remain byte-identical:

```text
kernel/ipc_abi.c
kernel/ipc_buffer.c
kernel/ipc_runtime.c
kernel/ipc_syscall_panic_test.c
arch/riscv64/ipc_ecall_test.S
arch/riscv64/ipc_syscall_test.S
arch/riscv64/ipc_syscall_panic_test.S
```

The nonsignaled native ABI, state, queue, queue-mutation, delivery, deadlock,
current-guard, and model-main sources remain unchanged. Endpoint/object
validation, scheduler transitions/return selection, generic syscall dispatch,
buffer-loss panic, grant cancellation, and assembly are not decomposed here.

No message, operation/result number, field, capacity, alignment, token,
generation, permission/profile, policy, state transition, error precedence,
allocation, public declaration, or execution identity changes. No asynchronous
IPC, retry, new timeout, priority inheritance, concurrency, recovery, user
feature, directory move, or unrelated source correction.

## Authority and failure preservation

Portable operations receive explicit endpoint/object tables under existing
external serialization. The target uses the one runtime registry with SIE
clear. The new modules introduce no production mutable storage.

Complete endpoint/object validation stays in its current owner. Do not create
another validator/cache, relax a shape, or eliminate nested validation merely
because extraction makes repeated checks visible.

Preserve:

- source and reply-token canonicalization before publication;
- exact held/non-current portable caller resolution;
- operation/target authorization and exact generations;
- matching before deadlock and blocking;
- first compatible FIFO selection without moving unmatched peers;
- independent RTS reasons, ordinary-send completion versus call reply wait;
- one monotonic nonzero token, success-only allocator update, and exhaustion;
- queued/staged request-return exclusion before token presentation;
- reply authority bypassing only the ordinary reverse send mask;
- kernel-event, lowest-source endpoint-event, then sender selection;
- specific receive ignoring kernel events and notifications excluding reply
  wait;
- exact prepared notification output and one non-failing consumed commit;
- `SEND`, `REPLY`, specific `RECEIVE` dependency precedence, sole-thread policy,
  candidate-cycle rejection, repeated-intermediate corruption, and capacity
  bound;
- post-reply deadlock projection with the exact caller dependency cleared;
- close preflight over every queue, token, staged reference, and event;
- owned staged-success rejection, cascading owned dead-endpoint completion,
  exact foreign waiter wake, and unrelated `ANY` preservation;
- scheduler checks before IPC stores, unchanged caller/output bytes on every
  returning failure, and no recoverable check after a non-failing commit.

Automatic plans may contain borrowed pointers only to caller-owned or
persistent objects whose serialized lifetime spans commit. They may not
retain a pointer to a returning helper's automatic local storage.

## Portable owners

| Source | Responsibility |
| --- | --- |
| `kernel/ipc_common.c` | Byte/message primitives, identity equality, current/held caller queries, error mapping, and staged-kernel-event query |
| `kernel/ipc_queue.c` | Bounded raw queue search, readiness, tail resolution, append, and unlink |
| `kernel/ipc_completion.c` | Message/no-message staging and consumed-sender completion |
| `kernel/ipc_tokens.c` | Token availability, exact waiter/request-return checks, shared reply preflight, and non-failing reply delivery |
| `kernel/ipc_notifications.c` | Canonical events, pending selection/consumption, user notify, and prepared kernel injection |
| `kernel/ipc_deadlock.c` | Exact endpoint/sole-thread projection and bounded dependency walk |
| `kernel/ipc_rendezvous.c` | Held sender/receiver enqueue and matching delivery, including call-token variants |
| `kernel/ipc_close.c` | Complete cancellation plan and non-failing close commit |
| `kernel/ipc_reply.c` | Reply, expected-caller reply, and atomic reply/receive |
| `kernel/ipc.c` | Authorized send/receive/call orchestration and sole call-token publication |
| `kernel/ipc_internal.h` | Sole declarations-only portable private contract |

The original private helpers have these exact final owners. Cross-owner
helpers gain `ipc_` names in the private contract; helpers used by only one
owner stay file-local.

| Owner | Original helpers |
| --- | --- |
| Common | `copy_bytes`, `clear_bytes`, `thread_handle_is_zero`, `thread_handles_equal`, `process_handles_equal`, `thread_is_current`, `endpoint_error_to_ipc`, `scheduler_error_to_ipc`, `canonicalize_message`, `clear_message`, `thread_is_held_and_clear`, `resolve_held_thread`, `resolve_operation_thread` |
| Queue | `resolve_queue_tail`, `receiver_is_ready_for`, `sender_is_ready_for`, `append_thread`, `find_matching_sender`, `find_matching_receiver_const`, `find_matching_receiver`, `unlink_thread` |
| Completion | `stage_no_message_completion`, `clear_delivered_sender`, `stage_delivery` |
| Tokens | `reply_token_is_available`, `resolve_reply_waiter`, `reply_request_is_pending`, `preflight_reply`, `commit_reply_delivery` |
| Notifications | `canonicalize_notification`, `find_pending_notification`, `consume_pending_notification` |
| Deadlock | `resolve_dependency_endpoint`, `resolve_sole_live_thread`, `blocked_dependency`, `preflight_deadlock` |
| Rendezvous | `sender_enqueue`, `sender_commit_delivery` |
| Close | `thread_has_staged_reference`, `thread_has_endpoint_reference`, `add_close_transition`, `preflight_close_threads`, `preflight_close_queue`, `preflight_close_queues`, `preflight_close_notifications`, `preflight_endpoint_close`, `commit_close_queue`, `clear_thread_ipc_state_for_close`, `stage_dead_endpoint_for_close`, `commit_endpoint_close` |

The pending-notification and reply-preflight structs move unchanged into the
private contract with private names. The close enum and both close-plan
structs stay local to the close owner. No plan enters a public header.

### Public function owners

| Owner | Stable public functions |
| --- | --- |
| Common | `micros_ipc_thread_has_staged_kernel_notification` |
| Completion | `micros_ipc_stage_no_message_completion` |
| Notifications | `micros_ipc_notify`, `micros_ipc_inject_kernel_notification`, `micros_ipc_prepare_kernel_notification`, `micros_ipc_commit_kernel_notification_prevalidated` |
| Rendezvous | `micros_ipc_sender_enqueue`, `micros_ipc_receiver_enqueue`, `micros_ipc_sender_commit_delivery`, `micros_ipc_receiver_commit_delivery` |
| Close | `micros_ipc_endpoint_close` |
| Reply | `micros_ipc_reply`, `micros_ipc_reply_expected_caller`, `micros_ipc_reply_receive` |
| Facade | `micros_ipc_send`, `micros_ipc_receive`, `micros_ipc_call` |

The private call variants of sender enqueue/delivery remain available only
to the facade. Queue primitives never call a complete operation. Notification
selection stays below rendezvous; it does not call it.

### Direct dependency direction

Dependencies here name IPC owners; existing supported endpoint/object/
scheduler interfaces and the current endpoint/scheduler private prevalidated
bridges retain their existing authority.

```text
common -> no IPC owner
queue -> common
completion -> common
tokens -> common + completion
notifications -> common + queue + completion
deadlock -> common
rendezvous -> common + queue + completion + tokens + notifications
close -> common
reply -> common + queue + completion + tokens + notifications + deadlock
ipc -> common + deadlock + rendezvous
```

There is no notification-to-rendezvous, queue-to-notification, or
token-to-reply reverse edge. Close keeps its existing filtered queue rebuild
and local cancellation plan instead of invoking other complete operations.

## Captured syscall owners

| Source | Responsibility |
| --- | --- |
| `kernel/ipc_syscall_result.c` | Existing panic, stable ABI return, and buffer-error classification |
| `kernel/ipc_syscall_request.c` | Scalar endpoint capture, request/buffer preflight, and portable dispatch |
| `kernel/ipc_syscall.c` | Current/runtime authority, controller restriction, context/guard, rollback/completion, and captured return |
| `kernel/ipc_syscall_internal.h` | Declarations-only request/result contract |

Original `panic_ipc_syscall`, `return_abi_error`, and `map_buffer_error`
belong to result. `capture_endpoint`, `preflight_request`, and
`execute_portable` belong to request. `operation_stages_immediate_completion`
stays local to captured orchestration. Required shared names gain
`ipc_syscall_` prefixes.

The existing automatic request struct moves unchanged to this private header.
Its operation, endpoint/source, token/event, primary/receive addresses, and
message retain their meanings and publication timing. Do not add persistent
request state.

```text
result -> no syscall owner
request -> result + supported portable IPC
captured handler -> result + request + supported completion/runtime APIs
```

The handler retains, in order: trusted storage checks; authoritative runtime
access/validation; exact current owner resolution; complete request preflight;
active-controller reply rejection; context store and guard begin; portable
execution; failure rollback and exhaustive ABI mapping; applicable immediate
completion; guard commit; captured return. `sepc` capture/advance remains in
the unchanged generic dispatcher.

## Native cases

Each family keeps its current public suite entry in its original facade.
Cases retain their existing names and exact order. Private case signatures
receive the family's context explicitly; fixture users do not get global
aliases. Original immutable suite tables/labels remain immutable.

For each of the five case families, every original function other than its
public suite entries, cases explicitly assigned below, and explicitly named
model helpers belongs to its fixture owner. This is the exact disposition
rule, not permission to invent a new generic fixture API. Export only helpers
used across its owners and retain all other helpers file-local.

### Operation family

```text
tests/host/ipc_operation_test.c
tests/host/ipc_operation_test_fixture.c
tests/host/ipc_send_operation_test.c
tests/host/ipc_receive_operation_test.c
tests/host/ipc_operation_test_internal.h
```

Send owns `test_authorized_send_blocks_with_snapshot`,
`test_authorized_send_matches_first_receiver`, and
`test_send_rejections_are_atomic`. Receive owns
`test_authorized_receive_blocks`, `test_receive_specific_and_any_fifo`,
and `test_receive_rejections_are_atomic`. The facade invokes those six in
that order.

Fixture owns the original identity/context/message, thread/setup,
extra-thread, canonical-message, preservation/expected-failure, and
current-held helpers.

### Call family

```text
tests/host/ipc_call_test.c
tests/host/ipc_call_test_fixture.c
tests/host/ipc_call_delivery_test.c
tests/host/ipc_call_validation_test.c
tests/host/ipc_call_test_internal.h
```

The exact case order is:

```text
test_call_delivers_to_waiting_receiver
test_call_preserves_independent_receiver_flags
test_queued_calls_bind_exact_threads_and_deliver_fifo
test_low_level_enqueue_rejects_reply_tokens
test_token_exhaustion_is_preflighted
test_call_rejections_are_atomic
test_staged_call_token_binding_is_validated
test_allocator_validator_rejects_unissued_token
```

The first three belong to delivery; the last five belong to validation.
Fixture owns the original zero/equality/byte/context/message/canonical-request,
thread/setup, preservation/expected-failure, and current-held helpers.

### Reply family

```text
tests/host/ipc_reply_test.c
tests/host/ipc_reply_test_fixture.c
tests/host/ipc_reply_authority_test.c
tests/host/ipc_reply_validation_test.c
tests/host/ipc_reply_receive_test.c
tests/host/ipc_reply_test_internal.h
```

The exact order is:

```text
test_staged_call_tokens_are_unique
test_reply_requires_request_return
test_reply_routes_exact_tokens_without_send_target
test_reply_preserves_independent_caller_flags
test_reply_rejections_are_atomic
test_reply_scheduler_failure_preserves_token
test_reply_rejects_source_only_caller
test_reply_receive_blocks_and_later_receives
test_reply_receive_selects_specific_sender_atomically
test_reply_receive_requires_combined_operation
test_reply_receive_rejections_are_atomic
test_reply_receive_scheduler_failures_are_atomic
```

The first four belong to authority, the next three to validation, and the
last five to reply/receive. Fixture owns the existing context/message/reply
builders, setup and extra thread, staged/delivered-call helpers, preservation
and expected failures, and current-thread preparation helpers.

### Notification family

```text
tests/host/ipc_notify_test.c
tests/host/ipc_notify_test_fixture.c
tests/host/ipc_notify_delivery_test.c
tests/host/ipc_kernel_notify_test.c
tests/host/ipc_notify_validation_test.c
tests/host/ipc_notify_model_test.c
tests/host/ipc_notify_test_internal.h
```

Preserve this interleaved facade order:

```text
test_notify_coalesces_and_never_blocks
test_kernel_notify_coalesces_and_any_receives
test_kernel_notify_wakes_only_any_receiver
test_kernel_notify_prepare_commit_is_retained
test_staged_kernel_notification_is_detected
test_kernel_notify_rejections_are_atomic
test_kernel_notify_validation_and_close_rules
test_notify_wakes_first_matching_receiver
test_call_reply_wait_does_not_consume_notification
test_receive_prefers_lowest_notification_before_sender
test_reply_receive_prefers_pending_notification
test_reply_receive_pending_failure_is_atomic
test_notification_validators_and_close_preflight
test_notify_rejections_preserve_state
test_notify_scheduler_failure_is_atomic
```

Kernel-prefixed cases and staged-kernel detection belong to kernel notify.
Ordinary coalescing, first receiver, call exclusion, lowest-source receive,
and pending reply/receive belong to delivery. The final four non-kernel
failure/validator cases belong to validation.

Fixture owns the original state/setup, byte/message/event, preservation,
thread/held/current, and exact receiver preparation helpers. Model owns
`notify_model_next_random`, `notify_model_record`, `notify_model_fail`,
`notify_model_pending_matches`, and `test_seeded_notify_model`.
`micros_ipc_notify_model_test_run` stays in the original facade.

### Close family

```text
tests/host/ipc_close_test.c
tests/host/ipc_close_test_fixture.c
tests/host/ipc_close_cancellation_test.c
tests/host/ipc_close_validation_test.c
tests/host/ipc_close_model_test.c
tests/host/ipc_close_test_internal.h
```

The exact order is:

```text
test_close_cancels_complete_transaction
test_cascading_close_discards_owned_dead_endpoint_result
test_source_only_close_cancels_queued_sender
test_source_only_notification_dependency_blocks_strict_close
test_successful_staged_deliveries_block_close_atomically
test_close_failures_are_atomic
```

The first three belong to cancellation and the final three to validation.
Fixture owns the original identity/bytes/context/message, endpoint/thread
setup, held/closing/dead-result/queue assertions, preservation, and current
preparation helpers. Model owns the random/record/failure helpers and
`test_seeded_close_scenarios`; its public model entry stays in the facade.

### Case-family state

Exactly one static context per family contains only its original mutable
globals, with original types and extents:

| Family | Context members |
| --- | --- |
| Operation | `registry`, `objects`, `processes[OPERATION_PROCESS_COUNT]`, `primary_threads[OPERATION_PROCESS_COUNT]`, `destination_extra_thread`, `endpoints[OPERATION_PROCESS_COUNT]`, `hart` |
| Call | `registry`, `objects`, `processes[CALL_PROCESS_COUNT]`, `primary_threads[CALL_PROCESS_COUNT]`, `client_extra_thread`, `endpoints[CALL_PROCESS_COUNT]`, `hart` |
| Reply | `registry`, `objects`, `processes[REPLY_PROCESS_COUNT]`, `primary_threads[REPLY_PROCESS_COUNT]`, `client_extra_thread`, `server_extra_thread`, `endpoints[REPLY_PROCESS_COUNT]`, `hart` |
| Notify | `registry`, `objects`, `processes[NOTIFY_PROCESS_COUNT]`, `primary_threads[NOTIFY_PROCESS_COUNT]`, `destination_extra_threads[2]`, `endpoints[NOTIFY_PROCESS_COUNT]`, `hart`, `notify_model_trace[32]`, `notify_model_trace_count` |
| Close | `registry`, `objects`, `processes[MICROS_PROCESS_CAPACITY]`, `primary_threads[MICROS_PROCESS_CAPACITY]`, `endpoints[MICROS_PROCESS_CAPACITY]`, `hart`, `close_scenario_trace[16]`, `close_scenario_trace_count` |

There is no existing mutable static-local case storage. Keep automatic
snapshots, actor/peer copies, and expected messages automatic. Preserve
fixture reset timing; do not clear an entire context where the original
fixture cleared only registry/objects and retained model trace.

Immutable profiles, profile-ID vectors, operation-name vectors, scripted
prefix arrays, and suite tables keep exact values and their fixture/model/
facade owner. New private headers declare only required shared interfaces,
types, literal constants, and existing assertion macros.

## Persistent acceptance model

The model remains one existing execution, not a new workflow.

| Source | Original responsibility/functions |
| --- | --- |
| `ipc_model_test.c` | `model_random_next`, `coverage_is_complete`, `micros_ipc_model_test_run` |
| `ipc_model_test_common.c` | `null_thread_handle`, handle equality/zero, `model_thread_handle`, `context_pattern`, `messages_equal`, `model_thread_is_held_clear` |
| `ipc_model_test_fixture.c` | `fill_message`, `message_for_step`, `receive_buffer_for_step`, `setup_model`, sole static fixture context |
| `ipc_model_test_trace.c` | `model_operation_name`, `record_trace`, `dump_trace`, `dump_state` |
| `ipc_model_reference_authority.c` | Active endpoint/operation caller/profile authorization, matching receiver/sender/pending event, sole thread, deadlock queries |
| `ipc_model_reference_scheduler.c` | `model_ready_append`, `model_ready_remove`, `model_apply_transitions`, `model_hold_thread` |
| `ipc_model_reference_ipc_state.c` | Message clear/canonicalization/staging, delivered sender, IPC-state clear/drain, `model_ipc_append`, `model_ipc_unlink` |
| `ipc_model_reference_rendezvous.c` | `model_send`, `model_receive`, `model_call`, exact reply waiter/preflight, `model_reply`, `model_reply_receive` |
| `ipc_model_reference_notifications.c` | `model_notify`, `model_kernel_notify` |
| `ipc_model_reference_lifecycle.c` | Endpoint reference/query, filtered rebuild, close, reset/reuse, close/reuse |
| `ipc_model_reference.c` | `model_execute_action`, `model_matches_production` and its complete comparison phases |
| `ipc_model_test_production.c` | Production drain/hold/reuse/close-reuse and action dispatch |
| `ipc_model_test_step.c` | `update_coverage`, `execute_action`, `run_checked_action` |
| `ipc_model_test_actions.c` | All ten original action builders, held/staged/holdable/reply/safe-reuse/recovery queries, `recovery_action`, `build_random_action` |
| `ipc_model_test_prelude.c` | Exact ordered forced prelude in named behavioral phases |
| `ipc_model_test_internal.h` | Declarations-only model types, fixture context, and required helpers |

All paths in this table are under `tests/host/`. This disposition covers all
88 original functions. Required shared helpers gain `ipc_model_` names;
owner-local helpers remain static.

The fixture context contains the original registry, objects, 64 messages,
96-entry trace, trace count, and trace hash. The reference `model_state`,
coverage, seed, RNG, and step remain automatic in the runner. Do not add a
global reference model or copy authoritative target state.

The model has seven process generations, ten initial threads, initial thread
counts `2,2,2,1,1,1,1`, full/denied profiles, and the same singleton dependency
projection. Reference decisions never call production operations, validators,
or private helpers. Comparison may retain its existing public validation of
production state after complete field equality.

The acyclic model direction is:

```text
common -> no model owner
reference authority/scheduler/IPC-state -> common
reference rendezvous/notifications/lifecycle -> common + reference primitives
reference facade/comparison -> common + reference operation owners
fixture -> common + reference comparison
production adapter -> common + supported production APIs
trace -> common
step -> reference facade + production adapter + trace
actions -> common + fixture + read-only reference queries
prelude -> common + actions + step
runner -> fixture + prelude + actions + step + trace
```

Actions may use the existing read-only reply-waiter and endpoint-reference
queries, but may not perform reference transitions. Explicit context
parameters allow comparison/trace to inspect the fixture without calling
its getter and creating a reverse dependency.

Preserve seed `0x30accee5`, total 8,192 steps including the forced prelude,
one RNG draw per subsequent step, the original 13-choice selection and exact
recovery priority, message/buffer factories, and every action field.
The step order remains reference execution, production execution, trace,
coverage, result publication, result comparison, full state comparison.
The latter includes process, endpoint, notification, thread, hart,
ready-queue, and public-validator checks in their original order.

Preserve the existing trace initializer `0x30accee57ace0001`, XOR fields,
seven-bit rotation, 96-entry diagnostics, and final hash
`65c2014aad08f70b`. That hash was observed in the current complete native
gate's retained `LastTest.log`; it excludes message/buffer addresses and
therefore survives fixture relocation. It supplements rather than replaces
full comparisons and error/operation coverage.

The notify model keeps seed `0x30c0a1e5`, 4,096 steps, two draws per step
before prefix override, its eleven forced operation/source pairs, complete
pending-state comparisons and coverage. The close scenarios keep seed
`0x30c105e1`, 512 fresh fixtures, three draws per case, operation selection,
nonzero event construction, actor/peer preservation, close result,
staged-completion checks, and all eight coverage classes.

## Trusted target scenario

```text
kernel/ipc_test.c
kernel/ipc_test_fixture.c
kernel/ipc_test_setup.c
kernel/ipc_test_scenarios.c
kernel/ipc_test_report.c
kernel/ipc_test_internal.h
```

The original public `micros_ipc_runtime_run_self_test` stays in the facade.
Fixture owns all original byte/identity/context/message/stack/snapshot/
queue/endpoint/consume/prepare/cleanup helpers. Setup owns profile/runtime,
process/root/endpoint/thread creation and execution snapshots. Scenarios
owns the ordered rendezvous/token/event/deadlock/close/reuse actions. Report
owns teardown, final baseline validation, failure stage output, and existing
IRQ restoration; it does not invent a pass marker or shutdown.

One static context owns the original aligned stacks, borrowed registry
pointer, registry/object/hart snapshots, receive buffers, eight send
messages, context snapshots, and root snapshots. The stack member retains
4,096-byte alignment and every 16-KiB row.

An automatic run-state struct may carry only the original automatic
authority pointers, handles/endpoints, expected/reply values, borrowed request,
tokens, baselines, saved status, and failure stage across phases. It owns no
second registry. Preserve stages 1 through 11, special `0x801` and `0x1101`
through `0x1108` diagnostics, and the actual original final-check order
(including ownership before later object/scheduler/endpoint checks).

The scenario order remains setup; immediate send; blocked send; call/reply
and stale token; second call/reply-receive and peer send; notification
coalescing excluded from reply wait; kernel coalescing/immediate event;
deadlock rejection and matching recovery; close cancellation; generation
reuse/stale/authority rejection; complete cleanup and baseline.

## Ecall-core target

```text
kernel/ipc_ecall_test.c
kernel/ipc_ecall_test_fixture.c
kernel/ipc_ecall_test_setup.c
kernel/ipc_ecall_test_report.c
kernel/ipc_ecall_test_internal.h
```

The non-returning runtime entry stays in the facade; both finish functions
move to report. Setup owns profiles/endpoints, client root/pages/thread,
payload and message initialization, execution context, and runtime preflight.
Fixture owns the original byte/address/state helpers, immutable profiles/IDs,
literal constants, and sole mutable context.

The context contains the original process array, client thread, endpoints,
borrowed registry, code/data/stack frame addresses, owned/free counters, and
process/thread/hart baselines. The three `micros_ipc_ecall_test_saved_*`
register objects remain separate exact assembly ABI symbols in fixture.

Scalar constants become namespaced literal private constants with identical
types/values where shared. Preserve the payload-derived addresses, current
volatile control read, status mask, non-result GPRs, exact pending event,
supervisor return preparation, cleanup order, failure stages, marker, and
reset behavior.

Keep the `sp/gp/tp` inline capture in the original public entry frame,
immediately before the same `micros_scheduler_test_enter_without_timer`.
Do not capture in a helper that returns before execution starts.

## Full syscall target

```text
kernel/ipc_syscall_test.c
kernel/ipc_syscall_test_fixture.c
kernel/ipc_syscall_test_setup.c
kernel/ipc_syscall_test_commands.c
kernel/ipc_syscall_test_scenarios.c
kernel/ipc_syscall_test_hooks.c
kernel/ipc_syscall_test_report.c
kernel/ipc_syscall_test_internal.h
```

The non-returning runtime entry remains in the facade. Before/after ecall
and timer hooks move to hooks; non-returning finish moves to report.
Fixture owns original byte/context/address/index/user-buffer/message/event/
diagnostic and negative-snapshot helpers. Commands owns `arm_command` and
the six typed arming helpers. Setup owns `prepare_process`,
`cleanup_process`, profiles/endpoints/pages/contexts and scheduling setup.
Scenarios owns `transition_after_return` as named command-phase handlers.
Hooks owns return validation/mismatch and supervisor-exit preparation.

One static context contains all original mutable non-ABI objects: three
process/thread/endpoint entries, borrowed registry, three scripts,
code/data-two-page/stack frames, counters/baselines, three tokens, finish
flag, scenario phase, deferred-timer flag/context, negative snapshot
flag/command, registry/object snapshot and candidate, and three paired
negative buffer snapshots. The three `micros_ipc_syscall_test_saved_*`
objects stay separate exact assembly ABI symbols in fixture.

The immutable 21-entry thread/command tables stay paired, with their
compile-time equal-length assertion. In order, retain:

```text
client SEND_ONE
server RECEIVE_SEND_ONE
server WAIT_CALL_ONE
client CALL_ONE
server REPLY_ONE
server WAIT_CALL_TWO
client CALL_TWO
server REPLY_RECEIVE_TWO
client WAIT_SERVER_CALL
peer SEND
server CALL_CLIENT
peer NOTIFY_ONE
peer NOTIFY_TWO
peer BLOCK
client REPLY_THREE
server RECEIVE_NOTIFY
server WAKE_PEER
server BLOCK_FINAL
client ERROR_UNAUTHORIZED
client ERROR_ARGUMENT
client ERROR_FAULT
```

Keep exact message types/payload seeds, three token capture sites, priorities,
quantum changes, peer admission, negative byte snapshots, phase increment,
register checks, deferred completion and timer sequence. Negative comparison
normalizes only the original client context and hart accounting fields in
its candidate, checks both buffers for all processes, then clears the
snapshot flag. It never rewrites authoritative objects for comparison.

Transition helpers group send/receive, first call/reply, second
call/reply-receive/peer activation, reverse call, notification/peer wake,
stable errors, and deferred timer. They preserve each command's internal
order and exact rejection of `NONE`, `PEER_BLOCK`, and `SERVER_BLOCK_FINAL`
as after-return transitions.

Setup/commands/scenarios/hooks/report share only the declarations-only
private contract. Setup depends on fixture/commands; commands on fixture;
scenarios on fixture/commands; hooks on fixture/scenarios; report on
fixture/setup; facade on fixture/setup and public scheduler APIs. There
is no reverse callback into hooks from scenarios.

Keep supervisor `sp/gp/tp` capture in the original public entry frame,
after `fence.i` and scheduling/runtime preflight, immediately before
`micros_scheduler_start`. Assembly and its saved symbols remain unchanged.

## Exact ordered source sets and consumers

The implementation adds these eleven sets. No raw split-source listing may
remain beside a set expansion.

```text
MICROS_IPC_CORE_SOURCES
  kernel/ipc_common.c
  kernel/ipc_queue.c
  kernel/ipc_completion.c
  kernel/ipc_tokens.c
  kernel/ipc_notifications.c
  kernel/ipc_deadlock.c
  kernel/ipc_rendezvous.c
  kernel/ipc_close.c
  kernel/ipc_reply.c
  kernel/ipc.c

MICROS_IPC_SYSCALL_SOURCES
  kernel/ipc_syscall_result.c
  kernel/ipc_syscall_request.c
  kernel/ipc_syscall.c

MICROS_IPC_OPERATION_HOST_TEST_SOURCES
  tests/host/ipc_operation_test.c
  tests/host/ipc_operation_test_fixture.c
  tests/host/ipc_send_operation_test.c
  tests/host/ipc_receive_operation_test.c

MICROS_IPC_CALL_HOST_TEST_SOURCES
  tests/host/ipc_call_test.c
  tests/host/ipc_call_test_fixture.c
  tests/host/ipc_call_delivery_test.c
  tests/host/ipc_call_validation_test.c

MICROS_IPC_REPLY_HOST_TEST_SOURCES
  tests/host/ipc_reply_test.c
  tests/host/ipc_reply_test_fixture.c
  tests/host/ipc_reply_authority_test.c
  tests/host/ipc_reply_validation_test.c
  tests/host/ipc_reply_receive_test.c

MICROS_IPC_NOTIFY_HOST_TEST_SOURCES
  tests/host/ipc_notify_test.c
  tests/host/ipc_notify_test_fixture.c
  tests/host/ipc_notify_delivery_test.c
  tests/host/ipc_kernel_notify_test.c
  tests/host/ipc_notify_validation_test.c
  tests/host/ipc_notify_model_test.c

MICROS_IPC_CLOSE_HOST_TEST_SOURCES
  tests/host/ipc_close_test.c
  tests/host/ipc_close_test_fixture.c
  tests/host/ipc_close_cancellation_test.c
  tests/host/ipc_close_validation_test.c
  tests/host/ipc_close_model_test.c

MICROS_IPC_MODEL_HOST_TEST_SOURCES
  tests/host/ipc_model_test.c
  tests/host/ipc_model_test_common.c
  tests/host/ipc_model_test_fixture.c
  tests/host/ipc_model_test_trace.c
  tests/host/ipc_model_reference_authority.c
  tests/host/ipc_model_reference_scheduler.c
  tests/host/ipc_model_reference_ipc_state.c
  tests/host/ipc_model_reference_rendezvous.c
  tests/host/ipc_model_reference_notifications.c
  tests/host/ipc_model_reference_lifecycle.c
  tests/host/ipc_model_reference.c
  tests/host/ipc_model_test_production.c
  tests/host/ipc_model_test_step.c
  tests/host/ipc_model_test_actions.c
  tests/host/ipc_model_test_prelude.c

MICROS_IPC_QEMU_TEST_SOURCES
  kernel/ipc_test.c
  kernel/ipc_test_fixture.c
  kernel/ipc_test_setup.c
  kernel/ipc_test_scenarios.c
  kernel/ipc_test_report.c

MICROS_IPC_ECALL_QEMU_TEST_SOURCES
  kernel/ipc_ecall_test.c
  kernel/ipc_ecall_test_fixture.c
  kernel/ipc_ecall_test_setup.c
  kernel/ipc_ecall_test_report.c

MICROS_IPC_SYSCALL_QEMU_TEST_SOURCES
  kernel/ipc_syscall_test.c
  kernel/ipc_syscall_test_fixture.c
  kernel/ipc_syscall_test_setup.c
  kernel/ipc_syscall_test_commands.c
  kernel/ipc_syscall_test_scenarios.c
  kernel/ipc_syscall_test_hooks.c
  kernel/ipc_syscall_test_report.c
```

| Current consumer | Exact required expansions |
| --- | --- |
| `endpoint_test` | Core; operation, call, reply, notify, close host sets |
| `ipc_model_test` | Core; model, notify, close host sets |
| `bootstrap_control_test`, `pm_test` | Core only |
| Common `micros` production sources | Core and syscall sets |
| `MICROS_BUILD_IPC_TEST` sources | Trusted IPC QEMU set |
| `MICROS_BUILD_IPC_ECALL_CORE_TEST` sources | Ecall QEMU set plus unchanged assembly |
| `MICROS_BUILD_IPC_SYSCALL_TEST` sources | Syscall QEMU set plus unchanged assembly |

Core expands five times, syscall once, operation/call/reply/model once each,
notify/close twice each, and each target set once. Preserve all other current
source sets and raw cohesive sources. The unchanged panic image retains its
current sources and assembly.

## Long-function disposition

All 42 original signals require named semantic phases; none receives an
algorithmic exception. Other existing cohesive walks are already below the
signal and may remain intact. New functions still receive cohesion review.

| Original function | Required tasks |
| --- | --- |
| `micros_ipc_notify` | Scalar/registry/caller authority; destination/event/candidate; eligible wake+unlink/stage or notifier wake+coalesce |
| `sender_enqueue` | Shape and complete validation; held/source/destination/token; match/tail preflight; exact blocked sender publication |
| `micros_ipc_receiver_commit_delivery` | Shape/held/source; pending event selection and commit; sender selection/scheduler pair; unlink/completion/output |
| `sender_commit_delivery` | Shape/held/source/token; receiver selection; snapshot and scheduler variant; unlink/stage and call binding/output |
| `micros_ipc_reply_receive` | Shared reply preflight; receive source/selection; notification/sender/wait scheduler plan and post-reply deadlock; one reply+receive commit |
| `micros_ipc_handle_captured_user_ecall` | Runtime/current authority; request/controller; stored context/guard begin; dispatch/rollback/error; immediate completion; guard commit/captured result |
| `micros_ipc_runtime_run_self_test` | Authority/baseline/setup; each ordered behavior phase; generation/denial; teardown/resource checks; failure/IRQ boundary |
| `transition_after_return` | Exact command dispatch over the send/receive, call rounds, peer/reverse-call/event, error, and timer tasks above |
| `micros_ipc_syscall_test_finish` | Authority/hold; endpoint cancellation; per-process cleanup; baseline/runtime checks; success/failure reset |
| `micros_ipc_syscall_runtime_run_self_test` | Baseline/runtime/profile; process/page/context preparation; `fence.i`/scheduler; original-frame capture/start; fatal failure |
| `micros_ipc_ecall_core_test_finish` | Close; execution detach; exact page/root/process release; baseline/validators; success/failure reset |
| `micros_ipc_ecall_core_runtime_run_self_test` | Runtime/roles; client root/pages/thread; payload/data/context; runtime/fence; original-frame capture/entry |
| `model_matches_production` | Aggregates; process; endpoint/events; thread; hart/ready queues; original public validators |
| `setup_model` | Reset/reference defaults; profiles/hart; process/endpoints/initial threads; execution/scheduler mirrors; exact initial comparison |
| `model_send` | Input/authority; canonical snapshot; receiver and transition; blocked dependency/queue publication |
| `model_receive` | Input/authority/source; kernel/endpoint pending event; matched sender; deadlock/receive publication |
| `model_call` | Input/authority/capacity; canonical token candidate; immediate or queued commit; allocator publication |
| `model_reply_receive` | Reply/source; input selection; post-reply dependency; scheduler variant; reply and exact receive commit |
| `model_close_endpoint` | Exact endpoint/thread cancellation plan; scheduler preflight; filtered queues; owned/foreign completion; event/endpoint clear |
| `production_reuse_process` | Exact old identity/owned threads; execution/thread release; process/endpoint recreation; exact slot/context/scheduler rebuild |
| `run_model_prelude` | Existing forced operation groups in exact order; unchanged checked-step advancement and expectations |
| `build_random_action` | Original thirteen choice classes over named send/receive/call/reply/event/drain/hold/reuse/denial/deadlock tasks; exact recovery |
| `setup_notify_fixture` | Exact profiles/hart; process/thread inventory; profile/publication; held scheduler fixture |
| `test_kernel_notify_prepare_commit_is_retained` | Setup/prepared snapshot; unchanged before commit; exact commit/consumption; retained recipient/results |
| `test_kernel_notify_validation_and_close_rules` | Canonical/pending cases; invalid state preservation; close/reference behavior |
| `test_receive_prefers_lowest_notification_before_sender` | Pending/FIFO setup; deterministic source consumption; ordinary sender consumption; complete queues/state |
| `test_reply_receive_prefers_pending_notification` | Delivered call/pending work; atomic reply+lowest event; retained sender/subsequent receive; final state |
| `test_notification_validators_and_close_preflight` | Canonical corruption groups; exact failure snapshots; close dependency checks and restoration |
| `test_seeded_notify_model` | Fixture/prefix; two draws/selection; operation handlers; pending/reference comparisons; coverage/diagnostics |
| `setup_reply_fixture` | Exact profiles/hart; process/thread inventory; profile/publication; held fixture and extra-thread roles |
| `test_reply_requires_request_return` | Staged call; sibling early reply/combined rejection; request drain; sibling success/one-shot result |
| `test_reply_routes_exact_tokens_without_send_target` | Multiple exact callers/tokens; reverse-mask denial; selected sibling/token replies; independent routing/stale rejection |
| `test_reply_rejections_are_atomic` | Shared setup; shape/token/authority/state rejection groups; unchanged full tables/output |
| `test_reply_receive_blocks_and_later_receives` | Delivered call; reply+blocked receive; later exact message; caller/server completion and queues |
| `test_reply_receive_selects_specific_sender_atomically` | Multiple queued senders; specific match; atomic caller and sender state; unmatched FIFO preservation |
| `test_close_cancels_complete_transaction` | Full owned/foreign queue/token/event setup; close; exact cancellations and unrelated preservation; final validation |
| `test_successful_staged_deliveries_block_close_atomically` | Message/event/no-message staged success classes; rejection snapshots; drain and legitimate close |
| `test_close_failures_are_atomic` | Shape/generation/queue/token/event/held/scheduler corruption groups; full preservation and restoration |
| `test_seeded_close_scenarios` | Three draws/fresh fixture; eight operation setups; close outcome; actor/peer/completion preservation; coverage/trace |
| `test_queued_calls_bind_exact_threads_and_deliver_fifo` | Multi-caller setup; exact queue/token bindings; FIFO deliveries; independent caller reply waits |
| `test_call_rejections_are_atomic` | Setup; shape/authority/state/endpoint/buffer/token/scheduler rejections; exact preservation |
| `test_receive_specific_and_any_fifo` | Ordered senders; specific then `ANY`; canonical messages/completions; unmatched queue and final state |

Preserve short-circuit evaluation, loop traversal, callback order, snapshots,
sentinels, failure stages, output assignment, and final validation. Moving or
renaming a body does not satisfy this table.

## Structural Red and validation inheritance

Implementation starts with `tests/host/test_kernel_ipc_source_layout.py`.
Its exact family includes all planned sources/headers plus unchanged cohesive
IPC implementations, public interfaces, nonsignaled native evidence, and
assembly. Scope discovery must reject unclassified added family files.

The initial owner migration has exactly 70 entries: fourteen moving portable
public functions; all 47 native cases plus two seeded model cases; four
syscall hooks and its finish function; and both ecall finish functions.
The original facades and public runtime entries remain in place. Retained
private helpers have exact separate owner/name maps.

The initial long migration has exactly the 42 entries above. Both migrations
are shrink-only and must be empty before final review. A moved over-limit
helper must be semantically split in its green extraction slice, not renamed
to escape the migration.

Unconditional gates cover:

- exact source/private-header families, ordered sets and every consumer;
- stable public/support/assembly hashes and retained public/case/helper owners;
- declarations-only private contracts and no textual source inclusion;
- recursive local include/macro/inline dependencies, distinguishing type tags
  from function references and forbidding model production-private imports;
- acyclic direct edges, narrow private call variants and read-only action
  queries, without sibling scenario cycles;
- the sole production runtime state, nine fixture contexts, six saved-register
  ABI exceptions, exact extents/alignment, and original automatic state;
- every static and static-local declaration, including initialized,
  anonymous-aggregate, array, alias and co-declaration forms; immutable
  tables require their exact approved role/owner rather than a blanket
  `const` exemption;
- suite labels/order, seeds/counts/draw schedules, prelude/selection/recovery,
  trace/hash, complete comparisons, target command/timer/failure stages,
  compile guards, assembly identities, markers and timeouts;
- exact original validation classification for each new source/header.

Core paths inherit `kernel/ipc.c`; syscall paths inherit
`kernel/ipc_syscall.c`; each case/model family inherits its original
monolithic source; each target family inherits its original target source.
Include selective slow-model ownership as well as QEMU ownership. Do not
narrow unknown/full fallback. The new structural test alone is host-unit-only.

No native or target entry is skipped because a source set now contains both
fast cases and a slow-model entry. The existing executables retain those
sources and their existing entry selection.

## Existing evidence and green sequence

Preserve `endpoint_test` / `endpoint-profile`, `ipc_model_test` /
`ipc-model`, `test-unit-fast`, `test-ipc-model`, and complete `test-unit`.
Keep the unchanged model-main order: endpoint lifecycle, notifications,
close scenarios, direct-grant/copy model, persistent IPC model.

Retain all current target fields and eight-second timeouts for:

```text
test-qemu-ipc
test-qemu-ipc-ecall-core
test-qemu-ipc-syscall
test-qemu-ipc-syscall-panic
```

The pass records remain exactly:

```text
MICROS_IPC_TEST_PASS endpoints=generation-safe queues=blocking calls=tokenized notifications=coalesced kernel-events=injected deadlock=rejected
MICROS_IPC_ECALL_CORE_TEST_PASS dispatch=production guard=transactional completion=returned errors=stable registers=preserved
MICROS_IPC_SYSCALL_TEST_PASS spaces=three operations=six blocking=validated calls=tokenized notifications=coalesced errors=stable completion=deferred registers=preserved cleanup=complete
```

The buffer-loss image still requires `invalid-bootstrap-ipc-buffer` and no
success marker. Other shared consumers retain their current validation.

After design merge, use coherent green slices:

1. Structural Red, eleven source sets/private skeletons, inheritance, exact
   migrations, and unchanged consumers.
2. Common/queue/completion/token/notification/deadlock leaves.
3. Rendezvous and reply/close/facade tasks, splitting their long functions
   without changing scheduler or publication ordering.
4. Captured request/result/guard phases.
5. Operation/call/reply fixture and case phases.
6. Notification/close fixtures, cases, and original models.
7. Acceptance-model common/fixture/reference/production/trace/step/actions/
   prelude phases with identical trace and complete comparison.
8. Trusted target context/setup/scenarios/report.
9. Ecall target context/setup/report with original entry-frame capture.
10. Full syscall fixture/setup/commands/scenarios/hooks/report and timer/
    negative-state evidence.
11. Complete unconditional structural enforcement and empty migrations.
12. One authoritative final PR plan and independent implementation review.

During extraction run only the affected existing native CTest/model or QEMU
gate, not the whole matrix after every move. Before review, stage the complete
change, verify a clean tracked worktree and no untracked task files, bind
evidence to `HEAD` and `git write-tree`, and execute:

```bash
python3 tools/validation_plan.py --tier pr --base main --execute
```

Shared CMake/IPC ownership retains full-tier coverage. Do not run selected
workflows again separately. Corrections invalidate only their declared
ownership, with old/new tree identities recorded; uncertain ownership
requires a rerun. Reviewers inspect the author's exact evidence and use only
a minimal targeted reproduction for a concrete unresolved hypothesis.

# MINIX Direct Reply-Token Binding Study

## Fixed inputs and question

```text
micros source: 9dc784b52170cefa5306336cdeb4869eff50efb2
micros tree:   b52c63848a02335356614c1160facb4f0d8a2e96
MINIX source:  4db99f4012570a577414fe2a43697b2f239b699e
```

Both local inputs were checked at these revisions. The MINIX checkout was
clean. IPC01 and IPC02 are independently reviewed and merged as #251 and #253.
IPC03 owns only B04-F004: ordinary token allocation, lookup and request-return
qualification still search the complete thread table for one binding.

This is design research, not executable Red/Green evidence. The
[B04 audit](minix-complete-baseline-b04-endpoint-ipc.md#b04-f004-reply-tokens-have-no-direct-binding-index)
allows an equivalent direct binding; it does not require a separate hash table.

## Fixed MINIX mechanism

Paths are relative to the local reference checkout.

| Source | Established mechanism |
| --- | --- |
| `minix/kernel/proc.h:23-90` | The process record owns its full endpoint, send/receive state, queued message and staged delivery. |
| `minix/kernel/proc.h:264-274` | Decoded process numbers index the process table directly. |
| `minix/kernel/proc.c:479-598` | Synchronous IPC checks the endpoint and operation/destination permissions. `SENDREC` sets reply-pending state, sends, then receives from that exact peer. |
| `minix/kernel/proc.c:868-960` | An ordinary reply send directly selects the caller process and its receive condition. Matching publication clears reply-pending state; a nonmatching send uses the ordinary queue path. |
| `minix/kernel/proc.c:965-1117` | Receive preserves send-before-receive blocking and suppresses notifications during `SENDREC`; queued requests are staged before the receiver returns. |
| `minix/kernel/proc.c:255-290` | `delivermsg` copies the staged message to user memory before marking it consumed. Its VM-assisted fault policy is not the `micros` bootstrap policy. |
| `minix/kernel/proc.c:1828-1860` | Endpoint admission checks implemented range, live process and full endpoint equality. |
| `minix/kernel/system.c:540-566` | Exit first inhibits communication, detaches IPC and cancels references to the exiting endpoint. This is a lifecycle scan, not a scan on each reply. |

MINIX has no one-shot token analogue. Its reply destination is already an
endpoint, so destination identity and selected reply-wait inspection are
`O(1)` with constant scratch. Ordinary queue matching and close retain their
separate queue/capacity costs. No token-history table, hash construction or
per-operation reverse-map rebuild belongs to this reference path.

`micros` must preserve the Accepted extension: a nonzero opaque `uint64_t`
token routes to an exact caller thread, authorizes only the exact callee
endpoint generation, becomes presentable only when the request completion
returns, and is consumed once. A process-owned endpoint is not a thread ID,
and a recipient thread cannot permanently own the callee's reply permission.

## Current lookup and lifecycle

The current implementation has one global high-water token in the endpoint
registry and one active token/callee pair in each caller thread. There is no
direct token routing or staged-request index.

| Current owner | Actual work or publication |
| --- | --- |
| `ipc.c:micros_ipc_call` | Plans `last_reply_token + 1`; invokes the all-thread availability scan; publishes the high-water value only after successful call commit. |
| `ipc_tokens.c:ipc_reply_token_is_available` | Scans all `T` slots despite monotonic nonreuse. |
| `ipc_tokens.c:resolve_reply_waiter` | Scans `T` slots for the token and duplicate active bindings, then checks the selected caller. |
| `ipc_tokens.c:reply_request_is_pending` | Scans `T` slots for staged requests, including malformed or duplicate matches. |
| `endpoint_completion_validation.c:staged_reply_token_binding_is_valid` | Scans `T` slots to bind one staged request and detect another staged copy. This is also used on the actual selected-completion path. |
| `endpoint_object_validation.c:thread_relationships_are_canonical` | Complete diagnosis compares each active token against preceding threads. |

Thus token-specific allocation, reply and completion work is `O(T)`, and
the token part of complete diagnosis includes `O(T^2)`. These scans are
separate from matching queues, deadlock, scheduler work and buffer translation.

The authoritative publication census is:

| Owner | Transition to preserve |
| --- | --- |
| `ipc_rendezvous.c:ipc_publish_sender_wait` | Publish an undelivered queued call with `SEND | REPLY`. |
| `ipc_rendezvous.c:ipc_deliver_selected_sender` | Bind an immediately staged call to the exact selected receiver. |
| `ipc_rendezvous.c:ipc_deliver_selected_receive` | Turn a queued call into a staged request after sender selection. |
| `ipc_reply.c:publish_reply_receive` | Perform that same queued-call delivery in the atomic combined operation. |
| `scheduler_completion.c` | Prepare the selected request's buffer, then copy and consume it in the lookup-free return commit. This is the real presentability boundary. |
| `ipc_tokens.c:ipc_commit_reply_delivery` | Consume the old caller token only after the final fallible scheduler step. |
| `ipc_close.c` | Preflight references and prohibit losing undrained successful deliveries; clear canceled bindings in its non-failing commit. |
| `kernel_object_state.c` and portable/PM lifetime owners | Initialize/clear complete IPC state, and prohibit detaching or releasing a thread with residual IPC ownership. |
| `pm_control_validation.c:validate_stopped_objects` | Consume the stopped application's active EXIT call to PM; its request has already returned to PM. |

There is no production portable `take_completion` API. Native IPC fixtures
simulate consumption by editing their fixture state, and
`kernel/tests/ipc/ipc_test_fixture.c` has a trusted component-test drain.
Those simulations must acquire the same new state transition; adding a
public completion API merely for tests is unnecessary.

The relevant native drain owners include `ipc_reply_test_fixture.c`,
`ipc_notify_test_fixture.c`, `ipc_close_test_fixture.c`,
`ipc_deadlock_test.c`, `ipc_model_test_production.c`,
`ipc_model_reference_ipc_state.c`, `selected_ipc_validation_test.c`,
`endpoint_source_only_test.c` and bootstrap completion fixtures.
Malformed-state controls must remain malformed, not be repaired by an adapter.

## Representation choice and alternatives

An opaque token can carry its caller slot in the low seven bits while the
existing high-water value prevents reuse. Choose the least representable
positive value above that high-water value with the required slot tag.
Decoding then selects one of the existing 128 thread slots without a second
index table. Full token equality, live thread/owner admission and exact callee
generation remain mandatory.

This preserves width, opacity, strict monotonicity and nonreuse, but not the
old consecutive numeric sequence. It also makes terminal exhaustion depend
on whether another value for that caller slot is representable. Existing
tests explicitly assume token values 1/2 and that every caller can issue
`UINT64_MAX` from `UINT64_MAX - 1`; the new ADR must supersede those assumptions
before implementation. Service/runtime consumers retain and echo opaque
values rather than decoding or incrementing them.

The staged-request search needs a second direct relation. One typed receiver
handle alone is insufficient: treating null as "returned" would let a missing
staged index manufacture presentability. Use an explicit current request
phase plus the exact receiver handle. The phase distinguishes no call,
queued request, staged request and returned request. A staged phase requires
a real reciprocal receiver; a returned phase requires exact null.
This is current transaction state, not consumed-token history or cached
whole-state validation.

Rejected alternatives:

- a linear fallback or rebuilding a map per call retains the divergence;
- a modulo-counter ring with consecutive numbers may collide with a much older
  still-active call despite free thread capacity;
- an ordinary hash table adds collision/tombstone policy and a linear worst
  case instead of the reference's direct identity;
- a radix/tree index is unnecessary storage and machinery solely to preserve
  unspecified opaque numeric values;
- equating process and thread slots or using `sole_thread` for token routing
  would break the existing multi-thread native caller/callee semantics;
- a pending receiver without explicit phase confuses a missing index with
  returned authority; a phase without the receiver cannot check its consumed
  staged relation directly.

## Classification and cost ledger

| Mechanism | Classification | Cost and reason |
| --- | --- | --- |
| Direct destination identity and selected wait state | Baseline parity | `O(1)` token-specific work instead of a thread-table search. |
| Opaque caller-slot ticket with exact callee binding | Required adaptation | Distinct threads and least-privilege reply authority replace MINIX's ordinary endpoint reply; constant arithmetic and direct access. |
| Strictly increasing, never-reused ticket values | Compatible extension | At most one checked increment by 128 after slot tagging; no wrap, retry or allocation scan. |
| Explicit current request phase and generation-safe receiver link | Required adaptation | Retain ADR-0036's request-return authority with constant selected admission rather than a global pending scan. |
| Complete token/index diagnosis | Compatible extension | Per-slot tag and reciprocal relation checks give an `O(T)` token component; other complete-validator costs remain separately owned. |
| Close reference scan | Baseline parity | Retain the existing lifecycle pass and constant token-relation work per examined thread. |

Append at most one fixed-width phase and one existing typed handle per
thread; conservatively bound actual registry growth by `16*T` bytes.
No process/hart member, global map, heap, history, epoch or capacity-sized
per-operation witness is required. The selected-completion plan retains one
additional caller pointer inside its existing 256-byte cap.

The [canonical contract](../architecture/direct-reply-token-binding.md) owns
the exact encoding, transitions, corruption/error scope, writer obligations
and acceptance. Its implementation must preserve current model counts,
independent reference data, real target composition and complete stack proof.

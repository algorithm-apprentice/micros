# MINIX Head-Only IPC Sender Queue Study

## Fixed inputs

```text
micros source: f88f35f545a496e8cc595665b99a0ab311ad0f29
micros tree:   2d6d9864dd5369276ccf446b97b34d3d7a54a4d2
MINIX source:  4db99f4012570a577414fe2a43697b2f239b699e
```

Both local checkouts were clean at these revisions. IPC03 is independently
reviewed and merged as #255. IPC04 follows it sequentially, but its actual
correction-DAG prerequisite remains IPC01 #251.

This study owns the fixed-source evidence for B04-F006, not a new performance
claim. The [B04 audit](minix-complete-baseline-b04-endpoint-ipc.md#b04-f006-sender-tail-indexing-is-a-pre-baseline-optimization)
requires the head-only sender list before evaluating a tail optimization.
The [canonical contract](../architecture/head-only-ipc-sender-queues.md)
owns the proposed replacement and acceptance.

## Fixed MINIX behavior and mechanism

Paths are relative to the local reference checkout.

| Source | Evidence |
| --- | --- |
| `minix/kernel/proc.h:23-90` | A destination owns `p_caller_q`; each sending process owns `p_q_link`. There is no retained caller-queue tail. |
| `minix/kernel/proc.c:868-958`, `mini_send` | Attempt immediate matching first. On blocking send, check deadlock and copy the message before blocking/publication. Walk from the destination head to the null link and append there. |
| `minix/kernel/proc.c:965-1117`, `mini_receive` | Check the established pending-work classes, then scan caller order for the first acceptable sender. Splice through the selected head/predecessor link, clear the removed sender's link and preserve unmatched order. |
| `minix/kernel/system.c:507-566`, `clear_ipc` and `clear_endpoint` | Inhibit the exiting process; find and unlink its outstanding send through the destination head. Cancel exact endpoint references through the existing lifecycle scan. |

Append visits every existing sender. For queue length `q`, its average and
worst work are `Theta(q)` with constant cursor storage. Enqueuing `m` senders
from empty without removals visits `m*(m-1)/2` existing links: aggregate
`Theta(m^2)`, not amortized constant-time append.

Receive visits the first matching prefix, or all `q` members on no-match.
Known-splice mutation is constant-time after that search. Without a stated
source/match distribution, no better average search bound is claimed.
Exit retains its process/reference and affected-queue work.

MINIX stores process pointers and performs its append walk after blocking.
`micros` uses checked thread handles and must reject a bad consumed link
without partial publication. Moving the fallible walk into read-only
preflight, while retaining one automatic terminal pointer for commit, is a
failure-atomic adaptation. It does not justify a persistent end index.

## Current micros ownership census

| Current owner | Consumed state or mutation |
| --- | --- |
| `include/micros/endpoint.h`, `micros_endpoint_record` | Persistent `sender_head` and `sender_tail`; separate receiver head/tail and notification state. |
| `ipc_queue.c`, `preflight_queue_endpoints` / `ipc_prepare_queue_append` | Admit the declared first/last members directly; obtain the cached tail with no sender-list traversal. |
| `ipc_queue.c`, `ipc_select_queue` | Search first-match order, using the declared tail for termination; retain predecessor/match and check the consumed successor/back-edge. |
| `ipc_queue.c`, `ipc_append_thread` / `ipc_unlink_thread` | Publish the selected link and update the persistent tail. The helpers also serve receiver queues. |
| `ipc.c`, `send_selected` | Receiver search, then existing deadlock preflight, then sender-append preparation and non-failing publication. |
| `ipc_rendezvous.c`, sender enqueue / sender publication / selected receive | Independently checked low-level enqueue, ordinary/call sender publication and consumed-sender splice. Receiver publication is a separate retained-tail path. |
| `ipc_reply.c`, `publish_reply_receive` | Splice a selected sender inside the existing failure-atomic combined operation. |
| `ipc_close.c`, queue preflight / commit | Validate all queues with the shared membership witness, rebuild retained order, terminate the surviving list and publish head/tail. |
| `endpoint_codec.c`, zero/reset helpers | Check and clear the sender tail as part of endpoint storage. |
| `endpoint_registry.c`, shallow registry validation | Check sender and receiver head/tail pairs. |
| `endpoint_queue_validation.c` and `endpoint_object_validation.c` | Selected-record validation, complete membership, source/role checks and matchability depend on the declared sender tail. |

The current append is not a head-only walk with an incidental cache: its
preflight admits at most the first and last records regardless of intervening
length. An invalid unconsumed middle record can therefore be outside its
selected proof. The new walk consumes that record and must reject it.

Receive must not compensate for removing the tail by traversing the entire
sender suffix before every head match. Preserve the checked first-match
prefix, immediate successor admission and at most one prefix replay.
The existing receiver queue is a distinct, already Accepted extension for
thread-owned waits; its tail is not the B04-F006 correction.

### Independent evidence consumers

The native queue, send/receive, call/reply, notification and close fixtures
construct or inspect sender tails. Some reply/close fixtures temporarily
remove and restore a queued sender. Those manipulations must preserve the
same head/link topology without manufacturing a new production index.
The trusted IPC target fixture and selected-IPC serializer also inspect the
field. All defined remaining state still needs comparison.

The independent persistent model has its own `model_endpoint.sender_tail`.
It may retain that private abstract-queue representation: it is not a
production algorithm or work oracle. Production equivalence still compares
the head and every thread link/role/message, so removal of one unavailable
production scalar must not remove ordered membership evidence. Preserve the
model's own decisions, original transitions, seed and trace.

The existing observation binding already distinguishes queue search,
link traversal and indexed access. On the fixed production source,
`ipc_prepare_queue_append` has no link-traversal loop. A valid nonempty
append can therefore fail an independently required exact `q` traversal
count while its FIFO semantics pass. No old runtime needs to be reconstructed
or executed during future ordinary acceptance.

## Classification and bounds

| Mechanism | Classification | Bound or reason |
| --- | --- | --- |
| Head-only sender FIFO and linear append | Baseline parity | `Theta(q)` walk, constant post-preflight splice. |
| Exact generation-safe thread links and role checks | Compatible extension | Existing checked identities add constant work per visited member, not a global audit. |
| Fallible walk before blocking, retained terminal for commit | Compatible extension | Preserves failure atomicity with constant temporary storage and unchanged publication effects. |
| Separate receiver FIFO with its existing tail | Compatible extension | Existing thread-owned-wait adaptation, unchanged by this sender-only correction. |
| Complete shared membership diagnosis and atomic close | Compatible extension | Existing diagnostic/lifecycle witnesses remain; queue work is linear in examined links. |

Remove one existing eight-byte handle per endpoint; add no replacement
persistent field, count, map or cache. The expected RV64 registry reduction
is `8*P`, currently 512 bytes, subject to actual compiler verification during
implementation. Process/thread/hart layout, the message ABI and reply binding
do not change. Append/search add no capacity-sized automatic storage.
Existing close and complete-validator witnesses retain their original scope.

The [ADR](../adr/0127-head-only-ipc-sender-queues.md) owns the representation
decision and rejected alternatives. This study does not claim that restoring
the reference append algorithm improves execution time.

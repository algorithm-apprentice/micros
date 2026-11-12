# Head-Only IPC Sender Queues

## Status and scope

This is the independently reviewed IPC04 contract Accepted by
[ADR-0127](../adr/0127-head-only-ipc-sender-queues.md).
Design #256 is separately merged; its paired implementation proceeds from
that main. The design alone does not close B04-F006.
The [fixed study](../research/minix-head-only-ipc-sender-queues.md)
pins local MINIX and current `micros` after IPC03 #255.

IPC04 owns only B04-F006. IPC01 #251 is its merged prerequisite; following
IPC03 is sequential execution, not a new runtime-DAG edge.

Replace persistent sender-tail indexing with the MINIX head-only intrusive
FIFO and checked linear append. Preserve permissions, exact generations,
first-match order, notification precedence, call/reply phases, independent
RTS reasons, scheduler/return boundaries, close and failure atomicity.
This restores a baseline; it is not a faster-append claim.

Receiver-tail removal, filters, asynchronous IPC, user threads, SMP,
different token/deadlock algorithms, new public APIs, generalized containers,
compatibility/history/recovery state and larger budgets are non-goals.

## Persistent state and invariants

Remove only `sender_tail` from `struct micros_endpoint_record`.
`sender_head` is the authoritative entry; `micros_thread.ipc_next` is the
existing generation-safe successor. Preserve every other endpoint member,
including `receiver_head`, `receiver_tail` and notification state.

An empty sender queue has exact null head. A nonempty queue is a finite
head-reachable sequence of exact live handles, each in the canonical sender
role for this destination, whose final next link is exact null.
The current ordinary-versus-call shape, source endpoint, destination,
IPC03 phase/token checks and complete shared-membership invariant remain.
Noncanonical null, stale/out-of-range handle and wrong consumed role fail.

No persistent replacement tail, last-slot count, auxiliary map, cached
validation result or reverse link is permitted. An operation-local terminal
pointer is allowed only after walking the actual head-reachable sequence.
It expires with the current serialized operation, not on a later enqueue.
No pointer survives return, interrupt enable, a blocking interval or object
reuse.

The endpoint record is kernel-internal C state, not a user wire or persisted
format. Remove the member directly; keep no dummy field or compatibility
alias. Public function signatures, handles, 64-byte messages and syscall
registers/results do not change.

## Checked append and publication

The high-level order remains:

1. existing argument, participant, destination and permission admission;
2. canonical message snapshot and first matching receiver selection;
3. only on valid no-match, existing deadlock preflight;
4. read-only sender append preflight from `sender_head`;
5. existing non-failing sender/call publication.

Keep call-ticket planning and success-only high-water publication at their
current boundaries. Low-level enqueue retains its existing kernel-only
structural contract and independent receiver selection; it does not gain a
deadlock or user-authorization policy merely to share append preparation.

For an empty head, return a null terminal. Otherwise visit members in link
order. Before indexing or following each consumed handle, use the existing
selected queue-member admission, including exact owner, source/destination,
RTS, message, call and successor identity checks. Retain the actual final
member only when its next handle is exact null.

Visit at most `T` members. Exact null after the final permitted member is
success, not an off-by-one failure. A nonterminating chain, repeated cycle or
bad consumed record is `INVARIANT`; never repair it or return a no-match
fallback. Publish the terminal output only on success. Use constant local
state, not a visited array or a full-registry validation pass.

Commit initializes the new sender's existing role, message, destination,
null next and optional call binding, then writes either the empty head or
the retained terminal's next link. It performs no queue traversal, handle
checked lookup or recoverable check after publication starts. Direct stores
through already admitted handles remain part of the existing commit; they
are not another identity search.
Preserve the current scheduler/captured-caller transaction and all failure
bytes and outputs.

Compared with IPC01's cached-end admission, append now consumes the whole
sender chain. A malformed middle record that was outside the old selected
scope is therefore rejected before mutation. This is intentional consumed
scope, not permission to audit unrelated endpoints or tables.

## First-match selection and splice

Sender selection starts at the head and uses the existing specific-source
or `ANY` predicate. Return the first match; do not reorder skipped senders.
On no-match, finish at exact null within `T` members.

Retain the checked predecessor, selected handle and record. A selected
terminal is recognized by its null next, not by an index. A nonnull
successor retains the existing admission and back-edge checks, including
the successor's own checked next identity. At most one replay of the
consumed prefix may reject a successor that reenters it. Do not replay a
growing prefix at every node or prewalk an unconsumed suffix to find its end.

After the existing final fallible scheduler step, splice the selected
sender through the head or predecessor next link and clear its removed
queue link at the existing completion boundary. Preserve ordinary-send
wakeup versus call `SEND` removal and `QUEUED -> STAGED` publication.
Atomic reply/receive consumes the same retained splice.

An unreachable or otherwise unconsumed malformed suffix remains a complete
diagnostic obligation, as under selected validation. Removal of the
out-of-prefix cached-tail check does not authorize stale consumed links,
cross-queue roles or a second match search during commit.
Receiver selection, receiver-tail admission and receiver mutation stay
unchanged.

The existing private append/unlink owners may retain their shared head/link
mutation and an optional receiver-tail writeback. Sender callers supply no
tail writeback; receiver callers retain the exact existing one. This is a
closed distinction between the two current queue kinds, not a new container
framework or caller-controlled validation bypass.

## Close and complete diagnosis

Update sender-specific termination in selected-record checks, complete queue
validation, matchability and close preflight to exact null. Receiver checks
continue to require their actual declared tail.

Complete diagnosis retains the shared `seen` witness across sender and
receiver queues, exact role/owner checks, missing/duplicate membership
rejection and the prohibition on immediately matchable queued work.
Keep existing profile, notification, token and object diagnostics; do not
claim their entire validator becomes linear.

Close retains its original thread/reference preflight, shared queue witness,
scheduler batch and non-failing cancellation. Its existing queue walk
retains survivor order and clears the final survivor's next link.
An automatic last-survivor handle inside that commit is not a persistent
sender index. Publish the sender head only; receiver head/tail still commit
together. Preserve successful-delivery drain barriers, IPC03 pending
relations, source-only behavior, foreign `ANY` waits and cancellation errors.

Accept a valid full-capacity null-terminated queue. Reject a bad head,
interior link, generation, role, cycle or duplicate membership before the
scheduler batch. No extra capacity pass, per-operation registry copy or
fallible post-publication validation is introduced.

## Ownership and storage

| Owner | Required change |
| --- | --- |
| `include/micros/endpoint.h` | Remove the one persistent sender-tail member. |
| `ipc_queue.c` and its existing private header | Checked head-only append/selection and constant splice; preserve receiver-tail behavior. |
| `ipc.c`, `ipc_rendezvous.c`, `ipc_reply.c` | Keep current admission/order; consume the new sender terminal/splice without another lookup. |
| `ipc_close.c` | Null-terminated sender preflight and survivor publication inside the existing transaction. |
| Endpoint codec/registry/queue/object validators | Sender head shape, clear/reset, full membership and null-terminated matchability. |
| Existing native/reference/target fixtures and serializers | Preserve ordered queue and all remaining defined state evidence without a production tail field. |

The expected RV64 persistent reduction is one eight-byte handle per endpoint,
currently 512 bytes. Verify actual compiler geometry; add no replacement
persistent runtime storage and change no process/thread/hart member or
assembly layout.
Append and selected search use constant additional automatic storage.
Existing 256-byte selected-receive/completion plan caps, lifecycle witnesses,
stack budgets, compiler/machine closure and B13 block remain.

Current observation sites, source ownership and any genuinely affected
layout/artifact qualification are updated with implementation under ADR-0123.
Do not introduce a historical-header projection, metadata-only prerequisite
PR or new execution workflow.

## Work contract

Let `q` be the existing sender length, `p` the selected prefix including the
match, `Q` all links examined by close, and `P`/`T` existing capacities.

| Boundary | Required work |
| --- | --- |
| Empty sender append preflight | Zero sender visits; constant checks. |
| Nonempty sender append preflight | Exactly `q` link visits/member admissions, once in head order; no capacity-table scan. |
| Sender append commit | Constant stores; zero traversal or fresh checked lookup. |
| Sender no-match search | Exactly `q` search-link visits; no end-index lookup or second search. |
| Sender match | `p` search visits plus at most `p` prefix-replay visits and one successor admission; no suffix walk. |
| Known sender splice | Constant stores after the retained admission. |
| Close queue component | One existing preflight and one commit walk; at most `2*Q + 2*P` loop visits including current terminal/empty-queue probes. |
| Complete membership component | `O(P + Q)` with its existing shared witness; separate matchability costs remain. |

Selected member checks remain constant per visited row under IPC01/IPC03.
No complete audit, hidden table scan, repeated queue selection or new
capacity-sized scratch is charged outside the measured operation.
Measure actual link, indexed, table and scheduler work at existing executing
primitives; observations must not manufacture visits from queue length.

Append is `Theta(q)` in average and worst cases for a fixed length.
Repeated append from empty without removal has quadratic aggregate work
and no constant amortized claim. This is the explicitly required MINIX
baseline, not a timing optimization.

## Test-first and phase acceptance

Implementation begins only after the separate design merge. Extend the
existing native pair and existing queue/lifecycle tests, not another runner
or duplicate execution.

First add valid FIFO cases against unchanged production. Isolate blocked
sender append with no receiver traversal: use queue lengths 0, 1, 7, 63 and
127 in the larger native policy, including sparse/permuted slots.
The last is a kernel-only structural enqueue with room for its one new
sender; high-level send/call cases retain feasible live destination peers.
Require ordinary/observed semantics to agree, but require exactly `q` real
append-link visits. The old cached-tail path's zero visits for nonempty
queues is the genuine algorithm Red, not a timeout or compilation failure.

Green additionally preserves or adds focused cases for:

- empty, singleton, repeated append and final-slot append, exact FIFO links
  and terminal null, then successful full-capacity diagnosis/close;
- `ANY` and specific head/middle/final/no-match receive, skipped-order
  preservation, splice back-edges and an unconsumed-suffix diagnostic control;
- stale/out-of-range/noncanonical heads or successors, wrong middle roles,
  early and non-head cycles, and failure-atomic append after a valid prefix;
- ordinary send, queued call, reply/receive and first/middle/last/all sender
  cancellation, with unchanged receiver tails, notifications and reply phases;
- complete shared-membership rejection, same-slot new generations, exact
  failure state/output bytes and no production replacement end index.

Keep all original case families. Replace obsolete tail-only corruption or
end assertions with the corresponding actual head/link/ordered-membership
evidence; do not silently drop their failure-preservation checks.
The independent model may keep its own tail as an abstract-queue convenience,
but never call production selectors to determine expected order.
Retain all 8,192 IPC transitions, 4,096 notification steps, 512 close cases,
original seeds and target scenarios. Current serializers cover every
remaining defined production member; no expected data comes from candidate
output.

Run the existing actual IPC/ecall/syscall, selected-return, bootstrap, PM and
composed lifecycle evidence selected by the authoritative final planner.
Preserve every required image/static/observed stack scope and budget.
One sufficient final author plan supplies affected-unit evidence; optional
receipt acceptance applies only if that feature's recipe is changed.

Independent design review precedes the design commit. Independent
implementation review precedes its first permanent commit. Only accepted
implementation and exact-tree merge close B04-F006.

## Current implementation and evidence owners

The endpoint record now retains only the sender head. The existing IPC queue
owner walks and admits the actual sender chain before append, retains its
terminal locally and publishes without another walk. Sender selection,
complete diagnosis and close use null termination; receiver-tail behavior
is unchanged. Shared mutation helpers write a tail only for receiver queues.

The existing ordinary/observed native pair has 111 cases: all 98 predecessor
cases and thirteen sender cases in `selected_ipc_queue_test.c`. Actual append
visits are exactly 0, 1, 7, 63 and 127; the permuted seven-member case also
visits seven links. Each append has zero capacity-record visits.
Additional cases cover a corrupt middle, stale/out-of-range successor,
non-head cycle, unconsumed suffix, preserved no-match output and all-128
sender close. The full close records 383 queue visits within the independent
384 bound. Its existing scheduler request passes remain separately counted;
the receive-splice control likewise retains four passes over two requests.

Original semantic fixtures now compare actual head/link order and terminal
null, including their temporary singleton detach/restore operations.
The independent model retains its private queue-end representation and
compares every remaining production head, link, role and message without
calling production selectors. Its transition count and seed are unchanged.

Actual native and RV64 compiler artifacts report a 568-byte endpoint record
and 38,424-byte registry: 512 fewer persistent bytes in the admitted 64-slot
registry. There is no replacement index or process/thread/hart layout change.
Current primitive bindings cover the added append loop and all changed
owners; no new execution workflow or historical source transport is added.

Exact final author, stack, review and publication evidence belongs to the
implementation PR. Focused native or target success alone is not finding
closure.

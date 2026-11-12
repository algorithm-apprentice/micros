# Direct Reply-Token Binding

## Status and scope

This is the independently reviewed IPC03 contract Accepted by
[ADR-0126](../adr/0126-direct-reply-token-binding.md).
The implementation remains pending separate design merge.
The [fixed study](../research/minix-direct-reply-token-binding.md)
pins current source to IPC02 #253 and the local MINIX reference.

IPC03 owns only B04-F004. Its dependency is merged IPC01 #251; development
proceeds sequentially after IPC02. This does not add a runtime-DAG edge.

Replace the allocation, waiter and staged-request scans with direct current
bindings. Preserve the 64-byte message, all six syscalls and stable result
codes, opaque 64-bit tokens, exact caller/callee ownership, request-return
authority, one-shot reply, failure-atomic reply/receive and endpoint close.

Non-goals are sender-queue redesign, filters, asynchronous IPC, user threads,
SMP, different scheduling/memory policy, new public completion operations,
recovery, compatibility machinery, larger budgets/deadlines or another
finding's closure. IPC02's sole-thread index and dependency walk remain intact.

## Opaque direct ticket

### Representation and issuance

The low seven token bits name a thread slot in the existing 128-slot domain.
The rest of the value participates in monotonic issuance, not a user-visible
thread identifier. Zero is never issued. Require a compile-time admission
that the implemented thread capacity is positive and no greater than 128.

Keep the existing `uint64_t last_reply_token` as the sole high-water value
`H`. For an admitted caller slot `s`, choose:

1. `x = 128 * floor(H / 128) + s`;
2. if `x <= H` and `x > UINT64_MAX - 128`, fail with
   `MICROS_IPC_ERROR_REPLY_TOKEN_EXHAUSTED`;
3. if `x <= H`, add 128;
4. on success, the planned token is `x`, the least value above `H` with
   `x mod 128 = s`.

Arithmetic must not first overflow and then test the wrapped result.
No scan, search, retry or second
counter is needed. Publish `H = x` only after successful call commit, exactly
at the existing publication boundary.

For independent examples with `H = 0`, a caller at slot 0 receives 128,
slot 1 receives 1, and slot 127 receives 127.
Repeated successful calls by one slot advance by 128.
Cross-slot calls still produce a strictly increasing global sequence.
With `H = UINT64_MAX - 1`, only slot 127 can issue `UINT64_MAX`; another
slot fails atomically even though that differently tagged value remains.
Exhaustion is therefore "no greater token for this caller", not a global
permission to wrap, reset or borrow another slot's tag.

This deliberately supersedes consecutive numeric issuance and its old
terminal-tail assumption. It preserves nonzero opaque `uint64_t` values,
strict monotonicity, nonreuse and explicit exhaustion. Clients continue to
echo the complete value; no old/new token negotiation or migration exists
within one registry lifetime.

Different slot tags cannot produce the same token. Reissuing at one slot
requires a value above the global high-water mark. Together with one active
call per thread and the existing no-release-with-IPC rule, this preserves
exact caller lifetime without a separate allocation or uniqueness scan.
Neither token predictability nor a decoded slot grants reply authority.

### Direct selected resolution

After existing replier, message and operation-permission admission:

- token zero is `REPLY_TOKEN`;
- decode and range-check the slot before indexing;
- a free/quarantined slot or nonmatching stored active token is `REPLY_TOKEN`;
- for a matching live token, admit the complete current thread handle, exact
  owner/endpoint, token high-water/tag relation and current request shape;
- malformed consumed identity, phase or reciprocal binding is `INVARIANT`;
- a valid queued or staged request is not yet presentable: `REPLY_TOKEN`;
- a returned request still requires the exact callee endpoint generation,
  active caller endpoint and optional expected-caller endpoint. An ordinary
  authority mismatch retains `REPLY_TOKEN`.

Preserve the current argument/replier/operation ordering and the last fallible
scheduler boundary. Do not select a representative through a process's
`sole_thread`; multiple native caller threads each retain their own binding.
No caller pointer or plan survives serialization, blocking or object reuse.

## Current request phase and receiver index

Append these kernel-internal fields to `struct micros_thread`:

- `uint32_t ipc_reply_phase`, with exactly `NONE=0`, `QUEUED=1`, `STAGED=2`,
  `RETURNED=3`;
- `struct micros_thread_handle ipc_request_receiver`.

Define the phase names in the kernel-object contract, not the user wire ABI.
Keep existing fields and their meanings. The four phases describe only the
current active outgoing call; receiving another thread's request does not
give the receiver its own active reply binding.

| Phase | Existing state | Receiver index |
| --- | --- | --- |
| `NONE` | No active caller token or callee; ordinary clear/send/receive/completion roles retain their existing rules | Exact null |
| `QUEUED` | Canonical call sender, `SEND | REPLY`, exact outbound token and callee | Exact null |
| `STAGED` | Canonical reply-only wait; request staged on one exact callee thread | Full nonzero live receiver handle |
| `RETURNED` | Canonical reply-only wait; request completion consumed | Exact null |

The IPC masks above do not erase independently allowed runtime flags.
Preserve the current source-only reply-wait restrictions and all other queue,
message, buffer, scheduler and endpoint shape checks.

A `STAGED` link must identify a live exact-generation thread whose successful
pending inbound request contains this full token and caller endpoint, whose
owner is the exact callee endpoint generation, and whose own active caller
state is `NONE`. The reverse staged-request check decodes its token, admits
the caller, and requires that caller's `STAGED` link to name this exact
receiver handle. A null, noncanonical null, out-of-range, stale, foreign or
nonreciprocal staged index is an error, never a scan or an inferred return.

The explicit phase is necessary: clearing only the receiver index must not
turn an undelivered request into presentable authority. `RETURNED` with a
nonnull link, `QUEUED` with a link, an unknown phase, and residual phase/link
state with no active token are likewise invalid.

Use shallow checked role predicates and one directional relation proof.
The caller check must not recursively invoke the receiver's complete token
check and back again. No full endpoint/object validator or table walk is
permitted in this selected relation.

## Exact transition owners

Every new field check precedes the existing final fallible step; every new
publication joins an existing non-failing commit.

| Owner | Required transition |
| --- | --- |
| `ipc.c:micros_ipc_call` | Retain the admitted caller slot and planned ticket; remove the global availability search, without moving successful high-water publication. |
| `ipc_rendezvous.c:ipc_publish_sender_wait` | A call publishes `QUEUED` and null receiver; an ordinary send keeps `NONE` and null receiver. |
| `ipc_rendezvous.c:ipc_deliver_selected_sender` | An immediate call publishes `STAGED` and the selected receiver's complete handle with its staged request. |
| `ipc_rendezvous.c:ipc_deliver_selected_receive` | A selected queued call moves `QUEUED -> STAGED` and records this exact receiver after scheduler success. |
| `ipc_reply.c:publish_reply_receive` | Its queued-sender branch makes the same transition; all three receive branches retain the old atomic reply/receive order. |
| `scheduler_completion.c` | Token-bearing request preflight retains the admitted caller pointer. Successful return commit moves `STAGED -> RETURNED` and clears its receiver link with consumption of that request. |
| `ipc_tokens.c:ipc_commit_reply_delivery` | Clear the consumed caller's token, callee, phase and receiver link when staging the zero-token reply. |
| `ipc_close.c` | Include indexed pending relations in reference preflight and clear every canceled caller binding with existing close publication. |
| `kernel_object_state.c` | Full IPC clear/reset includes `NONE` and exact null; creation, release, context and PM empty/prepared checks must not ignore residual new state. |
| Direct PM teardown | Its existing complete thread clear leaves no new state. The stopped EXIT-call predicate requires `RETURNED` with null receiver before detach. |

Only successful request return makes a token presentable. Staging, inspecting,
selecting another thread or preparing a return does not.

### Selected target completion

Keep the IPC01 retained message/ABI result and physical at-most-two-page
write plan. For a token-bearing request, preflight also retains the exact
checked caller pointer; other completions retain null. The existing selected
completion plan remains within 256 bytes.

Commit first performs the existing physical request copy. Then, within the
same SIE-clear non-failing return commit, publish `RETURNED`/null on that
caller and consume the receiver completion. Preserve existing context/register
stores, root/timer preparation and panic behavior. Do not look up a token,
thread, endpoint or user address again during commit.

A buffer failure in preflight leaves `STAGED` and its index unchanged.
An accepted bootstrap buffer fault still panics with the original reason;
there is no recovery path that grants reply authority without delivery.
The index is temporary routing evidence, not permanent callee-thread
authority: after return, an authorized sibling thread may reply as before.

### Close and source-only boundaries

Retain the existing bounded close transaction, generation checks, queue
rewrites, notification cancellation and successful-delivery drain barrier.
Closing either participant cannot discard a staged successful request merely
by clearing its new link.

The existing thread pass must also examine every `STAGED` phase or nonnull
receiver link before treating a binding as unrelated. Admit the full receiver
identity and reciprocal relation before using its owner for reference
classification. An invalid consumed pending relation fails atomically,
including one whose legacy endpoint fields do not mention the closing slot.
Do not equate receiver thread slots with process slots.

This is a constant selected relation per existing pass, not another full
validator or additional capacity pass. Other unrelated legacy-state
corruption retains IPC01's consumed-scope policy. Source-only sealing alone
does not consume a token, return a request or clear its phase/index.

## Complete diagnosis and corruption scope

Complete endpoint diagnosis still visits every live/nonlive role. Check
per-thread token tags/high-water, phase shape and both directions of every
staged relation. Duplicate active tokens cannot both satisfy their distinct
slot tags; duplicate staged copies cannot both equal the caller's one
receiver handle. This replaces only the token-specific pair/search loops,
not unrelated complete queue, notification, profile or object obligations.

An ordinary selected lookup is not a proof over all unindexed rows. The
old scans could discover an unrelated wrong-slot active duplicate or an
orphan staged token even when no canonical caller matched. Those hidden
records no longer dominate a direct lookup. Complete diagnosis must still
reject them, and no wrong-slot duplicate can route or consume another reply.
An unknown token may therefore yield `REPLY_TOKEN` instead of an unrelated
global corruption error.

Do not weaken checks on a matching caller or its indexed pending receiver.
Tests must distinguish a missing index with `STAGED` (always invariant) from
coherently falsified phase plus an unindexed foreign staged record (outside
selected proof, still rejected by complete diagnosis). No selected success
certifies that arbitrary kernel corruption has not occurred elsewhere.

All rejected operations preserve every promised output and actual state
byte. No lazy repair, alternate-thread search, cached audit success, consumed
token history or partially fallible commit is permitted.

## Source ownership and layout

Keep token planning/preflight in `ipc_tokens.c`, orchestration in its current
IPC owners, completion in the scheduler return owner and diagnosis in the
endpoint owners. Put the small shared token codec/selected-relation leaves
behind the existing endpoint internal interface so endpoint validation does
not call upward into IPC operations. Avoid duplicate codec constants and
recursive validator dependencies. Object clear/state ownership stays below
endpoint semantics.

No public user syscall/message/handle representation changes. Add no process
or hart field, global token map, allocation pool or per-operation table.
The two appended fields add twelve raw bytes per thread; bound compiler-measured
registry growth by `16*T`, currently 2,048 bytes, including alignment.
Only one additional retained pointer belongs in the completion plan.

Qualify the actual new thread stride, registry extent and unchanged field
offsets with the current compiler/source/machine machinery. Admit only these
typed additions at the existing finite header boundary, retain header-identity
cache keys and all fatal/indirect/assembly obligations. No budget or exclusion
changes.

Current fixture construction, defined-state serializers and independent
models include the new fields. The OBJ01 fixed common reference data remains
unchanged: its scheduler-only cases do not create active call tokens.
Extend only the explicit old-layout common-byte view for the new appended
thread fields, keep full current raw preservation/agreement, and check new
phase/index fields independently. Never normalize a malformed live binding
away or replace expected data with candidate output.

## Work bounds

Let `T` be thread capacity, `q` the actually consumed queue prefix and `d`
the dependency path. The current slot-tag domain has exactly seven bits.

| Boundary | Required token-specific cost |
| --- | --- |
| Ticket planning and uniqueness | Constant arithmetic and selected state; zero table visits |
| Reply/expected-caller/reply-receive preflight | One directly selected caller plus at most one indexed staged receiver |
| Queued/immediate request staging | Constant additional stores after existing scheduler preflight |
| Selected request completion | Constant preflight; zero lookup/translation in commit |
| Successful reply consumption | Constant clearing with the existing reply publication |
| Close | Constant index work per existing examined thread; no new capacity pass |
| Complete token diagnosis | `O(T)` and constant additional scratch |

Report actual index probes, record visits and repeated selection at executing
primitives. Do not charge a token scan to fixture setup, another owner or a
new label. Preserve separately owned `O(q)` matching, IPC02's `O(d)` walk,
scheduler work, physical buffer translation and non-token diagnostic costs.
Average/worst/amortized token-specific bounds introduce no hidden scan.

## Test-first and phase acceptance

After separate design merge, extend existing native products where possible.
First demonstrate real scan-work Red using caller/token inputs where old and
new opaque values coincide, so semantics pass and the old `T` scans fail for
the audited reason. Then add the new explicitly specified numeric-boundary
and state-lifetime cases; do not capture candidate results as expectations.

Retain all original cases and add focused evidence for:

- every slot tag, sparse/high-slot allocation, repeated and interleaved calls,
  monotonic nonreuse, initial slot 0, final representable values, terminal
  slot-dependent exhaustion and failure-atomic high-water publication;
- multiple caller and callee threads, exact expected-caller checks, early
  sibling reply/reply-receive rejection, and authorized sibling reply only
  after actual request consumption;
- queued/immediate/combined delivery, `NONE -> QUEUED -> STAGED -> RETURNED ->
  NONE` and the immediate `NONE -> STAGED` path, cancellation and exact
  generation reuse;
- missing/stale/foreign/noncanonical receiver links, wrong/unknown phases,
  tag mismatch, reciprocal token/source/callee mismatch and full diagnostics
  for unindexed duplicate/orphan state;
- malformed pending relations in close, unchanged successful-delivery drain
  barriers, PM EXIT-call consumption and complete rollback/teardown;
- actual selected-return preflight/commit, early buffer-fault preservation,
  exact physical writes and only existing register effects;
- constant token-related work, bounded new storage, zero commit lookups and
  ordinary/observed agreement without shortening any scenario.

The existing native drain simulations and trusted IPC component drain must
model the new return publication. The independent model derives expected
phase and receiver identity from its own logical queues/staged messages,
not the production fields or codec helper; its logical token lookup may
remain a linear reference search. Preserve the IPC 8,192 transitions,
notification 4,096 steps, close 512 cases, original seeds and operation coverage.
Update exact opaque numeric expectations only from this contract.

Run the existing real IPC/ecall/syscall, selected completion, bootstrap,
PM and composed spawn/rollback/exit workflows. No new trap, MMU or assembly
mechanism is introduced; portable state/work and the existing actual-C
completion fixture supply Red, with target behavior retained.

Stage the complete implementation and run the authoritative current planner
without path narrowing. Keep all required native/target/integration/image,
documentation and static/observed stack scopes, current budgets and blocked
B13. Current recipe/observation/layout updates belong in this outcome under
ADR-0123, not another metadata prerequisite or duplicate recertification run.

Independent design review precedes the design commit. Independent
implementation review precedes the first permanent implementation commit.
Only accepted implementation and exact-tree merge close B04-F004.

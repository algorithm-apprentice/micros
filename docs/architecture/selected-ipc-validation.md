# Selected Endpoint, IPC and Completion Validation

## Status and scope

This is the independently reviewed IPC01 contract Accepted by
[ADR-0124](../adr/0124-selected-ipc-validation.md). Its implementation follows
separately merged design #250. The current implementation and evidence owners
are described below; the correction register owns acceptance and finding
closure. The [fixed study](../research/minix-selected-ipc-validation.md)
pins current `micros` at `c8b8a26766f4908016d0aeedc8af95f990eca283` and the
local MINIX reference at `4db99f4012570a577414fe2a43697b2f239b699e`.

The outcome owns B03-F006, B04-F001 and B04-F002. MEM04, OBJ01 and OBJ02
are merged prerequisites. Design acceptance alone closes no finding.

Replace repeated complete endpoint/profile/object audits with selected
admission and one retained operation plan. Keep permissions, generations,
queue order, notifications, reply authority, failure atomicity and real
selected-return delivery. Correct the coupled close walk's full-capacity
boundary without redesigning its queues.

The public endpoint/message/syscall ABI, persistent registry/thread layouts,
profile capacities, source-only lifecycle and one-thread/one-hart checked
policy do not change. Native multithreaded/hart fixtures remain.
IPC02 deadlock projection, IPC03 token indexes, IPC04 sender-tail replacement,
IPC05 filters, grant/VM/PM validator cadence, copy primitives, asynchronous
IPC, VM-assisted faults, signals, SMP and hosted CI are non-goals.

## One serialized operation

Portable callers retain external serialization; target callers hold SIE
clear across admission, planning and commit. There is no new lock or epoch.

The local sequence is:

1. reject the entry point's existing malformed arguments;
2. admit current registry/object metadata and exact selected identities;
3. snapshot outgoing data and select/validate every consumed peer, queue,
   notification, token or completion field;
4. finish all fallible scheduler/deadlock/buffer preparation;
5. perform the existing non-failing state publication;
6. publish success outputs and discard the local view.

An admitted view contains concrete current pointers/handles and already
selected policy/queue facts. It is not proof of unrelated state or permission
to call a prevalidated helper with arbitrary inputs. It cannot survive a
return, block, interrupt-enable boundary, context switch, generation reuse,
profile/lifecycle change or unrelated mutation.

Checked public entry points create their own view. Internal continuations
consume their caller's view without reentering public resolution/authorization
and repeating the same checks. Expose no `already_validated` switch and add
no persistent approval bit, cache, token-history field or table index.

Keep the distinction between high-level operations and kernel-only low-level
rendezvous helpers. `send`, `receive`, `call`, `reply`, `reply_receive` and
user `notify` enforce their existing permissions. The existing low-level
queue/delivery entry points retain their structural kernel contract and do
not invent a second user-authorization policy.

## Selected metadata and identity

### Registry and objects

Require the exact endpoint initialization magic and bounded nonzero
`profile_count`; retain the fixed zero profile-slot-0 sentinel. Do not
reconstruct installed IDs, duplicate names, target closure or every endpoint
at an ordinary query. Those complete checks remain at initialization and
explicit diagnosis.

Use OBJ01's existing selected object-header and thread predicates rather than
copying a second scheduler validator. Their declarations may be exposed
through `scheduler_core_internal.h` for endpoint/IPC use; the implementation
may move declarations or add thin internal wrappers, not change those
predicates' semantics. Endpoint/IPC files must not import
`scheduler_core_private.h` or another family's private implementation header.

Object initialization, checked policy limits and scalar live-count ranges
precede indexed access. A selected process must be live at the exact
generation with legal selected counts; a selected thread additionally binds
that process, defined RTS bits, context/stack shape and assignment under
OBJ01. Stored owner/profile values are checked at their full width before
indexing or narrowing. Profile value 257 cannot become profile 1.

Current-thread exclusion uses the selected thread's admitted assigned hart
and full handle equality, not a scan of all harts for each ordinary operation.
Lifecycle operations that actually inspect all current/owned threads remain
allowed to perform their declared bounded scan, admitting each handle before
dereferencing it.

### Endpoint and profile rows

An endpoint identity view binds:

- the decoded implemented slot and nonzero generation;
- the current endpoint lifecycle state, packed value and owner;
- the exact live process and primary endpoint;
- the consumed lifecycle flag and profile assignment;
- the selected immutable profile row when the operation needs policy.

Malformed input endpoints retain each API's current stale/argument mapping;
a stored owner, profile or link that cannot support the consumed operation
is invariant corruption. `ACTIVE` resolution still rejects `RESERVED` with
`STATE` and `SOURCE_ONLY` with `CLOSING`. Internal resolution retains its
existing lifecycle visibility; it does not authorize an IPC destination.

Identity-only queries do not inspect queues or pending-event arrays that they
will neither use nor mutate. Reservation/publication/close separately check
the selected record's required zero/held/reference state before writing it.
This keeps a direct query independent of process/endpoint capacity.

Profile lookup retains argument/output checks and direct indexed ID admission.
An absent/out-of-range requested ID is `PROFILE`; malformed consumed
installed-row shape is `INVARIANT`. Validate the selected row's fixed-size
canonical representation and operation bits, not other rows or pairwise
names. Operation queries preserve one-defined-bit validation; kernel-operation
queries preserve the `<64` bound. Target authorization resolves the source
and destination once each, tests the source operation, then its corresponding
destination bit in the existing order.

Immutable policy remains kernel-owned and sealed for the registry lifetime.
The operation does not turn a profile name, message field or guessed endpoint
into permission. Complete target-mask closure and duplicate-name detection
remain in the full validator even though they are not rediscovered per query.

## Checked queue selection and retained splice

Every queue read that replaces a former global preflight must check the
actual consumed handle before `objects->threads[slot]`:

- exact null encoding or an implemented slot with nonzero generation;
- live thread, exact process owner and the relevant endpoint relation;
- queue kind and required IPC RTS reasons;
- canonical outbound/receive/reply/completion fields for that queue role;
- the matching source/filter and current lifecycle state required by the
  original queue validator;
- safe next-link admission before it is followed or published.

Sender rows bind outbound source, destination and ordinary-versus-call shape.
Receiver rows bind their destination owner, specific source or `ANY`, aligned
nonzero buffer and absent token/completion. Stored specific-source references
retain the original `ACTIVE`/`SOURCE_ONLY` allowance; this does not relax
active-destination admission.

Replace Boolean-only matching with explicit error/no-match/match results.
Retain first-match order and unmatched links. A corrupt owner, stale consumed
generation, unexpected queue kind, loop or missing claimed tail is not
`NOT_READY`.

The selected splice retains the predecessor, matched handle, successor and
affected head/tail. Validate their equality/termination relations before a
scheduler mutation. A matched tail has null next; a non-tail match has an
admitted successor. Reject self-links and a successor back into the consumed
prefix. A single bounded replay of that prefix may establish the latter
without a capacity-sized workspace or pairwise comparison at every node.
It is a proof of the selected splice, not a second selection search.

A no-match walk must finish at the declared tail within at most `T` nodes.
Exactly `T` valid nodes ending in null are valid; exceeding the bound or
cycling is corruption. Head/tail nullness and the tail's exact kind/owner/
null-next state are checked before append. The existing tail index remains
until IPC04; ordinary append does not acquire a new all-queue reachability
certificate. Unconsumed cross-queue reachability/duplicate-membership
invariants remain complete-diagnostic obligations.

The fixed `preflight_close_queue` currently checks null only before each of
its `T` iterations and then returns `INVARIANT` unconditionally. Its checked
replacement must accept null immediately after the final permitted member.
This coupled defect needs a genuine full-capacity close regression, not a
smaller queue or a raised traversal limit.

The high-level operation retains its selection or no-match result through
deadlock/scheduler preparation. On enqueue fallback it must not resolve the
same endpoints, rescan the same opposite queue or retest an already selected
notification. Low-level public enqueue/delivery entry points perform their
own checked selection when called independently.

### Notifications

Retain kernel-pending, lowest-slot endpoint-pending, then FIFO-sender order
for `ANY`; specific receives ignore kernel events. User notifications keep
exact source/destination authorization and never unblock reply wait.

For a consumed user source slot, check bit/event consistency and exact active
source identity before reading or clearing it. Specific-source selection and
coalescing may not return no-match or repair state when that selected bit
disagrees with its event word. `ANY` may scan source bits in the existing
order; it does not audit unselected event words or every source profile.
An invalid stored selected source is `INVARIANT`, distinct from an invalid
user-supplied source argument's existing error.

Kernel injection retains source `NONE`, one nonzero opaque event mask,
zero token/tail, exact destination and no notifier thread/completion.
Its existing prepare/commit plan remains within one serialized lifetime and
is cleared on consumption. Pending coalescing has no new queue or profile
scan once the selected destination/receiver proof is complete.

### Existing deadlock projection

Keep the current sole-live-thread projection, `SEND -> REPLY -> specific
RECEIVE` dependency order, `ANY` termination and post-reply cleared-caller
argument. Admit each consumed projected thread and stored next endpoint
locally before following it; removing global validation must not turn a
stale link or malformed local role into a new dependency or no dependency.
Retain candidate-cycle `DEADLOCK`, repeated-intermediate `INVARIANT`,
one-thread projection rejection and the original capacity bound.

The existing per-hop thread scan and visited-prefix comparison remain
IPC02 work. Do not add a token-uniqueness scan per dependency node or replace
the algorithm with a new index.

## Reply authority and atomic reply/receive

Keep `last_reply_token`, monotonic exhaustion and all current token formats.
IPC03 owns indexed lookup; this task must not encode a slot in the token or
add persistent token bookkeeping.

Allocation retains its bounded availability scan once for the operation;
fallback does not repeat it. Reply retains a complete bounded live-thread
lookup for the requested token, rejecting multiple active matches, and the
bounded staged-request scan required by ADR-0036.
Only matched live records supply authority. Admit their owner/callee links
before indexing, require canonical reply-only wait for presentation, valid
retained buffer and token range, and preserve the optional exact
expected-caller check. A canonical queued `SEND | REPLY` call is valid but
not yet presentable: return `REPLY_TOKEN`, not `INVARIANT`. The existing
stopped source-only caller and a valid different callee/expected-caller
likewise confer no reply authority and retain `REPLY_TOKEN`.

A valid but unreturned staged request still yields `REPLY_TOKEN` without
mutation. Malformed consumed binding/completion is `INVARIANT`, not that
ordinary early-reply result. Do not clear, rewrite or borrow another thread's
staged message to make a token presentable.

The staged scan checks all live rows carrying the selected nonzero inbound
token, including malformed rows with a false pending flag or wrong result.
Validate each matching row's canonical receiver/source/message against the
already admitted caller, without a nested token scan. Reject malformed or
duplicate copies; only after the scan establishes one valid undelivered
request may it return the ordinary early-reply error. The selected-completion
binding pass applies the same matching-token rule.

`reply_receive` retains one reply preflight, then selects notification/sender/
wait state without consuming the token. Its post-reply deadlock check still
uses the original cleared-caller dependency argument. The existing combined
two/three-thread scheduler transition is the last fallible step before
non-failing reply and receive publication.

No receiver search, output translation, permission query or recoverable
failure is allowed after the first scheduler/IPC mutation. Preserve all
state and externally promised outputs on rejection. No new `reply` or
`reply_receive` permission, send-mask bypass or cross-process pointer
authority is introduced.

## Lifecycle operations and teardown

| Entry | Selected and whole-owner obligations retained |
| --- | --- |
| Reserve | Exact live process, no primary/profile, unconsumed endpoint lifecycle, complete selected FREE record zero state; pack before publication |
| Install profile | Exact reserved primary endpoint, no old profile, selected installed profile, every owned live thread exactly held/unassigned and none current |
| Activate | Exact reserved record and installed profile; do not invent an additional scheduling/lifecycle precondition |
| Preflight publish | Exact reserved/zero IPC record, no old profile, selected profile and held/unassigned owned threads; existing publication remains atomic |
| Plain source-only seal | Existing zero IPC and no pending-source/foreign-waiter restrictions, owned threads held/clear |
| Teardown source-only preflight | Exact active identity and consumed canonical state; preserve its distinction from the stricter plain seal and PM's retained reply-wait contract |
| Substrate close | Selected endpoint IPC state clear, no foreign pending/wait/staged references, owned threads held/clear; clear endpoint/profile but preserve process generation and lifecycle-consumed history |
| IPC cancellation close | Preflight the complete reference/queue state that the existing cancellation commit reads or rewrites, then one scheduler batch and non-failing publication |

These are not all constant-time queries. A selected zero pending-event array
has `P` entries; ownership/current discovery may need `T`/`H` scans; IPC close
must visit the relevant global reference fields and queue links. Retain those
actual costs instead of disguising them as selected scalar checks.

For IPC close, validate every consumed queue's owner, role, full generation,
tail and termination, with the existing linear seen-membership workspace.
Every removal plan must identify an actually reached member, and no member
may be consumed twice. Relevant notification bits/event words and all pending
sources in the closing endpoint must retain their canonical/liveness checks.

The thread reference scan distinguishes:

- owned threads and exact foreign send/receive/reply dependents;
- successful staged references that must drain rather than be canceled;
- already staged dead-endpoint completion that an owned close may discard;
- unrelated `ANY` receivers, which remain unchanged.

A stored reference naming the closing **slot** with a wrong generation is a
consumed corruption case, not an unrelated record to skip because complete
endpoint equality failed. This preserves existing stale-callee/staged-source
close regressions. Unrelated token uniqueness is not new close authority;
only an operation that presents or validates a token performs its required
binding scan. Do not introduce a per-thread global token scan during ordinary
successful close.

All relevant preflight completes before rebuilding queues, clearing
notifications, canceling tokens or publishing results. Keep the existing
close-plan/scheduler workspaces and no-failure commit; no new sort, hash table,
free list, persistent index or complete profile audit is added here.

## Selected completion and captured syscall

### Completion shapes

The selected-completion owner accepts only the existing shapes:

| Shape | Required authority and result |
| --- | --- |
| Not pending | Entire IPC completion state is clear; otherwise invariant. No endpoint/token/buffer lookup is needed. |
| No-message success | Exact active owner, no queue/IPC reasons/token, zero message/buffer and `OK` |
| No-message error | Same canonical clear shape with `DEAD_ENDPOINT`; no invented buffer or message |
| Ordinary message | Exact active receiver/source, non-kernel type, aligned nonzero receive buffer and canonical delivery state |
| Endpoint notification | Exact active source, reserved notification type, nonzero event mask, zero token/payload tail |
| Kernel notification | The same canonical envelope with source `NONE`; no source process or notifier authority |
| Source-only message | Only the original canonical bootstrap-ready acknowledgment exception, including its original payload checks and zero token |
| Token-bearing request | Exact active caller/receiver, canonical reply waiter, token range/callee/source match and no duplicate active/staged binding for that token |

For the last shape, one bounded thread-table pass may find the unique active
binding and reject another staged copy, as the existing helper does.
Admit the matching caller's owner before following it. This is the explicit
IPC03 residual, not permission to validate every thread's complete IPC state
or run pairwise token checks.

### Retained target plan

Preserve selected root preparation before completion preflight. Preflight
copies the exact result/message/receiver identity into the existing local
completion plan and calls `micros_ipc_buffer_prepare_write` once only when a
message buffer exists. That API retains one or two actual physical chunks
through the current MEM04 selected translations; it is not replaced with
direct user-VA dereference.

The same protected return interval then loads or reuses the selected context
under OBJ02, prepares timer/accounting, commits the retained message/result,
clears completion once, and applies the scheduler plan. No second translation
or endpoint/token lookup belongs in completion commit. Preserve all other
registers and every root/fence, stack-owner and timer failure boundary.

An invalid accepted receive mapping remains
`invalid-bootstrap-ipc-buffer`; malformed selected completion remains
`scheduler-ipc-completion`. A discarded private preflight scratch object is
not a published success plan. Do not extend public output-preservation claims
to existing internal scratch APIs that never promised them.

### Captured syscall

Replace `resolve_current_runtime`'s complete runtime audit with readiness and
exact current-thread/process/registry admission. Preserve runtime-not-ready
panic, request argument/buffer preflight, `sepc` advancement, active-controller
reply exclusion, context storage, current-thread guard, rollback, immediate
completion and captured return in their current order.

The bootstrap controller query is already a scalar exact-identity predicate;
it needs no new bootstrap mechanism. Guard begin/commit/rollback and context
reuse remain owned by OBJ01/OBJ02. Do not retain an IPC view across guard
mutation unless its declared fields remain unchanged, and do not reuse an
incoming user buffer's validation across a blocking interval.

## Error and corruption boundary

Preserve valid-state argument/authorization/lifecycle/queue/token/buffer/
deadlock precedence at each existing entry point and the unchanged portable
and syscall error mapping. In particular:

- malformed direct arguments precede mutation and retain their existing
  `ARGUMENT`/stale result; stored malformed references are not user arguments;
- registry not initialized retains the public endpoint error and target
  runtime invariant behavior;
- selected corrupt owners, high-width profiles, links, RTS shapes or
  completion/token relations fail before an unsafe read or write;
- unauthorized operation/destination remains `UNAUTHORIZED`, stale input
  identity retains its dead-endpoint mapping, and source-only destination
  retains `ENDPOINT_CLOSING`;
- reply token exhaustion/invalid/early presentation remain distinct;
- scheduler failure leaves every IPC/token/queue/output byte unchanged;
- `NOT_READY` means a proven absence of a match, never structural failure.

The validation-scope change is confined to corruption outside consumed scope.
An unrelated malformed profile, unused endpoint/thread/hart, unreachable
unconsumed queue suffix or unrelated token relation need not override a
selected result. It remains unchanged by the operation and must fail explicit
complete diagnosis. Do not report selected success as whole-state validation
or silently repair unrelated state.

## Complete-audit boundary ledger

The fixed source census has nineteen ordinary sites to replace:

| Owner | Current complete-audit caller(s) |
| --- | --- |
| `kernel/endpoint/endpoint.c` | `micros_endpoint_resolve_internal` |
| `kernel/endpoint/endpoint_privilege.c` | `micros_privilege_profile_resolve` |
| `kernel/endpoint/endpoint_publication.c` | `micros_endpoint_reserve`, `micros_endpoint_install_profile`, `micros_endpoint_activate`, `micros_endpoint_preflight_publish` |
| `kernel/endpoint/endpoint_close.c` | `micros_endpoint_preflight_source_only_for_teardown`, `micros_endpoint_close` |
| `kernel/ipc/ipc.c` | `micros_ipc_send`, `micros_ipc_receive`, `micros_ipc_call` |
| `kernel/ipc/ipc_rendezvous.c` | `resolve_sender_destination`, `resolve_receive_destination` |
| `kernel/ipc/ipc_notifications.c` | `resolve_notification_destination`, `micros_ipc_prepare_kernel_notification` |
| `kernel/ipc/ipc_tokens.c` | `ipc_preflight_reply` |
| `kernel/ipc/ipc_completion.c` | `micros_ipc_stage_no_message_completion` |
| `kernel/ipc/ipc_syscall.c` | `resolve_current_runtime` |
| `kernel/scheduler/scheduler_completion.c` | `preflight_selected_completion` |

The following twelve direct production boundaries remain complete:

| Owner | Retained caller(s) |
| --- | --- |
| Endpoint diagnostics | `micros_endpoint_registry_validate_objects -> micros_endpoint_registry_validate` |
| IPC runtime | `micros_ipc_runtime_initialize`, `micros_ipc_runtime_validate` |
| Bootstrap | `micros_bootstrap_control_validate`, `bootstrap_preparation_rollback_preparation` |
| Grants | `micros_grant_registry_validate`, `micros_grant_runtime_initialize`, `micros_grant_handle_captured_user_ecall` |
| PM | `prepare_environment_is_valid`, `micros_pm_control_exit_begin_preflight`, `validate_stopped_exit`, `micros_pm_control_validate_prepared_shape` |

Keep existing test/diagnostic calls as well. This ledger identifies current
responsibilities, not frozen source bodies or an EV01 key registry. An
internal extraction may move a check with its owning behavior; it may not
silently drop a retained boundary or hide a new global scan under a new name.

Full validators retain all profile, process, queue, notification, matchability,
token and complete-object corruption checks. A full relation audit still
runs one full registry audit first; its subsequent public profile queries
may benefit from direct lookup without removing any diagnostic predicate.

The implementation also makes one formerly transitive diagnostic dependency
explicit: `tty_handoff_live_binding_matches` retains the complete endpoint/object
check previously supplied by `micros_endpoint_resolve_internal`. This keeps
TTY's complete validator/getter and MEM04's existing scenario-50 diagnostic
rejection unchanged. Its native fixture also checks corruption in an unrelated
free process record, so a registry-only check cannot replace the original
object/relation diagnostic. The selected TTY binding inspector acquires no audit.
The current direct census is therefore the twelve retained original sites
plus that one preserved diagnostic dependency, not thirteen new audits or a
relaxation of the nineteen selected boundaries.

## Time, storage and independent work oracles

As in the study, `P/T/H/F` denote process/endpoint, thread, hart and profile
capacities. Let `q` be the consumed queue prefix, `n` the notification source
bits examined, `u` the explicitly required token-slot visits, `d` the
dependency-chain length and `S(m)` the unchanged work for `m` requested
scheduler transitions.

| Path | Selected work, including explicit residuals |
| --- | --- |
| Endpoint/profile resolution and authorization | Constant in `P/T/H/F`; one admitted row per required role, no global table/name/notification scan |
| Immediate send/notify and receive selection | `O(q + n + 1 + S(m))`; no full endpoint/object validation |
| Call and reply | The selected work plus current token scans, `u = O(T)`; no IPC03 claim |
| Blocking operation | The selected work plus unchanged IPC02 `O(d*T + d^2)` projection |
| `reply_receive` | One reply proof plus one receive selection and, only if blocking, its existing post-reply deadlock check |
| Selected completion | Constant local shape plus at most two selected translations; token-bearing request additionally permits one `O(T)` binding/staged-uniqueness pass |
| Lifecycle zero/owned-thread checks | `O(P + T + H)` when those data are actually required; do not charge them to identity-only queries |
| Successful IPC close | `O(P + T + total queue links + S(m))`, with existing `O(T)` close/scheduler scratch; no profile-name, endpoint-pair or token-pair audit |
| Complete diagnostics | Preserve complete predicates and their explicit cost, not an ordinary-operation claim |

These are deterministic worst-case bounds for valid operations; average and
amortized work introduce no additional hidden scans. A rejection may consume
the prefix or token/reference evidence needed to establish that rejection,
but never performs an unbounded walk. Existing invalid-request scheduler
fallback work and IPC02/IPC03 costs must be reported separately, not renamed
as new constant-time success.

No new persistent storage is added. An endpoint/operation view is at most
256 RV64 bytes; the selected completion plan is at most 256 bytes.
Do not add another capacity-sized automatic array outside the existing
deadlock and teardown/scheduler workspaces. Keep all actual compiler-derived
simultaneous-frame bounds within the existing stack budgets.

Actual primitive evidence must establish:

1. zero complete registry/endpoint/object/runtime audit invocations in each
   migrated ordinary interval, including nested helpers;
2. no unselected process/hart/profile scans in query/message/completion paths;
   each selected endpoint/profile role is admitted once rather than once per
   nested helper;
3. one matching search per queue selection, at most one consumed-prefix
   back-edge proof, and no duplicate no-match search on fallback;
4. specific notification selection tests one source bit; `ANY` visits at
   most `P` bits, kernel-pending selection precedes both;
5. at most `T` allocation-availability visits, at most `2*T` reply lookup/
   pending-request visits, and at most `T` selected token-completion visits;
6. no message-buffer translation for no-message completion, one/two actual
   translations for one/two-page messages, and zero lookups/translations in
   prevalidated completion commit;
7. lifecycle/close work counts only their declared linear scans, with no
   pairwise/global diagnostic substitute hidden behind helper extraction;
8. actual plan sizes, copied/cleared bytes, ordinary collector absence and
   ordinary/observed semantic agreement.

Primitive declarations cover actual current source/functions/sites, including
empty site sets where appropriate. New loops/callees/indirect work cannot
escape the observation inventory. Work bounds derive from this contract and
independent models, not measured candidate output or wall-clock thresholds.

## Test-first implementation and acceptance

### Native evidence

After design merge, begin with a small current-source observed/ordinary native
fixture for these boundaries. It must fail the zero-global/repeated-work
oracles on the unchanged implementation while valid semantic controls pass.
Use the existing collector/current-source mechanism; do not build old Git
products, create author journals or invent another generic framework.

Keep all existing endpoint/IPC cases and the complete 8,192-transition
independent IPC model. Add focused boundary/adversarial coverage for:

- first/last/sparse endpoint, thread, hart and profile identities; overflowed
  owner/profile values before dereference/narrowing;
- profile/operation/target denial and immutable-row queries with unrelated
  corruption still detected by full diagnosis;
- empty/singleton/maximum queues, specific-source unmatched prefixes,
  first/last match, consumed self/back edges, stale head/tail/successor,
  false no-match and late scheduler failure;
- immediate/blocked send/call/receive, every reply/receive branch, kernel/user
  notification coalescing/order and bit/event disagreement;
- exact reply-token range/exhaustion, duplicate active/staged bindings and
  early sibling-thread reply/receive before request return;
- reserve/install/publish/activate/seal/close preconditions and repeated/stale
  lifecycle attempts;
- close's stale same-slot caller/callee/staged-source cases, invalid queued
  generation, invalid pending source, successful completion drain, already
  staged dead-endpoint discard and unrelated `ANY` preservation;
- no-message success/error, ordinary/notification/source-only/token message
  completion, malformed residual non-pending state and at-most-two-page plans;
- byte-exact state and promised-output preservation, plus independently
  specified allowed success under wholly unconsumed corruption.

Retain the original explicit corruption tests in `endpoint_validation_test.c`,
`ipc_*_validation_test.c`, `ipc_reply_authority_test.c`, queue/deadlock tests
and target malformed-completion fixtures. Do not move a mutation merely to
make it unconsumed or weaken complete validators. If an old ordinary-only
assertion genuinely depended on unrelated global corruption, document that
exact case and preserve the complete-diagnostic rejection in the same change.

### Actual target evidence

Extend the existing IPC syscall workflow, not a second guest. Its current
cases already include all six operations, deferred delivery, cross-page
messages and dead-endpoint completion. Require complete captured work records
for successful operation classes and selected completion classes:
no-message success/error and one/two-page message delivery.

Use test-owned scope/counters only in the explicit IPC syscall fixture,
separate from OBJ02's scheduler-fixture collector. Begin after custody
admission and before the measured audit/selected work. Close/cancel every
path; retain complete successful snapshots with actual operation, selected
thread generation and buffer-chunk count. Close completion preflight captures
before timer preparation. Commit lookup absence is also measured natively;
do not insert snapshot copying or UART reporting after timer preparation.

Publish records from the existing final test report after original semantic
and cleanup checks. The host must require the complete current records from
that same owned `QemuResult` and reject missing/duplicate/overflowed or drifted
evidence. Add one `selected-ipc` scope to the existing finite
`--require-current-evidence` mechanism; it requires `--expect pass` and
retains before/after kernel artifact checks. No generic new runner mode is
introduced.

The genuine target Red is complete original behavior plus measured nonzero
global-audit work and the prescribed work failure, not timeout, compiler
failure, missing markers or a generic panic. Current Green requires both
work records and all original pass conditions.

Retain endpoint, trusted IPC, IPC-ecall, syscall, revoked-buffer-panic,
scheduler/start/captured/idle, user-runtime, launcher and real cross-service
regressions. Keep the original scheduler 1/2/1 context witnesses, finite
indirect/fatal proofs, metadata passivity, all current static/observed/image
coverage and explicit B13 block.

### Final gate and review

Use the existing planner on the complete change, with no explicit-path
narrowing and no duplicate B-unit execution transaction:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier pr --base origin/main --execute --fresh
```

Shared inputs select the full required coverage. Current source-set, primitive,
recipe and explicit-profile declarations are maintained in this same outcome,
not metadata prerequisite PRs. If explicit receipt/runner closure changes,
verify the affected supported profiles under their existing rules; one full
producer may supply final acceptance without a second full fresh replay.

Record real Red, focused Green, exact final commands/results, HEAD/tree and
remaining owned findings. Independent implementation review precedes the
first permanent outcome commit. Only accepted implementation evidence and
merge may close the three IPC01 findings.

## Implementation ownership

The defined implementation boundary is:

- endpoint selected identity/profile/lifecycle helpers and their existing
  internal declarations; full validators retain diagnostic responsibility;
- IPC operation/rendezvous/queue/notification/token/close/completion owners
  and their private retained plans;
- captured IPC runtime admission and the existing selected scheduler
  completion owner;
- minimal declaration exposure of existing OBJ01 selected predicates;
- focused native/current-source observations, existing target fixtures and
  their directly dependent build/runner/source-ownership declarations;
- directly related contract/evidence status updates.

Representation/identity and canonical-shape leaves stay below checked
queries, complete diagnostics and mutation owners. New admitted endpoint and
completion interfaces cross families only through `endpoint_internal.h`;
operation/queue views stay in `ipc_internal.h`, and close retains its local
plan. These headers remain declarations-only: types, constants and prototypes,
not executable macros, inline algorithms or storage definitions.

The existing IPC dependency graph remains acyclic. In particular, queue
selection cannot call notifications or operations, token preflight cannot
call reply, and notifications cannot call rendezvous. Shared canonical checks
may use endpoint-owned read-only leaves without importing endpoint-private
headers. Complete validators must not recurse into an operation or mutation.
The minimal OBJ01 selected-predicate exposure is a downward internal
dependency, not a new public forwarding layer.

Do not redesign object storage, scheduler transitions, user translation,
grant authorization, bootstrap/PM/VM transactions or the deferred token/
deadlock/queue algorithms. No source body, historical catalog key or old
author event chain becomes an implementation prerequisite.

## Current implementation and evidence

The endpoint owners admit current identities, profiles and canonical selected
queue/completion shapes. IPC owners retain the caller, destination, matching
prefix or no-match, token and receive choice through the last fallible
scheduler step. `scheduler_completion.c` retains the existing physical chunks
and ABI result through its lookup-free commit. IPC01 itself adds no public ABI
or persistent object/registry layout change.

`selected-ipc-validation` compiles the actual current portable owners plus
completion, buffer and ABI sources into ordinary and observed native products.
Its original sixty cases cover queries, operations, malformed selected state,
lifecycle and actual completion planning/commit; the
[linear dependency contract](linear-ipc-deadlock.md) adds its own cases without
replacing them. The completion fixture substitutes only
runtime-registry access, address translation and fatal reporting, and checks
exact physical writes, register preservation and zero commit lookups.
Hardware authority remains with the real QEMU image.

`selected_ipc_validation_source.py` qualifies current loops, indexed probes,
audit entries, capture placement and the diagnostic census. The shared
primitive declarations remain current-source descriptions, not behavioral
expectations. Query probe bounds account for four probes per endpoint
identity, two per profile resolution and one process read per policy role.
Queue work permits one prefix and at most one replay, not another selection.
The independent bounds remain in `selected_ipc_validation_evidence.py`.

The existing syscall guest publishes ten first-success class snapshots, all
owner/dimension fields, and totals across every capture after its original
semantic and resource cleanup. Only that fixture enables the work collector;
disabled hooks do not evaluate their arguments. Reporting occurs outside
the measured intervals and completion capture ends before timer preparation.
The unchanged syscall workflow requires this witness from its same owned
result; no second guest, shorter scenario or larger deadline is introduced.
Its work flag is source-local to the declared audit/buffer/capture owners and
collector, not target-wide: unrelated UART/history observations remain disabled
and the existing finite fatal-stack qualification is unchanged.

Ordinary/observed success comparison serializes every defined state field,
including canonical profile/message bytes, without comparing unspecified
structure padding. Rejections still require byte-exact state preservation.
Original endpoint/IPC cases, the 8,192-transition model and MEM04 corruption
expectations remain intact. Exact Red/Green, final planner, stack, review and
publication evidence belongs to the implementation PR.

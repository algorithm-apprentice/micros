# ADR-0127: Head-Only IPC Sender Queues

- Status: Accepted
- Date: 2026-11-12
- Refines: ADR-0030, ADR-0059, ADR-0067, ADR-0080, ADR-0083, ADR-0084,
  ADR-0123, ADR-0124 and ADR-0126
- Supersedes after Acceptance and separate design merge: persistent sender
  tail indexing, cached-end-only sender append admission and declared-tail
  sender termination; receiver-tail behavior is unchanged

## Context

IPC03 #255 is independently reviewed and merged. The next preferred correction
is IPC04, owning B04-F006. Its actual dependency remains merged IPC01 #251.

The [fixed local study](../research/minix-head-only-ipc-sender-queues.md)
traces MINIX's head-only caller FIFO and current sender-tail consumers.
The existing tail changes both persistent organization and append algorithm
before the baseline is complete. ADR-0080 and the Accepted B04 finding require
the reference family, even when the current index has a faster asymptotic
append. No measured post-baseline optimization decision authorizes retaining it.

## Decision

After independent review, Acceptance and separate design merge, implement the
[head-only sender contract](../architecture/head-only-ipc-sender-queues.md).
This proposal authorizes no executable implementation artifacts.

### One head, existing links

Remove exactly `micros_endpoint_record.sender_tail`. Use the existing sender
head and generation-safe thread successor links; retain all receiver-tail,
notification, endpoint, process/thread/hart and reply-binding contracts.
Do not keep a dummy field, renamed index, count, map or compatibility format.

A blocked append walks from the head, admits every consumed member and
retains the actual terminal only for the current serialized commit.
Every recoverable check precedes mutation. Commit publishes existing sender
state and the one head/predecessor link without another search.
This preserves MINIX's FIFO/publication effects while moving fallible
generation checks before blocking.

### Selected matching, lifecycle and diagnosis

Keep first matching source/`ANY` order, unmatched links, notification
precedence, existing deadlock and scheduler order, one-shot reply authority
and failure-atomic reply/receive.

Sender match/no-match termination uses actual null links. Admit the selected
splice and successor, with at most one existing prefix replay for a back-edge;
do not find the full end before a head match.
Receiver matching and tail writeback remain unchanged.

Close retains its existing reference/queue preflight, membership witness,
scheduler batch and survivor-order commit. Complete diagnosis still rejects
missing/duplicate membership and matchable queued work. Exactly `T` valid
members ending in null are valid; no traversal limit or budget grows.

Append now consumes every middle record and rejects its corruption before
publication. Selected receive no longer admits an unrelated cached sender
tail; unconsumed suffix corruption remains a complete diagnostic obligation.
Neither refinement permits stale consumed identities, lazy repair or a
global audit on an ordinary path.

### Explicit work and storage tradeoff

Append performs exactly one visit per existing sender, `Theta(q)` average
and worst work at fixed length, with constant scratch. A run of `m` appends
from empty without removal has `Theta(m^2)` aggregate work; it is not an
amortized constant-time mechanism.

Selected matching remains linear in the consumed prefix, known splice is
constant-time, and close retains its existing lifecycle bound.
Remove one eight-byte handle per endpoint and verify actual compiler layout;
add no persistent state or capacity-sized temporary. Keep all existing
selected-plan caps, stack/source-machine obligations and B13 admission rules.

### Current independent evidence

Start with valid unchanged-production FIFO semantics and an independent
exact-visit requirement: nonempty cached-end append has zero actual link
visits and must fail the required `q` visits. Follow with head/link corruption,
first/middle/final splice, call, combined receive and close evidence.
Do not substitute timing or a candidate-generated expectation for this Red.

Preserve original native/model counts, seeds and real target scenarios.
The independent reference may retain its own abstract queue end; production
comparison must still cover every ordered member and link. Translate
obsolete production-tail assertions to genuine head/link evidence rather
than silently dropping their semantic/failure checks.

Current source/observation/layout/recipe maintenance belongs in this outcome
under ADR-0123. One sufficient current author plan supplies required acceptance
and affected-unit evidence. No historical catalog, metadata prerequisite,
new runner or duplicate recertification execution is introduced.

## Phase and non-goals

This phase is documentation only. `ipc04-implementation` remains pending the
separate design merge. No code, tests, models, harnesses, build scaffolding or
reusable executable prototypes belong here.

Independent design review precedes the first design commit. Fresh independent
implementation review precedes the first permanent implementation commit.
Design Acceptance alone does not close B04-F006.

No receiver-queue redesign, filters, asynchronous IPC, user-thread API, SMP,
new public authority, token/deadlock redesign, compatibility/history/recovery
machinery, larger deadline/stack, hosted-CI acceptance or node-Q/POSIX claim.

## Alternatives

- Retain or rename the cached tail: preserves the audited pre-baseline
  optimization instead of implementing the Accepted correction.
- Walk once but cache the result for later operations: adds persistent state
  and invalidation authority rather than the reference head-only structure.
- Walk or fail after blocking: violates existing failure atomicity.
- Prewalk the whole sender queue before every match: unnecessarily turns a
  head match into a full-queue operation.
- Remove receiver tails too: expands a sender-only finding into a separate
  thread-wait representation change.
- Use a ring, reverse links or a generic container: unrequired structures and
  ownership that do not reproduce the reference mechanism.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
nine-document proposal, fixed local MINIX/current source, relevant Accepted
contracts and exact four-command author evidence. It reported no substantive
design issue remaining, with High confidence. Targeted reproduction: None.

Acceptance records that design result, not an implemented queue change or
finding closure. The separate design merge still precedes implementation.

## References

- [Canonical sender queue contract](../architecture/head-only-ipc-sender-queues.md)
- [Fixed local research](../research/minix-head-only-ipc-sender-queues.md)
- [Selected IPC contract](../architecture/selected-ipc-validation.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)

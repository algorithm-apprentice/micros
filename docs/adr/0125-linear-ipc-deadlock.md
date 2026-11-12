# ADR-0125: Linear IPC Deadlock Projection

- Status: Accepted
- Date: 2026-11-12
- Refines: ADR-0011, ADR-0019, ADR-0030, ADR-0050, ADR-0068, ADR-0080,
  ADR-0083, ADR-0084, ADR-0086, ADR-0096, ADR-0119, ADR-0123 and ADR-0124
- Supersedes after Acceptance and separate design merge: per-hop complete
  thread-owner search and linear visited-prefix comparison in the v0.1
  dependency projection, including its whole-owner corruption precedence

## Context

IPC01 #251 is independently reviewed and merged. It removes ordinary global
IPC audits while preserving the original dependency projection. B04-F003
therefore remains: every hop scans all thread slots and the visited prefix,
costing `O(d*T + d^2)` before one caller blocks.

The [fixed local study](../research/minix-linear-ipc-deadlock.md) traces MINIX's
direct process-table dependency chain and the required distinct-object
adaptation. It also identifies production PM publications that bypass the
portable thread creator/releaser. An index maintained only in those portable
functions would become stale during normal spawn or exit.

## Decision

After independent review, Acceptance and separate merge, implement the
[linear dependency contract](../architecture/linear-ipc-deadlock.md).
It owns only B04-F003.

### One maintained process index

Append one existing typed `sole_thread` handle to each process. It is exact
null at zero or multiple live threads and the exact sole live handle at count
one. All lifetime writers maintain it with the existing count publication.

The selected lookup still checks count, slot, complete generation, liveness,
owner and current consumed scheduler/IPC role. The field is an ownership
index, not cached validation, a persistent permission or a fallback hint.
Missing or corrupt count-one metadata fails; no query rebuilds it by scanning.

Preserve checked larger native thread policies without implementing user
threads. Creation of a second thread clears the sole index. Release from
two to one preflights one bounded thread-table pass, retains the exact
survivor, then publishes that index without a later lookup.

This release-only `O(T)` pass is an explicit staged bookkeeping tradeoff,
not a no-worse or constant-time claim for the larger native policy. The
production one-thread limit cannot enter it. It avoids adding a per-thread
ownership collection solely for a deferred feature; actual measured native
bounds and failure atomicity are mandatory. SOM-08 must reconsider the
maintenance mechanism before general user-thread lifecycle is exposed.
It never permits a scan in an ordinary dependency hop.

### Include every real lifetime owner

Portable process/thread initialization, allocation, release and quarantine
participate. PM's existing reservation, preparation, abort, exit detach and
final-release prevalidated transactions publish or clear the same field.
Their existing preflights validate the new selected shape before any commit.
No PM authority, request/token order, resource plan or fallible commit step
is added.

Endpoint close alone and scheduler/context/mapping transitions do not remove
threads and do not rewrite the ownership index.

The complete object validator retains independent actual per-process thread
counts. Combined with count one, exact indexed owner/liveness proves uniqueness
without another scan per process or another capacity-sized scratch object.
Its ownership/count component remains `O(P + T)`.

### One bounded indexed walk

Keep matching-before-deadlock, `SEND -> REPLY -> specific RECEIVE`, `ANY`
termination, source-only traversal and the exact post-reply cleared handle.
Retain candidate-before-repeated-before-cleared precedence and the existing
at-most-`T` hop limit.

Use two scalar RV64 words for visited membership in the existing 128-slot
domain. Two explicit initialization stores and one selected word/bit operation
per node replace the 1,024-byte visited-handle vector and prefix scan.
Admit the complete handle before using its slot; serialization prevents
generation reuse during the walk.

The result is `O(d)` selected work with 16 bytes of visited state in this
explicitly admitted domain. It is not a generic variable-capacity bitmap:
larger table admission must account for initialization and storage separately.
No recursion, persistent stamps, map rebuild, token scan or complete audit is
introduced.

### Consumed corruption remains an error

Selected count/index/generation/owner errors fail before mutation. A candidate
or cleared thread at owner count one must agree with that owner's index.
Consistent multi-thread owners still cannot be projected as a sole thread.

The old full scan also detected hidden unindexed duplicates. The selected
lookup no longer proves unrelated rows; explicit complete validation retains
that diagnosis through independent counts and owner checks. This scope
refinement is intentional and must have both selected and full-diagnostic
tests. It cannot authorize a stale index, silently repair state or choose a
representative for a multi-thread process.

### Bound the layout and its actual proof

Add only one eight-byte handle per process, with registry growth at most
`8*P`, currently 512 bytes. No thread/hart field, runtime global, public
message/syscall/handle format, trap frame or user context changes.
Keep existing fields and append the new member.

Actual registry array geometry changes. Maintain current fixtures/serializers
and the proof-critical panic-owner header/layout admission for this exact
extension in the same implementation. Keep compiler-produced offsets, linked
effect checks, all stack scopes and budgets, and every fatal exclusion.
Do not accept arbitrary header changes or rebaseline historical whole-body
hashes. ADR-0123 does not turn an actual source/machine obligation into a
metadata-only prerequisite.

## Phase and acceptance

This Accepted design is documentation only. Independent design review is
clean; `ipc02-implementation` stays pending the separate design merge.
No code, tests, models, harnesses, build scaffolding or reusable executable
prototype belongs to the design phase.

After separate design merge, actual current native Red must expose the
per-hop table/prefix work while semantic controls pass. Green must preserve
the independent models, original error/output cases and existing real target
behavior, while proving indexed hop work, bitmap membership, index lifetime,
cold repair and bounded storage.

The canonical contract owns exact cases and mutation boundaries. Complete
current planner acceptance includes all required native/target/integration/
image and actual static/observed stack evidence. No timeout or budget grows,
and B13 remains blocked. One sufficient plan supplies affected-unit evidence;
historical products, catalogs, journals and duplicate recertification are
not prerequisites.

Independent design review precedes the first design commit. Independent
implementation review precedes the first permanent implementation commit.
Neither design Acceptance nor a partial Green closes B04-F003.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
seven-document proposal, fixed MINIX/current source, portable and direct PM
lifetime writers, relevant Accepted contracts and exact four-command author
evidence. It reported no substantive design issue remaining, with High
confidence. No targeted reproduction or reviewer gate rerun was needed.

Acceptance records that design result, not an implemented index, measured
runtime improvement or closed finding. Separate design merge still precedes
implementation.

## Non-goals

No general owner list, thread index framework, lazy repair, cache epoch,
multithread wait sets, user thread APIs, SMP, token indexing, queue redesign,
filters, asynchronous IPC, memory/PM/scheduler policy change, allocation
algorithm change, new public authority, hosted-CI acceptance or node-Q/POSIX
completion.

## Alternatives

- Keep a full thread scan per hop: retains the audited divergence.
- Rebuild a process-to-thread map at each operation: adds capacity-wide work
  even to a short chain.
- Equate process and thread slots: violates separate generation-safe identities
  and existing larger native policies.
- Keep a representative thread when the owner has several: invents a wait-set
  policy and may miss a real dependency.
- Add intrusive ownership links now: more per-thread state, publication and
  validation for a lifecycle that production does not yet expose.
- Maintain an opaque XOR membership aggregate: unnecessarily obscures the
  simple zero/one/many ownership contract.
- Search when the sole index is missing: turns corruption into repair and
  invalidates the ordinary work bound.
- Retain the visited vector or mutable global stamps: unnecessary stack or
  mutation when two current-domain words suffice.

## References

- [Canonical contract](../architecture/linear-ipc-deadlock.md)
- [Fixed local research](../research/minix-linear-ipc-deadlock.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)
- [Current selected IPC contract](../architecture/selected-ipc-validation.md)

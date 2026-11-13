# ADR-0129: Grant Free Lists and Targeted Cancellation

- Status: Accepted
- Date: 2026-11-13
- Refines: ADR-0005, ADR-0038, ADR-0039, ADR-0041, ADR-0050, ADR-0069,
  ADR-0080, ADR-0083, ADR-0084, ADR-0123, ADR-0126 and ADR-0128
- Supersedes after Acceptance and separate design merge: lowest-free grant
  search/reuse; complete grant/endpoint/object preflight and capacity scans in
  grant cancellation; mandatory zero values in unselected cancellation-plan
  generation entries; the corresponding allocation/cancellation corruption
  footprint and internal record/registry layout

## Context

GRANT01 #259 is independently reviewed and merged. It removes ordinary
complete validation, but deliberately retains lowest-free allocation and
capacity-wide cancellation. GRANT02 is now dependency-ready through GRANT01
and IPC03 #255, and owns B05-F003/F004.

The [fixed local study](../research/minix-grant-free-lists-and-targeted-cancellation.md)
traces MINIX's embedded free list, revoke-time sequence advance and
endpoint-bound table authority. A fixed kernel-owned registry does not need
MINIX's heap growth. It does need explicit two-party reclamation, which
MINIX's process-owned grant tables do not directly provide.

Deleting the cancellation scan alone would lose the exact selected set,
generation checks and atomic post-IPC-close commit. Keeping complete
validation or clearing/copying the entire generation array would retain
capacity-dependent work under another name.

## Decision

After independent review, Acceptance and separate design merge, implement
the [canonical GRANT02 contract](../architecture/grant-free-lists-and-targeted-cancellation.md).
This phase authorizes no executable artifacts.

### Restore the free-list family

Keep the fixed 64-slot registry and opaque token packing. Add one free head
and an embedded next-free reference. Initialize the chain in ascending slot
order; allocation pops its head only at successful publication. Retirement
advances the generation and pushes a reusable slot at the head. Terminal
slots remain quarantined and are never linked.

Later reuse is deliberately LIFO, not globally lowest-slot. This supersedes
ADR-0038's earlier deterministic extension under ADR-0080; it is not a new
user-visible token format. Failed create, including device denial, changes
neither the head, neighbors, count, record nor output.

### Index the two required participant memberships

Each active grant has two embedded doubly linked membership nodes, one per
participant. One generation-bound endpoint root selects a combined incident
list containing either role. A record appears exactly once in each
participant's list; self-grants remain prohibited.

Use complete grant tokens for links and an exact endpoint value for each
nonempty root. They are kernel-owned navigation data, not independent copy
permission. Selected record/participant authority still follows ADR-0128.
Empty roots release their endpoint binding before process-slot reuse.

This reverse index is a required adaptation for kernel-owned, two-party
reclamation, not a claim that MINIX has the same list. It replaces unrelated
capacity enumeration with necessary incident and immediate-neighbor work.
Do not add free counts, list counts, epochs, validation flags or another
registry.

### Preserve the two-phase cancellation transaction

Prepare walks the selected endpoint's incident list, admits the exact
records/participants and all prospective unlink/free-head writes, and
publishes a failure-atomic plan. Commit checks the exact set, generations,
counts and topology before its first mutation, without requiring the
already-closed endpoint to resolve again.

Keep the existing 288-byte bounded plan and one-use/registry binding. Only
its selected generation entries are meaningful. Prepare publishes those
entries and the scalar header, setting `active` last; unselected entries
and padding retain their caller bytes. Commit never reads an unselected
entry and never clears/copies the entire plan.

Thus plan storage remains fixed `O(G)`, explicitly acknowledged rather than
called proportional, while preparation/publication/commit work is `O(k+1)`.
This preserves exact generations without an epoch, hash-based authority,
heap, VLA or new capacity-sized buffer.

Commit retires selected slots in increasing slot order, preserving the
existing cancellation order. Each reusable retirement pushes onto the free
head. The mutation pass follows current links updated by its own earlier
splices, not obsolete neighbor snapshots. All fallible checks precede plan
consumption; no recoverable failure follows mutation.

The existing serialized prepare -> IPC close -> grant commit -> process
release ordering remains. Failed IPC close leaves the plan and all grant
state untouched. Standalone cancellation without closing the endpoint
remains supported. No new mutation may occur between prepare and commit
except the existing non-grant IPC close in that protected epoch.

### Keep local admission and complete diagnosis distinct

Ordinary allocation/revoke/cancellation validate their actual selected
heads, nodes, links and required neighbors, not every grant. No pointer is
followed before range/generation/state/role admission. Cycles, stale links,
foreign roles, reciprocal disagreement and impossible consumed counts fail
before mutation.

Full diagnostics retain complete storage/count/participant validation and
add exact free/incident membership, reachability and uniqueness. Unvisited
orphans or disconnected cycles remain diagnostic corruption; no hot path
repairs them through a fallback scan.

Newly consumed insertion/unlink heads can change a former unvisited-error
case. The contract requires explicit fixture/oracle adaptations, including
preserving the original modest count-mismatch selected/full distinction.
No expected result may be copied from candidate output.

### Bound the outcome

Allocation and indexed revoke have constant worst-case work. Cancellation
does work proportional to incident grants and necessary immediate neighbors;
empty cancellation does no grant-capacity traversal. Complete diagnostics
and initialization remain linear in grant/process capacity plus their
existing complete endpoint/object validation.

The existing registry owner remains cohesive. Keep copy algorithms,
permissions, token generation/quarantine, captured decode/result/register
behavior, TTY denial-only scope and all external BOOT/PM audits unchanged.
Do not claim that unrelated parent lifecycle audits have become constant.

## Acceptance and phase

Implementation remains pending the separate design merge. Require actual
unchanged-production allocation/cancellation work Red and a distinct LIFO
counterexample. Extend the existing ordinary/observed grant pair and
independent model, preserving original scenario purposes, counts, seed,
draw schedule and negative controls except the explicitly superseded
allocation/index/plan-tail expectations.

Green includes complete index state, mixed-role and aliasing neighbors,
zero/sparse/dense incidents, stale tokens/links, quarantine, malformed and
foreign/consumed plans, poisoned unselected tails, failed IPC close and
post-close commit. Count all plan publication/retirement work; no hidden
whole-plan initialization or copy is allowed.

Keep existing actual grant/syscall/endpoint, handoff, PM/TTY/VFS and composed
exit/resource evidence, all stack scopes/budgets and blocked B13 admission.
One sufficient current author plan supplies affected-unit evidence.
Current metadata changes belong here under ADR-0123, not another
prerequisite pair. Fresh independent implementation review precedes the
first permanent implementation commit.

## Non-goals and alternatives

No dynamic grant growth, user grant tables, delegation, wildcard authority,
fault history, new syscall, copy algorithm, VM retry, new IPC cleanup
algorithm, concurrency, persistent validation cache/epoch, migration,
recovery, larger budget, hosted-CI acceptance or node-Q/POSIX completion.

Keeping lowest-slot search loses the baseline allocation mechanism.
Keeping any complete cancellation audit or whole-plan transfer loses the
incident-work bound. A constant-size generation hash or epoch would add
authority/state or weaken exact identity. One-sided indexing cannot reclaim
both participants' grants. Capacity-sized dense indexes are unnecessary
when two embedded memberships and one root per endpoint suffice.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
eight-document proposal, fixed local MINIX/current sources, lifetime callers
and exact four-command author evidence. It reported no substantive design
issue remaining, with High confidence. Targeted reproduction: None.

Acceptance records that design result, not an implemented free list, index or
finding closure. The separate design merge still precedes implementation.

## References

- [Canonical contract](../architecture/grant-free-lists-and-targeted-cancellation.md)
- [Fixed local study](../research/minix-grant-free-lists-and-targeted-cancellation.md)
- [Selected grant validation](../architecture/selected-grant-validation.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)

# ADR-0119: Selected Object and Hart Validation

- Status: Accepted
- Date: 2026-11-09
- Refines: ADR-0011, ADR-0019, ADR-0022, ADR-0026, ADR-0027, ADR-0062,
  ADR-0068, ADR-0080, ADR-0082, ADR-0083, ADR-0084, ADR-0105 and ADR-0118
- Supersedes in part, after separate Acceptance and merge: complete
  registry validation at the sixteen owned scheduler entries; pairwise
  disjointness work for geometrically regular stacks; hart-capacity
  current-owner searches; and the unsupported unscheduled-current
  diagnostic exception

## Context

MEM04 and MEM05 are independently reviewed and merged. The separate
EV01-C08 and EV01-C09 prerequisites are also merged through #238 and
#239/#240. OBJ01 is next in the preferred correction order and owns only
B03-F001, B03-F003 and B03-F004.

The [fixed-source study](../research/minix-selected-object-and-hart-validation.md)
pins runtime and evidence policy to #240. It identifies forty-two direct
production calls to complete object/scheduler validation. Sixteen belong
to ordinary scheduler entry preflight; twenty-six other boundaries remain
with their existing owners.

The MINIX baseline uses CPU-local current state, direct identity, singly
linked priority queues and operation-local checks. Optional complete
queue sanity is separate. Its stack allocation uses fixed indexed
geometry rather than repeatedly comparing every stack pair.

Removing the existing complete checks without replacing their consumed
preconditions is unsafe. In particular, ordinary tail enqueue currently
stores a new node before a fallible current-thread dequeue. Selected
preflight must make the subsequent commit non-failing.

## Decision

After separate review, Acceptance and merge, implement the
[selected object/hart contract](../architecture/selected-object-and-hart-validation.md).
The implementation remains pending throughout this design phase.

### Selected checks, complete diagnostics

Replace only the sixteen owned scheduler entry audits with checked
registry metadata, exact thread/process/hart identities and the queue
records actually consumed by the operation. Preserve argument ordering,
public interfaces, generation checks, valid-state behavior, accounting,
queue ordering and failure-atomic publication.

Keep complete object/scheduler diagnostics and the twenty-six remaining
call sites. A successful selected operation does not certify unrelated
slots, stacks, queues, counters or hart uniqueness.

Use the existing object stateless leaves through one explicit downward
dependency from the common scheduler owner. Do not copy magic values,
create a second registry, or introduce a general validation framework.
Minimal user/idle trap accounting stays unchanged.

### Queue and IPC commit boundaries

Preflight consumed head/tail/current relationships and the exact dequeue
prefix plus successor before stores. A bounded replay of that prefix
rejects a consumed backedge without a capacity-sized initialized witness.
Destination and preemption preflight precede policy/flag publication.

Use the existing void prevalidated enqueue commits. Remove the obsolete
fallible tail wrapper; do not retain it as an unused compatibility API.
Checked priority-prefix selection may change the private `queue_pick`
signature, not a public interface.

The existing IPC candidate array temporarily supplies slot-indexed
duplicate stamps. Initialize only requested slots, then overwrite the
input-order candidates exactly as before. Valid batches are linear in
request count. Preserve the old exact pairwise shape check only on
malformed out-of-range or same-slot/different-generation input, retaining
the original argument/stale precedence and failure atomicity.

### Direct current ownership

Resolve a supplied hart-context integer through checked array extent,
stride and slot derivation before reading one hart record. Never
dereference the supplied integer as a pointer.

Resolve current membership through the exact thread's assigned hart and
that hart's current handle. Context capture retains its exact context copy
after local identity/ownership admission.

No supported production operation creates an unscheduled current thread.
Remove that diagnostic exception explicitly: complete validation rejects
it, and capture cannot use it as current authority. Whole-hart duplicate
current detection remains in complete diagnostics.

### Geometric stack proof

Preserve arbitrary aligned portable stack ranges of at least the existing
minimum sizes. During the existing complete slot passes, build at most
three ephemeral affine cohorts: thread, idle and emergency ranges.
Exact stride/slot geometry proves within-cohort disjointness.

Merge the actual regular intervals, not their enclosing pool envelopes.
Compare only genuine exceptions against other owned intervals.
The selected primary stack is a checked alias of a thread or idle stack,
not another owned interval.

For `D = tcap + 2*hcap` and `E` exceptional intervals, stack proof costs
`O(D + E*D)` with constant additional automatic storage. `E = 0` is
linear. The complete validator still has separately disclosed hardware-ID
and assigned-thread diagnostic costs; this does not claim that every
complete diagnostic becomes linear.

### Preserve the return-plan boundary

OBJ02 still owns the complete queue projection/rebuild and repeated
context/root work. OBJ01 reuses the already cleared candidate ready bitmap
to check the existing selected-hart queue load, then validates the
selected-hart thread records consumed by the unchanged apply loop.
It clears the temporary membership witness at the existing point before
projection.

The 4,224-byte static workspace, 1,344-byte return-plan shape, checked
recomputation, timer preparation, accounting and final apply sequence
remain. This is not a delta-plan implementation.

### Exact evidence transition

The fixed prospective selection has ninety-four active identities and a
thirty-eight-path direct closure. Retire only those keys after actual
independent native Red, Green and thirteen-unit recertification.
All 5,851 predecessor facts and twenty-five fingerprints remain unchanged;
all seven prior claims and 185 retirements remain valid without journals.

The new producer must name every current source/observation predicate at
its first implementation. An aggregate source-projection record alone is
not sufficient. Freeze its finite current contracts, individual keys,
oracle inputs and adapter bytes before production Red.

The existing panic source/machine proof receives only the declared
qualified hart-source and scalar arithmetic adaptations. Retain exact
source origins, linked objects, every hart-slot case, generation reads,
frame custody, unknown-operation rejection and every existing stack limit.

## Acceptance

Require native state/ownership/rollback evidence and actual primitive
counts for ordinary selected operations, regular/exceptional stack proof
and direct current identity. The fixed baseline must fail all three
replacement properties for the audited reasons, not a broken fixture.

Preserve existing native models and every target scenario. Renew the
actual stack proof, complete current author plan and same-run profiles.
Independent implementation review precedes the first permanent commit;
exact publication trees and fresh journal-free authority precede cleanup
and the next correction.

## Design review disposition

Independent read-only review inspected the complete eleven-document
proposal, its fixed MINIX/current-source and ninety-four-key census,
selected/complete failure boundaries, queue and batch commit rules,
regular/exceptional geometry, source/stack qualification and exact author
evidence at staged tree `60c2c6143cc1797fe0cfc2082b54db293ceaad8e`.
All four documentation-phase commands were FRESH/exit0.

The reviewer reported: "No significant issues found in the reviewed
changes. No substantive design issue remains." Confidence was high.
No targeted reproduction or author-gate rerun was needed.

Acceptance records this design result, not an implementation or retirement.
The dependent implementation remains pending the separate design merge.

## Non-goals

No process/thread/hart equivalence, public layout/API change, new persistent
stack index, extra production workspace, larger stack or deadline, new
SMP policy, scheduling policy, migration, user SCHED, return delta plan,
endpoint/completion redesign, memory-policy change, recovery framework,
hosted-CI acceptance or later POSIX feature.

This design contains no code, tests, models, harnesses, build scaffolding
or reusable executable prototypes.

## References

- [Fixed MINIX and current-source study](../research/minix-selected-object-and-hart-validation.md)
- [Exact object/hart contract](../architecture/selected-object-and-hart-validation.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)
- [B03 audit](../research/minix-complete-baseline-b03-objects-scheduler.md)
- [Evidence authority transition](0082-evidence-authority-transition.md)

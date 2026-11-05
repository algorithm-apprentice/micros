# ADR-0103: Selected Frame-Ownership Validation

- Status: Accepted
- Date: 2026-11-05
- Refines: ADR-0017, ADR-0020, ADR-0021, ADR-0040, ADR-0045, ADR-0050,
  ADR-0053, ADR-0054, ADR-0076, ADR-0080, ADR-0082, ADR-0083,
  ADR-0100, ADR-0101 and ADR-0102
- Supersedes in part, after separate design merge: inherited whole-ledger
  validation on the eighteen named operation paths; unrelated corrupt-state
  error precedence outside each operation's required ownership scope; and
  only the 38 declared transport assertions after their actual transition

## Context

MEM01 and MEM02 have corrected bootstrap availability search and kernel-map
topology. Separately reviewed EV01-C04 #213/#214 now supplies granular
current-native source identities without weakening any existing check.

MEM03 owns B02-F002/B02-E002. The
[fixed-source study](../research/minix-selected-frame-ownership.md)
shows that ordinary ownership operations still inspect configured inactive
capacity and can invoke complete object/scheduler validation before selected
metadata access. MINIX uses selected region/page relations, exact process
identity and separate diagnostics instead.

The compatible dense ledger and generation protection remain necessary.
Their complete diagnostic scan must not become the ordinary authority
operation. Removing the scan alone would be incorrect: VM transitions rely
on its bit/owner/target checks, handoff must not silently repair counts, and
VM snapshots consume every active record.

## Decision

After separate review, Acceptance and merge, implement the
[selected ownership contract](../architecture/selected-frame-ownership.md).

### Separate metadata, selected records and whole-active inputs

Add read-only metadata preflight for scalar/count/cursor bounds and exact
active geometry. It checks active ranges, not unused range capacity.

Selected mutation/read paths validate their actual owner, allocation bit,
handoff target and phase before publishing existing bounded plans or outputs.
Keep exact process-generation resolution in the owning target operation.

Whole-active handoff and VM snapshot operations use a distinct active-domain
preflight with independent per-kind counts and the original active/prefix
checks. They do not sample active state or infer validity from summaries.

Keep full portable and target validators complete and unchanged. Preserve
their original active/inactive algorithms, geometry predicates and token
contracts. No validation cache, mutable epoch or generalized framework is
introduced.

### Keep explicit audit boundaries

Retain the eight direct audit calls named by the architecture: one-shot
initialization/kernel-map/readiness/prepared-batch proof, rollback-baseline
diagnosis, the target diagnostic wrapper and existing guarded self-tests.
Explicit native/target diagnostic calls remain.

Migrate only the other eighteen named call sites. Other subsystems retain
their own object, root, queue, grant, protocol and lifecycle checks.
MEM04/MEM05/VM02 and the later owners still own their remaining algorithms.

### Preserve transitions and failure atomicity

Keep existing layouts, capacities, errors, generation identities, one-way
handoff, plan shapes, lowest-frame selection and non-failing commit order.
The target remains one-hart and SIE-clear across preflight and consumption.

Preserve the deliberate allocation bit-set/owner-free interval and exact
MEM01 defensive cursor rollback. A stable-state row check is not inserted
blindly into that intermediate state.

Metadata or selected corruption is rejected before use. Unrelated inactive
corruption belongs to explicit complete diagnosis, not every ordinary
operation. Complete selected-process root validation still precedes a
missing-leaf result until MEM04 separately replaces that algorithm.

### Measure the actual source and preserve earlier evidence

Use five independently compiled passive observation slices and one ordinary
control, with the unchanged EV02 collector. The fixed 99 valid-case matrix
distinguishes active/inactive frames/ranges, bitmap work and inherited
object/scheduler work without assigning false owners or summing inclusive
captures twice.

Three actual native oracles cover portable selected operations, target
runtime process release and a real PM ownership consumer plus the closed
caller ledger. Existing complete-diagnostic cases are preservation evidence,
not manufactured work Reds.

The exact 38-key retirement preserves all original 4,282 records, seventeen
fingerprints, previous claims and unrelated clauses. The twenty production
subjects and six precisely classified adapter/metadata paths exclude shared
inspectors from the candidate closure. Earlier native modes and bounds
remain independent; current execution compiles actual current bodies.

## Non-goals

No new persistent state or public structure layout, reduced capacity,
allocator policy, VM allocation/mapping policy, page-table indexing, release
bitmap redesign, process-owner index, pairwise uniqueness correction,
ordinary global validator rewrite outside the named inherited frame calls,
new target workflow, deadline/budget increase, SMP, recovery, hosted-CI
acceptance or node-Q implementation.

## Acceptance and phase barrier

This proposal is documentation only. No runtime/header, executable test,
model, generator, observation, scaffold or prototype precedes separate
design review, Acceptance and merge.

Implementation requires independent pre-edit declarations, actual
discriminating Red, complete current work/semantic/corruption Green,
thirteen-unit recertification, the complete fifty-command fresh author gate
and independent review before the first permanent commit.

Original target behavior, three reusable profiles, all 44 static
configurations/113 outputs/ten observed scopes and blocked B13 remain.
Publish only the identical reviewed/committed/remote/preview/squash tree,
verify journal-free committed authority and remove exact completed refs.

## Design review disposition

Independent read-only review inspected all eleven proposal documents,
relevant Accepted contracts, actual current sources, the fixed MINIX
baseline and exact author evidence at staged tree
`34ead5fde5487558f7c562075ec471e2b9b8abdd`.
All four documentation-phase commands were FRESH/exit0; implementation
native/build/QEMU/stack checks were correctly N/A.

The reviewer reported: "No substantive design issue remains." The proposal
was approved for Acceptance-only follow-up. No file was changed, gate rerun
or targeted reproduction needed. Acceptance records this reviewed design,
not a completed runtime correction or executable evidence. Implementation
remains pending the separate design merge.

## Alternatives

- Remove complete validators: loses independent inactive/corruption evidence.
- Cache a successful audit: adds stale authority and hides corruption.
- Keep full audit on each operation: retains B02-F002.
- Validate scalar totals only: loses selected owner/bit/target checks and can
  repair wrong per-kind counts at handoff.
- Treat every operation as one selected frame: is wrong for whole-active
  snapshots and handoff.
- Change release/root/object algorithms here: expands into later owners.
- Increase timeouts or reduce capacity: hides the work instead of correcting it.
- Retire aggregate source-projection records: discards granular ownership
  and violates independent-input closure.
- Rebaseline old hashes or compile old dependencies as current: destroys
  independent actual-source evidence.

## References

- [Fixed MINIX and current-source study](../research/minix-selected-frame-ownership.md)
- [Exact operation and evidence contract](../architecture/selected-frame-ownership.md)
- [Granular source identities](0102-granular-memory-native-source-authority.md)
- [Complete frame validator](0076-bounded-complete-frame-ownership-validation.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)

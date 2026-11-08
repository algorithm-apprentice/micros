# ADR-0113: Granular Selected-Ownership Observation Authority

- Status: Accepted
- Date: 2026-11-08
- Refines: ADR-0082, ADR-0083, ADR-0103, ADR-0105, ADR-0106 and ADR-0112
- Preserves: runtime behavior, source and observation admission, collector
  policy, existing facts/fingerprints and committed authority

## Context

The separately reviewed EV01-C06 implementation #230 is merged as
`5748ec20dcdb568a5ccfca2eccfcda09c6dd3f8c`, tree
`21b1b87ca2a5053d2d909d84cfd3507499949fd6`.
Its 477 MEM04 source/observation identities preserve every predecessor
record and all six journal-free committed authorities.

Resumed MEM05 research found a different missing identity. MEM03's
function guard accepts an unchanged signature/call set when the loop index
in `collect_process_release_summary()` is parenthesized. Its independently
enforced observation binding then rejects the changed enclosing primitive.
A parenthesized bitmap subscript and an added loop in a currently unobserved
helper similarly pass function admission but fail the observation predicate.

EV01-C05 names MEM03's existing function/file/definition/skeleton checks,
not these primitive-binding and complete-coverage comparisons.
The [fixed-source census](../research/selected-ownership-observation-authority-gap.md)
finds 120 current function-local obligations across seventeen compiled
owners, including 57 functions whose required observation set is empty.
The existing broad declaration records cannot substitute for those keys.

MEM05 remains a blocked design. No release/index/handoff algorithm,
implementation or executable prototype is accepted by this prerequisite.

## Decision

After separate review, Acceptance and merge, implement the
[observation-authority contract](../architecture/selected-ownership-observation-authority.md)
as the sole EV01-C07 outcome.

### Name the existing function-local comparisons

Add one literal-only declaration owner and the separate family
`selected-ownership-observation-admission`.
Its 120 `TRANSPORT_IDENTITY` records name the existing current MEM03
observation contract of each eligible function.

Preserve all 179 site records, in their original per-function order:
110 before-primitive records, 65 loop-entry records and four complete-validator
entry records. The 63 nonempty and 57 empty function contracts are equally
required. An empty set prohibits newly unobserved loops or watched fields;
it is not absent authority.

The fixed coverage policy distinguishes the nine memory owners, whose
loops and selected array fields are checked, from the eight object/scheduler
owners, whose loop coverage is checked. Do not invent field coverage for
the latter or expand the finite function subsets.

Each contract keeps its actual function/site identifier, source owner,
channel category, primitive kind, enclosing compounds and ordinals,
primitive token text, placement, unit count and field witness. These are
existing binding identities, not new counter sites, work bounds or oracles.

### Bind the actual assertion owner

Every fact binds exactly:

- the new literal declaration;
- one physical C subject; and
- `tests/host/selected_ownership_source.py`.

The generator delegates observation and inverse validation to that owner.
Its separate fixed-input, mode, generated-byte and native-product checks
remain independent obligations, not duplicate observation predicates.
No header, shared parser, collector, recipe or stack owner enters the new
payload merely because the observation code uses it.

Freeze every complete expected row and fact from fixed #230 policy and
MEM03's original #214 source reference before editing the declaration,
extractor or tests. Interpret the declaration with the existing checked
AST/literal readers, never by executing it.

### Conserve complete current and historical authority

Preserve all 5,259 complete predecessor facts, twenty-one fingerprints,
six committed claims and 103 retired identities. The addition yields
5,379 facts in twenty-two families and retires nothing.

Keep the original MEM03 catalogues, including their 173-original and
179-current site digests, and all old declaration/source-family values.
The new records name the current comparisons; they do not replace the
original literal contracts or certify an algorithm.

Only assertions explicitly about the complete current catalog totals advance.
Existing MEM03/MEM04 historical comparisons keep their fixed family sets,
counts and fingerprints. ADR-0112's complete-current validation followed by
the fixed nineteen-family historical view already supports this addition
and must remain unchanged.

### Preserve actual measurement

No source or observation admission changes in this implementation.
No predicate is disabled because its new identity exists.

A later runtime correction must name every affected binding, retain all
unaffected sites and coverage, supply independent actual-current replacement
observations and complete ADR-0082's real Red/Green/recertification.
It may not remove a record to hide unmeasured work, compile historical
bodies as current, change a collector dimension or use a transport identity
as its complexity oracle.

## Acceptance

Start with genuine missing-key Red after the unchanged source guard accepts
and unchanged observer rejects representative loop-anchor, field-primitive
and empty-set mutations. Missing imports, new scaffolding errors or an
earlier function rejection are not that evidence.

Green requires independent equality of every one of the 120 complete
records, all predecessor records/fingerprints/claims, exact consumers and
strict literal/schema/path/value negatives. Preserve the difference between
memory-field/loop coverage and object/scheduler loop coverage. Require
narrow closure evidence through the unchanged transition validator and all
six committed authorities without an author journal.

Use the existing checked host runner for focused iteration, then all fifty
fresh author commands, same-run receipts and complete native/target/image/
stack acceptance. Independent review precedes the first permanent commit.
Publish only identical reviewed/committed/remote/preview/squash content,
verify actual-main authority and clean exact completed refs.
Only then resume MEM05 design.

## Phase boundary and non-goals

This design contains documentation only. No declaration, extractor, test,
model, generator, scaffold or reusable executable prototype precedes its
separate review, Acceptance and merge.

No runtime change, MEM05 algorithm, observation or source relaxation,
new retirement, old fingerprint rebaseline, journal, collector ABI,
shared parser, transition/checked-view policy, build recipe, workflow,
timeout, capacity, stack/image budget, hosted-CI acceptance or node-Q
implementation is included.

## Design review disposition

Independent read-only review inspected the complete eleven-document
proposal, actual current predicates, closed census, historical/current
consumers and exact author evidence at staged tree
`5e7c7d618cbebfde2633c609594cb73913a4d683`.
All four documentation-phase commands were FRESH/exit0; executable
implementation/native/build/QEMU/integration/stack checks were correctly N/A.

The reviewer reported: "No substantive design issue remains."
No targeted reproduction, gate rerun, edit or delegation was needed.
Acceptance records this design result, not an implemented metadata family
or runtime retirement. The paired implementation remains pending the
separate design merge; MEM05 design remains blocked until that
implementation completes.

## Alternatives

- Treat a function call-set key as an observation key: the read-only
  mutations prove these are distinct rejecting predicates.
- Register only nonempty functions: misses the actual zero-loop/field
  obligations of 57 eligible functions.
- Retire the source-projection aggregate: conflates a 38-path literal
  declaration with individual current observation comparisons.
- Retire the state/work contract: it is a permanent invariant, not a
  transport identity available for this purpose.
- Relax the observer now: would admit unmeasured work before replacement
  evidence exists.
- Broaden EV01-C05's old family or rehash its data: would change already
  reviewed facts instead of adding the missing authority.

## References

- [Fixed-source gap and complete census](../research/selected-ownership-observation-authority-gap.md)
- [Exact metadata contract](../architecture/selected-ownership-observation-authority.md)
- [Evidence transition](0082-evidence-authority-transition.md)
- [Passive observation contract](0083-test-only-operation-count-evidence.md)
- [Selected-ownership source identities](0106-granular-selected-ownership-native-source-authority.md)
- [Complete-current historical projection](0112-granular-selected-address-space-native-source-authority.md)

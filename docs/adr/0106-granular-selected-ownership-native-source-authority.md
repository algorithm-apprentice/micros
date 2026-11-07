# ADR-0106: Granular Selected-Ownership Native Source Authority

- Status: Accepted
- Date: 2026-11-07
- Refines: ADR-0082, ADR-0102, ADR-0103 and ADR-0104
- Preserves: existing runtime behavior, source admission, historical
  declarations, complete facts, family fingerprints and committed claims

## Context

MEM03 implementation #219 is independently reviewed and merged as
`da258293f507c745c8e157cd84bd13b2fb4b5e77`, tree
`5793c993338c22cf30aa552c9397c8aa234c33cc`.
Its renewed Red, Green, thirteen-unit recertification and complete author
acceptance close B02-F002/B02-E002.

Read-only MEM04 design research found another instance of the source-authority
boundary addressed by ADR-0102. MEM03's native generator rejects changes to
196 fixed noncandidate inputs, including the public and private user-address-
space headers. Its current-source guard also enforces individual function,
definition-set, skeleton and header clauses. None has an individual MEM03
inventory identity.

MEM03's aggregate source-projection record binds its historical declaration,
not each executable clause. Its 38 payloads include shared immutable
inspectors, and do not include those two noncandidate headers. Retiring it
would neither name the missing header assertions nor form a narrow,
closure-disjoint replacement.

The [fixed-source study](../research/selected-ownership-native-source-authority-gap.md)
records the exact 484-clause census and its different consumers. MEM04 design
is blocked on this evidence prerequisite; its implementation remains pending.

## Decision

After separate review, Acceptance and merge, implement the
[selected-ownership source-authority contract](../architecture/selected-ownership-native-source-authority.md)
as the sole EV01-C05 prerequisite.

### Declare existing clauses without changing admission

Add one literal-only declaration owner and the separate
`selected-ownership-native-admission` family. Exactly 484
`TRANSPORT_IDENTITY` records name:

- 251 current function clauses across seventeen C owners;
- seventeen definition sets and seventeen source skeletons;
- three exact header extensions; and
- 196 fixed native inputs: 43 C files and 153 headers.

Freeze their complete expected records independently from fixed #219 policy
and MEM03's fixed #214 runtime reference before modifying the extractor or
tests. Use existing checked literal readers, not execution of the declaration.

Preserve the actual comparison semantics. In particular, MEM03's 216
retained function bodies compare raw body text, not MEM01's compact lexical
body representation. The other 28 original functions and seven added helpers
have the existing exact call-set contracts. Signature and preprocessing
conditions remain exactly those enforced by the current guard.

### Name the actual consumers

Each record binds only its declaration owner, one physical subject and the
actual assertion owner or owners. Fixed C dependencies have both generator
and source-observation consumers. Fixed header dependencies have the generator
consumer; the source observer does not admit those headers.

Eight of the seventeen production C owners are in the native bundle. The
other nine are checked by the existing author-source envelope. Do not claim
that all seventeen are compiled in that bundle.

Shared C inspectors, work/stack policy and build recipes do not become
candidate payloads merely because they interpret a comparison. Existing
closure construction and immutable/candidate overlap rejection are unchanged.

### Conserve all prior authority

Preserve all 4,290 complete predecessor records, eighteen family fingerprints,
five committed claims and seventy retired identities. The resulting catalog
has 4,774 facts in nineteen families and no additional retirement.

Do not append these records to ADR-0102's old family or rebaseline any old
fingerprint. The original aggregate declaration record remains unchanged and
distinct from these per-clause current-source identities.

### Keep the runtime phase barrier

The implementation changes metadata materialization and focused inventory
tests only. It adds no selector, waiver or source-admission relaxation.
Historical and current guards, native generation, observations and passive
inverses remain unchanged.

A future MEM04 or other runtime correction must independently define its
actual helper/observation closure, refine the relevant current contracts,
name every affected identity, preserve unrelated clauses and complete the
real Red/Green/recertification transition. These keys alone authorize nothing.

## Acceptance

Require actual missing-identity Red, with unchanged guards rejecting
representative retained-function, definition-set and fixed-header mutations.
A missing declaration import or newly invented scaffold failure is not Red.

Green requires complete independent 484-record equality, exact predecessor
conservation, strict declaration/path/identity negatives, narrow synthetic
closure evidence and unchanged source-guard behavior. Then require all fifty
current fresh author commands, complete native/target/stack acceptance,
journal-free prior authority and independent implementation review before
the first permanent commit.

Only the identical reviewed/committed/remote/preview/squash tree may be
published. Clean the exact completed task refs before resuming MEM04 design.

## Phase boundary and non-goals

This proposal contains documents only. No declaration module, executable
test, extractor, model, fixture, generator, build scaffold or prototype is
authored before separate design review, Acceptance and merge.

No runtime algorithm, MEM04 API/error/exception policy, source relaxation,
identity retirement, author journal, collector ABI, shared parser, transition
state machine, build recipe, workflow, timeout, capacity, stack/image budget,
hosted-CI acceptance or node-Q implementation is in scope.

## Design review disposition

Independent read-only review inspected the complete ten-document proposal,
fixed source guards and census, canonical context and current author evidence
at staged tree `eceac10f65fc31a5bb5fd28c3911464fe8c797a6`.
All four documentation-phase commands were FRESH/exit0; implementation
native/build/QEMU/integration/stack checks were correctly N/A.

The reviewer reported: "No substantive design issue remains." No targeted
reproduction or gate rerun was needed. Acceptance records this design result,
not an implemented declaration family, source waiver or runtime correction.
Implementation remains pending the separate design merge.

## Alternatives

- Retire the aggregate: conflates literal transport with executable clauses,
  includes immutable policy and misses the two header subjects.
- Reuse a MEM02 header identity: does not cover MEM03's distinct rejection.
- Recompute old hashes or change the fixed reference: lets the candidate
  replace independent authority.
- Admit every changed dependency: weakens actual-source evidence without a
  replacement oracle.
- Avoid shared declarations solely to evade a source guard: lets an evidence
  accident dictate the runtime architecture.
- Fold the gap into MEM04: hides the missing authority behind the correction
  it is supposed to constrain.

## References

- [Fixed-source gap and census](../research/selected-ownership-native-source-authority-gap.md)
- [Exact metadata and acceptance contract](../architecture/selected-ownership-native-source-authority.md)
- [Granular memory-native precedent](0102-granular-memory-native-source-authority.md)
- [Selected ownership](0103-selected-frame-ownership-validation.md)
- [Evidence transition](0082-evidence-authority-transition.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)

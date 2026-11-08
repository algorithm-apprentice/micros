# ADR-0112: Granular Selected Address-Space Native Source Authority

- Status: Accepted
- Date: 2026-11-08
- Refines: ADR-0082, ADR-0102, ADR-0106, ADR-0107 and ADR-0109
- Preserves: runtime behavior, source admission, observations, all existing
  facts/fingerprints, committed claims and stack authority

## Context

MEM04 implementation #228 is independently reviewed and merged as
`8bd33e58a8e661cff78a8e4aad9d6cbc6a406120`, tree
`fb74bc2458a4a3bf4fce45b4e396fa7fb0cde465`.
Actual native/target Red and Green, thirteen-unit recertification and all
fifty fresh author commands passed. The complete stack certificate covers
44 configurations, 113 ELFs and ten observed scopes, with B13 still blocked.

Read-only MEM05 research found that MEM04's current source guard has no
per-clause inventory identities. It rejects changes to the destruction
source as a fixed native input, constrains release helpers through exact
dependent bodies, and checks the whole handoff dependency after its two
specified error assignments. Its separate current-observation comparison
can also reject a source change accepted by its function guard.

The [fixed-source census](../research/selected-address-space-native-source-authority-gap.md)
identifies 477 existing clauses. The eight aggregate MEM04 records do not
name these individual comparisons. Their source-projection payload also
includes shared immutable policy and omits the noncandidate destruction
source. Retiring that aggregate is not an admissible replacement.

MEM05 design is blocked on this evidence prerequisite. No MEM05 runtime
implementation has begun.

## Decision

After separate review, Acceptance and merge, implement the
[selected address-space authority contract](../architecture/selected-address-space-native-source-authority.md)
as the sole EV01-C06 prerequisite.

### Name the existing comparisons

Add one literal-only declaration owner and the separate family
`selected-address-space-native-admission`. Exactly 477
`TRANSPORT_IDENTITY` records name:

- 218 current function clauses across eleven regular production C owners;
- eleven definition sets and eleven source skeletons;
- five byte-exact current header extensions;
- one byte-exact handoff dependency;
- 198 fixed native inputs, comprising 47 C files and 151 headers; and
- 33 live current-observation shape comparisons.

The special handoff source is not a twelfth regular function-guard owner:
its existing branch checks the complete extended file and returns.
Removed functions have no fabricated current function identity.

Function content retains its actual policy: 178 raw body comparisons,
nine call-set-only original functions, nine original functions requiring
both calls and their compact dependent body, and 22 helpers with the fixed
signature/empty-guard/call-set contract.

Observation identities are separate because their rejection is independent
from the function signature/call-set guard. Preserve all 33 comparisons,
including the 28 empty automatic inventories, plus their fixed explicit
primitive declarations. These are source-binding identities, not new work
bounds or permission to omit counter observations.

### Use the actual assertion owner

All 477 clauses are enforced by the existing
`tests/host/selected_address_space_source.py` owner. The generator and
ordinary four-product gate delegate to it. Their mode, stable-read,
generated-byte, artifact, output, observation-inverse and oracle checks
remain independent obligations, not duplicate fixed-source identities.

Each fact binds only its new declaration, one exact subject and that source
assertion owner. Do not import MEM03's different generator/source consumer
split or add shared inspectors, recipes, collectors, stack tooling or design
documents to the payload.

Freeze every complete expected row and resulting fact from the fixed #228
policy and fixed #221 runtime before extractor/test edits. Read the
declaration with the existing checked AST/literal helpers; never execute it
or derive an expectation from edited production.

### Preserve all existing authority

Keep all 4,782 complete predecessor records, twenty fingerprints, six
committed claims and 103 retired identities. The addition yields
5,259 facts in twenty-one families, with no new retirement.

The original eight MEM04 declaration records and all prior native-source
families retain their values and roles. No source/observation guard, selector,
transition, primitive site, source-stage binding, native fixture or runtime
algorithm changes. Only the historical catalog comparison and its
historical regression inputs acquire the explicit adaptation below.

A future correction must refine the relevant Accepted contracts, identify
every affected source and observation key, preserve unrelated clauses,
bind actual current helpers/observations and complete real Red/Green/
recertification before any consumer stops enforcing a comparison. These
metadata keys alone authorize nothing.

### Separate historical and complete-current catalog checks

Independent design review identified one required compatibility boundary.
MEM04's `declared_scope()` currently hashes every non-MEM04 family as its
original 4,774-record predecessor set. The new family would enter that
historical comparison and reject otherwise unchanged committed authority.

Add a bounded historical projection in the existing MEM04 authority owner.
First validate the complete current inventory through the unchanged closed
inventory rules, including every currently registered family. Then select
only the nineteen historical families named by the fixed #227 inventory
declaration, requiring their original fingerprints, complete membership and
the unchanged predecessor digest. Unknown or invalid current records must
fail before this projection; do not silently filter them or return a
projected inventory as the current catalog.

The current-catalog tests independently compare all 5,259 records with the
frozen 4,782 predecessors and 477-record addition. MEM04's historical test
retains its original 4,774-record/fingerprint expectations and eight-fact
family check on the checked historical view. All full current inventories
continue into transition validation.

Its historical stack-scaffold negative test must use fixed #228
registration-file inputs, prove the unchanged scaffold predicate succeeds
on that historical fixture, then prove each stack-input mutation fails at
the intended boundary. Do not weaken the production scaffold guard to admit
later registration changes. These two bounded existing-owner adaptations
are part of this prerequisite, not a generic compatibility framework.

## Acceptance

Start with genuine missing-identity Red after unchanged guards have
demonstrated their fixed-input, dependent-body, header, whole-handoff and
observation rejections. An observation case must distinguish its predicate
from the earlier function guard; a missing import or invented guard is not
coverage evidence.

Green proves complete independent 477-record equality, exact predecessor
conservation, strict schema/path/consumer/value rejection, raw-versus-compact
comparison fidelity and unchanged positive/negative admission.
It also proves that the complete new catalog preserves ordinary committed
MEM04 authority without journals, that invalid current catalogs cannot hide
behind a historical view, and that unchanged historical scaffold negatives
still reach their intended stack-input rejection.
Use the unchanged transition validator for narrow synthetic closure tests:
shared policy remains disjoint and actual subject/declaration overlap fails.
This is not a real runtime retirement.

Then run the complete fifty-command fresh author plan, current same-run
receipts, full native/target/image/stack coverage and independent review
before the first permanent commit. Publish only identical reviewed/
committed/remote/preview/squash content, verify journal-free prior authority
and clean exact completed refs. Resume MEM05 design only after this
metadata implementation separately merges.

## Phase boundary and non-goals

This design authors documentation only. No declaration, extractor, test,
model, fixture, generator, scaffold or reusable prototype precedes its
separate review, Acceptance and merge.

No MEM05 algorithm, admission relaxation, identity retirement, old
fingerprint rebaseline, journal, collector ABI, shared parser, transition
rule, checked-view policy, build recipe, workflow, timeout, capacity,
stack/image budget, hosted-CI acceptance or later runtime task is included.

## Design review disposition

Independent read-only review found one High issue: the original implementation
footprint omitted MEM04's closed historical catalog comparison and historical
scaffold-test compatibility. The author verified the finding and specified
complete-current validation before the fixed historical projection, unchanged
predecessor values, exact historical registration fixtures with positive
controls, and the two bounded existing authority/test adaptations.

Complete correction review inspected staged tree
`35f9499f686953a9d6a4f975e4a3f186b5f8995c` and its matching four FRESH/exit0
documentation/diff results. The reviewer reported: "The original High
finding is resolved. No substantive design issue remains."
No targeted reproduction or author-gate rerun was needed.

Acceptance records this design result, not an implemented metadata family
or runtime retirement. The paired implementation remains pending the
separate design merge; MEM05 design remains blocked until that metadata
implementation completes.

# ADR-0117: Granular Selected Release/Handoff Native Source Authority

- Status: Accepted
- Date: 2026-11-09
- Refines: ADR-0082, ADR-0102, ADR-0106, ADR-0112, ADR-0113 and ADR-0114
- Preserves: runtime behavior, source and observation admission, every
  existing fact/fingerprint, seven committed claims and 185 retirements

## Context

MEM05 implementation #236 is independently reviewed and merged as
`aba02c1f8a583866a85c3012d20c6007b9445a22`, tree
`7609a9578e8010c735310923a90953833c93e3d5`.
Its renewed actual Red/Green, thirteen-unit recertification and all fifty
fresh author commands passed. Complete current stack coverage, same-run
receipts, journal-free committed/main authority and protected ref cleanup
also passed.

The next preferred runtime outcome is OBJ01, not VM02. OBJ01's actual
MEM04 prerequisite is already merged. Read-only research found that the
hart-context owner remains a byte-exact noncandidate input of MEM05's
native evidence. Both its generator and source guard enforce that fixed
comparison, but no individual inventory identity names it.

The [fixed-source census](../research/selected-release-handoff-native-source-authority-gap.md)
identifies 453 current source clauses. The eight aggregate MEM05 records
do not replace those names. Its recipe is an artifact contract; its coarse
source-projection transport does not bind the noncandidate hart source.
Neither is an admissible waiver for an individual OBJ01 source change.

OBJ01 design is blocked on this evidence prerequisite. No OBJ01 code,
model, test, fixture or executable prototype has been authored.

## Decision

After separate review, Acceptance and merge, implement the
[selected release/handoff authority contract](../architecture/selected-release-handoff-native-source-authority.md)
as the sole EV01-C08 prerequisite.

### Name existing comparisons without changing them

Add one literal-only declaration and the separate family
`selected-release-handoff-native-admission`. Exactly 453
`TRANSPORT_IDENTITY` records name:

- 226 current functions in thirteen production C owners;
- thirteen definition sets and thirteen source skeletons;
- four byte-exact current headers; and
- 197 fixed native inputs: 45 C files and 152 headers.

The function records preserve two different policies. The 171 retained
originals compare raw signatures, bodies and preprocessing ownership.
The other 55 compare compact signatures, guards, callee sets and complete
primitive shapes. All 23 new helpers additionally forbid body directives.
The existing idle-check function also requires its exact independently
derived body; the other 54 current contracts do not acquire a body hash.

Preserve all fifteen empty and forty nonempty current primitive contracts.
The observation renderer repeats the same primitive equality already
enforced by its function guard. It does not need a duplicate observation
identity as MEM03/MEM04 did for their genuinely separate comparisons.
Observation placement, passivity, actual work and legacy component schemas
remain independent obligations.

### Bind every actual assertion owner

The function, definition, skeleton and header clauses belong to
`tests/host/selected_release_handoff_source.py`.
Each fixed-input clause additionally belongs to
`tools/generate_selected_release_handoff_fixture.py`, whose current
`raw != original` condition compares against fixed source, not a second
read of the current file.

One fixed-input identity therefore binds both assertion owners. It is not
two duplicate facts and must not omit either consumer. Generator file-mode
and stable-read obligations remain unchanged.

Each record binds only the new declaration, its one exact subject and its
actual assertion owner or owners. Do not include shared inspectors,
collectors, recipes, stack policy or design documents in those payloads.
Freeze complete rows and facts from the fixed #236 policy and #232 runtime
before declaration/extractor/test edits; edited production is not an
expectation source.

### Preserve complete current and historical authority

Preserve all 5,387 complete predecessor facts, twenty-three fingerprints,
seven claims and 185 retired identities. The addition yields 5,840 facts in
twenty-four families and no new retirement.

MEM05 already validates the complete current inventory before positively
selecting its fixed twenty-two-family historical predecessor set. Keep
that authority code and its 5,379-record historical obligation unchanged.
Do not introduce a new catalog projection or silently discard future
records.

Its historical scaffold regression does require one bounded fixture
adaptation. Supply the complete fixed #236 scaffold-read inputs, prove the
original positive predicate, then exercise the existing mutations at their
intended boundaries. Do not weaken the real scaffold or rehash its frozen
adapter declarations to accept new registration/test bytes.

### Keep the implementation boundary closed

The eventual implementation changes only the new literal declaration,
existing inventory materialization, existing authority regressions,
the existing MEM05 historical scaffold regression and directly related
documentation.

The MEM05 source guard, generator, authority owner, fixtures, counters,
transition validator, checked view, build recipes and runtime stay
byte-identical. No selector, journal, source-admission exception or runtime
replacement oracle is added.

## Acceptance

First demonstrate real missing-identity coverage after unchanged guards
reject representative fixed-input, raw-function, current primitive,
idle-body, helper-directive, definition, skeleton and header mutations.
Fixed-input coverage must exercise both assertion owners. Do not label
the renderer's repeated function-primitive comparison as a distinct guard.

Green requires complete independently frozen record equality, all
predecessor conservation, exact paths/owners/consumers, strict literal
schema and comparison fidelity, and positive as well as negative admission.
Current catalog validation must reject unknown or malformed additions
before historical selection. All seven committed claims must work without
author journals. The historical scaffold must still reject each intended
mutation after its fixed positive control succeeds.

Then require all fifty fresh author commands, complete native/Python and
target/image/stack coverage, three same-run reusable profiles and clean
independent implementation review before the first permanent commit.
Publication requires exact reviewed/committed/remote/preview/squash content,
fresh journal-free committed/main checks and protected completed-ref
cleanup. Resume OBJ01 design only after this metadata implementation
separately merges.

## Phase boundary and non-goals

This design contains documentation only. No declaration, extractor, test,
model, harness, fixture, build scaffold or reusable executable prototype
precedes its separate review, Acceptance and merge.

No OBJ01 algorithm, source relaxation, identity retirement, old fingerprint
rebaseline, parser/framework, runtime layout/API, deadline, capacity,
stack/image budget, hosted-CI acceptance or later runtime task is included.

## Design review disposition

The initial independent complete review found one Medium schema issue:
Boolean directive flags and a null absent-body hash are not admitted by the
existing checked literal reader. The author verified its exact-type rules
and replaced only those encodings with integer `0`/`1` and an empty-string
sentinel. All comparison semantics, 453 clauses and the frozen-parser
implementation boundary remain unchanged.

Correction review inspected staged tree
`eea5604f12ff6f1e36114de1c1db790190cecd71` and its matching four
FRESH/exit0 documentation/diff results. The reviewer reported:
"The Medium issue is resolved. No significant issues found in the reviewed
changes; no substantive design issue remains."
No targeted reproduction or author-gate rerun was needed.

Acceptance records this design result, not an implemented metadata family
or runtime retirement. Separate design merge still precedes the dependent
metadata implementation; OBJ01 design remains blocked until that
implementation separately merges.

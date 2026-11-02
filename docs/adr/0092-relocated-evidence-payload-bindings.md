# ADR-0092: Relocated Evidence Payload Bindings

- Status: Accepted
- Date: 2026-11-02
- Refines: ADR-0033, ADR-0034, ADR-0068, ADR-0073, ADR-0080, and ADR-0082
- Supersedes in part: ADR-0091's complete prior-record
  conservation rule for the 104 independently identified payload/unit
  metadata corrections below; no identity or historical value changes

## Context

EV01-C01 design #192 and implementation #193 are separately merged.
PLAT01's resumed source-authority study then found a distinct binding defect.

The existing inventory treats old flat kernel-file labels as absent files,
although ADR-0073's fixed directory map names their exact current owner.
For 104 direct file-keyed hash/property entries, it substitutes an unrelated
declared-path fallback that omits that owner.

The [fixed-source study](../research/relocated-evidence-payload-gap.md)
records all seventeen tables, thirteen owners and the concrete consequence:
twenty prospective PLAT01 identities derive 102 candidate paths, including
independent inspection/command tools. Exact existing directory bindings
reduce that example to eighteen paths. This is a prerequisite repair, not
permission to weaken immutable-oracle disjointness.

## Decision

After separate review, Acceptance and merge, implement the bounded
[relocated payload contract](../architecture/relocated-evidence-payloads.md).

### Bind the named current source

Use the existing literal logical-source key, actual consumer and fixed
directory map. For 102 entries the corrected payload is the evidence owner
plus its exact current physical source. The scheduler and endpoint
`kernel/kernel_objects.c` hashes instead retain their complete ADR-0068
reconstruction: nine current C sources and the private header, plus their
respective evidence owner. Reject unknown, missing, malformed, ambiguous
or linked bindings. Do not use a facade-only shortcut, prefix guess, old
flat alias, body scan or silent fallback.

Recompute primary and consumer units through the existing mapping rules.
The affected source and its real consumers remain covered; B14 is not
removed.

### Preserve authority, correct only metadata

Keep all 3,912 fact keys, families, owners, kinds, scopes, authority classes
and historical evidence values. Only the 104 specified payload/primary/
consumer tuples change: 55 in `kernel-structure`, 49 in
`service-structure`.

Independently derive those records and the two changed family fingerprints
from fixed commit `de31bce3de93f58c94e053ea55e41040aba79987`
before modifying the extractor. The nine other fingerprints and 3,808 other
records remain unchanged.

The packed scheduler family remains exact. COPY01's completed prior claim
continues to work without its old author journal. No identity retires or
changes class in EV01-C02.

### Retain historical comparisons

Earlier conservation tests keep their original expectations. A bounded
test-side inverse restores only the three approved metadata fields after
checking the exact corrected record. It rejects other drift rather than
overwriting it.

The inverse is not used for current transition authorization. Current
events use precise physical bindings and retain the unchanged whole-file
oracle/candidate disjointness rules.

## Non-goals

No runtime, source-assertion retirement, new family, new CTest or target
workflow, profile, timeout, compiler, stack/image budget or transition-state
change. No generic evaluator, per-task closure override, broad unresolved-
schema rewrite, mutable generated inventory or hosted-CI acceptance.

PLAT01's preserved documentation remains inactive until this separate
corrective implementation merges. The pinned runtime correction graph and
its finding ownership remain unchanged.

## Acceptance

Require actual missing-owner native Red, all independently specified
corrected records, exact remaining-record/fingerprint conservation,
both complete reconstruction closures, strict path and inverse rejection
controls, preserved packed coverage and
COPY01 prior retirement, the unchanged complete fresh author gate, and
clean independent implementation review.

## Phase barrier

This Accepted design authors only documentation. Its dependent
implementation remains pending. No code, test, model, harness, fixture,
build scaffold, dependency or reusable executable prototype is created
before the design is independently reviewed, Accepted and separately merged.

## Design review disposition

Independent read-only review found one Medium: the scheduler and endpoint
`kernel/kernel_objects.c` hashes consume a complete reconstructed family,
not the relocated facade alone. The author verified the actual assertions,
reconstruction helper and ADR-0068, then specified the two exact
nine-source/private-header exceptions, their independent declarations and
strict inverse/path controls.

Corrected independent re-review reported that the Medium is closed and no
substantive design issue remains. No targeted reproduction was needed and
the reviewer reran no gate. Acceptance records this design result, not an
implemented binding or a PLAT01 runtime correction.

## Alternatives

- Keep the fallback: rejected because the named current source is missing
  and unrelated command/oracle paths enter the candidate closure.
- Rename overlapping inputs: rejected because a different label does not
  make source bytes independent.
- Allow per-task overrides: rejected because fact ownership must remain
  canonical and auditable.
- Rebaseline the current extractor's output: rejected as circular authority.
- Rewrite all unresolved schemas: rejected; the demonstrated exact
  logical-file boundary is finite and sufficient for this prerequisite.

## References

- [Relocated payload contract](../architecture/relocated-evidence-payloads.md)
- [Fixed-source gap study](../research/relocated-evidence-payload-gap.md)
- [Evidence authority transition](0082-evidence-authority-transition.md)
- [Packed identity coverage](0091-packed-evidence-identity-coverage.md)

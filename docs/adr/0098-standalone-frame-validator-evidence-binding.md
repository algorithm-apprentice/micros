# ADR-0098: Standalone Frame-Validator Evidence Binding

- Status: Accepted
- Date: 2026-11-04
- Refines: ADR-0033, ADR-0034, ADR-0073, ADR-0080, ADR-0082, ADR-0091,
  and ADR-0092
- Supersedes in part: prior complete-record/fingerprint conservation for
  the one independently specified metadata correction below; no historical
  hash, identity, class, runtime contract or retirement changes

## Context

PLAT03 implementation #204 is independently reviewed and merged. The next
MEM01 design requires a granular transition for the bootstrap allocator and
its directly dependent cursor validation.

Independent MEM01 design review identified a blocking evidence defect.
`BOUNDED_FRAME_VALIDATION_SHA256` is a standalone whole-source assertion in
`kernel_layout_source.py`. Unlike ADR-0092's direct logical-file table
entries, it has no exact semantic binding. Its fact inherits 797 fallback
paths, including the actual validator but also unrelated CMake and planner
inputs that MEM01 must keep immutable.

The unchanged ADR-0082 validator correctly rejects that overlap as
`PROJECTION`. Passing runtime oracles cannot authorize the planned transition.
The [fixed-source gap study](../research/standalone-frame-validator-payload-gap.md)
records the actual assertion, fallback, closure calculation and independently
specified corrected record.

MEM01's nine-document proposal was preserved as non-authoritative reference.
It is not implementation-ready and no MEM01 implementation has begun.
EV01-C03 is the sole active prerequisite.

## Decision

After independent review, Acceptance and separate merge, implement the
[exact standalone binding contract](../architecture/standalone-frame-validator-binding.md).

### One exact qualified binding

Recognize only this existing canonical identity:

```text
tests/host/kernel_layout_source.py:data-entry:BOUNDED_FRAME_VALIDATION_SHA256
```

Its source authority is the existing scalar declaration and actual
`restore_frame_validation_hot_path` consumer. Resolve logical
`kernel/frame_ownership_validation.c` through the unchanged kernel-directory
map to its one current physical owner. Reuse the existing ordinary-file and
path validation.

The payload is exactly the evidence owner and that physical C file. Preserve
the identity, class, family, owner, scope, kind and historical hash value.
Use the unchanged unit-mapping rules; the corrected primary unit is B02 and
consumers are B02/B14.

No suffix match, similar hash name, unrelated owner, arbitrary override or
general unresolved-schema rewrite is admitted. Missing, malformed, duplicate,
ambiguous or linked source bindings fail rather than falling back.

### Independent conservation exception

Fixed source #204 has 3,938 facts in fourteen families. Exactly one complete
record changes, and only its payload/primary/consumer metadata changes.
The 3,937 other records and thirteen other family fingerprints remain
identical.

The independently specified authority-core fingerprint changes from:

```text
c9f825f5b1488d03ee1c54db9ae3324b312807cd62de6dfa23166331a32e7e5d
```

to:

```text
152ba765d3a332961b59e88a7725f4a823c335bb62b97ba09c662bb84e1098fd
```

These values derive from the fixed base plus the exact declared metadata
replacement, before an extractor edit. Recompute no historical source hash
from a candidate.

Existing earlier conservation tests retain their original expected values.
A bounded test-only inverse first checks the exact corrected metadata, then
restores only those three fields from fixed historical source. It must not
erase identity/value/class drift. Current authorization always consumes the
precise corrected fact, never this historical view.

### Preserve transition strictness

Do not change candidate/immutable closure construction, overlap rejection,
event order, registry validation, snapshot/receipt rules or source assertions.
The repair permits a future properly scoped transition; it does not authorize
MEM01's retirement or claim any runtime correction.

COPY01 and PLAT01 committed claims remain usable without author journals.
Every other key, including the separate original frame-validator hash,
retains its current handling.

## Non-goals

No runtime C/header/assembly, source-assertion suppression, new family,
new test executable/CTest/target workflow, build recipe, compiler option,
timeout, capacity, budget, general binding framework or hosted-CI acceptance.
The blocked MEM01 design remains inactive until this prerequisite's separate
implementation merges.

## Acceptance

Require a native Red for the actual overbroad binding and resulting immutable
collision, the exact corrected record and fingerprint, complete remaining-fact
conservation, strict schema/path/identity negatives, historical inverse
tamper rejection, unchanged real transition disjointness and ordinary prior
claim acceptance.

Then run the existing complete fresh author plan and independent
implementation review. Verify reviewed/committed/remote/squash tree equality,
ordinary committed authority without author journals and exact task-branch
cleanup before restarting MEM01 design.

## Phase barrier

This design contains documents only. No extractor, test, inverse, model,
fixture, executable declaration, build scaffold or dependency is authored
before separate review, Acceptance and design merge.

## Alternatives

- Widen MEM01's candidate closure to all 797 files: rejected; that would put
  independent command/oracle inputs under candidate authority.
- Rename overlapping input labels: rejected; aliases do not make the bytes
  independent.
- Remove disjointness validation: rejected; the validator correctly exposes
  the binding error.
- Change the historical hash: rejected; the source expectation is not wrong.
- Bind every standalone hash by naming convention: rejected; only this
  demonstrated consumer/source relation has been independently reviewed.
- Let MEM01 silently change the fact and fingerprint: rejected; it contradicts
  that design's conservation claim and would let the candidate approve its
  own authority.

## References

- [Exact standalone binding contract](../architecture/standalone-frame-validator-binding.md)
- [Fixed-source gap study](../research/standalone-frame-validator-payload-gap.md)
- [Relocated bindings](0092-relocated-evidence-payload-bindings.md)
- [Evidence authority transition](0082-evidence-authority-transition.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)

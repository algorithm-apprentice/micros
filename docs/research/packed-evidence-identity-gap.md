# Packed Evidence Identity Coverage Gap

## Scope and fixed source

This is the read-only source study for the EV01-C01 corrective evidence
prerequisite. It was discovered while defining PLAT01's source-authority
boundary after COPY01 #191, before any PLAT01 implementation was authored.

The fixed current source is `micros` commit
`b99848f52d01100a6db2833f610cff7007e1cc6f`, tree
`978df155e3953e293824a8a6eb5f11a5f25b1db9`.
The original scheduler preservation owner additionally retains its existing
`BASELINE_COMMIT` pin
`e5b4da6e2fd33f752cfdadc7beb77f0ebf9b025b`.
Neither pin is moved to a future implementation candidate.

This is project-owned evidence infrastructure, not a MINIX runtime
algorithm. The change is a compatible development extension under ADR-0080.
No MINIX source, new runtime mechanism or reusable executable prototype is
part of this study.

## What is actually missing

`tests/host/test_scheduler_source_layout.py` stores two packed declarations.
They are not ordinary dictionaries whose leaf values are standalone
40- or 64-character hexadecimal strings.

| Declaration | Shape | Fixed coverage |
| --- | --- | --- |
| `FROZEN_FUNCTIONS` | Original-source key to multiline `name lines sha256` records | Six source families, 163 function records |
| `FROZEN_FACTS` | `(original source, function)` key to whitespace-separated tagged 96-bit fingerprints | 14 semantic minimum-multisets, 1,080 token occurrences |

The six function-record groups contain 60, 34, 28, 11, 13 and 17 rows.
The semantic-token occurrences contain 416 call tags (`C`), 238 condition
tags (`P`), 279 write tags (`W`) and 147 exit tags (`E`). Repeated tokens
represent multiplicity; they are not duplicate declaration errors.

The existing inventory's `_contains_fixed_hash` and `_hash_leaves` recognize
standalone hexadecimal string constants. A multiline function record is
not such a constant. A tagged 96-bit fingerprint is neither an untagged
40-character nor a 64-character string.

A read-only query of the fixed tree, using the existing `current_inventory`
and `validate_inventory`, found:

```text
current materialized facts:                    3735
current closed families:                      10
inventory validation:                         OK
scheduler FROZEN_FUNCTIONS entries represented: 0
scheduler FROZEN_FACTS entries represented:     0
```

The scheduler module is registered and contributes 40 other facts, including
its owner boundary, baseline pin, stable artifacts, macro/constants,
command identities and Python restoration/assertion helpers. Registering
the module does not materialize these missing data entries.

## Existing consumers and the precise claim

`frozen_functions` splits each nonblank line into a function name, original
line count and full body fingerprint. The owner checks original membership,
pending or non-phase bodies, and line-review/disposition conditions using
those records.

The completed long/short phase roots do not all retain a direct complete-body
equality assertion. Their separate semantic preservation gate visits the
root and its phases, computes concrete call/condition/write/exit fingerprints,
and requires every frozen occurrence to remain. Its comparison is a
**minimum multiset**, not ordered execution and not equality of the complete
new operation set. Added refactoring calls are not automatically forbidden.

The fourteen semantic entries equal the twelve
`INITIAL_FUNCTION_MIGRATION` roots plus the two `SHORT_PHASE_ROOTS`.
The existing 120-line review signal, phase responsibilities, ordering,
guard, ownership and signature checks remain separate obligations.

The omission does not mean the existing tests stopped checking scheduler
behavior or source. The directory-layout gate also freezes the complete
scheduler source-gate AST, including these literals. The defect is that
EV01's per-key authority interface cannot name the packed declarations as
their own facts even while reporting a valid complete inventory.

Retiring the whole source-gate module or a generic assertion helper would
not be an acceptable substitute for materializing the missing declaration
entries. It would conflate expected data, preserved helper logic and
unrelated source constraints.

## Fixed-owner discovery

Read-only inspection of the currently registered authority owners found the
two formats above only in the scheduler module's two named top-level
declarations. This is the initial closed admission, not permission to
interpret arbitrary embedded hashes or execute Python to discover data.

The correction must reject a missing, duplicated, computed or malformed
admitted declaration. A newly recognized packed declaration outside that
closed admission cannot silently inherit a family, disappear from coverage
or become an automatically approved exception.

Original source labels are historical logical paths. Current kernel
payloads resolve through the existing fixed directory map; native payloads
remain their exact declared host paths. Function ownership must resolve
uniquely through `ORIGINAL_FUNCTIONS` and the matching `SOURCE_SETS` family,
not through a filename guess or production-body scan.

## Required correction boundary

Add exactly the missing 163 function records and 14 semantic-multiset
entries as transport authority. Preserve the 3,735 previous individual
facts and all ten previous family fingerprints. Preserve every old literal,
line count, tag, multiplicity, source pin and existing assertion.

The new family expectation must be independently derived from the fixed
Git declarations above before the candidate extractor is used. It must not
be regenerated from whatever the edited implementation happens to emit.

No source identity retires in this correction. In particular, COPY01's
already reviewed memory retirement must survive the larger inventory in
ordinary no-plan validation without another author journal.

PLAT01 research remains preserved but inactive until this separate
documentation-first, test-first correction is reviewed and merged.

## References

- [Evidence authority contract](../architecture/evidence-authority-transition.md)
- [Packed identity contract](../architecture/packed-evidence-identities.md)
- [ADR-0082](../adr/0082-evidence-authority-transition.md)
- [Scheduler source contract](../architecture/kernel-scheduler-source-organization.md)
- [Correction queue](minix-complete-baseline-findings-and-correction-dag.md)

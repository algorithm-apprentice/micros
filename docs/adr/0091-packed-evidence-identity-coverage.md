# ADR-0091: Packed Evidence Identity Coverage

- Status: Accepted
- Date: 2026-11-02
- Refines: ADR-0033, ADR-0034, ADR-0080, and ADR-0082
- Corrects: EV01 implementation coverage; no runtime behavior or existing
  retirement is superseded

## Context

COPY01 #191 is merged after its actual primitive Red/Green, granular author
recertification, complete fresh gate and clean independent re-review.
The next PLAT01 design traced the source authority around potential
notification integration and found an earlier inventory omission.

The existing scheduler source gate contains 163 function records inside
multiline strings and fourteen tagged semantic minimum-multisets containing
1,080 occurrences. The current inventory registers that module and
validates successfully, but materializes none of those entries.

The [fixed source study](../research/packed-evidence-identity-gap.md)
records the exact evidence and distinguishes missing per-key inventory from
the source assertions that still run. The old whole-module AST and
scheduler source/phase gates have not disappeared. COPY01's separate
memory-file transition is not invalidated by this finding.

ADR-0082 requires individual authority keys and rejects a runtime-discovered
digest with no declaration. Retiring a whole gate or rewriting expected
hashes would not repair this omission. PLAT01 therefore pauses while a
separate evidence-only design/implementation pair closes the prerequisite.

## Decision

After separate review, Acceptance and merge, implement the bounded
[packed identity contract](../architecture/packed-evidence-identities.md).

### Admit the actual fixed data forms

Register one new `scheduler-packed-transport` family, owned only by
`tests/host/test_scheduler_source_layout.py`, with keys disjoint from that
module's existing family.

Materialize one transport fact for each of the 163 `FROZEN_FUNCTIONS`
records and each of the fourteen `FROZEN_FACTS` dictionary entries. Retain
the complete original full digest and line metadata for function records;
retain tagged-token multiplicities as canonical minimum-multisets.
Do not turn those multisets into an ordered trace or a new complete-operation
equality assertion.

### Bind real ownership and closed coverage

Use literal AST declarations, the exact original-function/source-family
maps and the existing kernel directory map. Reject duplicates before they
can collapse in a dictionary. Missing, extra, malformed, computed, unknown
or ambiguously owned records fail.

Extend discovery to recognize these supported packed forms without
automatically approving new owners or tables. This is two closed formats,
not a general embedded-hash parser or Python evaluator.

### Preserve all existing authority

Derive the new family's expectation independently from fixed commit
`b99848f52d01100a6db2833f610cff7007e1cc6f`, not the edited extractor.
Keep every one of the existing 3,735 facts and all ten existing family
fingerprints unchanged. The resulting inventory has 3,912 facts across
eleven families.

No identity is retired, rebaselined or reclassified by this correction.
COPY01's reviewed prior committed retirement remains effective in no-plan
mode without replaying or inventing author events. Original scheduler
line-review, source ownership, phase, guard, signature and semantic
preservation assertions remain unchanged.

### Keep the correction separate

EV01-C01 is an evidence prerequisite, not another runtime correction or a
second owner for an audited finding. The pinned runtime correction graph
and its historical counts remain intact. PLAT01 research is preserved but
inactive until this separately reviewed correction is merged.

## Non-goals

No scheduler, diagnostic-history, UART, TTY, panic, timer, IPC or other
runtime behavior changes. No source identity retires. No transition-state,
profile, timeout, workflow, compiler or source-parser contract is relaxed.
No broad re-audit, generalized interpreter, generated mutable inventory,
new target gate or compatibility/recovery framework is added.

## Acceptance

Implementation must demonstrate:

- the actual missing-record native Red;
- all 177 independently specified new facts;
- exact schemas, original membership and current payload binding;
- failure for malformed, duplicate, missing, extra or undeclared packed
  authority;
- preserved multiplicity and original historical data;
- unchanged previous facts and family fingerprints;
- preserved COPY01 prior retirement and active new facts;
- current native, complete fresh author and documentation/Git evidence; and
- clean independent implementation review.

## Phase barrier

This Accepted design contains documentation only. No code, tests, models,
fixtures, build scaffolding, dependency or executable prototype is authored.
The paired implementation remains pending until this design is independently
reviewed, Accepted and separately merged.

## Design review disposition

Independent read-only review inspected the complete twelve-file design,
fixed source declarations and actual consumers, authority granularity,
discovery and payload boundaries, prior-fact/retirement conservation and the
exact current author evidence. It reported no significant issue and no
substantive design issue remaining. No targeted reproduction was reported.

Acceptance records that design result, not an implemented extractor or
additional materialized facts. The paired implementation remains pending
until this design is separately merged.

## Alternatives

- Treat a registered module as complete coverage: rejected because the
  missing declaration entries still cannot be named independently.
- Retire the entire scheduler gate: rejected because unrelated source and
  structural obligations must remain active.
- Regenerate old fingerprints from current output: rejected as circular
  authority.
- Interpret every embedded string as a digest: rejected because a finite,
  explicit data admission is sufficient and safer.
- Hide the correction inside PLAT01: rejected because this is an upstream
  evidence defect with an independent acceptance boundary.

## References

- [Packed identity contract](../architecture/packed-evidence-identities.md)
- [Fixed source study](../research/packed-evidence-identity-gap.md)
- [Evidence authority transition](0082-evidence-authority-transition.md)
- [Correction queue](../research/minix-complete-baseline-findings-and-correction-dag.md)

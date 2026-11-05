# ADR-0104: Precise Complete-Validator Token Authority

- Status: Accepted
- Date: 2026-11-05
- Refines: ADR-0076, ADR-0082, ADR-0083 and ADR-0103
- Supersedes in part, only after separate Acceptance and merge:
  ADR-0103's 38-key/26-path/five-adapter evidence envelope. The corrected
  envelope has 40 keys, 27 candidate paths and six frozen adapters.
- Preserves: the MEM03 runtime contract, both original token values, every
  predecessor fact, the complete diagnostic bodies and all AST work checks

## Context

MEM03 design #215 is separately merged. Its first uncommitted implementation
recorded actual fixed-source Red and current Green, then passed all twelve
QEMU recertification units. B14 ran 267 tests and rejected one retained
complete-validator token assertion. No terminal retirement, implementation
commit or merge occurred.

The [scope investigation](../research/complete-validator-token-scope-gap.md)
proved that both complete-scan helpers are unchanged. The old lexical
selector passes a zero segment start to a blank-line-based signature finder.
After preprocessing, its selected span starts at private-header declarations
and includes the preceding lookup/count functions. The supposed helper
identities therefore also freeze unrelated declarations and ordinary code
that MEM03 explicitly must change.

Updating either old hash from the candidate would hide this authority error.
Changing the shared parser would broaden one correction into unrelated
evidence families. Removing the token or AST checks would weaken the complete
diagnostic guarantee.

## Decision

Apply the bounded
[complete-helper authority contract](../architecture/selected-frame-ownership.md#complete-helper-token-authority)
when MEM03 restarts after this separate design merge.

### Keep historical and current scopes explicit

Keep `ACTIVE_TOKENS_SHA256`, `INACTIVE_TOKENS_SHA256`, their original values
and existing inventory records unchanged. Preserve the old
`function_source`/`function_token_digest` interpretation as historical
prefix evidence. Fixed #214 source and headers must still reproduce both
original values.

Before the transition authorizes retirement, each original assertion remains
active. A staged claim or a passing new helper check cannot suppress it.
After the corresponding exact key is legitimately retired, compare the
actual preprocessed helper's complete signature and body with the
independently declared exact-helper token identity instead.

Use the existing correct function-record inspector for that bounded
selection. Require exactly one definition and preserve signature, linkage,
body, token spelling and C17 preprocessing. Do not normalize current runtime
code into old code, substitute a fixed helper for execution, or alter the
shared C inspectors.

### Preserve the complete work proof

The complete active and inactive helper bodies remain byte-identical to the
fixed runtime reference. Their current exact-token checks remain mandatory,
and the existing Clang AST checks still inspect the actual current complete
translation unit without modification.

Retain every original negative control: per-frame bitmap reloads, pointer
aliases, helper calls, loop-header work, unstructured control flow,
incorrect cached-word use and missing inactive-boundary/tail work must fail.
Add positive prefix-only changes and exact missing/duplicate/helper-mutation
negatives. A compiler rejection or a skipped assertion is not a positive
scope proof.

### Extend only the actual affected authority

Append the two old prefix-identity keys to the original 38-key MEM03 plan.
Their real payloads are only the validator C owner and its hot-path test;
the C owner is already a candidate. Add only
`tests/host/test_frame_validation_hot_path.py` to the candidate envelope
as the sixth frozen adapter.

The selected-record oracle now covers 25 keys; runtime-owner and
consumer-boundary coverage remain five and ten. The new helper expectations
belong to the independently declared MEM03 source authority, not replacement
values in the historical test module. All 4,282 predecessor records and
seventeen fingerprints remain unchanged; the restarted eight-fact family
still yields 4,290 facts in eighteen families.

Install and independently freeze all six adapters before actual Red.
Their candidate-payload ownership does not permit mutation within an attempt:
bind their complete identities as an immutable external input. Keep generic
closure/disjointness checks unchanged. Only the original twenty production
paths may change between Red and Green.

### Restart from the corrected design

Abandon the uncommitted implementation branch and preserve its source/evidence
as reference, not current acceptance. After this design merges, create a fresh
implementation branch from current `main`, bind both Accepted design records
and the new merged design tree, and independently redeclare the complete
40-key plan before production edits.

Repeat actual Red, Green, thirteen-unit recertification and the complete
fifty-command fresh author gate. Preserve all native/target/stack coverage,
three reusable profiles, budgets and the blocked B13 row. Independent
implementation review still precedes the first permanent commit.

## Non-goals

No change to runtime validation scope, owner/phase/count semantics, source
layouts, capacities, algorithms, collector dimensions, timeouts, stack/image
budgets, shared parser behavior, hosted-CI acceptance or node Q. This is not
permission to bypass another source assertion or retire an entire family.

## Acceptance and phase barrier

This design contains documentation only. The dependent implementation must
prove exact old-prefix reproduction, current exact-helper identity under
unrelated prefix changes, all original AST/mutation negatives, preserved
predecessor records, pre-Red non-retirement and exact 40-key authority.

No C/header, executable test, model, generator, adapter, harness, build
scaffold or reusable prototype changes in this design phase. Acceptance and
separate merge precede the restarted implementation.

## Design review disposition

Independent read-only review inspected the complete ten-document correction,
the relevant Accepted contracts, the actual token selector and identity
closure, and the author's current evidence at staged tree
`d39a9a3e0d074f492cdc536aa8c3d0bc5fba3e39`.
All four documentation-phase commands were FRESH/exit0; implementation
native/build/QEMU/stack checks were correctly N/A.

The reviewer concluded: "No significant issues found in the reviewed changes.
No substantive design issue remains." Acceptance records this bounded
40-key/27-path evidence correction, not implementation approval or a current
runtime acceptance result. Separate design merge still precedes the fresh
MEM03 implementation restart.

## Alternatives

- Recompute old token hashes: candidate-derived and still prefix-wide.
- Keep unconditional prefix checks: blocks the required ordinary-path change.
- Remove complete-scan checks: loses the retained diagnostic guarantee.
- Modify runtime layout to satisfy the bad selector: distorts production
  code for an incidental test boundary.
- Change the generic C parser: unnecessary scope expansion.
- Suppress the two keys without extending the plan: violates ADR-0082.

## References

- [Observed prefix-scope defect](../research/complete-validator-token-scope-gap.md)
- [Corrected MEM03 authority contract](../architecture/selected-frame-ownership.md)
- [Complete frame validation](0076-bounded-complete-frame-ownership-validation.md)
- [Evidence authority transition](0082-evidence-authority-transition.md)
- [Original MEM03 decision](0103-selected-frame-ownership-validation.md)

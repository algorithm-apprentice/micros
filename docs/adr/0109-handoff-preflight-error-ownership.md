# ADR-0109: Handoff Preflight Error Ownership

- Status: Accepted
- Date: 2026-11-07
- Refines: ADR-0040, ADR-0080, ADR-0082, ADR-0103, ADR-0107 and ADR-0108
- Supersedes only: ADR-0107's sixteen-production-subject limit and exact
  affected-fact-only candidate domain, for the one explicit dependency below
- Preserves: selected-path semantics, all existing handoff failures and
  mutation boundaries, complete diagnostics and the 33-key retirement plan

## Context

The separately reviewed reporting prerequisite #223/#224 is merged.
The fresh MEM04 attempt recorded genuine native/target Red, then its current
handoff workflow failed at stage 5 before the planned stage-8 witness.
No Green event, retirement or implementation publication occurred.

The [source and debugger investigation](../research/minix-selected-user-address-space.md#indirect-handoff-preflight-error-dependence)
establishes an implicit error dependency:

1. Handoff validates each prepared execution context before its own complete
   root certification.
2. The context validator checks PC/SP through ordinary page lookups.
3. An off-path orphan owner previously failed that lookup's global audit.
   The context layer converted it to `MAPPING`, then handoff returned `STATE`.
4. MEM04 correctly makes the PC/SP queries local. Handoff's existing complete
   root validator still rejects the orphan, but now returns `OWNERSHIP`.

The original target requires `STATE`. A read-only debugger observed the
unchanged missing-context case return 5 (`STATE`) and the orphan case return
11 (`OWNERSHIP`). This is not a timeout or missing successful transcript.

## Decision

After independent review, Acceptance and separate merge, restart MEM04 with
the [explicit handoff dependency contract](../architecture/selected-user-address-space.md#handoff-preflight-error-ownership).
The implementation remains blocked and archived during this design.

### Preserve the caller's error contract at the existing complete boundary

Add exactly one dependent production owner:
`kernel/memory/user_address_space_handoff.c`.

In `micros_user_address_space_prepare_wired_handoff()` and
`micros_user_address_space_complete_wired_handoff()`, normalize a non-OK
result from each function's existing `micros_user_address_space_validate_locked()`
call to `MICROS_USER_ADDRESS_SPACE_ERROR_STATE` before its existing
scratch-release cleanup.

Do not add, repeat, move or remove a complete validation call. Do not change
the validator's own specific result. The public complete diagnostic still
reports `OWNERSHIP`, `PTE` or `INVARIANT`; the owning handoff operation reports
its existing unusable-execution-state failure.

All other failures retain their original location and classification:
runtime/phase, object/ledger metadata, context, scratch admission, reachable
target selection, explicit `VM_WIRED` target checks and lower ownership
preflight/commit. In particular, a valid root whose leaf is staged
`VM_TRANSFERABLE` still fails `OWNERSHIP` at the existing target check.
Do not normalize every handoff error or alter its non-failing commit.

### One dependency, not another retirement

The candidate domain becomes 29 paths: seventeen production subjects and
the same twelve frozen candidate/assertion paths. Its 33 affected active
keys still derive exactly the original 28-path closure. The one additional
path is independently declared as a dependent production input and included
in every affected unit's exact candidate projection.

Its historical whole-file key is already retired by committed MEM03.
Do not claim or retire it again. The existing active MEM03 definition,
signature/callee, skeleton and unrelated-body clauses remain valid and
active: the two functions are already mutable under their exact call sets,
and the correction adds only scalar error assignments.

The existing EV01 closure mechanism already includes explicit unit input
paths. No shared validator, new authority family, metadata rebaseline or
general extra-path exception is needed. Preserve all 4,774 predecessor facts,
nineteen fingerprints, five claims and seventy retirements. MEM04 still adds
one eight-fact family and, only after complete acceptance, its original
33-key claim.

### Keep the appropriate work boundary

Selected lookup/translation remains local. Context validation does not regain
a hidden whole-root audit. Handoff retains its already owned complete
certification; the twenty residual caller rows and their downstream ownership
remain unchanged.

Normalization adds constant work, no call, state object, storage or mutation.
The MINIX selected-walk mechanism is unchanged. The typed one-way handoff
and its internal error enum are existing `micros` staged-substitution
contracts, not MINIX behavior to infer or copy.

## Evidence and restart

Before any new production edit, independently freeze the new base,
seventeen-subject/twelve-frozen-input partition, explicit dependency,
reporting inputs and exact two-site error-normalization inverse.
All unaffected handoff source bytes and active MEM03 clauses remain checked.

Keep the original strict orphan-state, missing-context and transferable-target
target assertions unchanged. The observed sixteen-subject candidate is the
regression failure, not a passing baseline or permission to weaken them.
Fresh native/target Red against the fixed runtime must still precede the
restarted implementation. Green must pass the original handoff assertions,
reach the independent stage-8 semantic witness, and pass both complete
canonical target workflows.

If capture or witness parsing fails, the MEM04 evidence owner must surface
its already captured workflow output for diagnosis before propagating failure.
It must not publish Green, reinterpret the failure or rerun QEMU to fetch a
second copy. This affects only the existing new MEM04 evidence owner.

Renew all four native products, both target captures, B01-B12/B14
recertification and the unchanged fifty-command producer with its current
receipts, target/image/stack coverage and independent first-commit review.
No archived execution is borrowed.

## Non-goals

No weakened fixture expectation, third target fixture owner, new target or
timeout, extra full scan, context API, handoff algorithm, commit order,
recovery, caller-stack inspection, cache, source-family replacement,
duplicate identity retirement, hosted-CI acceptance or later runtime task.
This design authors documentation only.

## Design review disposition

Complete independent read-only review inspected the nine-document proposal
and exact author evidence at staged tree
`f093c51730551cebfd858658bb448ea3940c6304`.
All four documentation/diff commands were FRESH/exit0 with matching
authoritative/final-candidate identity.

The reviewer reported: "No significant issues found in the reviewed changes.
No substantive design issue remains." No targeted reproduction or author-gate
rerun was needed. Acceptance records this design result, not a completed
runtime correction. The dependent implementation remains blocked until the
separate design merge and then restarts with renewed evidence.

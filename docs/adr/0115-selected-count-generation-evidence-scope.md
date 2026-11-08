# ADR-0115: Selected-Count Generation Evidence Scope

- Status: Accepted
- Date: 2026-11-08
- Refines: ADR-0103 and ADR-0114
- Supersedes only after separate Acceptance and merge: MEM05's unqualified
  preservation of the second result/output in the current MEM03
  `selected_count_guard`
- Preserves: the Accepted whole-slot count contract, exact-generation
  counting, historical expectations, source scope, work bounds and all
  final acceptance gates

## Context

MEM05 design #233 is separately merged as
`d256a5bb1ed8bf67e435a36107c9cec931a49d46`, tree
`2b134a5cf27d467ae072801dca79b110b5a74dd8`.
Implementation stopped during immutable fixture preparation, before a
production edit, formal Red event or identity retirement.

The [fixed-source investigation](../research/minix-selected-release-and-wired-handoff.md#mem05-c01-same-slot-count-evidence-conflict)
establishes a conflicting retained expectation.
At fixed #232, `selected_count_guard` corrupts one process owner's handoff
target. The exact-generation query returns `INVARIANT` and preserves its
output. A second query uses the same slot and the next encoded generation;
the old implementation skips that record's target check, returns `OK`
and publishes zero.

ADR-0114's selected-slot chain must validate every reached record's
encoding, allocation bit and target before publishing a generation-filtered
count. It therefore rejects that same-slot second query too. The simultaneous
promise to preserve every MEM03 fixture result, and the fixture adaptation
table limited to new inputs/index expectations, omitted this consequence.
Both requirements cannot hold.

An actual four-source native probe at the fixed runtime confirmed the old
results. A compiler failure, timeout, missing interface or synthetic journal
did not establish this conflict.

## Decision

After separate review, Acceptance and merge, apply the exact
[current-versus-historical count evidence contract](../architecture/selected-release-and-wired-handoff.md#mem05-c01-count-evidence-scope).

### Keep the Accepted count mechanism

Do not weaken the selected-slot chain check or add a physical-owner scan.
Every reached owner, including a different encoded generation in the same
slot, must have a valid encoding, bitmap relation and handoff target.
Only the successfully checked records contribute to the exact-generation
count.

A malformed reached record returns `INVARIANT` and leaves the output and
complete state unchanged. A valid other-generation record remains a valid
chain member but does not increment the result.
Another slot's corruption remains outside this selected query; explicit
complete diagnosis must still reject it.

This clarifies evidence for the already Accepted algorithm. It does not
change its runtime API, error precedence, authority, complexity or storage.

### Admit one exact current-fixture expectation change

In the current branch of
`tests/host/selected_ownership_test.c:selected_count_guard`, keep:

- the original geometry, process/owner input and invalid target;
- the first exact-generation rejection;
- the same-slot `foreign = context.process` plus generation increment;
- the guard name, order, invocation and complete snapshot comparisons.

Replace only the second query's `OK`/zero expectation with `INVARIANT`
and preservation of the original `UINT64_MAX` output.
Do not change the foreign query to another slot to retain a success-shaped
old result, repair the injected target before this negative query, skip the
guard, or normalize production errors in a fixture wrapper.

This is the sole exception to MEM05's legacy-result preservation promise.
All other legacy cases, outputs, seeds, schedules, measured bounds and
complete diagnostic counts remain required.
It fits the already declared immutable fixture owner and is frozen before
the fresh formal Red.

### Keep historical and current evidence separate

The fixed #232 implementation and its fixture retain their original
`INVARIANT`/preserved-output then `OK`/zero behavior.
No historical literal, fact, fingerprint or recorded result is rewritten.
The old guard belongs to MEM03's historical current branch, not its earlier
baseline-only branch; do not claim that a baseline-flag run executes it.

Current MEM05 evidence additionally requires independent paired cases:

1. an invalid target reached through the requested slot fails even when
   the record's generation differs from the requested generation;
2. with that same record's target valid, the other-generation query returns
   `OK` with zero rather than treating an encoded generation as a live
   process requirement; and
3. an invalid target in another slot does not reject a valid selected-slot
   count, while complete validation rejects the corrupted ledger.

Each rejection preserves the output and all ownership/index/allocator
state. The successful cases independently check the expected count and
absence of mutation. These are current-only guards in the existing MEM05
fixture, not additions to the fixed 128 common rows or a substitute Red.

### Retain the closed implementation and acceptance scope

Keep the seventeen production subjects, 82 retirement keys, twenty-nine
candidate paths, twenty-three existing noncandidate adapters and eight new
evidence/tool owners. The existing current MEM03 fixture owner receives the
one additional adaptation above; no file is added to that register.

Preserve all 5,379 predecessor facts, twenty-two fingerprints, six claims
and 103 retirements. The three-oracle partition, five native products,
128 common rows, target witnesses, B01-B12/B14 recertification and full
native/target/image/stack acceptance remain unchanged.

The archived pre-Red scaffold is reference only. Restart on a fresh branch
from the corrected main, independently freeze its actual base and every
immutable input, then renew genuine Red before production changes.
Do not borrow the preliminary native/target runs or create a synthetic
earlier event. No timeout, capacity, image or stack budget increases.

## Phase boundary and acceptance

This corrective design contains documentation only. No runtime, test,
model, fixture, generator, scaffold or reusable executable prototype is
authored in its pull request.
Documentation/structure and all three diff checks precede independent
review. Clean review, Acceptance and separate merge precede the fresh
MEM05 implementation.

Implementation proves the exact current fixture change, the paired scope
controls, preserved historical evidence and unchanged full acceptance
before independent implementation review and its first permanent commit.
This correction closes no runtime finding and does not accept hosted CI
or later POSIX work.

## Design review disposition

Independent read-only review of the complete nine-document proposal found
one Medium issue: the specified preserved guard name did not match the
existing `selected-process-count` protocol identifier.
The author verified the fixture emitter and strict ordered parser, then
corrected only that documentation literal without changing either test.

Correction review inspected staged tree
`3819a86970c01751c3c2416241a999b505d60795` and its matching four
FRESH/exit0 documentation/diff results. The reviewer reported:
"The prior Medium finding is closed. No substantive design issue remains."
No targeted reproduction or author-gate rerun was needed.

Acceptance records this design result, not an implemented fixture correction,
runtime Green or retirement. The separate corrective design merge still
precedes the fresh MEM05 implementation and its complete actual evidence.

## Alternatives

- Check targets only after generation filtering: weakens the Accepted
  complete consumed-chain contract.
- Move the negative query to another slot: changes the test input to hide
  the demonstrated scope conflict.
- Rewrite all legacy expectations from current output: makes the oracle
  candidate-derived instead of independently specified.
- Remove the guard or mark it passed without execution: loses existing
  negative coverage.
- Retire more source identities or change an old fingerprint: the existing
  MEM05 scope already covers the count implementation; the missing decision
  is only the precise current-fixture expectation.

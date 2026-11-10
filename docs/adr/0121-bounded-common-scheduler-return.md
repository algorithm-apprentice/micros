# ADR-0121: Bounded Common Scheduler Return

- Status: Accepted
- Date: 2026-11-10
- Refines: ADR-0011, ADR-0022, ADR-0023, ADR-0026, ADR-0027, ADR-0033,
  ADR-0034, ADR-0035, ADR-0062, ADR-0068, ADR-0080, ADR-0082, ADR-0083,
  ADR-0084, ADR-0105, ADR-0107, ADR-0119 and ADR-0120
- Supersedes only after separate Acceptance and merge: whole-capacity
  scheduler return projection/apply, its 1,344-byte plan and static matrix;
  whole-selected-hart preflight needed only for that apply; duplicated
  validation of the same outgoing/selected context; and the precisely
  identified current fixture/structure expectations

## Context

OBJ01 implementation #243 is independently reviewed and merged after MEM04.
OBJ02 is the next preferred correction and owns only B03-F002/F005.
The [fixed-source study](../research/minix-common-scheduler-return.md) pins
runtime and evidence policy to #243 and the original MINIX reference.

Ordinary scheduler preflight is now selected, but return planning still
copies queues into 4,224 bytes of static scratch, shifts them for exhausted
quanta, publishes a 1,344-byte projection and applies every assigned slot
and priority. The portable checked commit rebuilds that whole plan.
The production target already calls the prevalidated apply directly.

MEM04 also removed complete root audits from ordinary lookup.
The remaining same-thread common-return duplication is more specific:
the outgoing context's status/PC/SP are validated and stored, then the
same selected context is inspected and validated again.
Inspection and storage do not themselves repeat PC/SP validation.

MINIX keeps CPU-local current and singly linked priority queues, updates
affected links and binds the selected context directly.
The `micros` failure-atomic, generation-safe and protected-epoch boundaries
remain required adaptations, not reasons to project all state.

## Decision

After independent review, Acceptance and separate merge, implement the
[common-return contract](../architecture/common-scheduler-return.md).

### Retain one bounded affected-state plan

Replace the matrix and capacity-wide arrays with at most two queue
descriptions, each containing before/after endpoints, one checked original
renewal prefix and at most three next-link patches.
Handle a separately reinserted current explicitly.
Keep the complete plan at most 320 RV64 bytes and remove static plan scratch.

Preserve current retention, priority order, preemption repair, quantum
renewal, nonpreemptible selection and permitted null encodings.
Check every consumed endpoint, successor, generation, owner and policy before
stores. Bound malformed traversal and distinguish real tail wrap from a
consumed cycle.

The checked portable API still rejects mismatched plans without mutation by
checking the bounded expected result. The target still consumes its exact
prepared plan directly. Apply renews prevalidated original prefixes before
changing links, then publishes only affected queue/current/stack state.
It performs no validation, new search or recoverable operation.

### Keep scope proportional to consumed state

Remove the whole-hart thread scan that existed to protect capacity-wide apply.
Do not normalize unrelated null fields or certify unselected assignment,
membership or reschedule existence through ordinary success.
Complete diagnostics still reject all such corruption.

Retain stale explicit-handle and consumed-invariant errors, failure-atomic
publication and the existing stale consumed-tail regression.
The contract explicitly identifies the three historical current cases whose
whole-hart error expectation changes; their input corruption and complete
diagnostic rejection remain.

### Reuse only the actual protected outgoing proof

Retain an ordinary outgoing context snapshot only after its actual
validation and storage succeed.
The selected loader may reuse it only for the identical generation-safe
thread within the same SIE-clear, mapping-stable return epoch.
Different-thread, captured, start and idle selection cannot borrow it.

Keep root preparation, complete ASID-zero fences, completion preflight and
commit, accounting/timer ordering, first-start's two samples, invalid-context
diagnostics, frame custody and idle assembly unchanged.
No memory/execution API, mapping lifecycle, persistent validity cache or
new authority token is introduced.

### Keep evidence independent and complete

The prospective exact transition has 58 existing active records and a
28-path direct closure, partitioned 45 delta-work / thirteen context-reuse.
Preserve all 6,327 predecessor records, twenty-seven fingerprints, eight
prior claims and 280 retirements. Renewed success reaches 338 retirements.
Freeze the actual merged base, new individual current predicates and every
fixture/adapter before genuine Red.

Preserve fixed historical OBJ01 tables and original literal values.
Only the independently specified newer-current semantic/layout adaptations
may differ; raw results are not normalized and corrupt inputs are not moved
to hide a scope change. Unrelated source, storage and phase invariants stay.

Require actual native work/state evidence and real QEMU context-validation
captures with original scheduler semantics and cleanup.
Use bounded test-owned capture storage and report after final semantic checks,
not UART output after a timing-sensitive return's timer preparation.
A generic failure, timeout, compile error or aggregate-only result is not Red.

Keep all original native, target, integration and stack coverage, the complete
affected-source selection, fifty fresh author commands, three same-run
profiles and independent review before the first completed-claim commit.

## Non-goals

No scheduling policy or user SCHED, priority/quantum change, new public syscall,
process/thread/hart equivalence, SMP, ASID shortcut, memory-policy change,
IPC completion/endpoint rewrite, persistent cache/index, generic transaction
framework, capacity/timeout/stack increase, hosted-CI acceptance or node Q.

This Accepted design contains documentation only. No executable prototype,
fixture, observer, test, model, harness, build scaffold or implementation
branch precedes its independent review, Acceptance and separate merge.

## Design review disposition

Independent read-only review inspected the complete eleven-document proposal,
the relevant Accepted contracts, fixed MINIX/current-source behavior,
bounded queue/context design, exact source-authority selection and the
author's current evidence at staged tree
`e6a3e6bea14b9b85a177aec5abc5db893c95e15b`.
All four documentation-phase commands were FRESH/exit0; implementation
build, native/model, QEMU, integration and stack gates were correctly N/A.

The reviewer reported: "No significant issues found in the reviewed changes.
No substantive design issue remains." No targeted reproduction or author-gate
rerun was performed.
Acceptance records this design result, not a runtime change, executed
replacement oracle or retirement. The separate design merge still precedes
the fresh implementation and its complete renewed evidence.

## Alternatives

- Keep the matrix but optimize its copy: retains capacity-wide work and shifts.
- Apply caller patches without checking them: loses the portable stale/tamper
  boundary.
- Add a persistent validation ticket or cache: requires invalidation and
  lifecycle authority absent from this outcome.
- Reuse a captured or different thread's context proof: borrows authority
  across distinct custody.
- Delete the old corruption fixtures: hides the deliberate scope change.
- Rewrite old hashes/tables from current output: makes expected results
  candidate-derived.
- Print every observation in the return tail: can recreate the expired-timer
  progress failure fixed by ADR-0027.

## References

- [Exact common-return contract](../architecture/common-scheduler-return.md)
- [Fixed-source study](../research/minix-common-scheduler-return.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)
- [MINIX-baseline scheduler](0026-minix-baseline-kernel-scheduler.md)
- [Return timer preparation](0027-return-boundary-timer-rearming.md)
- [Selected object/hart predecessor](0119-selected-object-and-hart-validation.md)

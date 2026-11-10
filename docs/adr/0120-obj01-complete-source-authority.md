# ADR-0120: Complete OBJ01 Source-Authority Closure

- Status: Accepted
- Date: 2026-11-10
- Refines: ADR-0033, ADR-0034, ADR-0068, ADR-0082, ADR-0084, ADR-0105
  and ADR-0119
- Supersedes in part, only after separate Acceptance and merge:
  ADR-0119's ninety-four-key selection, its derived 279-retirement total,
  and the insufficiently explicit affected-source test selection
- Preserves: the accepted runtime algorithms, thirty-eight candidate paths,
  original fact values, complete diagnostics and all final acceptance gates

## Context

OBJ01 design #241 separately merged after EV01-C09 #240. Its uncommitted
implementation reached actual Red/Green and the declared thirteen-unit
transaction. The complete native workflow then passed 46 CTests but failed
three of 1,667 host tests. No implementation commit, pull request, independent
implementation review or complete author acceptance followed.

The [fixed-source investigation](../research/minix-selected-object-and-hart-validation.md#obj01-c01-complete-source-authority-gap)
identifies an existing active identity omitted from the accepted selection:

```text
tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/kernel/scheduler_core_validate.c
```

Its expected value is
`73bd02fdac06a05c0bdc33130258132e3fe90cfe8c2204f5502e054d5021f38c`.
The guard freezes the complete projected scheduler-validation source, including the
unscheduled-current exception that ADR-0119 already requires removing.
Neither another retired identity for the same file nor a passing native
oracle retires this separate assertion.

B14 inherited a memory-task host selection. It did not include the complete
object source-layout module, the panic machine class or the stack-source
projection module. Its reported pass therefore did not execute these three
failed checks. The other failures are historical/current fixture mismatches,
not permission to weaken source or machine proof.

The implementation is blocked and archived under ADR-0033. Its branch is
abandoned. The source and successful partial results are reference evidence,
not acceptance for the corrected design.

## Decision

After separate review, Acceptance and merge, apply the
[OBJ01-C01 closure contract](../architecture/selected-object-and-hart-validation.md#obj01-c01-complete-source-authority).

### Extend exactly one existing retirement

Select the original ninety-four complete records plus the one identity above.
The corrected ninety-five-record digest is:

```text
df04235e5286127c14a13ce42fdba7eda52bbe8835bb6e72090eccfe62b40c41
```

Both its source and assertion owner already belong to the thirty-eight-path
domain. Do not add a candidate path, change the existing record, register a
duplicate identity or regenerate its expected hash.

The local-work and stack-work partitions remain 68 and eleven keys.
The direct-current-ownership partition becomes sixteen keys and explicitly
includes the omitted complete-validator identity. Successful renewed
recertification adds one claim and reaches 280 total retirements.
All 5,851 predecessor records, twenty-five fingerprints, seven prior claims
and 185 prior retirements remain unchanged.

Before that exact key is legitimately retired, its original comparison stays
active. After retirement, qualify the actual current validation source under
the independently declared OBJ01 contracts. Keep every other neighbor hash,
unrelated function, definition, source boundary and diagnostic obligation.

### Make its replacement property discriminating

The direct-current oracle retains every existing probe and ownership bound.
It additionally requires an otherwise coherent fixed-source state whose exact
current thread is unscheduled and whose hart has no assigned thread.
The fixed complete scheduler diagnostic accepts this unsupported state;
the accepted replacement must return `INVARIANT` without changing state.
Context capture must retain ADR-0119's `STATE` rejection.

The actual fixed-source result is a separate discriminating Red witness for
the additional identity. A failure only in an unrelated lookup-work case is
insufficient coverage for this key. Do not change the fixed observation into
an expected current result or treat a compiler/harness failure as Red.

### Include the actual affected evidence owners

B14 must execute the complete object source-layout, panic-trace and
stack-source projection modules in addition to its retained obligations.
Replace covered partial panic selectors with the complete module, preserving
every old case exactly once. Keep the existing checked host runner; do not
add a generic selection framework or move this work into the fast tier.

The two fixture adaptations are independently frozen before Red:

- The panic machine negative mutates the selected output store in the actual
  `micros_hart_resolve_context` definition, not an old indentation pattern or
  another resolver. Real compiled fault-prefix, hart-slot, latch, context and
  protected-effect checks remain mandatory.
- The historical stack-projection fixture derives its expected bytes from
  the fixed historical reference and exact directory/include projection.
  Actual current source is independently qualified and is never substituted
  for, or reported as, historical source.

`tests/host/test_panic_trace.py` and
`tests/host/test_stack_source_projection.py` are explicit immutable fixture
inputs, not mutable runtime candidates or new retirement owners.

### Restart and renew complete acceptance

Restart OBJ01 from current `main` only after this corrective design merges.
Bind both accepted decisions and the new merged design commit/tree.
Independently declare the actual base, complete ninety-five-key partition,
current source predicates and all immutable fixtures/adapters before Red.
Do not cherry-pick the abandoned implementation wholesale or reuse its events.

Renew actual Red, Green and all B01-B12/B14 units. Verify real terminal
checked-source-view construction before ordinary native generation consumes
retirement. Preserve complete native discovery, all fifty fresh author
commands, forty QEMU workflows, four standalone images, 44 static
configurations, 113 ELFs, ten observed scopes, blocked B13 and three same-run
profiles. Independent implementation review still precedes the first permanent
completed-claim commit and exact-tree publication.

## Non-goals and phase barrier

No runtime algorithm, layout, ABI, policy, failure result, capacity, timeout,
stack/image budget, parser, collector, planner or runtime-DAG edge changes.
OBJ02, IPC/VM corrections, hosted CI and later POSIX work retain their owners.

This design contains documentation only. No code, executable test, model,
fixture, adapter, generator, build scaffold or reusable prototype belongs to
this pull request. Independent review, Acceptance and separate merge precede
implementation.

## Alternatives

- Recompute the old neighbor hash: circular candidate-derived authority.
- Suppress it under another file identity: violates granular retirement.
- Keep the unscheduled-current exception: contradicts ADR-0119.
- Treat the smaller B14 selection as complete coverage: contradicted by the
  actual full-suite failure.
- Delete the historical or machine negatives: removes independent evidence.
- Borrow the archived terminal record: its plan omits the required identity.

## Design review disposition

Independent read-only review inspected the complete nine-document proposal,
the relevant Accepted contracts, the fixed validator identity and runtime
exception, affected fixture/selection boundaries and the author's exact
evidence at staged tree `96b2b520846b6595a446af4a6eb7ac994e06d79c`.
All four documentation-phase commands were FRESH/exit0; implementation
build, native, QEMU, integration and stack gates were correctly N/A.

The reviewer reported: "No significant issues found in the reviewed changes.
No substantive design issue remains." No targeted reproduction was reported.
Acceptance records this bounded design correction, not implementation
acceptance or retirement. Separate design merge still precedes the fresh
OBJ01 implementation and its complete renewed evidence.

## References

- [Original OBJ01 decision](0119-selected-object-and-hart-validation.md)
- [Corrected closure contract](../architecture/selected-object-and-hart-validation.md#obj01-c01-complete-source-authority)
- [Observed source-authority gap](../research/minix-selected-object-and-hart-validation.md#obj01-c01-complete-source-authority-gap)
- [Evidence authority transition](0082-evidence-authority-transition.md)
- [Corrective design barrier](0033-reviewed-design-before-implementation.md)

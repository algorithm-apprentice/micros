# MINIX Operation-Count Evidence Baseline

## Scope and provenance

This is the fixed-reference study for EV02 and B14-F002. Only the local
checkout at MINIX commit
`4db99f4012570a577414fe2a43697b2f239b699e` was inspected. The corresponding
current `micros` predecessor is EV01 implementation #177, commit
`81abd08b1ac6f24c2df5b2980fbe518a7e3f7c8d`.

No MINIX source, test body, or shell runner is copied or executed. The
reference runner performs filesystem cleanup and privilege transitions, and
some service fixtures use sleeps; those are source evidence, not commands to
run on the development host.

## Fixed-reference trace

| MINIX source | Behavior, authority, and logical state | Algorithm and bounds | EV02 mapping |
| --- | --- | --- | --- |
| `minix/tests/common.h`: `e`, `em`, `efmt`, `errct`, `subtest` | Test-owned error reporting and case identity; no runtime authority | Fixed scalar state; reporting forwards one failure to the common owner | A work measurement is separately test-owned and must not be confused with action or error coverage |
| `minix/tests/common.c`: `start`, `e_f`, `quit` | Initialize a test, increment the error count, abort after the error threshold, and exit nonzero when errors remain | Counter mutation is constant work; formatting and cleanup have independent costs and fixed local buffers | Observation faults remain explicit and cannot produce a successful numeric snapshot |
| `minix/tests/run`: `alltests`, `runtest`, aggregate result | Enumerate the fixed test set, run each selected case, retain failures, and report aggregate status | Linear enumeration over selected tests; passed/failed/skipped/total are scalar action/result counts | Keep current CTest and model coverage; add primitive work rather than replacing existing gates |
| `minix/tests/testvm.c`: `dowriteblock`, `readblock`, `testinit`, `main` | Exercise service-owned cache/mapping behavior at explicit sizes; transfer exact bytes and transport the result to the driver | Work depends on requested block sizes and selected workload; the result/time fields do not count underlying copies, maps, or validation | Use declared boundary inputs and exact primitive observations; do not infer work from a successful result or elapsed time |

The reference establishes independent error visibility and workload selection.
It does not supply a complete internal operation-counter mechanism. Required
runtime structures and algorithm families remain the authority of B01-B13,
not the reference test runner's result counters.

## Current micros gap

[B14](minix-complete-baseline-b14-evidence.md) records that current CTests,
models, and target scenarios prove useful behavior but do not count the work
terms in the audited paths. Model coverage establishes that an action was
visited, not how often a production comparator, record walk, complete
validator, or copy primitive executed.

The existing `micros` source-transport assertions also preserve earlier
project bodies. Instrumenting a body must preserve that assertion through an
exact observation projection; changing its algorithm requires the separate
ADR-0082 authority transition.

## Classified adaptations

| Difference | Classification | Required invariant |
| --- | --- | --- |
| Hosted CTest/Python and freestanding RV64 compilation rather than the MINIX shell runner | Required adaptation | Existing gates and explicit failures remain authoritative; no reference runner is executed |
| Producer-local, typed primitive-work vectors rather than scalar error/action counts | Compatible extension | Instrumentation is test-owned, records actual primitive work, and adds no production work |
| 64-bit lock-free atomic updates for interrupt-safe observation | Compatible extension | No lost update under the supported test execution; no external atomic runtime; start/finish are quiescent |
| Overflow invalidation and exact complete-producer composition | Compatible extension | No wrap, saturation, partial record, omission, or double counting can be accepted |
| Finite source-annotation projection | Compatible extension | Immutable primitive/placement anchors reject relocated observations; projected source still satisfies the original transport assertion and all permanent invariants |
| Keeping runtime correction, stack proof, and node-Q implementation in their existing later tasks | Staged substitution | EV02 never claims that tool availability closes those tasks or inherited evidence gaps |

## Work and storage accounting

The observer's work is distinct from the measured algorithm. Disabled
annotations add zero observation work and state. Enabled recording uses a
constant number of abstract atomic operations per site; this is not a CPU-cycle
or wall-clock guarantee. Reset and snapshot work are linear in the fixed
owner-by-kind vector, outside the measured scope.

The collector and one snapshot each fit a 4 KiB page and use fixture-owned BSS,
not the 4 KiB service/application stack. Hosted composition and source
projection have their explicit average/worst bounds in
[the canonical contract](../architecture/operation-count-evidence.md).
Future target fixtures must retain their Accepted resident-page and stack
budgets; a counter does not justify raising a deadline or budget.

## Acceptance boundary

EV02 must demonstrate measurement and transport admission using independent,
test-only fixtures. Actual production annotations and baseline work bounds
belong to each future owning correction. Those designs pin their primitive
definitions, sites, boundary/adversarial inputs, producer set, and independent
expected bounds before Red. Reference models never use observed production
counts to compute their expected result.

## References

- [ADR-0083](../adr/0083-test-only-operation-count-evidence.md)
- [Operation-count contract](../architecture/operation-count-evidence.md)
- [B14 evidence audit](minix-complete-baseline-b14-evidence.md)
- [Correction DAG](minix-complete-baseline-findings-and-correction-dag.md)

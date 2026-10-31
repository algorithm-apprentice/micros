# ADR-0083: Test-Only Operation-Count Evidence

- Status: Accepted
- Date: 2026-10-31
- Refines: ADR-0033, ADR-0034, ADR-0080, and ADR-0082

## Context

The complete baseline audit records B14-F002: existing semantic tests, model
action-coverage counters, and QEMU results do not measure the primitive work
in the audited algorithms. They can pass while ordinary operations repeatedly
scan complete state, move bytes individually, or perform quadratic searches.

EV01 is merged as design #176 and implementation #177. It separates historical
source transport from independent baseline authority, but it does not provide
operation counters. EV02 is the next preferred dependency-ready outcome in
[the correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md).

The fixed local MINIX testing baseline is traced in
[the operation-count study](../research/minix-operation-count-evidence.md).
MINIX's error and action counters are not primitive-work oracles. The new
measurement mechanism is an explicit compatible testing extension, not a
claim that MINIX provides this instrumentation.

## Decision

Implement the bounded
[operation-count evidence contract](../architecture/operation-count-evidence.md).

### Passive, test-only observations

One shared annotation header has two exact modes:

- undefined `MICROS_TEST_WORK_COUNTS`: statements disappear without evaluating
  their arguments; comparison annotations evaluate only their original
  expression, exactly once;
- `MICROS_TEST_WORK_COUNTS=1`: annotations record work in a producer-local,
  test-owned C17 collector.

Every other explicitly defined value is rejected. There is no runtime switch,
production collector, new syscall, or cross-process pointer authority.
Enabled compilation is private to explicitly selected evidence targets.

Primitive work is recorded where it executes, not estimated at callers.
Owners identify source responsibilities; producer labels distinguish separate
executions of shared code. Counter updates use lock-free 64-bit atomics.
Overflow invalidates the entire measurement instead of yielding usable
wrapped or saturated counts.

### Closed measurements and composition

Each measurement has one producer label, one capture label, and a declared
owner mask. These are observation data, not process, endpoint, thread, grant,
or reply authority. Capture labels strictly increase within one executable and
are consumed even by a later failed measurement. Start and finish require
fixture-enforced quiescence.

Snapshots are complete and immutable after publication. Hosted composition
requires an independently declared producer set and owner masks, one successful
snapshot per producer, the exact capture label, known dimensions, and unsigned
64-bit counts. It rejects duplicates, omissions, unexpected producers, partial
records, failed measurements, and arithmetic overflow.

Composition adds matching work dimensions exactly once. It does not sum
unlike units, infer counts from elapsed time, multiply caller invocations by
average callee costs, or accept an inclusive parent aggregate as another leaf.

### Transport-preserving annotation admission

Approved observation statements and comparison wrappers may be projected out
before an existing historical source-transport assertion. The projection has
an exact, externally declared source/function/site inventory and removes only
the admitted observation constructs and their single declared header include.
Every site also binds an immutable source-stage-specific primitive occurrence,
its complete enclosing compound stack, and exact before/after/wrapping
placement. A loop-local observation cannot be moved after the loop merely
because removing it would reconstruct the same original body.

All other source remains unchanged. Missing, duplicate, malformed, misplaced,
redefined, or undeclared annotations fail. Existing expected source identities
are not regenerated, unrelated identities remain active, and permanent
ownership, recipe, storage, ABI, and source-size invariants remain required.

A production correction still follows ADR-0082. Projection preserves a
transport identity during observational instrumentation; it cannot approve a
different production algorithm or retire an identity.

### EV02 boundary

EV02 adds the collector, hosted composition and annotation-projection helpers,
deterministic native fixtures, disabled-mode and freestanding RV64 compile
evidence, and directly dependent inventory/build/documentation updates.

It does not install annotations in the current production mechanisms, correct
their algorithms, retire real source identities, supply stack tooling, change
target assembly, add telemetry IPC, or implement node Q or later functionality.
Its fixtures are measurement evidence, never reusable production prototypes.

## Acceptance

The implementation must prove:

- exact primitive counts on boundary and adversarial fixture inputs;
- unchanged fixture behavior with observation enabled and disabled;
- single evaluation of comparison expressions and no evaluation of disabled
  observation arguments;
- complete, failure-atomic snapshots and fail-closed capture/composition;
- overflow, invalid dimensions, overlapping/stale capture, duplicate producer,
  and missing-producer rejection;
- preservation of an immutable source identity under exactly admitted
  annotations, with negative controls for hidden production changes and
  intra-function primitive/observation relocation;
- lock-free freestanding RV64 compilation without an external atomic runtime;
- collector and snapshot objects each fitting one 4 KiB page without
  automatic-storage placement;
- no observer symbols or state in the disabled fixture or current production
  artifacts;
- unchanged existing native/model/QEMU identities and mandatory coverage;
- complete current author validation and clean independent implementation
  review.

This is design only until its separate PR is reviewed, Accepted, and merged.
Completing EV02 supplies measurement infrastructure; it does not close the
inherited per-operation evidence gaps or certify the runtime baseline.

## Alternatives considered

### Use existing model action-coverage counters

Rejected. Visiting an operation category does not measure the operation's
comparisons, scans, copies, or nested validation.

### Use timing thresholds

Rejected. Host load and QEMU speed do not establish an algorithmic bound.

### Instrument every current runtime path in EV02

Rejected. That would expand one foundation into the later correction tasks.
Each owning correction declares its actual primitive sites and independent
Red/Green oracle.

### Automatically rewrite historical identities

Rejected. Instrumentation is admitted through a finite transport projection;
algorithm changes still require ADR-0082's independent replacement authority.

## References

- [Operation-count contract](../architecture/operation-count-evidence.md)
- [Fixed MINIX study](../research/minix-operation-count-evidence.md)
- [B14 evidence audit](../research/minix-complete-baseline-b14-evidence.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)
- [Evidence authority transition](0082-evidence-authority-transition.md)
- [Structural and algorithmic baseline](0080-minix-structural-and-algorithmic-baseline.md)

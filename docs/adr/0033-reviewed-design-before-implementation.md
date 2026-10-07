# ADR-0033: Reviewed Design Before Implementation

- Status: Accepted
- Date: 2026-10-07
- Refines: ADR-0013 and ADR-0028
- Supersedes:
  - ADR-0013 combined design/Red-Green template evidence;
  - ADR-0013 next-task sequencing only for a blocked corrective-design
    transition;
  - ADR-0028 Red-Green evidence requirement for design pull requests.

## Context

`micros` already requires documentation-first, test-first, dependency-ordered
development and independently reviewed small pull requests.

Recent work still began implementation before the design boundary was fully
resolved. Review then discovered missing authority rules, state invariants,
validation ownership, and scope boundaries. Fixing those discoveries inside
the implementation pull request caused repeated redesign, oversized diffs, and
long review cycles.

The problem is not insufficient implementation effort. The problem is that
design and implementation were allowed to converge in the same review loop.

## Decision

Every implementation-bearing task uses two sequential, separately reviewed
phases:

1. reviewed design;
2. reviewed implementation.

The design phase must merge before implementation work begins.

### Phase 1: design

One design task and pull request define exactly one dependency-ready outcome.

Permitted committed design artifacts are:

- research and behavioral-baseline documents;
- Proposed ADRs;
- architecture and interface specifications;
- task/DAG dependency metadata;
- acceptance-test specifications;
- workflow and contributor documentation required to make the design
  unambiguous.

The design phase must not commit:

- production code or assembly;
- executable tests, models, harnesses, fixtures, or generated binaries;
- build targets or implementation scaffolding;
- reusable prototype code;
- dependency or package changes needed only by the future implementation.

The design pull request contains no speculative framework.

The design must trace the accepted behavioral baseline and identify:

- goal and measurable outcome;
- explicit non-goals;
- dependency readiness and affected DAG node;
- authoritative ADRs and architecture constraints;
- object ownership and authority boundaries;
- public, internal, and target interfaces;
- complete state and lifecycle transitions;
- invariants before, during, and after each transition;
- validation and serialization boundaries;
- failure ordering, atomicity, rollback, and diagnostics;
- stale-generation and teardown behavior;
- target-specific versus portable responsibilities;
- native, model, QEMU, integration, and end-to-end evidence required;
- expected performance and resource bounds;
- implementation slices and commit boundaries;
- known risks and rejected alternatives.

The design must be deep enough that implementation does not need to invent a
new authority rule, externally visible behavior, lifecycle state, failure
contract, or acceptance criterion.

Durable decisions belong in a new or revised Proposed ADR. Research evidence
and baseline adaptations belong in canonical research or architecture
documents. A task prompt, issue, or pull-request description is not sufficient
design authority.

### Design review

The design pull request receives an independent, read-only review before its
ADR can become Accepted.

The reviewer verifies:

- source-of-truth and dependency ordering;
- baseline completeness and classified adaptations;
- authority, ownership, and generation rules;
- state-machine completeness;
- success and failure commit boundaries;
- testability at the correct layers;
- bounded scope and non-goals;
- feasibility without hidden implementation decisions;
- consistency with every relevant Accepted ADR.

Review findings must identify a missing or contradictory design decision, not
request implementation details that do not affect the contract.

The design is implementation-ready only when no substantive design finding
remains. The reviewed design is then marked Accepted and squash-merged.

### Phase barrier

An implementation branch, production-code edit, executable test, model,
harness, build scaffold, or implementation dependency change must not begin
before the design pull request merges.

The design and implementation todos are created together before Phase 1. The
design todo becomes active; the implementation todo remains `pending` and
records the barrier explicitly:

```text
implementation task -> design task
```

Exploratory design research may inspect code and run existing probes or
commands. Newly authored executable prototypes, tests, models, harnesses,
fixtures, build scaffolds, or dependencies begin only after design merge.

Design pull-request descriptions identify `Phase: Design`. Implementation
pull-request descriptions identify `Phase: Implementation` and reference the
merged design pull request and outcome commit.

### Phase 2: implementation

The implementation task references the merged design and changes only the
defined outcome.

Implementation follows the existing test-first loop:

1. reproduce the required Red state;
2. implement the smallest complete behavior;
3. keep each permanent commit green;
4. run the design's required evidence;
5. obtain independent implementation review;
6. validate findings and fix only justified defects;
7. re-review until no substantive issue remains;
8. squash-merge the single outcome.

Implementation review does not reopen accepted design preferences. If the code
cannot satisfy the accepted design, implementation stops and the design is
changed through a separate reviewed design pull request.

### Scope discoveries

Every pull request starts with one written focus.

A newly discovered problem becomes a separate todo when it is not required to
make the current focus correct and green. It does not expand the current pull
request.

If the discovery reveals that the accepted design itself is incomplete or
wrong:

1. stop implementation;
2. mark the implementation todo `blocked` and stop all work on its branch;
3. close the implementation pull request when one exists;
4. preserve a patch only as non-authoritative reference, then abandon the
   implementation branch;
5. record a corrective design todo and make the implementation todo depend on
   it;
6. create and review only the corrective design pull request;
7. after it merges, update the implementation todo's design reference, move it
   from `blocked` to `pending`, then to `in_progress`;
8. create a fresh implementation branch from current `main`;
9. reintroduce still-valid behavior through the normal test-first loop rather
   than cherry-picking the abandoned implementation wholesale.

This corrective design task is a prerequisite repair inside the same outcome,
not parallel product development. It narrowly supersedes ADR-0013's next-task
rule only for this transition: the blocked implementation is inactive, and the
corrective design is the only active task.

Closely coupled defects caused by the current change remain in scope only when
leaving them unfixed would make the current outcome incorrect, unsafe, or
unbuildable.

### Documentation-only work

Pure documentation corrections that introduce no new decision or
implementation behavior need only one documentation pull request. A document
that defines future implementation behavior is a design phase and must pass
the design review barrier.

## Consequences

- Design review occurs before code volume makes decisions expensive to change.
- Implementation reviews become narrower and evidence-driven.
- More pull requests are created, but each has one explainable purpose.
- Todo dependencies make blocked implementation work visible.
- Design mistakes cause an explicit stop and redesign rather than scope creep.
- Small typo-only documentation changes remain lightweight.

## Alternatives considered

### Design and implementation in one pull request

Rejected because design findings repeatedly force broad code rewrites and
inflate review scope.

### Begin implementation while design review runs

Rejected because review changes can invalidate in-flight code and recreate the
same rework.

### Allow implementation to refine missing contracts

Rejected. Refining internal mechanics is expected, but inventing authority,
state, failure, or acceptance contracts belongs in reviewed design.

### Require a design pull request for every typo

Rejected. The barrier applies to implementation behavior and durable
decisions, not mechanical documentation corrections.

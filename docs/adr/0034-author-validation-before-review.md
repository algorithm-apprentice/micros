# ADR-0034: Author Validation Before Review

- Status: Accepted
- Date: 2026-10-07
- Refines: ADR-0013, ADR-0031, ADR-0032, and ADR-0033

## Context

Independent review is required for both design and implementation phases.
Recent reviews sometimes repeated the author's complete native or QEMU
validation matrix before reading the change. That duplicated expensive work,
lengthened small-PR feedback, and did not make the review more independent.

Review independence comes from a separate correctness analysis, not from
rerunning the same commands without a concrete hypothesis.

The author must still provide trustworthy, current, exact evidence before
review begins. A reviewer must still be able to reproduce a suspected defect
when static evidence is insufficient.

## Decision

Authors own phase-appropriate validation. Reviewers own independent analysis of
the complete change and its evidence.

### Author evidence before review

Before requesting review, the author must:

- stage every intended file, leave no unstaged or untracked task file, and make
  the complete staged change available;
- run every check required by the merged design and current validation plan;
- record each exact command and its pass/fail result;
- identify required checks that were not run and the explicit reason;
- ensure evidence was produced from the current diff, not an earlier revision;
- run staged, worktree, and branch-range diff checks where applicable;
- provide the Red evidence for an implementation PR;
- provide design-readiness evidence for a design PR.

A review request without complete required evidence is not ready. The reviewer
returns it to the author as blocked rather than running the missing suite.

Each evidence record is bound to:

- the current `HEAD` commit;
- the staged tree ID produced by `git write-tree`;
- the exact command;
- status: `PASS`, `FAIL`, `NOT RUN`, or `N/A`;
- the relevant output summary, machine-readable record, or retained artifact.

`git diff --quiet` must prove there is no unstaged tracked change, and
untracked-file discovery must return no task file. `git diff --check` remains a
separate whitespace gate; it does not establish cleanliness. With those
preconditions, validation executes the same content represented by the staged
tree ID, which is the reviewed change identity.

Any correction creates a new staged tree ID. Documentation checks and all
three diff checks are rerun. A prior test result may carry forward only when
the author records both tree IDs and shows that the changed paths do not
intersect that command's declared validation ownership. Uncertain or shared
ownership invalidates the result and requires a rerun.

### Reviewer behavior

The independent reviewer:

- remains read-only;
- reads the complete diff, canonical context, acceptance criteria, and author
  evidence;
- checks whether the selected commands measure the required behavior;
- checks that output and machine-readable records support the claimed result;
- does not rerun native suites, persistent models, QEMU workflows, builds, or
  documentation gates merely to confirm the author's report;
- does not treat not rerunning a reported command as reduced independence.

The reviewer may run one minimal targeted reproduction only when:

1. a concrete suspected defect has been identified;
2. static evidence is insufficient to establish it confidently;
3. the command is narrowly scoped to that hypothesis;
4. the reviewer records the hypothesis, command, and observed result.

A targeted reproduction must not expand into a baseline or full validation
matrix. If broad validation is required, the reviewer requests it from the
author.

Read-only inspection commands such as diff, symbol, generated-plan, or static
metadata queries are not validation reruns, but they must remain proportionate
to the finding being investigated.

### Findings and corrections

A finding reports whether the author evidence is:

- missing;
- stale relative to the current diff;
- insufficient for an acceptance criterion;
- contradicted by the code or recorded output;
- technically valid but unrelated to the changed scope.

After a justified correction, the author reruns the smallest checks affected
by the fix and any required phase gate invalidated by it. The author updates
the evidence record and staged tree ID. Carried-forward results identify their
original tree ID and invalidation rationale.

Re-review reads the corrected diff and updated evidence. It does not rerun the
author's corrected suite unless another concrete suspected defect requires a
minimal reproduction.

### Evidence retention

The pull-request description is the durable evidence record. It distinguishes:

- author-run validation;
- reviewer-run targeted reproduction, when any;
- checks not run and reasons;
- final independent review result.

Review agents receive the author evidence in their prompt so they do not need
to rediscover or rerun it.

### Failure handling

If a required author command fails or is not run, the task remains in
authoring or is marked blocked. Recording the failure explains why review has
not begun; it never makes the change review-ready.

If evidence cannot be reproduced because a required tool or target is
unavailable, the author reports the blocker. The reviewer does not create a
success-shaped substitute.

## Non-goals

This decision does not:

- reduce required validation coverage;
- prohibit a minimal reproduction of a concrete finding;
- let authors self-review implementation correctness;
- turn recorded output into proof beyond what the command measures;
- add CI or remote check requirements;
- change security-review procedures.

## Acceptance criteria

The workflow implementation must:

- make author evidence mandatory before review;
- make missing evidence a review-readiness blocker;
- prohibit routine reviewer suite/build/QEMU reruns;
- define and record the targeted-reproduction exception;
- make PR records distinguish author validation from reviewer reproduction;
- require author reruns after fixes;
- bind evidence to `HEAD` and a clean staged tree;
- verify worktree cleanliness independently from whitespace checks;
- distinguish `PASS`, `FAIL`, `NOT RUN`, and `N/A`;
- prohibit review while a required check is failed or not run;
- define narrow ownership-based carry-forward after corrections;
- require re-review of the corrected diff and evidence;
- update agent, contributor, AI workflow, and PR-template contracts
  consistently.

## Consequences

- Small PR reviews spend time on reasoning rather than duplicate execution.
- Validation remains complete because it is an author prerequisite.
- Review findings gain clearer evidence provenance.
- Reviewers can still prove concrete defects with a narrow reproduction.
- Missing or stale validation blocks review earlier and explicitly.

## Alternatives considered

### Reviewer always reruns the complete matrix

Rejected because it duplicates author work and scales review time with suite
cost rather than change complexity.

### Reviewer never runs any command

Rejected because a minimal reproduction can be the strongest evidence for a
specific suspected defect.

### Rely only on CI

CI can execute gates but cannot replace local Red evidence, phase-specific
validation, or independent design and correctness analysis.

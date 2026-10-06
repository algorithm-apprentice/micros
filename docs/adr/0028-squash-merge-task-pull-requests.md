# ADR-0028: Squash-Merge Task Pull Requests

- Status: Accepted
- Date: 2026-10-07
- Supersedes in part: ADR-0013 merge-history preservation

## Context

Small green local commits help development, independent review, and bisection.
However, merging every design revision and correction into remote `main`
produces a noisy delivery history. The project now requires one coherent
result commit per reviewed task pull request.

## Decision

- Continue documentation-first, test-first development with small green local
  commits.
- Preserve those commits on the pull-request branch during review.
- Squash-merge each accepted task pull request into `main`.
- Give the squash commit an English title describing the task outcome.
- Keep exact Red/Green evidence, validation commands, and independent-review
  results in the pull-request description.
- Keep the required contributor trailer in the resulting commit.
- Do not rewrite existing `main` history.

ADR-0013's requirement to merge without squashing is superseded. Its task
readiness, green-commit, test-first, and independent-review requirements remain
unchanged.

## Consequences

`main` records delivered task outcomes rather than intermediate corrections.
Fine-grained development history remains available on the pull request, while
main-branch bisection operates at the task boundary.

## Specification basis

- [ADR-0013: AI-Native Test-First Development](0013-ai-native-test-first-development.md)
- [AI-native development workflow](../development/ai-native-workflow.md)

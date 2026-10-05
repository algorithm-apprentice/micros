# Contributing to micros

`micros` uses the same engineering contract for human contributors and AI
agents.

## Before changing the repository

Read, in order:

1. [Agent contract](AGENTS.md)
2. [System overview](docs/architecture/system-overview.md)
3. [Development dependency DAG](docs/architecture/development-dag.md)
4. [Roadmap](docs/roadmap.md)
5. [ADR index](docs/adr/README.md) and every ADR relevant to the change
6. [Testing strategy](docs/testing-strategy.md)
7. [AI-native workflow](docs/development/ai-native-workflow.md)

Accepted ADRs are the architectural source of truth. Do not silently edit an
Accepted decision to change its meaning. Add a new ADR that supersedes it.

## Development rules

- Work on one dependency-ready task at a time.
- Keep the pull request limited to one coherent outcome.
- Split that outcome into small, coherent commits. Each permanent commit should
  be explainable in one sentence and pass its applicable build/tests.
- Keep history bisectable; do not commit a knowingly broken build or unfinished
  cross-commit invariant to the shared branch.
- Update design documentation before implementation when behavior, authority,
  ownership, ABI, or dependencies change.
- Use test-first development:
  - native unit/property tests for portable logic;
  - QEMU component tests for privileged or hardware behavior;
  - native protocol/transition tests while a downstream service is not yet
    dependency-ready, followed by QEMU integration when all peers exist;
  - a regression test before fixing a bug.
- Implement only enough behavior to satisfy the current acceptance criteria.
- Refactor only while the relevant tests remain green.
- Keep all source, identifiers, comments, diagnostics, tests, documentation,
  commit messages, and pull requests in English.
- Do not copy MINIX or NetBSD source into the repository without an explicit
  dependency and license decision.

## Pull requests

Use the repository pull request template. A pull request must identify:

- task and dependency position;
- goal, scope, and non-goals;
- relevant ADRs;
- test-first evidence;
- exact verification commands;
- independent review outcome;
- effects on documented extension boundaries.

After an independent review, apply only findings that are technically
justified. Preserve meaningful green commits when merging; do not squash a
reviewable sequence into one opaque change. Merge when no substantive issue
remains and all required checks pass. Then begin the next dependency-ready
task.

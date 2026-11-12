# Contributing to micros

`micros` uses the same engineering contract for human contributors and AI
agents.

## Before changing the repository

Start with [AGENTS.md](AGENTS.md) and follow its required context order.
The [documentation map](docs/README.md#reading-map-and-ownership) identifies
the current status, architecture, dependency, testing and workflow owners.

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

## Current validation commands

Use the [build guide](docs/development/building.md) for the supported tool
environment and runnable commands. Generate the current change's plan with:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier fast
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier pr
```

Add `--execute --fresh` for ordinary execution. The planner owns coverage:
fast is iteration only, implementation PRs include complete native validation,
and shared/unknown changes escalate. Full/milestone selection keeps all
required current target, image and stack evidence.
Explicit paths add to discovery rather than replacing it.

Pure documentation corrections need one documentation PR and the
[documentation-only gate](docs/development/building.md#documentation-only-checks),
not a full native/QEMU run. No new behavior or decision may be hidden in that
classification. [ADR-0123](docs/adr/0123-current-contract-validation.md)
retires historical source catalogs and repeated author ceremonies, not real
behavioral, hardware, cross-service or stack checks.

Before review, stage all intended files, prove the tracked worktree is clean,
require empty untracked task-file output, and bind exact command results to
`HEAD` and `git write-tree`. Whitespace checks do not prove cleanliness.
Use the current planner; do not copy an old task's gate list.

## Pull requests

Use the repository pull request template. A pull request must identify:

- design or implementation phase, or an explicit pure-documentation correction;
- task and dependency position;
- goal, scope, and non-goals;
- relevant ADRs;
- design-readiness evidence for a design PR;
- the merged design reference and test-first evidence for an implementation
  PR;
- exact verification commands;
- independent review outcome;
- effects on documented extension boundaries.

After an independent review, apply only findings that are technically
justified. A design PR is merged before its dependent implementation branch is
created. Keep meaningful green commits on an implementation branch for review,
then squash-merge the accepted task into one outcome commit on `main`. Merge
when no substantive issue remains and all phase-appropriate checks pass.

Authors run and record required validation before requesting review. Reviewers
do not repeat complete suites, builds, models, QEMU workflows, or documentation
gates. They may run one minimal targeted reproduction for a concrete suspected
defect when static evidence is insufficient, and record the reason and result.
Missing or stale author evidence blocks review and is returned to the author.

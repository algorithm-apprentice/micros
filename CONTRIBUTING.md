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

## Current validation commands

Generate the validation plan for the current change with:

```bash
python3 tools/validation_plan.py --tier fast
python3 tools/validation_plan.py --tier pr
```

Add `--execute` to run it. Use `--tier full` for `main`, milestone acceptance,
shared build/boot/harness changes, or any change that should bypass path
classification. The PR tier always includes complete `test-unit`; the fast
tier is for iteration only. Explicit `--path` values are added to repository
discovery rather than replacing it. Stage untracked files before using
`--execute`.

For documentation and repository-instruction changes, run:

```bash
python3 -m unittest discover -s tests/host -p 'test_*.py'
python3 tools/check_docs.py
git add <intended-files>
git diff --quiet
git diff --check
git diff --cached --check
git ls-files --others --exclude-standard
git write-tree
git diff --check origin/main...HEAD
```

`git diff --quiet` is the pre-review cleanliness gate and must exit zero.
Worktree and cached `--check` commands are separate whitespace gates. Untracked
output must be empty for task files, and `git write-tree` identifies the
reviewed staged change. The `origin/main...HEAD` check is the post-commit
pull-request range gate.

Implementation milestones add their own native and QEMU commands when those
test targets exist. The current boot-foundation gates are:

```bash
cmake --workflow --preset test-unit
cmake --workflow --preset test-qemu-smoke
cmake --workflow --preset test-qemu-panic
cmake --workflow --preset test-qemu-trap
cmake --workflow --preset test-qemu-timer
cmake --workflow --preset test-qemu-frame-allocator
cmake --workflow --preset test-qemu-trap-panic
cmake --workflow --preset test-qemu-mmu
cmake --workflow --preset test-qemu-object-model
cmake --workflow --preset test-qemu-nested-trap
cmake --workflow --preset test-qemu-frame-ownership
cmake --workflow --preset test-qemu-user-address-space
cmake --workflow --preset test-qemu-user-execution
```

Each workflow configures and builds its own image before running the selected
gate. `build-riscv64-debug` remains available when only the normal target
artifact is needed.

## Pull requests

Use the repository pull request template. A pull request must identify:

- design or implementation phase;
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

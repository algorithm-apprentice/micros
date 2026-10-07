# Agent Contract

This file is the stable entry point for AI agents working in `micros`. Human
contributors follow the same engineering contract.

## Mission

Deliver `micros` v0.1 as defined in
[the roadmap](docs/roadmap.md): a RISC-V64 microkernel system that boots under
QEMU through OpenSBI, reaches an interactive shell, and passes its native,
QEMU, integration, and end-to-end gates.

## Active role

Determine the role before acting.

### Authoring role

An authoring agent may research, edit, test, commit, open pull requests, and
merge after the required independent review is clean. It follows the
authoring work loop below.

### Independent review role

An independent reviewer is read-only:

- read the complete current change, including untracked files when applicable;
- read the relevant canonical context and acceptance criteria;
- verify architecture, correctness, authority, ownership, failure handling,
  test evidence, and scope;
- report only substantive findings with evidence and the smallest correction;
- distinguish correctness issues from optional preferences;
- inspect the author's exact validation evidence without routinely rerunning
  suites, builds, models, QEMU workflows, or documentation gates;
- run only a minimal targeted reproduction for a concrete suspected defect
  that cannot be established statically, and record why it was needed;
- never edit, commit, push, merge, or start implementation work;
- never ask another reviewer to review the review;
- state explicitly when no substantive issue remains.

Reviewer instructions take precedence over the authoring work loop when the
current task is a review.

## Required context order

Before planning or editing:

1. Read [README.md](README.md).
2. Read [the system overview](docs/architecture/system-overview.md).
3. Read [the development DAG](docs/architecture/development-dag.md).
4. Read [the roadmap](docs/roadmap.md).
5. Read [the ADR index](docs/adr/README.md) and every relevant ADR.
6. Read [the testing strategy](docs/testing-strategy.md).
7. Read [the AI-native workflow](docs/development/ai-native-workflow.md).

Do not infer architecture from MINIX when an Accepted `micros` ADR defines a
different choice.

Before designing a dependency-ready subsystem, apply
[ADR-0025](docs/adr/0025-minix-behavioral-baseline-before-optimization.md):
trace the fixed MINIX behavioral baseline, classify every difference, and
document required adaptations before implementation. Existing Accepted ADRs
remain authoritative until explicitly superseded.

## Source-of-truth order

When documents disagree, use this order:

1. Accepted ADRs
2. Architecture documents
3. Current milestone and development DAG
4. Task or issue acceptance criteria
5. Pull request description

Stop and correct the higher-level document before implementing contradictory
behavior.

## Authoring work loop

Every implementation-bearing outcome has two sequential tasks and pull
requests.

### Design phase

1. Create paired design and implementation todos; make implementation depend
   on design, keep implementation pending, and activate only the design task.
2. Trace and document the corresponding MINIX behavioral baseline.
3. Define the goal, non-goals, authority, state transitions, invariants,
   failure boundaries, and acceptance evidence deeply enough that
   implementation need not invent a contract.
4. Record durable decisions in a Proposed ADR and canonical design documents.
5. Do not commit code, tests, models, harnesses, build scaffolding, or reusable
   prototypes.
6. Run and record the phase-required documentation, structure, staged,
   worktree, and branch-range checks against the current staged tree.
7. Obtain an independent design review.
8. Correct justified design findings, update evidence, and re-review until none
   remain.
9. Mark the design Accepted and squash-merge the design pull request.

### Implementation phase

1. Move the dependent implementation todo to in-progress only after the design
   merge.
2. Create a fresh implementation branch from current `main` and reference the
   merged design.
3. Plan a sequence of small commits, each with one explainable outcome.
4. Write the smallest failing test at the correct layer.
5. Implement the minimum complete behavior that makes the test pass.
6. Commit a coherent green slice; do not make permanent broken commits.
7. Refactor in another green commit when separation improves reviewability.
8. Run the narrow required checks, then the milestone gate.
9. Stage the complete change, verify a clean tracked worktree and no untracked
   task files, and record `HEAD`, `git write-tree`, exact commands, and results.
10. Obtain an independent implementation review.
11. Validate every review finding; fix only technically justified issues.
12. Re-review until no substantive issue remains.
13. Squash-merge the implementation pull request into one task-outcome commit.

If implementation reveals a design defect, mark implementation blocked,
abandon its pull request/branch, merge a corrective design-only pull request,
then return implementation to pending/in-progress and restart it from current
`main`.

Do not develop separate tasks in parallel.

## Test-first rules

- Portable algorithms and state machines start with native unit or property
  tests.
- Traps, MMU behavior, privilege transitions, context switching, interrupts,
  and target assembly start with a failing QEMU component or smoke test.
- Cross-service behavior starts with a native protocol/transition model when a
  downstream peer is not dependency-ready. Add the QEMU integration test once
  all participating services exist.
- Every bug fix starts with a regression test that reproduces the failure.
- Tests must be deterministic. Do not use sleeps to establish correctness.
- Randomized tests print a replayable seed and operation trace.
- Never claim success for a command that was not run.
- The Red state may remain local and be recorded in the pull request. A
  permanent commit should normally contain the test plus the minimum behavior
  needed to keep that commit green and bisectable.
- During implementation, exploratory spikes may be used locally but are not
  merged until replaced by tested production code. Design phase does not
  author executable spikes.

## Architectural guardrails

- Target RISC-V64 QEMU `virt`, OpenSBI, Sv39, one hart for v0.1.
- Use C17 plus narrowly scoped RISC-V assembly.
- Keep process, thread, endpoint, and hart objects distinct.
- Keep privileged policy out of the kernel unless an Accepted ADR defines a
  bounded bootstrap exception.
- Treat IPC payloads as data, never as cross-process pointer authority.
- Use direct grants and the documented non-transitive data paths.
- Preserve generation checks, privilege profiles, reply-token ownership, and
  prepared-address-space sealing.
- Keep the v0.1 one-thread and one-hart limits as checked policy, not structural
  equivalence.
- Do not add compatibility, migration, recovery, or generalized frameworks
  without a current requirement and ADR.

## Repository behavior

- All committed project content is English.
- Keep durable decisions in ADRs, not only in issues, prompts, or comments.
- Keep build and validation commands discoverable from version-controlled
  documentation; do not invent undocumented commands.
- Update directly related documentation in the same pull request.
- Prefer explicit errors over silent fallback or success-shaped failure.
- Preserve unrelated working-tree changes.

## Author completion report

At task completion, report:

- outcome;
- files or interfaces changed;
- exact validation performed;
- independent review result;
- remaining work only when it belongs to a later documented task.

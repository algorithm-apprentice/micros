# AI-Native Development Workflow

## Purpose

`micros` is developed AI-natively: repository state, architectural decisions,
task boundaries, verification, and review evidence are structured so a human
or AI agent can discover the same constraints and produce the same acceptance
evidence.

AI-native does not mean autonomous code generation without controls. It means
that correctness-relevant context is versioned, discoverable, executable where
possible, and reviewed independently.

## Principles

### Canonical context

Architecture is not reconstructed from chat history. Accepted ADRs and the
architecture documents are the durable source of truth. Agent instruction
files remain small and point to those sources rather than copying them.

### Bounded tasks

Every implementation task has one outcome and a known position in the
development DAG. A task is ready only when all predecessor tasks are merged.
Separate tasks are not developed in parallel.

### Small green commits

A pull request represents one task outcome, but it is implemented as a sequence
of small commits. Each commit:

- has one purpose that can be explained in one sentence;
- preserves applicable build and test gates;
- leaves ownership and invariants internally consistent;
- is useful for review, rollback, and `git bisect`;
- avoids unrelated cleanup.

The TDD Red state is demonstrated locally and recorded in the pull request. It
does not require preserving a permanently broken commit. When the test alone
would make history red, commit the test together with the smallest Green
implementation. Separate later refactoring into another green commit.

Meaningful green commits remain visible on the pull-request branch during
review. The accepted pull request is squash-merged into one task-outcome
commit on `main`, as defined by ADR-0028.

### Executable feedback

Acceptance criteria are represented by tests or deterministic build checks.
Prose explains why behavior is required; executable feedback demonstrates that
the repository currently provides it.

### Independent review

The authoring persona does not serve as the only reviewer. A separate review
persona reads the complete diff, reports only substantive findings, and is
re-run after justified corrections until no substantive issue remains.

### Durable handoff

The next contributor should be able to continue from the repository, issue,
and pull request without needing private prompt history.

## Context discovery

An agent begins with [AGENTS.md](../../AGENTS.md), then follows its required
context order. Path-specific files under `.github/instructions` add local
constraints when relevant.

The repository uses:

- `AGENTS.md` as the tool-neutral agent contract;
- `.github/copilot-instructions.md` as the Copilot repository adapter;
- `.github/instructions/*.instructions.md` for path-specific constraints;
- Accepted ADRs for durable decisions;
- the development DAG and roadmap for task readiness;
- pull requests for change evidence and review history.

Instruction files must not become a second architecture. If an instruction
needs extensive explanation, put the decision in an ADR or architecture
document and reference it.

## Agent roles

### Author

The author researches, updates design, writes tests, implements, validates,
commits, and prepares the pull request. It may merge only after an independent
review reports no substantive issue.

### Independent reviewer

The reviewer is a separate read-only persona. It reads the complete current
change and canonical context, validates acceptance evidence, and reports only
substantive findings. It does not edit, commit, push, merge, implement fixes,
start the next task, or request another review.

Repository and path-specific instructions apply to both roles, so their wording
states invariants rather than assuming the recipient is an author. The explicit
review role overrides authoring workflow steps during a review.

## Task contract

Before implementation, record:

- task ID and milestone;
- predecessor that makes the task ready;
- one measurable outcome;
- scope and explicit non-goals;
- relevant ADRs and invariants;
- expected files or interfaces;
- failing test or acceptance scenario;
- exact completion gate.

If these fields cannot be stated, the task is not ready to implement.

## Documentation-first and test-first sequence

Every implementation-bearing outcome uses two separately reviewed phases.

### Design phase

1. Create paired design and implementation todos, make implementation depend
   on design, leave implementation pending, and activate only design.
2. Research the relevant source and platform behavior.
3. Define the baseline, goal, non-goals, ownership, state, failure, and
   acceptance contracts in canonical documents and a Proposed ADR.
4. Keep production code, executable tests/models/harnesses, build scaffolding,
   dependencies, and reusable prototypes out of the design pull request.
5. Stage the complete design change, verify no unstaged/untracked task files
   remain, and record documentation, structure, and three-layer diff evidence
   against `HEAD` plus the staged tree ID.
6. Independently review the design until no substantive issue remains.
7. Update the staged evidence after corrections.
8. Mark the design Accepted and squash-merge it.

The design pull request records `Phase: Design`, its dependency-ready outcome,
and the evidence used to make the design implementation-ready.

### Implementation phase

1. Move the dependent implementation todo from pending to in-progress and
   create its fresh branch only after the design merge.
2. Reference the merged design pull request and commit.
3. Plan the small green commit sequence.
4. Write the smallest failing test at the correct layer.
5. Implement the minimum production behavior that makes it pass.
6. Commit that coherent green slice.
7. Refactor in a separate green commit when useful.
8. Run the narrow suite and then the milestone gate.
9. Stage the complete change, verify `git diff --quiet`, verify no untracked
   task files remain, and record `HEAD`, `git write-tree`, commands, and
   results.
10. Independently review the complete implementation.
11. Correct justified findings, update the staged evidence, and repeat review.
12. Squash-merge the implementation task into one `main` commit.

The implementation pull request records `Phase: Implementation`, its merged
design reference, Red-Green-Refactor evidence, exact author-run validation,
and independent review outcome.

If implementation exposes an incomplete accepted design, stop. Mark the
implementation blocked, close and abandon its pull request/branch, merge a
corrective design-only pull request, update the implementation design
reference, return the todo to pending/in-progress, then restart implementation
from current `main`. The corrective design task is the only active task while
the implementation is blocked.

## TDD by layer

| Change type | Red step | Green evidence |
| --- | --- | --- |
| Pure algorithm or state machine | Native unit/property test | Native test passes under normal and sanitizer builds |
| Parser or untrusted input | Native malformed-input/property test | Valid corpus passes and invalid corpus fails explicitly |
| Trap, MMU, context switch, IRQ, assembly | QEMU component test or expected-failure image | Machine-readable target result passes |
| IPC or grant rule | Native transition model plus negative cases | Model and target IPC tests pass |
| Cross-service protocol before every peer exists | Native protocol/transition model | Message, ownership, rollback, and error rules pass without pulling in a successor |
| Cross-service protocol after every peer exists | QEMU integration scenario | Expected messages, ownership, rollback, and errors observed end to end |
| Boot/toolchain behavior | Automated smoke command expecting a marker/exit | Clean build and deterministic QEMU result |
| Bug fix | Regression test reproducing the bug | Test fails without and passes with the fix |
| Documentation only | Link/structure checks when available | No implementation test is invented |

The Red step may require first creating a test harness. The harness itself must
have an observable failure mode and cannot report success merely because the
target did not respond.

## TDD boundaries

- Do not force hardware behavior into a misleading host mock when QEMU is the
  correct test layer.
- Do not test private implementation details when an invariant or public
  protocol can be tested.
- Do not use sleeps as synchronization or correctness evidence.
- Do not add production generality solely to make a unit test convenient.
- Exploratory executable code is allowed only during implementation as a
  disposable spike; design work may inspect code and run existing probes but
  does not author executable spikes.
- Documentation-only changes do not require fictional runtime tests.

## Review protocol

The independent reviewer receives:

- complete task context and non-goals;
- the full branch diff, including untracked files when applicable;
- relevant ADRs and acceptance criteria;
- commands already run;
- a request to separate correctness findings from preferences.

The reviewer remains read-only and does not commit, merge, implement fixes, or
request another reviewer.

Before review starts, the author supplies exact current commands and results
for every phase-required check. Missing or stale evidence blocks review; the
reviewer does not run the missing suite on the author's behalf.

The reviewer does not routinely repeat native suites, persistent models,
builds, QEMU workflows, or documentation gates. A minimal targeted
reproduction is allowed only for a concrete suspected defect that static
evidence cannot establish. The review records the hypothesis, command, and
result.

For every finding, the authoring persona:

1. verifies the evidence;
2. accepts or rejects the finding with technical reasoning;
3. applies the smallest justified correction;
4. runs affected checks and updates the author evidence;
5. asks the reviewer to re-read the current files.

Re-review inspects the corrected diff and updated evidence without repeating
the author's suite. Review ends only when no substantive issue remains.

## Pull request and merge

The pull request template is the durable phase record. Every PR identifies
`Phase: Design` or `Phase: Implementation`.

A design PR records baseline evidence, completed contracts, ADR status,
non-goals, acceptance evidence, and independent design review. It does not
invent Red/Green evidence or contain executable implementation artifacts.

An implementation PR references the merged design and records commit planning,
Red/Green/Refactor evidence, exact author-run validation, and independent
implementation review. "Tests pass" without commands is insufficient.

After required checks and independent review are clean, the pull request may be
merged without waiting for an additional manual approval. Squash-merge each
phase into one outcome commit. The implementation branch is created only after
the design outcome is on `main`.

## Preventing stale AI context

- Do not store changing task status in agent instruction files.
- Do not copy full ADR text into prompts or instruction files.
- Do not leave acceptance criteria only in chat.
- Do not preserve generated plans after the repository has a newer canonical
  plan.
- Update references in the same pull request as a moved or renamed document.
- Prefer one stable verification command per intent over agent-specific command
  sequences.

## Evolution

As implementation begins, the repository will add deterministic build presets,
test targets, CI checks, and machine-readable test output. Those executable
interfaces become the preferred agent tools, but they remain governed by the
same Accepted ADRs and task contract.

The initial reproducible documentation gate is:

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

After staging, `git diff --quiet` must prove the tracked worktree is clean.
Then run the worktree and cached whitespace checks before review. Untracked task
output must be empty, and the tree ID binds the evidence to the staged change.
Run the range check after committing to validate the complete pull-request
diff.

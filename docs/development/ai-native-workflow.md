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

Meaningful commit history is preserved at merge. Do not turn a reviewable
sequence into one large squash commit.

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

The required order is:

1. Research the relevant source and platform behavior.
2. Update design and Proposed ADRs when authority, ownership, ABI,
   dependencies, or observable behavior changes. To change an Accepted
   decision, add a new ADR that explicitly supersedes it.
3. Review the design until no substantive issue remains.
4. Plan the small green commit sequence.
5. Write the smallest failing test at the correct layer.
6. Implement the minimum production behavior that makes it pass.
7. Commit that coherent green slice.
8. Refactor in a separate green commit when useful.
9. Run the narrow suite and then the milestone gate.
10. Independently review the complete change.
11. Correct justified findings and repeat review.
12. Merge while preserving meaningful commits and select the next
    dependency-ready task.

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
- Exploratory code is allowed only as a disposable spike; it is not merged.
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

For every finding, the authoring persona:

1. verifies the evidence;
2. accepts or rejects the finding with technical reasoning;
3. applies the smallest justified correction;
4. runs affected checks;
5. asks the reviewer to re-read the current files.

Review ends only when no substantive issue remains.

## Pull request and merge

The pull request template is the durable execution record. Exact commands and
results are included; "tests pass" without commands is insufficient.

After required checks and independent review are clean, the pull request may be
merged without waiting for an additional manual approval. Use a merge strategy
that preserves meaningful green commits. Development then moves to the next
dependency-ready task.

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

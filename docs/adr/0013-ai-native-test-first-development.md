# ADR-0013: AI-Native Test-First Development

- Status: Accepted
- Date: 2026-10-05

## Context

The project will be developed by humans and AI agents over many sequential
pull requests. Chat history and model-specific prompts are not durable enough
to carry architecture, task readiness, verification, and review requirements.

The kernel also requires different test layers: portable logic can use native
unit tests, while traps, MMU behavior, interrupts, and assembly require target
execution. A rigid "unit test before every line" rule would create misleading
mocks, but implementation without a failing acceptance signal would make
agent-generated changes difficult to trust.

## Decision

The repository is AI-native and test-first.

### Durable context

- `AGENTS.md` is the tool-neutral agent entry point.
- `.github/copilot-instructions.md` is a self-contained Copilot adapter that
  directs each Copilot surface to the repository context; it does not depend
  on CLI-only file-inclusion behavior.
- `.github/instructions/*.instructions.md` contains narrow path-specific rules.
- Accepted ADRs remain the source of truth for durable architecture decisions.
- Instruction files reference canonical documents instead of duplicating
  architecture or changing task state.

### Task execution

- Work proceeds one dependency-ready task and pull request at a time.
- Every task states one outcome, non-goals, predecessor, relevant ADRs,
  invariants, failing test/acceptance scenario, and completion gate.
- Architecture or protocol changes are documented and reviewed before
  implementation.
- Proposed ADRs may be revised. An Accepted decision changes only through a
  new ADR that explicitly supersedes it.

### Small commits

- Each task is split into small commits with one explainable purpose.
- Every permanent commit keeps its applicable build and tests green and leaves
  invariants internally consistent.
- The TDD Red state is observed locally and recorded in the pull request; when
  a test-only commit would break history, the test and minimum Green
  implementation are committed together.
- Refactoring is separated when it forms an independently useful green step.
- Meaningful commit history is preserved during merge rather than squashed into
  one opaque change.

### Test-first development

- Portable algorithms and state machines begin with a failing native unit or
  property test.
- Privileged, assembly, MMU, context-switch, and interrupt behavior begins
  with a failing QEMU component or acceptance test.
- Cross-service behavior begins with a failing native protocol/transition model
  while any downstream peer is not dependency-ready, then gains a QEMU
  integration scenario once all participating services exist.
- Every bug fix begins with a reproducing regression test.
- The implementation is the minimum change that makes the test pass, followed
  by refactoring while tests remain green.
- Documentation-only changes do not require fictional runtime tests.
- Exploratory spikes may be used locally but are not merged.

### Review and merge

- A persona independent from the author reviews the complete change in a
  read-only role.
- The reviewer never edits, commits, pushes, merges, implements fixes, starts
  another task, or requests another review.
- Findings are applied only after technical validation.
- Review repeats after corrections until no substantive issue remains.
- A clean independently reviewed pull request may be merged without waiting
  for an additional manual approval.
- The next task starts only after the current pull request is merged.

The pull request template records task position, design impact, Red-Green-
Refactor evidence, exact commands, and independent review outcome.

## Consequences

- A new agent can recover project rules from the repository rather than private
  conversation history.
- Humans and AI agents use the same acceptance gates.
- Test harnesses and deterministic commands become first-class product
  infrastructure.
- Small green commits make reviews, rollback, and regression bisection more
  reliable.
- Repository instructions require review because a mistake can influence many
  future changes.
- Some low-level Red steps use QEMU and are slower than native tests, but they
  measure the real privilege and hardware behavior.
- The workflow adds structure to every pull request but reduces rework and
  unsupported success claims.

## Alternatives considered

### Prompt-only agent guidance

Prompts are session-scoped, difficult to audit, and easily diverge from the
repository's current architecture.

### Code-first development followed by tests

This allows interfaces and implementations to become difficult to test before
their acceptance criteria are executable.

### Unit-test-only TDD

Host unit tests cannot validate target privilege levels, page-table activation,
interrupt delivery, context switching, or RISC-V assembly behavior.

### Different workflows for humans and agents

Separate standards produce inconsistent evidence and make reviews depend on who
authored the change rather than what the repository demonstrates.

# ADR-0064: Fail-Closed Validation Evidence Reuse

- Status: Accepted
- Date: 2026-10-15
- Refines: ADR-0031, ADR-0032, ADR-0034
- Supersedes in part:
  - ADR-0032's unconditional full-matrix treatment of `CMakeLists.txt`, only
    for the bounded existing-workflow command edits specified below;
  - ADR-0034's requirement to execute a required command again when an exact
    current-candidate, current-context successful execution receipt is
    available. All evidence identity, freshness, review and failure rules
    otherwise remain in force.

## Context

The node-P implementation is merged as #128. The current complete plan has
47 commands: complete native validation, 38 QEMU workflows, four image
workflows, documentation and three diff checks. That candidate passed
35 CTests and 566 Python tests before independent review.

The current planner already eliminates duplicate command tuples within one
plan. It does not record or reuse successful executions between invocations.
Every execution starts each selected command again. `CMakeLists.txt` is also
an unconditional shared input, even when the only changed command belongs to
one existing, explicitly owned workflow.

Existing read-only probes at merged commit
`ee6be77c1d786eea0cae96b175a9923eba3e0b55` produce 47 commands for explicit
`CMakeLists.txt` ownership and seven for the already owned dynamic child
source. This is selection evidence, not a claim that every CMake edit can
safely use the smaller plan.

The corresponding fixed MINIX test-driver baseline and required development
adaptations are recorded in
[the validation-driver study](../research/minix-validation-driver-baseline.md).
No MINIX source or execution-reuse mechanism is imported.

## Decision

Preserve the planner as the sole authority for required coverage. Add an
opt-in, local, exact-candidate execution ledger and a deliberately narrow
content-aware exception for existing workflow commands in `CMakeLists.txt`.

The complete contract is
[Validation Evidence and Owned Build Changes](../architecture/validation-evidence.md).
It defines receipt identity, artifact and execution-context closure,
publication, invalidation, command provenance, Git-layer classification,
bounded CMake syntax, failure handling, interfaces and acceptance cases.
Its initial reusable profile names complete native, smoke and the standalone
TTY image on Darwin arm64/Ninja, with bounded tool/runtime/firmware resolution.
Implementation may not invent a weaker receipt or classifier.

### Coverage and reuse are different decisions

The planner first computes the complete required command sequence using all
committed, staged, unstaged, untracked and explicit paths. Reuse cannot remove
a required command from that sequence or infer that one gate covers another.

An eligible successful receipt may satisfy only the exact same command on
the exact same repository, HEAD, staged tree and declared execution context.
It remains a prior execution, displayed as `REUSED`, with its producer,
source identity and log digest. It is never reported as a new execution.
Missing, stale, unsupported or invalid evidence runs the required command
fresh and explains the miss.

Documentation and all three diff checks always execute fresh. Explicit
`--tier full` remains a fresh complete run. Existing invocations without
reuse retain their current fresh execution behavior.

### Fail closed on execution context

HEAD equality alone is insufficient. A changed index, dirty tracked input,
untracked task file, different command recipe, toolchain, inherited
environment, generated-input state, artifact or validation implementation
invalidates reuse.
Legacy fresh iteration still accepts unstaged tracked edits; such executions
cannot publish reusable or final author evidence.

The ledger never stores environment values, credentials or arbitrary command
lines supplied by a caller. Only fixed canonical workflows are executable
through the recording interface. Unmodeled tools or inputs are not eligible.
Context fingerprinting and output verification are part of the contract,
not optional performance shortcuts.

An attempted fresh rerun invalidates an older success before launch. Failure,
interruption, source mutation or incomplete log/artifact publication cannot
leave an older receipt available as current success.

### Narrow CMake classification

Only a recognized timeout-token edit to an existing, reviewed
`add_custom_target` workflow may receive narrower ownership. The target
identity, enclosing guard, command grammar and all outside bytes must remain
unchanged. Its owners come from reviewed metadata, never inferred from source
membership or arbitrary target spelling. The v1 table names smoke, panic and
the shared scheduler-invalid target; the latter owns both invalid-context
workflows. Every other body byte, marker, variable and expression is immutable.

Added or removed targets, changed factories, source sets, compiler/linker
options, include chains, guards, presets, shared commands, unsupported syntax
or ambiguous ownership retain full-matrix validation.
An explicit path without a verified before/after change also stays full.
Every Git discovery layer contributes independently; no later revert or
explicit path may hide an earlier shared or unknown change.

### Authority and failure boundaries

Receipts are local author evidence, not a remote cache or reviewer approval.
They confer no execution, merge or input-selection authority. The final
report identifies every required command as freshly passed, validly reused,
failed or not run; any unsatisfied requirement blocks review.

One OS-backed repository-local ledger lock prevents competing publishers.
Records and complete logs publish atomically only after current-candidate and
artifact checks. Parse failures, digest disagreement and unsupported records
produce explicit diagnostics, never success-shaped fallback.

## Non-goals

This decision does not:

- remove, combine or weaken any native, model, QEMU or image gate;
- reduce model transitions or replace target evidence with host mocks;
- reuse across HEADs, staged trees, repositories, worktrees or machines;
- automate ownership-disjoint cross-tree carry-forward from ADR-0034;
- add a general CMake interpreter, build dependency inference or framework;
- cache arbitrary shell commands or trust hand-authored success receipts;
- add CI, compiler caching, shared build directories or service changes;
- deduplicate independent expected-value oracles into production metadata.

## Acceptance

The separately merged implementation must retain all existing planner
regressions and add the complete receipt/classifier cases in the canonical
contract. Fresh and reuse-enabled runs must have identical required coverage.
Only actual fresh subprocess executions may produce new successful receipts.

The author validates the current staged candidate, records exact commands and
results, runs a fresh full milestone plan, then obtains independent read-only
implementation review. Reviewers inspect the evidence and do not routinely
rerun the matrix.

## Status and phase barrier

This is Accepted design only, after clean independent review.
`validation-plan-dedup-implementation` may start only after this design merges
and must pass its separate implementation review. No executable implementation,
tests, models, harnesses, build scaffolding or prototype belongs to this
design pull request. Receipt reuse and narrower classification are not yet
implemented.

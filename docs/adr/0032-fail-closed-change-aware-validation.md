# ADR-0032: Fail-Closed Change-Aware Validation

- Status: Accepted
- Date: 2026-10-07
- Refines: ADR-0031

## Context

ADR-0031 separates deterministic native tests from persistent models so the
normal host feedback loop does not pay the model cost after every edit. The
QEMU matrix still contains isolated images with intentionally repeated
OpenSBI, FDT, trap, MMU, ownership, and object-readiness evidence.

Running every QEMU image after every small change is safe but inefficient.
Selecting gates manually is also unsafe: target sources contain
`MICROS_BUILD_*` branches, shared headers feed several images, and a path that
looks local may compile only under another component preset.

The planner must therefore optimize only when it can prove ownership. Unknown
or mixed ownership must cost more validation, never less.

## Decision

`micros` will provide one repository-owned changed-path validation planner.
It produces commands but does not replace the underlying CMake workflows.

### Repository and path authority

The planner derives its repository root from its own checked-in location, not
from the caller's current Git repository.

Path discovery includes:

- committed changes in `base...HEAD`, where `base` defaults to `origin/main`;
- staged changes;
- unstaged tracked changes;
- untracked files.

Rename collapsing is disabled so both old and new ownership paths are
classified.

Explicit relative paths are resolved against the caller's working directory
and then converted to paths relative to the planner's repository root.
Absolute or relative paths outside that root are rejected.

Explicit paths are always added to the complete Git-discovered path set. They
never replace or restrict repository discovery. A diagnostic path-only mode,
if added later, must be labeled non-authoritative, cannot execute commands, and
cannot report final validation success.

### Validation tiers

The planner exposes three tiers.

#### Fast

The fast tier runs:

- `test-unit-fast` for any code change;
- documentation checks for documentation changes;
- every directly affected QEMU component gate;
- index, worktree, and branch-range diff checks.

Every path in a slow-model ownership set selects that model workflow in the
fast tier, including production sources, headers, entry points, CMake
definitions, presets, toolchain inputs, and runners. An unknown
non-documentation path escalates to the full tier.

#### Pull request

The pull-request tier runs:

- complete `test-unit`;
- QEMU smoke;
- every affected QEMU component gate;
- documentation checks when documentation changed;
- index, worktree, and branch-range diff checks.

The pull-request tier is the minimum author validation before independent
review and merge for a code change. It preserves ADR-0031's complete native
gate requirement. Selective slow-model ownership still controls the fast tier
and provides mapping diagnostics, but never substitutes for `test-unit` before
merge.

#### Full

The full tier runs:

- complete `test-unit`;
- every documented QEMU workflow;
- documentation checks;
- index, worktree, and branch-range diff checks.

The full tier is always valid and is required for milestone acceptance, shared
build/boot/harness changes, unknown ownership, or when explicitly requested by
a task or merge policy.

Documentation-only fast and pull-request plans run documentation and diff
checks but no native or QEMU workflow.

### Authoritative gate inventories

QEMU gate identity is defined by the distinct `workflowPresets` in
`CMakePresets.json` whose names begin with `test-qemu-`. Distinct configure
presets remain distinct gates even when they invoke the same CMake target.

The slow-model inventory is the explicit set of model workflow presets defined
by ADR-0031, currently `test-ipc-model`.

The planner's QEMU ownership keys, QEMU full-tier commands, and documented
QEMU matrix must have exact parity with the QEMU inventory.

The selective slow-model command map and documented slow-model matrix must
have exact parity with the slow-model inventory. The full tier does not emit
those workflows separately because `test-unit` is the aggregate native gate.
Instead, a separate parity regression must prove that every slow model is a
CTest included transitively by `test-unit` and excluded by `test-unit-fast`.
Inventory parity verifies gate identity; it does not infer behavioral
ownership from CMake.

### Fail-closed classification

Classification uses reviewed many-to-many ownership sets:

- each QEMU gate lists its production sources, public headers, and target-test
  sources;
- each slow-model gate lists every linked production source, header, model
  source, entry point, CMake target/label definition, configure/build/workflow
  preset, toolchain input, and runner input;
- shared toolchain, linker, target-entry, post-link, and QEMU-harness inputs
  select the complete QEMU matrix;
- public kernel headers and shared runtime/ownership inputs select the complete
  QEMU matrix;
- sources containing branches for several `MICROS_BUILD_*` images select the
  complete QEMU matrix unless every branch owner is enumerated explicitly.

Classification is performed per changed path. If any non-documentation path is
unknown to both component and slow-model ownership, the full tier is selected,
including complete `test-unit` and the complete QEMU matrix. A mapped path must
never suppress an unmapped path's fallback.

The fast tier runs all selected component gates. It does not arbitrarily choose
the first matching gate.

### Diff authority

Every generated plan ends with three independent whitespace checks:

1. staged index;
2. unstaged worktree;
3. pull-request range from the selected base to `HEAD`.

Untracked files are included in path classification. Execute mode must refuse
to begin final validation while any discovered untracked path remains. Those
files must be staged before index, worktree, and branch-range checks can form
complete merge evidence.

### Maintenance and regression requirements

The planner is test infrastructure. Its native regression table must cover:

- one target-only representative for every QEMU workflow;
- exact parity among QEMU workflow presets, QEMU ownership keys, full-tier
  commands, and the documented matrix;
- exact parity among slow-model workflow presets, selective slow-model command
  keys, and the documented model matrix;
- transitive inclusion of every slow CTest in `test-unit` and exclusion from
  `test-unit-fast`;
- every shared full-matrix input category;
- every known cross-gate conditional dependency;
- every slow-model entry point and shared dependency category;
- documentation-only fast and pull-request plans;
- mixed mapped and unmapped target paths;
- explicit paths unioned with separate dirty or committed paths;
- committed, staged, unstaged, untracked, deleted, and renamed paths;
- invocation from the repository root and a subdirectory;
- `./` relative paths, absolute in-repository paths, and rejected external
  paths;
- index, worktree, and custom-base branch-range diff commands;
- duplicate-command elimination;
- execute-mode rejection while untracked files remain.

Adding or renaming a gate, model, target-only source, shared header, toolchain
file, linker input, or conditional build branch requires updating the planner
mapping and its regression table in the same PR.

### Execution

The planner prints shell-escaped commands by default. An explicit execution
option first verifies that no discovered untracked path remains, then runs each
command sequentially from the planner's repository root and stops at the first
failure.

## Non-goals

This decision does not:

- remove or merge existing QEMU component gates;
- remove repeated production-readiness evidence;
- infer behavioral ownership from CMake source membership alone;
- add CI configuration;
- add compiler caching or shared build directories;
- change test semantics or accepted model transition counts.

Compiler caching and QEMU gate consolidation require separate measured
decisions.

## Acceptance criteria

Implementation may merge only when:

- every current QEMU gate and slow model has explicit tested ownership;
- authoritative QEMU inventories have exact parity with ownership maps, full
  commands, and documentation;
- authoritative slow-model inventories have exact parity with selective model
  commands and documentation, and are transitively included by `test-unit`;
- unknown and mixed-unknown non-documentation paths select the full tier;
- shared and conditional multi-image inputs select the full matrix;
- documentation-only plans contain no build command;
- explicit paths cannot hide other repository changes;
- execute mode rejects remaining untracked files;
- rename and caller-directory regressions pass;
- the emitted diff commands cover index, worktree, and branch range;
- the full tier is command-equivalent in coverage to the documented complete
  validation matrix;
- documentation explains when contributors use each tier.

## Consequences

- Small, well-owned changes avoid unrelated guests.
- Ambiguous changes remain safe by escalating to complete validation.
- Planner maintenance becomes part of adding a new gate or shared input.
- The explicit map is verbose, but reviewable and testable.
- Incorrect classification is treated as a validation bug.

## Alternatives considered

### Always run every QEMU image

This is safe but repeats unrelated guests after narrowly scoped changes.

### Fall back to QEMU smoke for unknown target paths

Rejected. Smoke cannot compile or execute target-specific conditional code.

### Choose only the first matching QEMU gate

Rejected. One path may affect several images, and ordering is not ownership.

### Infer dependencies automatically from CMake

CMake source membership proves compilation, not behavioral ownership or
conditional branch coverage. An explicit fail-closed map is easier to audit.

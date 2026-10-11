# ADR-0052: VFS Test Source Decomposition

- Status: Accepted
- Date: 2026-10-12
- Owners: micros maintainers
- Related:
  - [ADR-0025](0025-minix-behavioral-baseline-before-optimization.md)
  - [ADR-0031](0031-tiered-native-validation.md)
  - [ADR-0032](0032-fail-closed-change-aware-validation.md)
  - [ADR-0033](0033-reviewed-design-before-implementation.md)
  - [ADR-0034](0034-author-validation-before-review.md)
  - [ADR-0049](0049-vfs-service-and-application-io-protocol.md)
  - [ADR-0050](0050-post-handoff-elf-spawn.md)
  - [ADR-0051](0051-responsibility-based-source-modules.md)
  - [VFS test source organization](../architecture/vfs-test-source-organization.md)
  - [MINIX VFS test-organization baseline](../research/minix-vfs-test-organization.md)

## Context

ADR-0051 decomposed the portable VFS implementation and required a complete
repository source-module audit before further lifecycle work. That audit
identified eight VFS test sources as the next dependency-ready correction:

- five native host sources currently combine fixture, core behavior, spawn
  transactions, service codecs, and model logic;
- the target application probe combines client IPC, filesystem, directory,
  console, drain, and entry orchestration;
- the kernel test hook combines report matching, reporter authentication,
  terminal-state validation, readiness, UART drain, and shutdown; and
- the fixture generator combines ELF metadata validation, page-limit policy,
  image rendering, manifest rendering, and report-symbol output.

Together the files contain 12,788 physical lines. Twenty-three functions
exceed the 120-line review signal, including a 498-line console scenario, a
374-line backend protocol test, a 300-line model scheduler, and a 268-line
fixture renderer. The concern is mixed responsibility and hidden task
structure, not size alone.

Step 14 next adds VFS exit cleanup. Adding that evidence to the current files
would further mix process teardown, pending-console cancellation, PM-control
codec behavior, service dispatch, and model transitions.

The fixed local MINIX baseline uses a small shared `common.c`/`common.h`
harness and behavior-oriented programs for open/close, directory operations,
descriptor duplication, read/write, interrupted calls, getdents, and spawn.
That file ownership is useful baseline behavior. MINIX also contains long
scenario functions, so its function shape is not adopted.

The accepted `micros` testing architecture requires additional white-box,
byte-exact protocol, deterministic model, QEMU probe, kernel-hook, and
fixture-generation evidence. Those layers are required adaptations and must
be retained.

## Decision

Decompose the VFS test sources by fixture authority, behavior, state machine,
protocol layer, and evidence layer before adding exit-cleanup coverage.

The exact target files, ownership, private dependency direction, future
exit-cleanup homes, and disposition of every current over-limit function are
defined in
[VFS Test Source Organization](../architecture/vfs-test-source-organization.md).

### Stable execution identities

Source decomposition does not create new test executions. Retain:

- native targets `vfs_test`, `vfs_model_test`, and `vfs_service_test`;
- target image `micros_vfs_service_probe`;
- CTest identities `vfs-core`, `vfs-model`, and `vfs-service-core`;
- the fixed VFS model seed, 8,192 transitions, and 32-step cycle;
- the standalone `build-vfs-service-image` workflow;
- the isolated `test-qemu-vfs` workflow and all exact markers; and
- the existing pull-request validation policy.

The same assertions may move into responsibility-specific sources and named
semantic helpers. They may not be removed, weakened, duplicated into extra
jobs, reordered across the current short-circuit execution sequence, or
replaced by a different evidence layer. The initialization-to-mount state
handoff in `vfs_test` remains explicit.

### Shared fixture boundary

Retain `vfs_test_fixture.h` as the test-only white-box contract. Split its
implementation into facade initialization, grant behavior, RAMFS behavior,
and TTY behavior behind one private fixture header.

Grant behavior is the low-level leaf. RAMFS and TTY fixture modules may use
private grant lookup. Callers continue to pass the fixture explicitly; the
split introduces no duplicate mutable global context.

### Native behavior boundaries

The `vfs-core` executable retains one entry point but compiles separate
sources for:

- state initialization and complete validation;
- process attach/share/detach and process-owned cleanup;
- root mount and namespace behavior;
- regular open/close/read/write behavior;
- directory translation and cursors;
- console pending I/O and cleanup; and
- spawn image, descriptor, and hidden-publication transactions.

The service executable separates application protocol, backend protocol,
PM-control protocol, configuration, and dispatch behavior.

The model separates context and replay diagnostics, independent reference
transitions, complete-state comparison, process/file/console operation
drivers, pressure/fatal cases, and exact trace scheduling.

### Target and tooling boundaries

The target probe separates client IPC from filesystem and console scenarios
while retaining one entry/orchestration module and one aligned transfer
buffer.

The kernel VFS test remains one scenario file, but its trap handler exposes
report matching, reporter authentication, report classification,
terminal-state validation, readiness, UART drain, and shutdown in that exact
order.

The fixture generator remains one focused Python tool, but `render_fixture()`
becomes an ordered outline over descriptor validation, limit calculation,
image rendering, manifest rendering, report-address rendering, and final
assembly.

### Private contracts

Each test family uses narrowly scoped private fixture and orchestration
headers. Private headers contain declarations and shared types, not function
bodies. The target-level orchestration contract owns the
`micros_vfs_spawn_test_run()` declaration and permits responsibility-owned
case or phase runners to preserve the exact current order. A family may not
include another family's fixture-private header. Production sources may not
include test-private headers.

Cross-file state is passed explicitly. Generic `utils`, `helpers`, or
`common` modules and textual inclusion of `.c` files are rejected.

### Function boundaries

Every current VFS test function over 120 physical lines is a required split.
None is accepted as one intrinsically cohesive algorithm.

Extraction follows semantic task boundaries such as setup, action, injected
failure, state commit, rollback verification, cleanup, or operation-family
dispatch. Helpers must preserve validation order, backend-call order, grant
revocation, reference changes, token consumption, random draws, and result
publication.

### Build and validation ownership

CMake defines exact source sets for the shared fixture, core tests, spawn
tests, service tests, model, and target probe. The existing targets consume
those sets without repeating path lists.

Implementation begins with a structural Red test for exact source ownership
and build integration. Its first green state includes an exact migration
allowlist for the 23 documented over-limit functions. The list may only shrink
and must be empty in the final implementation. Existing semantic tests remain
the oracle.

Every new VFS test source is added to the fail-closed validation ownership
map. This preserves gate selection and does not add a second execution of a
selected workflow.

Narrow native gates run during extraction. Before review, the author records
one execution of the authoritative pull-request validation plan. That plan
must contain complete native validation, the standalone VFS image, and the
QEMU VFS gate. A workflow selected by the plan is not rerun separately. The
independent reviewer performs static analysis and does not routinely rerun
those suites.

## Non-goals

This decision does not add:

- VFS exit cleanup or any new behavior;
- changes to production VFS, RAMFS, TTY, PM, grant, bootstrap, or runtime
  code;
- wire, state-layout, capacity, manifest, profile, or scheduler-policy
  changes;
- new executables, CTest names, workflow names, model operations, seeds,
  markers, or timeouts;
- a repository-wide test framework or generic fixture abstraction;
- dynamic allocation, concurrency, recovery, or multi-filesystem behavior;
  or
- a mechanical line-count limit.

## Alternatives considered

### Add exit-cleanup cases to the current files first

Rejected. It would increase the number of responsibilities in the exact
sources already identified by the repository audit and make later extraction
riskier.

### Create one test executable per new source

Rejected. Source ownership does not require additional execution identities.
It would increase build and review cost, multiply jobs, and risk inconsistent
coverage selection without adding independent evidence.

### Keep one native file and extract only helper functions

Rejected. Named helpers would improve local readability but would leave core,
spawn, service, fixture, and model responsibilities coupled in the same
translation units.

### Copy the MINIX test program layout exactly

Rejected. The selected MINIX programs do not own the complete white-box,
byte-exact protocol, model, target, and generated-fixture evidence required by
the accepted `micros` architecture.

### Make the shared fixture opaque

Rejected for this behavior-preserving correction. Current tests intentionally
inspect counters, injected failures, node references, grants, and TTY state.
Replacing all of those assertions with a new accessor API would enlarge the
change without improving the current evidence boundary.

### Enforce line and function counts mechanically

Rejected. Numeric thresholds locate review work. Responsibility and task
cohesion determine the accepted modules and helpers.

## Consequences

Positive consequences:

- future exit-cleanup tests have explicit process, console, protocol,
  dispatch, and model homes;
- the source tree exposes the same behavior-oriented separation that is useful
  in the MINIX baseline while retaining required `micros` evidence;
- suite orchestration reads as an ordered list of named behavior owners;
- long scenarios expose meaningful phases without changing their assertions;
- model trace and target evidence remain independently reviewable; and
- the refactor does not increase the number of test jobs.

Costs and risks:

- the implementation moves many test lines and expands CMake source lists;
- private-contract mistakes can create cycles or leak test internals;
- careless extraction can reorder model random draws, validation, rollback,
  or target markers;
- exact validation ownership must be updated for every new path; and
- one final full validation pass remains required even though behavior is
  unchanged.

The structural test, small green extraction commits, retained semantic
evidence, fail-closed path ownership, and one independent static review
mitigate those risks.

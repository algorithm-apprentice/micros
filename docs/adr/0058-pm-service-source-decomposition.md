# ADR-0058: PM Service Source Decomposition

- Status: Accepted
- Date: 2026-10-12
- Related:
  [ADR-0025](0025-minix-behavioral-baseline-before-optimization.md),
  [ADR-0032](0032-fail-closed-change-aware-validation.md),
  [ADR-0033](0033-reviewed-design-before-implementation.md),
  [ADR-0034](0034-author-validation-before-review.md),
  [ADR-0046](0046-pm-process-lifecycle-and-spawn-metadata.md),
  [ADR-0050](0050-post-handoff-elf-spawn.md),
  [ADR-0051](0051-responsibility-based-source-modules.md),
  [ADR-0056](0056-kernel-pm-control-source-decomposition.md),
  [ADR-0057](0057-vm-service-source-decomposition.md),
  [source-module audit](../architecture/source-module-audit.md),
  [PM service source organization](../architecture/pm-service-source-organization.md),
  [MINIX baseline](../research/minix-pm-service-source-organization.md)

## Context

Kernel PM control and VM service source corrections are merged. The
dependency-ready PM-service correction is the final source-organization
predecessor before the PM/VFS/VM coordinator extends Step 14.

At reviewed base `6fa6984a2491bafa005feb205438a3c4c63508b6`,
seven audited files contain 8,349 physical lines, 164 functions, and
16 functions above the 120-line review signal:

```text
servers/pm/pm_core.c
servers/pm/pm_service.c
lib/spawn/spawn_request.c
tests/host/pm_test.c
tests/host/spawn_abi_test.c
kernel/pm_service_test.c
tools/generate_pm_service_fixture.py
```

These files mix protocol parsing, common identity primitives, full table
validation, forward/reverse lifecycle policy, wait ownership, service
scenarios, canonical request and stack layout, case fixtures, reference-model
operations, immutable kernel configuration, final reporting, and rendering.

The current behavior is already defined by Accepted ADR-0046 and ADR-0050.
This decision changes ownership, not process policy or integration readiness.
The current real service still does not run the production application
coordinator; its target self-test remains the existing synthetic lifecycle.

## Decision

Apply the exact owners, dependencies, preservation contracts, source sets,
and evidence sequence in
[PM Service Source Organization](../architecture/pm-service-source-organization.md).

### Portable lifecycle

Separate common leaf primitives from the public initialization/runtime/query
facade, codec, complete validation, forward spawn, reverse rollback, exit,
and wait owners. One declarations-only private header carries only required
cross-owner helpers.

The common/facade distinction is necessary: the facade invokes complete
validation, while validation needs identity and record queries. A common
leaf avoids a module cycle.

Exit may invoke the existing private wait-staging helper after zombie
publication. Wait does not call exit. Spawn, rollback, and exit do not call
one another; validation invokes no transition.

Preserve every PID, generation, token, phase, error, publication boundary,
parent/reaper relationship, deterministic child selection, and failure-atomic
output contract. Preserve ADR-0050's `BUSY` hidden-child normal-exit gate and
its ordered, independently completed two-parent wait bundle.

### Canonical request and layout

Keep canonical request decoding and initial stack-layout planning as separate
cohesive owners sharing the unchanged public request header. There is no
cross-owner private helper, so no additional private header is created.

The decoder becomes named header/count, ordered-reference/tail, cursor,
descriptor, and publication phases. Its result order and success-only
publication remain unchanged. Checked layout arithmetic remains independent
of decoding; stack construction and ELF policy remain VM responsibilities.

### Service adaptation

Retain one PM service entry and ELF. Separate fixture/configuration,
bootstrap readiness, existing ordinary request rejection, synthetic lifecycle
self-test, and scratch-peer adapters. One private context replaces the
existing service table, data sentinel, and conditional snapshot globals.
The exported bootstrap configuration remains a separate exact ABI object.

Preserve all compile-time guards, initialization/readiness order, notification
dispatch, one-shot runtime enable, scenario entry, and atomic reply/receive.
Adapters do not call siblings or include the portable private contract.

### Native and target evidence

Keep `pm_test`/`pm-lifecycle` and `spawn_abi_test`/`spawn-abi`, with exact
suite/case order. Existing kernel PM-control evidence remains unchanged.

Separate stateless PM fixture helpers, protocol/spawn/validation/exit-wait
cases, model orchestration, independent reference queries, and operation
handlers. PM tables and snapshots remain automatic; no global fixture or
model is added. Preserve the 4,096-step seed, random draws, trace, and existing
per-operation comparisons.

The spawn-ABI fixture gains one explicit context for its seven existing
mutable static buffers, including function-local boundary/occupancy storage.
Preserve their sizes and distinct roles; cases receive explicit context.

The kernel PM scenario separates immutable configuration from launch and
final trap/report ownership. It has no mutable global scenario state and
gains none. The report becomes named authority/fault, transaction/resource,
runtime/current/readiness, and output/shutdown phases.

### Generator

Keep one CLI and one generated format. Extract validated descriptors/report
symbol, resident limits, images, manifest, and address rendering. Preserve
byte-exact output for identical checked ELF inputs.

### Stable interfaces and builds

The eight headers listed by the architecture contract remain byte-identical.
The existing PM-control wrapper stays unchanged.

Six exact ordered CMake source sets replace repeated raw split-source
listings in every existing consumer. New paths inherit their original
validation ownership; unknown/full-tier fallback remains fail-closed.

No source split introduces another executable, CTest, model workflow, service
ELF, target fixture, marker, timeout, assembly hook, or CI job.
Enforce the already Accepted 32-page PM limit by adding its budget and
one-stack-page options to the existing common ELF-check invocation for every
PM target variant, not by duplicating that check.

### Structural Red

Begin implementation with `tests/host/test_pm_service_source_layout.py`.
Enforce exact family/consumer inventories, public and helper owners, private
header shape, stable hashes, recursive dependency direction, static and
static-local state ownership, native/scenario/model identities, generator
parity, validation inheritance, and long functions.
Adapt the existing kernel PM-control structural gate's retained PM marker
read to the report owner in the same extraction slice, preserving its marker,
field, count, and timeout assertions.

Retain an initial 44-function owner migration and 16-function long migration.
Both may only shrink and must be empty at final review. Moving or renaming
an unchanged long function is not a semantic split.

### Implementation gate

Implementation remains pending until this design is independently reviewed,
Accepted, and squash-merged. Follow the architecture's small green sequence,
use narrow existing evidence during extraction, then execute one authoritative
PR plan before independent implementation review. Selected workflows are not
rerun separately, and reviewers inspect rather than repeat author evidence.

## Consequences

- Process identity, validation, rollback, publication, exit, and wait ownership
  become visible without inventing new lifecycle mechanisms.
- The real service reads as initialization and dispatch over explicit adapters.
- Native model operations remain independent and replayable.
- Mutable evidence storage is explicit without replacing local PM state with
  unnecessary globals.
- The later coordinator can extend reviewed owners rather than mixed core and
  test files.
- CMake and validation inventories become longer but remain exact.
- The extraction must preserve many negative cases, caller/output snapshots,
  callback guards, model draws, generated symbols, and target budgets.

## Alternatives considered

### Extend the coordinator before decomposing PM

Rejected. It would add cross-service lifecycle policy to the same mixed
portable and service files before their correction gate is complete.

### Combine common primitives with the facade

Rejected because facade-to-validation and validation-to-common dependencies
would create a cycle between those mixed owners.

### Put every public operation in its own source

Rejected. Forward spawn, reverse rollback, and wait collection each form one
cohesive transaction; one file per call fragments their invariants.

### Mirror MINIX `forkexit.c` exactly

Rejected. Its broader fork, tracing, signals, accounting, and recovery scope
is not dependency-ready `micros` behavior. Preserve responsibilities and
authority, not a historical file list.

### Add mutable contexts to every test family

Rejected. PM cases/model and the kernel final report already use local state.
Only existing service and spawn-ABI mutable static storage needs consolidation.

### Generalize wire codecs or fixture generation

Rejected. This task preserves fixed current interfaces, not a reusable
framework.

## Non-goals

No public API/ABI/layout, PID, token, error, transaction, authority, scheduling,
or failure-order change. No production spawn coordinator, new rollback,
fault/signal exit, init, app runtime, shell, credentials, groups, migration,
compatibility, recovery, concurrency, kernel directory move, unrelated
decomposition, or weakened/replaced evidence.

# ADR-0054: Frame-Ownership Source Decomposition

- Status: Accepted
- Date: 2026-10-12
- Related:
  [ADR-0017](0017-bootstrap-physical-frame-allocator.md),
  [ADR-0020](0020-typed-bootstrap-frame-ownership.md),
  [ADR-0025](0025-minix-behavioral-baseline-before-optimization.md),
  [ADR-0032](0032-fail-closed-change-aware-validation.md),
  [ADR-0033](0033-reviewed-design-before-implementation.md),
  [ADR-0034](0034-author-validation-before-review.md),
  [ADR-0045](0045-static-vm-bootstrap-and-handoff.md),
  [ADR-0050](0050-post-handoff-elf-spawn.md),
  [ADR-0051](0051-responsibility-based-source-modules.md),
  [source-module audit](../architecture/source-module-audit.md),
  [source organization](../architecture/source-organization.md),
  [frame-ownership source organization](../architecture/frame-ownership-source-organization.md),
  [MINIX baseline](../research/minix-frame-ownership-source-organization.md)

## Context

ADR-0051 requires cohesive source owners, explicit private contracts,
acyclic dependencies, and function boundaries based on semantic tasks. The
source-module audit identifies frame ownership as the next correction after
kernel address spaces.

At reviewed base
`fac35da1fe54489b34a13a1c9929029d5660ce72`, four signal files contain
6,739 physical lines and 97 functions:

```text
kernel/frame_ownership.c
kernel/frame_ownership_test.c
tests/host/frame_ownership_test.c
tests/host/frame_allocator_test.c
```

Fifteen functions exceed ADR-0051's 120-line review signal. More importantly,
the sources mix:

- canonical owner encoding, allocator geometry, initialization, queries, and
  complete validation;
- bootstrap allocation, exact release, process-frame release, handoff
  staging, and irreversible commit;
- post-handoff VM claim, rollback release, freeze, and process release;
- native deterministic cases and replayable model transitions; and
- target fixture, process lifecycle, handoff, and sealed-state evidence.

The runtime behavior is already specified and tested. This decision changes
source ownership only. It preserves every public declaration, structure,
enum, error, precedence, output guarantee, transition boundary, seed, case
order, marker, target, CTest, workflow, and timeout.

The fixed MINIX baseline separates temporary kernel boot allocation, VM-owned
physical allocation, page-table mechanism, process memory policy, startup,
and exit. MINIX has no equivalent dense generation-bound kernel ledger.
`micros` therefore retains its typed ledger as a compatible safety extension
while adopting the baseline responsibility direction: low-level
availability/ownership validation remains below lifecycle transitions.

## Decision

Decompose the frame-ownership production and evidence sources according to
[the exact architecture contract](../architecture/frame-ownership-source-organization.md).
The extraction is behavior-preserving and introduces no new public interface.

### Stable public interface

`include/micros/frame_ownership.h` remains byte-for-byte stable. No owner kind,
phase, handoff target, error, constant, plan, structure layout, or function
signature changes.

The allocator remains the availability authority. The ledger remains the
semantic ownership authority. VM remains the ordinary post-handoff policy
owner, while the kernel retains privileged validation and commit mechanism.

### Production owners

The final production layout is:

| Source | Cohesive owner |
| --- | --- |
| `kernel/frame_ownership.c` | Owner construction, allocator/ledger geometry primitives, low-level state mutation primitives, and initialization |
| `kernel/frame_ownership_validation.c` | Complete validation, lookup, process counting, handed-off preflight, and exact VM frame-set resolution |
| `kernel/frame_ownership_bootstrap.c` | Bootstrap allocate, release, and selected process-frame release |
| `kernel/frame_ownership_handoff.c` | Handoff plan staging, complete wired-set staging, and irreversible commit |
| `kernel/frame_ownership_vm_claim.c` | Post-handoff free-frame claim |
| `kernel/frame_ownership_vm_freeze.c` | Transferable data and free table-frame freeze |
| `kernel/frame_ownership_vm_release.c` | Transferable rollback and process-owned release |
| `kernel/frame_ownership_internal.h` | Minimal private cross-module contract |

Complete validation is read-only. Mutation modules may depend on validation
and low-level ledger primitives, but no mutation module depends on another
mutation module.

### Public symbol ownership

The exact public-symbol owners are defined in the architecture contract.
Three construction/initialization symbols remain in `frame_ownership.c`.
Seventeen symbols move to validation, bootstrap, handoff, VM claim, VM
freeze, or VM release owners.

The private header contains declarations and shared types only. It contains no
function body or mutable definition. The split adds no mutable production
global and no target-runtime dependency.

### Failure and publication boundaries

Every current operation retains:

- exact argument, phase, geometry, owner, distinctness, capacity, and count
  check order;
- exact error precedence;
- unchanged caller outputs on failure;
- complete read-only preflight before the first authoritative mutation;
- rollback after a defensive post-allocation mismatch;
- non-failing prevalidated VM commits; and
- phase or aggregate-count publication at the existing final boundary.

Helper extraction must expose preflight, candidate construction, commit, and
publication where those are distinct tasks. It must not add fallible work
after mutation begins.

### Native evidence owners

The frame allocator retains one executable and separates deterministic cases
from its seeded reference model.

The frame-ownership host test retains one executable and separates:

- fixture and failure-atomicity helpers;
- bootstrap behavior;
- whole-ledger validation and capacity;
- handoff behavior;
- the replayable owner/target/phase model; and
- post-handoff VM transitions.

One explicit mutable context is owned by the fixture and passed to every
runner. The current 14-case order, seed, 4,096 steps, handoff step, validation
interval, and output remain exact.

### Target evidence owners

The QEMU test retains one public entry and separates:

- one fixture/context owner;
- baseline, process-generation, allocation, owner, and release-blocking
  lifecycle evidence; and
- handoff, sealed-phase, final-count, and final-validation evidence.

The public entry retains interrupt save/restore and the exact short-circuit
scenario order. The pass record and timeout remain unchanged.

### Build and validation ownership

CMake defines exact source sets for production, allocator host evidence,
ownership host evidence, and QEMU evidence. Every current consumer expands the
appropriate set exactly once.

The validation planner gives each new path the exact classification of its
current monolithic source. Production frame-ownership paths remain shared
fail-closed inputs for the complete affected QEMU and slow-model set. The
structural test itself is host-unit-only.

No source split creates another behavioral executable, CTest, workflow,
marker, or duplicate behavioral execution. The sole new evidence artifact is
the structural Python test below, executed by the existing host-unit
discovery and existing `test-unit-fast` workflow.

### Structural Red gate

Implementation begins with:

```text
tests/host/test_frame_ownership_source_layout.py
```

The test verifies exact paths, ordered source sets, consumer expansion,
private-header shape, dependency hygiene, public symbol ownership, unique
test-context ownership, stable evidence identities, validation-plan
inheritance, and migration completion.

The initial migration collections contain:

- 17 public symbols whose owner changes; and
- 15 current over-limit functions.

Both collections may only shrink and must be empty in the final
implementation. Renaming or moving an unchanged long function does not
satisfy the gate.

### Implementation order

The implementation proceeds as small green commits:

1. structural Red gate, source-set skeleton, private headers, and complete
   migration maps;
2. native allocator deterministic/model split;
3. core, validation, bootstrap, and matching native evidence split;
4. handoff and matching native evidence split;
5. VM claim/freeze/release plus native VM/model split;
6. QEMU fixture/lifecycle/handoff split and empty migration maps; and
7. one authoritative pull-request validation execution and independent
   review.

Each extraction runs only the narrow existing evidence required by its active
slice. The final pull-request plan is authoritative; selected workflows are
not rerun separately. The reviewer inspects the author evidence and does not
routinely repeat suites.

## Consequences

### Positive

- Allocator/ledger invariants become a visible lower-level owner.
- Bootstrap and post-handoff transitions no longer share one implementation
  file merely because they mutate the same ledger.
- VM claim, freeze, and release have distinct reviewable failure and commit
  boundaries.
- Native model state and target scenario state each have one explicit owner.
- Future frame transition changes have a narrow source and evidence home.

### Costs

- CMake and validation-plan path mappings become more explicit.
- The implementation moves several thousand lines while preserving behavior.
- Private declarations must remain narrow enough to avoid replacing one
  monolith with a broad internal API.
- Exact check, random-draw, commit, and target scenario order require careful
  extraction.

### Risks and mitigations

- **Semantic drift:** the retained native model and QEMU gate remain the
  behavior oracle.
- **State duplication:** the structural gate enforces one mutable context per
  split test family and no new production global.
- **Dependency cycle:** validation is below every mutation owner, and mutation
  owners cannot call one another.
- **Partial commit:** the design preserves complete preflight and non-failing
  commit functions.
- **Coverage duplication:** stable evidence identities prohibit extra jobs.
- **Mechanical splitting:** every long function has a required semantic
  disposition in the architecture contract.

## Alternatives considered

### Leave frame ownership intact until the coordinator

Rejected. The source-module audit makes this correction a prerequisite before
later VM/PM coordination adds more ownership transitions around the same
ledger.

### Split only host and target tests

Rejected. Production still combines bootstrap and post-handoff transitions
that change for different lifecycle reasons.

### Keep one production file and extract local helpers

Rejected. Named helpers would improve local readability but leave validation,
handoff, claim, freeze, and release ownership ambiguous.

### Create one file per public function

Rejected. Public entry points are not necessarily separate responsibilities.
The accepted modules group operations by lifecycle and commit invariant.

### Mirror MINIX VM files exactly

Rejected. MINIX has no equivalent typed ledger and includes region, paging,
cache, contiguous allocation, and recovery behavior outside the `micros`
v0.1 contract.

### Change ownership behavior while moving code

Rejected. Any owner, phase, transition, error, or VM policy change requires a
separate reviewed design and regression evidence.

## Non-goals

This ADR does not authorize:

- public API, ABI, structure, enum, owner-kind, phase, or error changes;
- new frame allocation, handoff, VM transition, or process behavior;
- paging, reclaim, overcommit, sharing, copy-on-write, recovery, or SMP;
- changes to runtime serialization, object resolution, or page-table policy;
- decomposition outside the four audited signal files and their build and
  validation ownership; or
- a new behavioral executable, CTest, workflow, marker, or duplicate
  behavioral execution beyond the authorized structural source-layout test.

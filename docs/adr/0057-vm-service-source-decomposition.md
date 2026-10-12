# ADR-0057: VM Service Source Decomposition

- Status: Accepted
- Date: 2026-10-12
- Related:
  [ADR-0025](0025-minix-behavioral-baseline-before-optimization.md),
  [ADR-0032](0032-fail-closed-change-aware-validation.md),
  [ADR-0033](0033-reviewed-design-before-implementation.md),
  [ADR-0034](0034-author-validation-before-review.md),
  [ADR-0045](0045-static-vm-bootstrap-and-handoff.md),
  [ADR-0050](0050-post-handoff-elf-spawn.md),
  [ADR-0051](0051-responsibility-based-source-modules.md),
  [ADR-0055](0055-kernel-vm-handoff-source-decomposition.md),
  [ADR-0056](0056-kernel-pm-control-source-decomposition.md),
  [source-module audit](../architecture/source-module-audit.md),
  [source organization](../architecture/source-organization.md),
  [VM service source organization](../architecture/vm-service-source-organization.md),
  [MINIX baseline](../research/minix-vm-service-source-organization.md)

## Context

ADR-0051 requires cohesive responsibility owners, explicit private contracts,
acyclic dependencies, and semantic function boundaries. The source-module
audit identifies the VM service as the next correction after kernel PM
control.

At reviewed base
`d3cb32b031ea7494ec00220cf879df96eb668907`, seven signal files contain 6,976
physical lines and 139 functions:

```text
servers/vm/vm_core.c
servers/vm/vm_image.c
lib/vm/vm_boot_validate.c
kernel/vm_scratch_preemption_test.c
tests/host/vm_core_test.c
tests/qemu/vm_server.c
tools/generate_vm_scratch_preemption_fixture.py
```

Eleven functions exceed ADR-0051's 120-line review signal. More importantly,
the sources mix:

- immutable boot import, common primitives, frame translation/selection, and
  complete runtime validation;
- executable/request snapshotting, ELF and stack planning, scratch page fill,
  freeze, abort, commit, and exit release;
- service configuration, handoff, readiness, TTY, probe, scratch, and PM-test
  protocols;
- native fixture construction and unrelated lifecycle cases;
- scratch-preemption fixture, runtime validation, trap transitions, and final
  reporting; and
- fixture descriptor validation, image/manifest rendering, and symbol-address
  generation.

The behavior is already specified and tested. This decision changes source
ownership only. It preserves every public header, layout, phase, state, token,
protocol, result, error, marker, generated symbol, target, CTest, workflow,
and timeout.

The fixed MINIX baseline separates VM service dispatch, physical allocation,
region policy, page-table mechanism, executable loading, exit release, and
fault handling. `micros` retains that responsibility direction while
preserving required and accepted adaptations: unprivileged VM policy,
kernel-controlled scratch/PTE mutation, fixed mapping records, deterministic
frame choice, pointer-free requests, direct grants, exact generation/tokens,
and fatal VM self-faults.

## Decision

Decompose the VM service production, adapter, fixture, and evidence sources
according to
[the exact architecture contract](../architecture/vm-service-source-organization.md).
The extraction is behavior-preserving and introduces no new supported
interface.

### Stable interfaces

The following headers remain byte-for-byte stable:

```text
include/micros/vm_bootstrap.h
servers/vm/vm_core.h
servers/vm/vm_image.h
kernel/vm_scratch_preemption_test.h
kernel/vm_scratch_preemption_test_fixture.h
```

No boot, VM-core, stack, ELF, scratch-test, service, PM/VM, or operation-12
ABI changes.

### Portable production owners

The final portable VM layout separates:

- common state primitives and initialization;
- complete read-only validation;
- managed-frame translation and deterministic selection;
- one load transaction;
- one exit-release transaction;
- shared image primitives;
- stack planning/building; and
- ELF planning.

Two declarations-only private headers carry only the portable state and
image-planning contracts required across accepted owners.

Validation invokes no transition. Frame selection mutates no lifecycle state.
Load and exit do not call one another. Stack and ELF owners do not call one
another.

### Boot validator

The boot validator remains one cohesive fixed-object owner. Its 343-line
public function becomes a short orchestration over header, range,
address-space/mapping, frame/count/tail, and digest phases. It does not become
a second runtime state or frame database.

### Native evidence owners

The native VM-core suite retains one `main`, exact case order, and one mutable
context. Separate files own:

- fixture/build/digest/frozen setup;
- ABI and boot import;
- load lifecycle;
- abort paths; and
- capacity, corruption, staged inputs, and immutable database.

No new native executable or CTest is created.

### Service and target evidence owners

The real target VM service retains one service entry and one ELF. Common
startup, handoff, readiness, and receive-loop sequencing remain in the facade.
Separate private modules own the shared fixture/configuration, handoff,
retained grant probe, TTY mapping, scratch-preemption VM path, and PM-test
load/commit/release path.

The scratch-preemption kernel scenario separately owns fixture/configuration,
runtime validation, final report, trap transitions, and the retained public
hooks. Each target family has exactly one mutable context owner.

Scenario modules do not call sibling scenario handlers. The common facade
does not implement their protocol logic.

### Fixture generator

The generator remains one CLI and one output format. Descriptor/symbol
validation, resident limits, images, manifest, and exported-address rendering
become named helpers. No general generation framework is introduced.

### Build and validation ownership

CMake defines exact ordered source sets for boot validation, image planning,
portable VM core, host VM-core evidence, the target VM service, and the
scratch-preemption kernel scenario. Every current consumer expands only its
required sets once.

Every new path inherits the exact validation classification of its current
monolithic owner. Existing unknown/full-tier ownership remains fail-closed.
The structural test itself is host-unit-only.

No source split creates another behavioral executable, CTest, service ELF,
custom target, workflow, marker, timeout, generated fixture, or duplicate
execution.

### Structural Red gate

Implementation begins with:

```text
tests/host/test_vm_service_source_layout.py
```

The fail-closed gate verifies exact family paths, ordered source sets,
consumer expansion, private-header shape, stable-header hashes, public/test
function owners, service/host/scratch mutable-state ownership, dependency
direction across recursive local includes, validation inheritance, native
case order, service/scenario order, generator symbol/output identities, stable
evidence identities, C and Python long functions, and migration completion.

The initial migration collections contain:

- 23 moving public/test-case functions; and
- 11 current over-limit C/Python functions.

Both collections may only shrink and must be empty in the final
implementation. Renaming or moving an unchanged long function does not
satisfy the gate.

### Implementation order

The implementation proceeds as eight green slices:

1. structural gate, exact source sets, private-header skeletons, and migration
   collections;
2. image primitives, stack planning, ELF planning, boot-validator phases, and
   matching native fixture/boot evidence;
3. frame selection and complete runtime validation;
4. load and exit transactions plus native load/abort/validation evidence;
5. target service facade, fixture, handoff, probe, TTY, scratch, and PM
   adapters;
6. scratch-preemption kernel fixture/runtime/report/trap owners and generator
   rendering phases;
7. empty migration collections and one authoritative pull-request validation
   execution; and
8. independent implementation review.

Each slice runs only its narrow existing evidence. The final pull-request plan
is authoritative and selected workflows are not rerun separately.

## Consequences

### Positive

- Complete validation, frame policy, load lifecycle, and exit release become
  visible source owners.
- ELF and stack algorithms no longer share a file merely because both consume
  spawn bytes.
- The target VM service reads as common startup plus explicit scenario
  adapters rather than one conditional file.
- Native and target evidence each gain one explicit context owner.
- Scratch-preemption trap/report behavior and fixture generation become
  reviewable named phases.
- The later PM-service correction can depend on a clear VM service boundary.

### Costs

- CMake and validation-plan ownership become more explicit.
- The implementation moves nearly seven thousand lines while preserving
  behavior.
- Service and target private contracts must remain narrow enough not to become
  another framework.
- Extraction must preserve many compile-time guards, exact request orders,
  generated bytes, trap stages, markers, and failure boundaries.

### Risks and mitigations

- **Frame-policy drift:** exact function owners and retained native lifecycle
  tests preserve deterministic selection and local/kernel mirroring.
- **Validation weakening:** complete-state validation remains a read-only
  lower owner and structural dependency checks forbid transition calls.
- **Service scenario coupling:** exact adapter owner/call maps forbid sibling
  scenario calls.
- **State duplication:** one mutable context is enforced for host, service,
  and scratch-kernel families.
- **Fixture drift:** generator output identities and checked-ELF symbol names
  remain structural requirements.
- **Coverage dilution:** stable native/QEMU identities and fail-closed path
  inheritance preserve current evidence.
- **Structural bypass:** exact family inventory, recursive include closure,
  type-based state checks, and C/Python long-function scans fail closed.
- **Mechanical splitting:** every long function has a required semantic task
  outline.

## Alternatives considered

### Leave VM service intact until the coordinator

Rejected. The PM-service and cross-service coordinator would add behavior
around the same mixed loader, adapter, fixture, and test sources.

### Split only the portable VM core

Rejected. Image/stack planning, service conditionals, target scratch evidence,
native fixtures, and generator responsibilities would remain mixed.

### Create one source per public transition

Rejected. Page iteration, scratch lifecycle, freeze, abort, and commit are one
serialized load transaction. One file per public function would fragment its
invariants.

### Split the boot validator into a broad validation framework

Rejected. It validates one immutable object and needs named phases, not a
general parser hierarchy.

### Mirror MINIX VM files exactly

Rejected. MINIX includes dynamic regions, direct VM page-table mutation,
paging, page faults, fork/COW, live update, recovery, and architecture code
outside the `micros` v0.1 contract.

### Change VM behavior while moving code

Rejected. Any frame policy, mapping, loader, protocol, token, result, error,
fault, or publication change requires a separate reviewed design and
regression evidence.

## Non-goals

This ADR does not authorize:

- public API, ABI, layout, phase, state, token, result, error, or behavior
  changes;
- new allocation, mapping, page-fault, demand-paging, file-backed, fork, COW,
  sharing, reclaim, overcommit, recovery, live-update, or SMP behavior;
- changes to kernel operation 12, PM control, PM service, VFS, scheduler, IPC,
  endpoint, grant, frame, or address-space policy;
- decomposition outside the seven audited signal files and their direct build
  and validation ownership;
- new service executables, fixtures, workflows, markers, timeouts, or CI jobs;
  or
- function splitting by line count instead of task cohesion.

# ADR-0055: Kernel VM-Handoff Source Decomposition

- Status: Accepted
- Date: 2026-10-12
- Related:
  [ADR-0025](0025-minix-behavioral-baseline-before-optimization.md),
  [ADR-0032](0032-fail-closed-change-aware-validation.md),
  [ADR-0033](0033-reviewed-design-before-implementation.md),
  [ADR-0034](0034-author-validation-before-review.md),
  [ADR-0040](0040-post-handoff-wired-address-space-resolution.md),
  [ADR-0045](0045-static-vm-bootstrap-and-handoff.md),
  [ADR-0050](0050-post-handoff-elf-spawn.md),
  [ADR-0051](0051-responsibility-based-source-modules.md),
  [ADR-0054](0054-frame-ownership-source-decomposition.md),
  [source-module audit](../architecture/source-module-audit.md),
  [source organization](../architecture/source-organization.md),
  [kernel VM-handoff source organization](../architecture/kernel-vm-handoff-source-organization.md),
  [MINIX baseline](../research/minix-kernel-vm-handoff-source-organization.md)

## Context

ADR-0051 requires cohesive responsibility owners, explicit private contracts,
acyclic dependencies, and semantic function boundaries. The source-module
audit identifies kernel VM handoff as the next correction after frame
ownership.

At reviewed base
`b47d56e2ece2253a3ab2a4e9ac8ddc8cab56da37`, seven signal files contain
11,780 physical lines and 267 functions:

```text
kernel/vm_handoff_core.c
kernel/vm_handoff_syscall.c
kernel/vm_snapshot.c
kernel/address_space_handoff_test.c
tests/host/vm_handoff_syscall_test.c
tests/host/vm_handoff_test.c
tests/host/vm_bootstrap_test.c
```

Thirteen functions exceed ADR-0051's 120-line review signal. More
importantly, the files mix:

- nine command ABIs, complete state validation, static handoff, dynamic load,
  scratch, freeze, commit, abort, exit release, and self-fault policy;
- fixed user-object translation, VM/PM/bootstrap authorization, TTY handoff,
  frame ownership, address-space mutation, and command dispatch;
- snapshot construction and current-state validation;
- native portable-state, boot-object, syscall-adapter, and linker-backend
  evidence; and
- one target scenario containing setup, snapshot, handoff, resident
  operation, corruption, real syscall, TTY, and trap responsibilities.

The behavior is already specified and tested. This decision changes source
ownership only. It preserves every public header, command, state, token,
error, output boundary, commit order, case/scenario order, marker, target,
CTest, workflow, and timeout.

The fixed MINIX baseline separates generic kernel VM control, VM bootstrap,
request dispatch, physical allocation, page-table mechanism, exec, exit, and
fault handling. `micros` retains the same downward authority direction while
preserving required RISC-V adaptations: register ABI, unprivileged VM,
pointer-free objects, scratch aliasing, kernel-built Sv39 roots, exact tokens,
and non-failing commits.

## Decision

Decompose the kernel VM-handoff production and evidence sources according to
[the exact architecture contract](../architecture/kernel-vm-handoff-source-organization.md).
The extraction is behavior-preserving and introduces no new supported
interface.

### Stable interfaces

The following headers remain byte-for-byte stable:

```text
kernel/vm_handoff_core.h
kernel/vm_handoff_syscall.h
kernel/vm_snapshot.h
kernel/address_space_handoff_test.h
```

No command, enum, structure, field order, token, phase, result, error, or
public function signature changes.

### Portable state owners

The final portable layout separates:

- complete state validation and page-plan digest;
- register ABI decoding;
- static handoff and self-fault classification;
- dynamic load begin;
- scratch map/commit;
- freeze;
- prepared/activated/final commit; and
- abort/exit release.

One declarations-only `vm_handoff_internal.h` carries only cross-module
predicates and candidate helpers used by at least two accepted owners.
Transition modules depend on complete validation and common predicates but
never on another transition module.

### Snapshot owners

`vm_snapshot.c` retains the sole aligned snapshot storage and preparation
orchestration. `vm_snapshot_validation.c` owns current-state re-inventory and
authority validation. They share only a narrow declarations-only private
contract.

### Syscall adapter owners

The public captured-`ecall` facade retains command decode, common authority
acquisition, phase selection, and dispatch. Separate private modules own:

- shared runtime authorization and diagnostics;
- fixed user-object translation;
- static VM-ready and TTY mapping;
- load begin and final commit;
- scratch map/commit;
- freeze; and
- abort/exit release.

Operation adapters do not call one another. They depend downward on portable
state and existing subsystem mechanisms. No portable state module includes a
syscall-private header.

### Failure and publication boundaries

The split preserves:

- exact decode and authority-check precedence;
- unchanged outputs on every rejected decode or preflight;
- complete input/output physical access plans before mutation;
- input-byte copies into kernel storage before portable preflight or
  mutation;
- unchanged output bytes on every returning failure;
- output publication only after all accepted non-failing commits through the
  retained prevalidated output plan;
- full PM reservation, frame, address-space, and state preflight;
- exact scratch/freeze/abort/exit commit order;
- fatal classification for runtime or invariant corruption;
- stable recoverable results for invalid input, state, authorization, or
  memory faults; and
- no fallible work after an accepted non-failing commit boundary begins.

### Evidence owners

Native portable-state tests separate ABI, boot, load, and fixture tasks while
retaining one suite entry and exact case order.

Native VM-bootstrap tests separate one mutable fixture, ABI validation, and
authoritative build/failure cases.

Native syscall tests separate one mutable fixture, boot/load scenarios, and
runtime/boot/load linker adapters. Every backend obtains the one context only
through the fixture accessor.

The retained address-space handoff QEMU image separates one explicit context,
setup, snapshot, handoff, post-handoff runtime, real syscall, TTY, and trap
owners. The public entry retains phase order, failure-stage diagnostics, SIE
save/restore, and final result.

### Build and validation ownership

CMake defines exact ordered source sets for portable core, syscall adapters,
snapshot, three native test families, and the retained QEMU test. Every
current consumer expands the appropriate set once.

The validation planner gives every new path the exact classification of its
current monolithic owner. The structural test itself is host-unit-only. No
new behavioral test identity or duplicate execution is introduced.

### Structural Red gate

Implementation begins with:

```text
tests/host/test_kernel_vm_handoff_source_layout.py
```

The gate verifies exact paths, source sets, private-header shape, dependency
hygiene, final symbol owners, unique context/snapshot state, stable evidence
identities, validation-plan inheritance, and migration completion.

The initial migration collections contain:

- 92 production/test public symbols whose source owner changes; and
- 13 current over-limit functions.

Both collections may only shrink and must be empty in the final
implementation.

### Implementation order

The implementation proceeds as seven green slices:

1. structural gate, source-set skeleton, private headers, and migration maps;
2. ABI, validation, static handoff, and native ABI/boot evidence;
3. load, scratch, freeze, commit, release, and native lifecycle evidence;
4. snapshot preparation/validation and bootstrap evidence;
5. syscall adapters and native fixture/backend/scenario evidence;
6. retained address-space handoff target evidence and empty migrations; and
7. one authoritative pull-request validation execution plus independent
   review.

Each slice runs only its narrow existing evidence. The final pull-request plan
is authoritative and selected workflows are not rerun separately.

## Consequences

### Positive

- Register decoding, portable transitions, privileged adaptation, snapshot,
  and evidence ownership become explicit.
- Static boot handoff and dynamic spawn/exit lifecycle no longer share one
  state source merely because they use operation 12.
- Freeze and release commit order becomes locally reviewable.
- The retained target scenario reads as named phases rather than one
  1,745-line function.
- Future VM-handoff changes have narrow production and evidence homes.

### Costs

- CMake and validation-plan source ownership becomes more explicit.
- The implementation moves more than ten thousand source lines while
  preserving behavior.
- Test-only linker backends require a narrow shared context accessor.
- Extraction must preserve many error, commit, trap, and scenario-order
  boundaries exactly.

### Risks and mitigations

- **Authority drift:** exact public-header and symbol-owner gates preserve the
  accepted caller and phase model.
- **Commit reordering:** required semantic helper dispositions make preflight
  and non-failing commit order visible.
- **State duplication:** one context owner per test family and one snapshot
  owner are structural requirements.
- **Dependency cycles:** portable state never calls adapters; adapters do not
  call one another.
- **Coverage dilution:** stable native/QEMU identities and fail-closed path
  inheritance preserve existing evidence.
- **Mechanical splitting:** every current long function has an accepted task
  outline.

## Alternatives considered

### Leave VM handoff intact until the coordinator

Rejected. The next coordinator would add more PM/VM transitions around the
same mixed core and adapter files.

### Split only the syscall dispatcher

Rejected. Portable state, snapshot, native evidence, and target scenario
ownership would remain mixed.

### Keep all dynamic load transitions together

Rejected. Scratch, freeze, prepared/activated commit, abort, and exit release
have distinct authorities and commit boundaries.

### Mirror MINIX files exactly

Rejected. MINIX VM writes page tables directly and includes dynamic regions,
fault handling, fork, recovery, and architecture mechanisms outside the
`micros` v0.1 contract.

### Change VM lifecycle behavior while moving code

Rejected. Any command, state, token, error, authority, or mapping change
requires its own reviewed design and regression evidence.

## Non-goals

This ADR does not authorize:

- public API, ABI, command, state, token, result, error, or behavior changes;
- page-fault delivery, fork, copy-on-write, paging, recovery, live update, or
  SMP;
- changes to PM control, VM service, PM service, TTY, grants, frame ownership,
  address-space behavior, or scheduler policy;
- new generated fixtures, wire formats, markers, timeouts, or test jobs;
- decomposition outside this audited correction; or
- mechanical line-count splitting.

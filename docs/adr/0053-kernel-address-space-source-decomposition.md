# ADR-0053: Kernel Address-Space Source Decomposition

- Status: Accepted
- Date: 2026-10-12
- Related:
  [ADR-0021](0021-generation-safe-user-address-spaces.md),
  [ADR-0022](0022-user-execution-contexts-and-u-mode-entry.md),
  [ADR-0025](0025-minix-behavioral-baseline-before-optimization.md),
  [ADR-0033](0033-reviewed-design-before-implementation.md),
  [ADR-0034](0034-author-validation-before-review.md),
  [ADR-0040](0040-post-handoff-wired-address-space-resolution.md),
  [ADR-0045](0045-static-vm-bootstrap-and-handoff.md),
  [ADR-0050](0050-post-handoff-elf-spawn.md),
  [ADR-0051](0051-responsibility-based-source-modules.md),
  [source-module audit](../architecture/source-module-audit.md),
  [source organization](../architecture/source-organization.md),
  [kernel address-space source organization](../architecture/kernel-address-space-source-organization.md),
  [MINIX baseline](../research/minix-kernel-address-space-source-organization.md)

## Context

ADR-0051 requires one cohesive source owner for each runtime task, narrow
private contracts, explicit state flow, acyclic dependencies, and semantic
function decomposition. The source-module audit identifies the kernel
address-space family as the next correction after the VFS test
decomposition.

At reviewed base
`40e5e01e8fa7e23d661967e4c15908f76485100f`, seven signal files contain
10,298 physical lines and 184 functions:

```text
kernel/address_space.c
kernel/user_address_space.c
kernel/user_address_space_core.c
kernel/user_execution.c
kernel/user_address_space_test.c
kernel/user_execution_test.c
tests/host/user_address_space_core_test.c
```

Twenty-three functions exceed ADR-0051's 120-line review signal. More
importantly, the large sources mix distinct responsibilities:

- kernel-root construction and allocator/table-tree validation;
- process-root lifecycle, traversal, validation, mapping mutation, and
  activation;
- wired handoff, TTY publication, VM scratch, freeze, and teardown;
- portable owner classification and VM page-table planning;
- execution-object lifecycle, mapping validation, trap capture, and return
  installation; and
- QEMU fixture, orchestration, scenario, and trap-hook evidence.

The public behavior is already specified and tested. This ADR changes source
ownership only. It must preserve all existing public declarations, layouts,
error values and precedence, output-preservation guarantees, transaction
boundaries, test-only guards, scenario ordering, markers, targets, CTest
names, workflows, and timeouts.

The fixed MINIX baseline separates generic privileged VM control,
architecture root binding, page-table mechanism, region policy, fault
orchestration, process copy, startup loading, and exit release. `micros`
cannot copy that layout literally because it has an unprivileged VM server,
kernel-validated Sv39 mutation, generation-safe frame owners, bounded static
tables, exact root validation, and no general fault or copy-on-write path.
The reusable result is responsibility separation and downward dependency
direction, not MINIX's broad private header or function size.

## Decision

Decompose the kernel address-space and user-execution family according to
[the exact architecture contract](../architecture/kernel-address-space-source-organization.md).
The extraction is behavior-preserving and uses no new public interface.

### Stable public interfaces

These headers remain byte-for-byte stable unless a later reviewed design
explicitly changes behavior:

```text
include/micros/kernel_address_space.h
include/micros/user_address_space.h
include/micros/user_address_space_core.h
include/micros/user_execution.h
```

The decomposition does not change declarations, enum values, structures,
field order, constants, callback signatures, test-only build guards, or
failure/output contracts in those headers.

### Production owners

The final production layout is:

| Source | Cohesive owner |
| --- | --- |
| `kernel/address_space.c` | Kernel-root construction, kernel mappings, initialization, and report publication |
| `kernel/address_space_validation.c` | Allocator/table-tree ownership, reachability, and expected kernel-root validation |
| `kernel/address_space_internal.h` | Private kernel-root state and validation declarations |
| `kernel/user_address_space_runtime.c` | Runtime/process acquisition, error translation, interrupt serialization, and the sole shared scratch lease owner |
| `kernel/user_address_space_table.c` | Direct-map table access, address/PTE/frame-index primitives, and bounded bitmap operations |
| `kernel/user_address_space_core.c` | Portable phase-aware frame-owner and managed-leaf classification |
| `kernel/user_address_space_vm_plan.c` | Portable deterministic four-table VM plan construction |
| `kernel/user_address_space_validation.c` | Complete private-root validation and public validation |
| `kernel/user_address_space_mapping.c` | Mapping lookup, translation, inventory, slot location, and leaf walking |
| `kernel/user_address_space.c` | Private process-root create/destroy lifecycle |
| `kernel/user_address_space_mutation.c` | Bootstrap page allocation and release |
| `kernel/user_address_space_handoff.c` | Wired-handoff preparation and completion orchestration |
| `kernel/user_address_space_tty.c` | Exact one-shot TTY UART mapping preparation, commit, and validation |
| `kernel/user_address_space_vm_scratch.c` | Active-VM-root scratch mapping and removal |
| `kernel/user_address_space_vm_freeze.c` | VM freeze preflight, construction, and installation |
| `kernel/user_address_space_vm_teardown.c` | VM teardown preflight and non-failing commit |
| `kernel/user_address_space_activation.c` | Process-root and kernel-root activation |
| `kernel/user_address_space_internal.h` | Sole private cross-module user-address-space contract |
| `kernel/user_execution.c` | Execution preparation, stack ownership, inspect/detach, and post-handoff binding commit |
| `kernel/user_execution_validation.c` | Canonical status, entry/stack mapping, context, return, and object validation |
| `kernel/user_execution_trap.c` | Trap capture, stored-context mutation, and return-frame installation |
| `kernel/user_execution_internal.h` | Private execution-family declarations |

`kernel/address_space_test.c` and its assembly fixture remain outside this
correction. The existing kernel-MMU test already has a bounded source owner
and is not one of the audited signal files.

### Public function ownership

The stable public symbols move only to their accepted owners:

- kernel-root initialization and report publication remain in
  `address_space.c`;
- create and destroy remain in `user_address_space.c`;
- allocate, release, and the existing test-only allocation-failure control
  move to `user_address_space_mutation.c`;
- lookup, translate, and inventory move to
  `user_address_space_mapping.c`;
- wired-handoff preparation and completion move to
  `user_address_space_handoff.c`;
- activation and kernel activation move to
  `user_address_space_activation.c`;
- public user-root validation moves to
  `user_address_space_validation.c`;
- phase-aware owner and leaf classification remain in
  `user_address_space_core.c`;
- VM table-plan preparation moves to
  `user_address_space_vm_plan.c`;
- execution prepare, post-handoff commit, inspect, detach, and stack-bounds
  test support remain in `user_execution.c`;
- execution context and return validation move to
  `user_execution_validation.c`; and
- trap capture, context storage, and return-frame installation move to
  `user_execution_trap.c`.

The structural contract verifies this ownership rather than relying on
filename convention alone.

### Private contracts and state

There are exactly three production-private headers for this family:

```text
kernel/address_space_internal.h
kernel/user_address_space_internal.h
kernel/user_execution_internal.h
```

They contain declarations, shared private types, and constants only. They do
not contain function bodies, mutable state definitions, or general-purpose
inline implementations. Production sources do not textually include `.c`
files or include test-private headers.

Existing TTY, scratch, freeze, and teardown plan types remain in
`user_address_space_internal.h`. New private declarations are added only
when at least two accepted owner modules need them. File-local helpers remain
`static`.

The decomposition retains exactly one pair of bounded 32-KiB
reachability/release bitmaps and one checked busy flag. The runtime module is
their sole owner. Successful acquisition returns an explicit scratch lease
that is passed to complete validation and release operations. Acquisition
fails with the existing busy result when the single lease is held. No module
may:

- define another copy of either bitmap or busy state;
- cache process, root, table, frame-owner, TTY, VM, or execution authority in
  new mutable globals;
- reacquire the same lease through a nested operation; or
- restore supervisor interrupts before the lease is released or the
  associated prevalidated commit completes.

The current test-only fail-after-allocation control remains test-only and
keeps one authoritative owner.

### Dependency direction

The concrete module graph is acyclic:

1. Runtime acquisition, low-level table primitives, portable owner
   classification, and portable VM planning are leaves.
2. Complete user-root validation depends only on those leaves and read-only
   TTY/VM authority snapshots.
3. Mapping lookup and inventory depend on validation and table primitives.
4. Root lifecycle, bootstrap mutation, activation, and the exact TTY,
   scratch, freeze, and teardown modules depend downward on validation and
   mapping.
5. Wired handoff is a high-level address-space orchestrator that depends on
   low-level address-space validation/mapping and public execution
   validation.
6. Execution validation depends on public address-space
   validation/lookup/translation but never on wired-handoff orchestration.
7. Execution lifecycle and trap mutation depend on execution validation.

Lower-level validation, table, mapping, and execution-validation modules
never call operation handlers above them. This ordering permits
`user_address_space_handoff.c` to validate both families without creating
the current conceptual bidirectional ownership.

### Transaction and failure boundaries

Source extraction must preserve every existing phase boundary:

- read-only preflight validates all fallible authority, state, range,
  generation, mapping, capacity, and PTE conditions;
- preparation acquires every resource needed by the operation;
- commit publishes only the prevalidated state and is non-failing where the
  current contract requires it;
- rollback releases resources in the current reverse order and never masks
  the initiating error;
- verification occurs only where the current contract already performs it;
  allocation and unmapping add no new fallible post-commit check; and
- publication updates authoritative owner, root, context, or activation
  state only at the current final boundary.

Splitting a function must not reorder checks, change the first returned
error, write caller outputs earlier, widen a valid state, or add a
success-shaped fallback.

### Test owners

Portable host evidence is split into:

```text
tests/host/user_address_space_core_test.c
tests/host/user_address_space_owner_test.c
tests/host/user_address_space_vm_plan_test.c
tests/host/user_address_space_core_test_suites.h
```

The existing `micros_user_address_space_core_test_run()` remains the sole
public suite entry and preserves the exact current test order.

The user-address-space QEMU test is split into:

```text
kernel/user_address_space_test.c
kernel/user_address_space_test_fixture.c
kernel/user_address_space_test_lifecycle.c
kernel/user_address_space_test_mapping.c
kernel/user_address_space_test_validation.c
kernel/user_address_space_test_internal.h
```

The user-execution QEMU test is split into:

```text
kernel/user_execution_test.c
kernel/user_execution_test_fixture.c
kernel/user_execution_test_prepare.c
kernel/user_execution_test_trap.c
kernel/user_execution_test_lifecycle.c
kernel/user_execution_test_internal.h
```

The user-address-space entry owns scenario order and its Boolean result;
`kernel/main.c` retains the current marker and shutdown publication. The
user-execution entry owns scenario order and its current final marker and SBI
shutdown. Private fixture context is passed explicitly. Assembly entry
points, trap hooks, current short-circuit order, assertions, and exact pass
markers remain unchanged.

### Stable evidence identities

The implementation preserves:

- native executable `endpoint_test`;
- CTest `endpoint-profile`;
- `test-qemu-user-address-space`;
- `test-qemu-address-space-handoff`;
- `test-qemu-user-execution`;
- every current related QEMU marker and timeout;
- the existing RISC-V assembly test entry points; and
- the current CI and validation-plan workflow identities.

No new executable, CTest name, custom target, workflow, marker, timeout, or
duplicate test run is introduced merely because a source is split.

### Build ownership

`CMakeLists.txt` defines exact ordered reusable source sets for:

- kernel-root address-space production sources;
- portable user-address-space core sources;
- target user-address-space production sources;
- user-execution production sources;
- portable host address-space tests;
- QEMU user-address-space tests; and
- QEMU user-execution tests.

Each existing consumer expands the appropriate set once. No split source is
listed directly in parallel with its source set, and no existing target is
renamed.

### Structural Red gate

Implementation begins with:

```text
tests/host/test_kernel_address_space_source_layout.py
```

The test initially fails against the accepted design, then the first green
implementation slice adds the exact target paths, declarations-only private
headers, ordered CMake source sets, and minimal empty translation units while
runtime behavior remains in the current owner files.

The gate verifies:

- exact production/test path existence and ordered CMake membership;
- one expansion of each source set by every accepted consumer;
- no textual C inclusion and no production inclusion of test-private
  headers;
- declarations-only private headers;
- stable public-symbol ownership;
- no unintended CTest, custom-target, marker, or workflow identity;
- fail-closed validation-plan classification for every new path; and
- an exact 19-entry public/test symbol-owner migration map plus an exact
  allowlist containing the 23 current over-limit functions.

Both migration collections may only shrink. While a symbol remains in the
owner map, the gate requires it to stay in its recorded current owner; after
removal, the gate requires its final accepted owner. A function cannot evade
the long-function gate by renaming or moving unchanged. The final
implementation tree must have both collections empty. The line signal
remains a review trigger, not the reason for a split; every extracted helper
must represent a named cohesive task or semantic phase.

### Implementation order

The implementation proceeds as small green commits:

1. establish the structural Red gate, target path skeleton, source sets, and
   complete symbol-owner map and long-function allowlist;
2. split portable owner classification, VM planning, and native tests;
3. split kernel-root validation, runtime/scratch ownership, and low-level
   table primitives;
4. split complete validation, mapping lookup/inventory, root lifecycle, and
   bootstrap mutation;
5. split wired handoff, TTY, VM scratch, freeze, teardown, and activation;
6. split execution lifecycle, validation, and trap mutation; and
7. split the QEMU address-space and execution evidence, empty the migration
   allowlist, and run the authoritative PR validation plan once.

Each intermediate commit is behaviorally green. Narrow existing evidence is
run for the affected slice. The final
`tools/validation_plan.py --tier pr --base <review-base> --execute` selection
is authoritative and its selected workflows are not rerun separately.

## Consequences

### Positive

- Source names and function structure expose the accepted runtime tasks.
- Complete validation and shared scratch ownership become reviewable
  invariants rather than hidden monolith details.
- Special mapping exceptions remain isolated from ordinary mapping
  lifecycle.
- Wired handoff no longer creates a conceptual low-level cycle between
  address-space and execution ownership.
- Portable owner/planning evidence can remain fast while target-only
  mechanisms retain QEMU coverage.
- Future changes can select the narrow owner and gate without broadening
  public interfaces or inventing another test identity.

### Costs

- CMake and validation-plan path classifications become more explicit.
- Several private context and lease declarations are required.
- Extraction must carefully preserve check order, output timing, rollback,
  interrupt state, and QEMU scenario sequence.
- The first implementation commit contains deliberate source-layout
  scaffolding so later responsibility moves remain small and structurally
  checked.

### Risks and mitigations

- **State duplication:** the structural gate and private contract permit one
  scratch owner and explicit context passing only.
- **Dependency cycle:** the accepted graph places wired handoff above both
  validation families and forbids lower modules from calling it.
- **Partial-publication regression:** semantic preflight/prepare/commit/
  rollback helpers preserve the existing non-failing commit boundaries.
- **Coverage dilution:** stable test identities, markers, ordering, and
  validation-plan equality are structural requirements.
- **Mechanical splitting:** every current over-limit function has a
  required phase disposition; arbitrary line-count helpers do not satisfy
  the design.

## Alternatives considered

### Leave the sources intact until the cross-service coordinator

Rejected. The audit makes this correction an explicit dependency before the
coordinator grows the same files further.

### Split only the longest functions

Rejected. Function extraction alone would leave root lifecycle, mapping,
device exceptions, VM transactions, execution validation, and test
orchestration with ambiguous source ownership.

### Create one source file per public function

Rejected. Public entry points are not necessarily cohesive tasks. That shape
would over-fragment shared validation and lifecycle work and replace one
monolith with an unreviewable web of tiny owners.

### Keep wired handoff in the low-level validation module

Rejected. Wired handoff coordinates address-space and execution validation.
Putting it below either family creates a concrete or conceptual dependency
cycle.

### Mirror the MINIX VM file hierarchy exactly

Rejected. MINIX's dynamic regions, page-fault service, direct VM page-table
management, copy-on-write, broad private header, and x86/ARM architecture
details are not the `micros` v0.1 contract.

### Change address-space behavior while moving code

Rejected. The accepted behavior has exact ownership, generation, error,
transaction, and evidence contracts. Behavioral changes require their own
reviewed design and regression test.

### Add new tests or workflows for every new module

Rejected. Existing tests exercise behavior, not filenames. Source splitting
must preserve and reuse the current evidence identities without duplicate
execution.

## Non-goals

This ADR does not authorize:

- public API, ABI, enum, structure, wire, or syscall changes;
- new address-space or execution behavior;
- general page faults, `fork`, copy-on-write, `mmap`, file-backed paging,
  ASIDs, SMP, shootdown, migration, recovery, or compatibility machinery;
- a new allocator, frame-owner model, root representation, or policy owner;
- decomposition of PM, VM, frame ownership, scheduler, trap, or IPC sources
  outside the named family;
- a repository-wide source framework;
- extra test executions, workflows, or CI jobs; or
- function splitting by line count instead of task cohesion.

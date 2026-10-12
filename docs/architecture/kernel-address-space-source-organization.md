# Kernel Address-Space Source Organization

This document defines the exact behavior-preserving source decomposition for
the kernel address-space and user-execution family. It applies ADR-0051 to
the second correction identified by the source-module audit.

Implementation must not begin until
[ADR-0053](../adr/0053-kernel-address-space-source-decomposition.md) is
Accepted and its design pull request is merged.

## Reviewed base

The reviewed source base is:

```text
40e5e01e8fa7e23d661967e4c15908f76485100f
```

The fixed local MINIX baseline and the required `micros` adaptations are
recorded in
[the research note](../research/minix-kernel-address-space-source-organization.md).

## Current inventory

The audit signal covers seven files:

| File | Physical lines | Functions | Longest function |
| --- | ---: | ---: | --- |
| `kernel/user_address_space.c` | 4,190 | 80 | `validate_locked`, 294 |
| `kernel/address_space.c` | 998 | 19 | `tree_and_table_ownership_are_valid`, 195 |
| `kernel/user_address_space_core.c` | 453 | 11 | `micros_user_address_space_prepare_vm_table_plan`, 191 |
| `kernel/user_execution.c` | 915 | 25 | `micros_user_execution_preflight_post_handoff`, 154 |
| `kernel/user_address_space_test.c` | 1,930 | 24 | `micros_user_address_space_runtime_run_self_test`, 861 |
| `kernel/user_execution_test.c` | 1,223 | 20 | `micros_user_execution_runtime_run_self_test`, 342 |
| `tests/host/user_address_space_core_test.c` | 589 | 5 | `micros_user_address_space_core_test_run`, 229 |
| **Total** | **10,298** | **184** | **23 functions above 120 lines** |

The physical-line and function-size values are review signals. They do not
define module or function boundaries. The boundaries below follow cohesive
runtime tasks, authority ownership, transaction phases, and evidence
scenarios.

## Behavioral baseline

The decomposition preserves all Accepted behavior in ADR-0021, ADR-0022,
ADR-0040, ADR-0045, and ADR-0050.

### Kernel-root invariants

- Kernel initialization still requires supervisor interrupts to be disabled.
- The kernel root still contains exactly the accepted text, read-only data,
  data/BSS, stack, device, direct-map, and trampoline mappings.
- Linker-layout, physical-range, capacity, PTE-conflict, allocation, and
  `satp` errors keep their current precedence.
- Every allocated kernel page-table frame is reachable exactly once through
  the accepted tree, has the current owner, and is represented by the
  current report.
- Initialization publishes the report and active root only after complete
  construction and validation.

### Private user-root invariants

- A process handle is generation-checked before its root or mappings are
  trusted.
- Bootstrap roots contain the current private user subtree and exact shared
  kernel entries.
- User virtual-range, alignment, permission, owner, PTE, and active-root
  restrictions remain unchanged.
- Complete validation proves the equality of reachable private table/user
  frames and authoritative process-owned frames, with only the accepted TTY
  and VM scratch exceptions.
- Page-table frames retain `PROCESS_PAGE_TABLE` ownership. User leaves retain
  the phase-dependent `PROCESS_USER` or `VM_WIRED` ownership.
- The exact active VM scratch leaf may reference one `VM_TRANSFERABLE` frame
  only while the matching load-token authority is active.
- Root creation, bootstrap page allocation/release, TTY publication, scratch
  mutation, VM freeze, VM teardown, and activation preserve their current
  error and publication boundaries.
- Caller outputs remain unchanged on every failure where they are unchanged
  today.

### Handoff and VM transaction invariants

- Wired handoff validates both address-space and execution state before
  converting frame ownership or revoking bootstrap-only operations.
- TTY publication remains an exact one-shot UART leaf with no general device
  mapping authority.
- VM scratch remains one exact token-bound mapping in the active VM root.
- Freeze validates and prepares every fallible input before constructing and
  installing the accepted four-table application root.
- Teardown validates the complete root, table set, data set, mapping list,
  process generation, and active-root constraints before its non-failing
  commit.
- No ordinary post-handoff map, unmap, allocation, or root-construction
  authority is added.

### Execution invariants

- Process, thread, address-space, execution binding, kernel stack, and hart
  remain distinct objects.
- Each thread keeps the same statically assigned kernel-stack bounds.
- User status sanitization, entry alignment, executable mapping, stack
  alignment/range, writable stack mapping, and context ownership checks keep
  their current order and result.
- Trap capture writes only the current thread's stored context.
- Return validation rejects the same status, mapping, alignment, and
  ownership mutations before installing the return frame.
- Post-handoff preflight writes no binding output until all checks succeed;
  its commit remains prevalidated and non-failing.

### Evidence invariants

- Native executable `endpoint_test` and CTest `endpoint-profile` remain the
  portable evidence identity.
- QEMU targets `test-qemu-user-address-space`,
  `test-qemu-address-space-handoff`, and
  `test-qemu-user-execution` retain their current assembly entry points,
  scenario ordering, short-circuit behavior, markers, timeouts, and
  shutdown results.
- No new executable, CTest, custom target, workflow, seed, marker, or timeout
  is introduced for source organization.

## Production source map

### Kernel root

| Path | Owned responsibility |
| --- | --- |
| `kernel/address_space.c` | Build the kernel root, install accepted mappings, initialize `satp`, and publish `micros_kernel_address_space_report` |
| `kernel/address_space_validation.c` | Validate allocator state, table ownership, expected mappings, tree reachability, and exact table-set equality |
| `kernel/address_space_internal.h` | Declare private kernel-root state and validation contracts shared only by those sources |

`address_space.c` owns construction and publication. It passes an explicit
read-only view of the candidate or installed root to
`address_space_validation.c`; the validator does not recover construction
state from a second global.

### Portable address-space core

| Path | Owned responsibility |
| --- | --- |
| `kernel/user_address_space_core.c` | Map ownership phase to accepted leaf owner, validate typed frame owners, and classify ordinary versus exact VM scratch leaves |
| `kernel/user_address_space_vm_plan.c` | Validate and construct the deterministic four-table VM application plan in a local candidate |

Both sources remain target-independent C and use only public portable types.
They do not acquire runtime registries, touch direct-mapped physical memory,
read CSRs, or mutate authoritative state.

### Target user-address-space mechanism

| Path | Owned responsibility |
| --- | --- |
| `kernel/user_address_space_runtime.c` | Acquire authoritative runtime/process state, translate subsystem errors, serialize interrupt-sensitive operations, and own the single shared scratch lease |
| `kernel/user_address_space_table.c` | Convert frame indices and physical addresses, access direct-mapped tables, decode/build PTEs, compute VPN slots, and operate bounded bitmaps |
| `kernel/user_address_space_validation.c` | Perform complete private-root validation, exact shared-kernel equality, owner/reachability set equality, and accepted TTY/scratch exception checks |
| `kernel/user_address_space_mapping.c` | Locate slots, walk leaves, look up page mappings, translate arbitrary addresses, and inventory mappings in current order |
| `kernel/user_address_space.c` | Create and destroy private process roots and publish/clear the process root at the current boundary |
| `kernel/user_address_space_mutation.c` | Allocate and release bootstrap user pages, including subordinate-table preparation, rollback, pruning, zeroing, and output publication |
| `kernel/user_address_space_handoff.c` | Preflight and complete the wired handoff across address-space, frame-owner, process, and execution state |
| `kernel/user_address_space_tty.c` | Prepare, commit, and validate the exact TTY UART mapping |
| `kernel/user_address_space_vm_scratch.c` | Prepare and commit exact VM scratch mapping and removal |
| `kernel/user_address_space_vm_freeze.c` | Preflight VM freeze, construct the prevalidated root, and install it |
| `kernel/user_address_space_vm_teardown.c` | Preflight complete VM teardown and perform its prevalidated non-failing commit |
| `kernel/user_address_space_activation.c` | Validate and activate a process root or restore the kernel root |
| `kernel/user_address_space_internal.h` | Declare the sole private contract shared by target user-address-space modules |

### User execution

| Path | Owned responsibility |
| --- | --- |
| `kernel/user_execution.c` | Prepare execution state, own kernel-stack lifecycle, inspect/detach stored contexts, commit post-handoff bindings, and expose test-only stack bounds |
| `kernel/user_execution_validation.c` | Validate runtime state, process/thread relation, user status, entry/stack mappings, stored context, trap return, and binding candidates |
| `kernel/user_execution_trap.c` | Capture trap state, store an accepted context, and install a validated return frame |
| `kernel/user_execution_internal.h` | Declare private execution-family types and helper contracts |

## Stable public-symbol ownership

The structural gate binds each public symbol to one accepted owner.

| Owner | Public symbols |
| --- | --- |
| `address_space.c` | `micros_kernel_address_space_initialize`, `micros_kernel_address_space_report` |
| `user_address_space.c` | `micros_user_address_space_create`, `micros_user_address_space_destroy` |
| `user_address_space_mutation.c` | `micros_user_address_space_allocate_page`, `micros_user_address_space_release_page`, `micros_user_address_space_test_fail_after_allocations` |
| `user_address_space_mapping.c` | `micros_user_address_space_lookup`, `micros_user_address_space_translate`, `micros_user_address_space_inventory` |
| `user_address_space_handoff.c` | `micros_user_address_space_prepare_wired_handoff`, `micros_user_address_space_complete_wired_handoff` |
| `user_address_space_activation.c` | `micros_user_address_space_activate`, `micros_user_address_space_activate_kernel` |
| `user_address_space_validation.c` | `micros_user_address_space_validate` |
| `user_address_space_core.c` | `micros_user_address_space_leaf_owner_kind`, `micros_user_address_space_validate_frame_owner`, `micros_user_address_space_classify_managed_leaf` |
| `user_address_space_vm_plan.c` | `micros_user_address_space_prepare_vm_table_plan` |
| `user_execution.c` | `micros_user_execution_prepare`, `micros_user_execution_preflight_post_handoff`, `micros_user_execution_commit_post_handoff_prevalidated`, `micros_user_execution_inspect`, `micros_user_execution_detach`, `micros_user_execution_test_stack_bounds` |
| `user_execution_validation.c` | `micros_user_execution_validate_return`, `micros_user_execution_validate_context` |
| `user_execution_trap.c` | `micros_user_execution_capture_trap`, `micros_user_execution_store_context`, `micros_user_execution_install_return_frame` |

Test-only public symbols retain the guards and declarations in the existing
public headers. Their source owners are defined in the test map below.

Stable target-test symbol ownership is:

| Owner | Test symbols |
| --- | --- |
| `kernel/mmu_test.c` | Existing kernel-MMU self-test and trap-hook symbols; unchanged and outside this correction |
| `kernel/user_address_space_test.c` | `micros_user_address_space_runtime_run_self_test`, `micros_user_address_space_handle_test_trap` |
| `kernel/user_execution_test.c` | `micros_user_execution_runtime_run_self_test`, `micros_user_execution_test_enter_production` |
| `kernel/user_execution_test_trap.c` | `micros_user_execution_test_pre_capture`, `micros_user_execution_handle_test_trap` |

The existing RISC-V assembly symbols remain in their current assembly
sources.

## Private state and scratch ownership

### Runtime view

`user_address_space_runtime.c` constructs an explicit operation view from
the existing authoritative registries. The view may carry:

- the resolved process and generation-checked handle;
- kernel object, frame-owner, allocator, TTY, and VM authority pointers;
- the current ownership phase;
- the process root and kernel-root report;
- the saved interrupt state; and
- an optional active scratch lease.

It does not copy authoritative structures or become a second policy owner.
Callers request only the data needed by their operation.

### Scratch lease

The complete validator and release paths retain one existing bounded pair of
32-KiB bitmaps:

```text
reachable frame set
release/owned frame set
```

`user_address_space_runtime.c` is their sole definition owner together with
the one busy flag. Acquisition:

1. requires the current operation's interrupt precondition;
2. rejects a held lease with the existing busy error;
3. marks the lease held;
4. clears only the range required by the current managed-frame count; and
5. returns an explicit private lease containing the two bitmap views and
   bounded lengths.

Every path releases the lease exactly once. No helper reacquires it. For an
operation whose prevalidated commit must remain in the same critical
section, SIE remains clear through commit and lease release. The owner
restores the saved interrupt state only after the full operation boundary.

No new module-global copy of process state, root state, table state,
frame-owner state, execution state, or test context is permitted.

### Private headers

The three production-private headers contain:

- shared private structure declarations;
- opaque or explicit scratch/runtime views where cross-module transfer is
  required;
- existing TTY, scratch, freeze, and teardown plan structures;
- declarations used by at least two accepted owner modules; and
- no function bodies or mutable definitions.

File-local helpers remain `static`. A declaration used by only one source
does not move to a private header.

## Dependency graph

The following are the only permitted internal edges. Every line uses
`dependent -> dependency`; an implementation need not introduce an unused
permitted edge, and it may not introduce an edge in the opposite direction:

```text
address_space -> address_space_validation

user_address_space_validation -> user_address_space_runtime
user_address_space_validation -> user_address_space_table
user_address_space_validation -> user_address_space_core
user_address_space_validation -> read-only TTY/VM authority snapshots

user_address_space_mapping -> user_address_space_runtime
user_address_space_mapping -> user_address_space_table
user_address_space_mapping -> user_address_space_validation

user_address_space -> user_address_space_runtime
user_address_space -> user_address_space_table
user_address_space -> user_address_space_validation

user_address_space_mutation -> user_address_space_runtime
user_address_space_mutation -> user_address_space_table
user_address_space_mutation -> user_address_space_validation
user_address_space_mutation -> user_address_space_mapping

user_address_space_tty -> user_address_space_runtime
user_address_space_tty -> user_address_space_table
user_address_space_tty -> user_address_space_validation
user_address_space_tty -> user_address_space_mapping

user_address_space_vm_scratch -> user_address_space_runtime
user_address_space_vm_scratch -> user_address_space_table
user_address_space_vm_scratch -> user_address_space_validation
user_address_space_vm_scratch -> user_address_space_mapping

user_address_space_vm_freeze -> user_address_space_runtime
user_address_space_vm_freeze -> user_address_space_table
user_address_space_vm_freeze -> user_address_space_validation
user_address_space_vm_freeze -> user_address_space_vm_plan

user_address_space_vm_teardown -> user_address_space_runtime
user_address_space_vm_teardown -> user_address_space_table
user_address_space_vm_teardown -> user_address_space_validation
user_address_space_vm_teardown -> user_address_space_mapping

user_address_space_activation -> user_address_space_runtime
user_address_space_activation -> user_address_space_table
user_address_space_activation -> user_address_space_validation

user_address_space_handoff -> user_address_space_runtime
user_address_space_handoff -> user_address_space_validation
user_address_space_handoff -> user_execution_validation

user_execution_validation
    -> public user-address-space validation/lookup/translation
user_execution -> user_execution_validation
user_execution_trap -> user_execution_validation
```

Runtime acquisition, table primitives, portable owner classification, and
portable VM planning are low-level leaves relative to this family. The edge
list is constrained further as follows:

- validation may read TTY and VM authority snapshots but cannot call their
  operation handlers;
- mapping depends on complete validation and table primitives;
- root lifecycle and mutation depend downward on runtime, table, validation,
  and mapping helpers as needed;
- exact TTY, scratch, freeze, teardown, and activation modules cannot call
  wired-handoff orchestration;
- wired handoff may call public execution validation;
- execution validation may call public address-space validation, lookup, or
  translation;
- execution validation cannot call wired handoff; and
- execution lifecycle and trap mutation cannot become address-space policy
  owners.

This is acyclic because the cross-family path is:

```text
user_address_space_handoff
    -> user_execution_validation
    -> user_address_space_validation/mapping
```

No edge returns from the low-level address-space modules to handoff.

## Transaction shape

Every mutating owner exposes its existing semantic phases in named helpers.
The common shape is:

1. **Acquire:** resolve generation-safe authority and enter the current
   interrupt/scratch critical section.
2. **Preflight:** validate every argument, phase, state, owner, mapping,
   conflict, capacity, and active-root restriction in existing order.
3. **Prepare:** allocate frames or construct a local plan without
   publication.
4. **Commit:** write only the prevalidated PTE, owner, root, context, or
   activation state.
5. **Rollback:** before the first non-failing committed mutation, undo
   prepared resources in current reverse order and return the initiating
   error.
6. **Verify:** run only an invariant check that the current contract already
   performs at that boundary.
7. **Publish:** update caller outputs or externally visible authoritative
   state only at the current final boundary.
8. **Release:** release the scratch/runtime lease and restore the saved
   interrupt state.

Not every operation needs all eight helpers. A helper is extracted only when
the phase is a cohesive subtask. This list prevents a split from hiding a
fallible check inside a nominally non-failing commit.

Verification is not a universal post-commit phase. Bootstrap allocation and
unmapping complete all fallible validation before the first mutation and add
no recoverable check after commit. An impossible post-preflight condition
retains the current panic behavior rather than returning an error after
partial mutation.

## Test source map

### Portable host evidence

| Path | Owned evidence |
| --- | --- |
| `tests/host/user_address_space_core_test.c` | Sole public runner and exact suite ordering |
| `tests/host/user_address_space_owner_test.c` | Ownership-phase, typed-owner, and exact VM scratch classification cases |
| `tests/host/user_address_space_vm_plan_test.c` | Deterministic VM table-plan success, rejection, and output-preservation cases |
| `tests/host/user_address_space_core_test_suites.h` | Private declarations shared by the host test sources |

`micros_user_address_space_core_test_run()` calls the owner suite and then
the VM-plan suite in the current effective order. The split does not create
another executable or CTest.

### QEMU user-address-space evidence

| Path | Owned evidence |
| --- | --- |
| `kernel/user_address_space_test.c` | Public runtime self-test entry, exact top-level scenario order, SUM trap hook, pass/fail result, and return |
| `kernel/user_address_space_test_fixture.c` | Sole test context, byte helpers, snapshots, table/root resolution, owner checks, and cleanup support |
| `kernel/user_address_space_test_lifecycle.c` | Root create/destroy, last-page pruning, active-root restrictions, generation reuse, stale-handle, and handoff-revocation scenarios |
| `kernel/user_address_space_test_mapping.c` | Permission/range rejection, allocation rollback, lookup/translation, IPC-buffer boundary, reuse/zeroing, and activation/SUM scenarios |
| `kernel/user_address_space_test_validation.c` | Kernel-root equality and corruption-rejection scenarios |
| `kernel/user_address_space_test_internal.h` | Private context and suite declarations |

The top-level runner preserves this effective sequence:

1. acquire the runtime fixture and baseline counts;
2. create two process roots and prove kernel-root equality;
3. reject invalid permissions, injected allocation failures, and invalid
   ranges without mutation or output writes;
4. prove last-page allocation/release and subordinate-table pruning;
5. prove process isolation, lookup, translation, IPC-buffer cross-page
   behavior, permissions, and output preservation;
6. prove active-root switching, SUM isolation, and kernel-root restoration;
7. prove release/reuse zeroing and reject owner, PTE, alias, and reachability
   corruption;
8. reject destroy while a thread is live, then destroy/reuse the slot and
   reject every stale operation;
9. clean bootstrap state, complete frame-ownership handoff, and prove
   bootstrap-only operations are revoked while kernel activation remains
   valid; and
10. release the final process, prove baseline counts and complete runtime
    validity, restore interrupt state, and return the current Boolean result.

The assembly SUM-fault symbols and exact trap-state transition remain
unchanged.

### QEMU user-execution evidence

| Path | Owned evidence |
| --- | --- |
| `kernel/user_execution_test.c` | Public runtime self-test entry, exact setup sequence, final pass/fail marker and SBI shutdown, target-private production-entry bridge, and sole top-level orchestration |
| `kernel/user_execution_test_fixture.c` | Sole test context, byte/context/stack helpers, snapshots, runtime access, and private context accessor |
| `kernel/user_execution_test_prepare.c` | Process/address/thread fixture creation, payload installation, invalid preparation matrix, valid preparation, and stack isolation |
| `kernel/user_execution_test_trap.c` | Public pre-capture hook, trap-state dispatch, invalid-return matrix, user-return mutation, and supervisor-return preparation |
| `kernel/user_execution_test_lifecycle.c` | Post-return inspection, scheduler removal, detach/release, stale-thread reuse, resource cleanup, baseline verification, and Boolean completion result |
| `kernel/user_execution_test_internal.h` | Private context, suite, trap-state, and fixture declarations |

The callback signatures in `include/micros/user_execution.h` cannot carry a
test-context argument. `user_execution_test_fixture.c` therefore owns one
bounded private test context and returns its address through a test-private
accessor. All ordinary suite calls also receive that pointer explicitly. No
other test source defines parallel mutable state.

The top-level runner preserves this effective sequence:

1. acquire runtime authority and baseline owner/free counts;
2. create two processes, roots, code/stack pages, and threads;
3. verify exact static kernel-stack bounds and non-overlap;
4. install the existing payload in both code frames and execute `fence.i`;
5. construct the initial context and patterned stacks;
6. reject invalid status, entry, and stack contexts with byte-exact state
   preservation;
7. prepare both threads, prove only the selected stack is zeroed, and reject
   duplicate preparation;
8. enter the production path and preserve the current kernel-fault,
   first-ecall, second-ecall, and supervisor-return state machine;
9. reject the current invalid return-context matrix in the same order;
10. inspect the final context, detach/release the thread, reject stale
    operations after generation reuse, clean both processes, and prove
    baseline ownership; and
11. return the lifecycle result to the entry owner, which prints the exact
    current marker and requests the same SBI shutdown result, or prints the
    current failure markers on the same paths.

## Current long-function disposition

The initial structural migration allowlist contains exactly the following 23
functions. No function is accepted as an atomic exception. Each row defines
the semantic tasks that must become reviewable before its allowlist entry is
removed.

| Current path and function | Lines | Required disposition |
| --- | ---: | --- |
| `kernel/address_space.c`: `tree_and_table_ownership_are_valid` | 195 | In `address_space_validation.c`, separate allocator/table candidate checks, expected kernel mapping walk, reachable-table collection, owner validation, exact set comparison, and aggregate result |
| `kernel/user_address_space_core.c`: `micros_user_address_space_prepare_vm_table_plan` | 191 | In `user_address_space_vm_plan.c`, separate argument/count validation, unique table-frame validation, fixed index derivation, image/stack mapping classification, local PTE construction, conflict checks, and final plan publication |
| `kernel/user_address_space.c`: `validate_locked` | 294 | In `user_address_space_validation.c`, separate runtime/root preconditions, shared-kernel equality, private-tree walk, table-owner validation, managed-leaf classification, exact TTY/scratch exception checks, reachable/owned set comparison, and final result |
| `kernel/user_address_space.c`: `visit_user_mappings_locked` | 165 | In `user_address_space_mapping.c`, separate validated root acquisition, ordered root/middle/leaf traversal, leaf classification, visitor invocation, count accumulation, and output publication |
| `kernel/user_address_space.c`: `micros_user_address_space_allocate_page` | 225 | In `user_address_space_mutation.c`, separate complete preflight/slot location, data/table preparation, zeroing/PTE construction, ordered commit, precommit reverse rollback, and physical-output publication, with no new fallible post-commit check |
| `kernel/user_address_space.c`: `micros_user_address_space_release_page` | 156 | In `user_address_space_mutation.c`, separate complete validated lookup and release-set/prune-chain preparation, atomic frame-owner/allocator release, prevalidated non-failing leaf/parent clearing and empty-table pruning, then physical-output publication, with no recoverable post-commit error |
| `kernel/user_address_space.c`: `micros_user_address_space_prepare_tty_uart_mapping` | 179 | In `user_address_space_tty.c`, separate argument/phase/authority checks, exact root/slot validation, current-leaf conflict checks, local plan construction, and success-only publication |
| `kernel/user_address_space.c`: `micros_user_address_space_prepare_vm_scratch_mapping` | 151 | In `user_address_space_vm_scratch.c`, separate token/process/root/frame authority checks, active-root and exact-slot validation, PTE construction, and success-only plan publication |
| `kernel/user_address_space.c`: `micros_user_address_space_prepare_vm_scratch_removal` | 142 | In `user_address_space_vm_scratch.c`, separate token/process/root/frame matching, exact installed-leaf validation, local removal-plan construction, and success-only publication |
| `kernel/user_address_space.c`: `micros_user_address_space_prepare_vm_freeze` | 147 | In `user_address_space_vm_freeze.c`, separate action/process/phase validation, mapping/frame-set validation, portable table-plan construction, owner preflight, and success-only freeze-plan publication |
| `kernel/user_address_space.c`: `micros_user_address_space_prepare_vm_teardown` | 170 | In `user_address_space_vm_teardown.c`, separate argument/phase/process/root checks, table/data-set validation, complete mapping and ownership validation, active-root restriction, and success-only teardown-plan publication |
| `kernel/user_execution.c`: `micros_user_execution_preflight_post_handoff` | 154 | In `user_execution.c` with validation helpers from `user_execution_validation.c`, separate argument/runtime/thread/process validation, address-space/context/mapping validation, stack-bound derivation, local binding construction, and success-only publication |
| `kernel/user_execution.c`: `micros_user_execution_prepare` | 125 | In `user_execution.c`, separate thread/runtime acquisition, current-state rejection, context sanitization and mapping validation, stack clearing, stored-context preparation, and final publication |
| `kernel/user_address_space_test.c`: `post_handoff_operations_are_revoked` | 132 | In `user_address_space_test_lifecycle.c`, separate create/translate/destroy/allocate/release/lookup/validate/activate rejection cases, each retaining snapshot, output, and `satp` checks, followed by kernel activation |
| `kernel/user_address_space_test.c`: `stale_operations_are_rejected` | 131 | In `user_address_space_test_lifecycle.c`, separate stale create/destroy/allocate/release/lookup/validate/activate cases, each retaining the live process snapshot, caller outputs, and `satp` |
| `kernel/user_address_space_test.c`: `micros_user_address_space_runtime_run_self_test` | 861 | Keep only top-level ordered orchestration in `user_address_space_test.c`; move fixture, lifecycle, mapping/IPC/activation, validation/corruption, handoff-revocation, cleanup, and result phases to their accepted owners |
| `kernel/user_execution_test.c`: `micros_user_execution_test_pre_capture` | 121 | In `user_execution_test_trap.c`, separate argument/runtime acquisition, current thread/hart validation, trap-stack validation, active-root validation, state-specific expected context selection, and frame comparison |
| `kernel/user_execution_test.c`: `micros_user_execution_handle_test_trap` | 209 | In `user_execution_test_trap.c`, separate state dispatch, kernel-fault handling, first-ecall origin validation, invalid-return matrix, user-return mutation, second-ecall validation, and supervisor-return preparation |
| `kernel/user_execution_test.c`: `micros_user_execution_test_finish` | 137 | Replace it with lifecycle helpers in `user_execution_test_lifecycle.c` that separate final context verification, scheduler/detach/release, generation-reuse stale matrix, second-thread/process cleanup, and baseline verification, then return a Boolean result for entry-owned marker/shutdown publication |
| `kernel/user_execution_test.c`: `micros_user_execution_runtime_run_self_test` | 342 | Keep top-level orchestration in `user_execution_test.c`; move fixture creation, payload/context setup, invalid-prepare cases, valid preparation/stack isolation, production entry, and failure shutdown to accepted test owners |
| `tests/host/user_address_space_core_test.c`: `test_classifies_exact_vm_scratch_leaf` | 129 | In `user_address_space_owner_test.c`, separate exact scratch success, root mismatch, permission mismatch, owner mismatch, and ordinary wired-leaf classification cases |
| `tests/host/user_address_space_core_test.c`: `test_prepares_exact_vm_table_plan` | 160 | In `user_address_space_vm_plan_test.c`, separate mapping fixture construction, successful plan shape, permission rejection with output preservation, and table/leaf alias ownership rejection |
| `tests/host/user_address_space_core_test.c`: `micros_user_address_space_core_test_run` | 229 | Keep only exact suite ordering in the runner; move phase-to-owner and typed-owner validation matrices to the owner suite, then call the VM-plan suite |

If extraction reveals a current measurement error, the design must be
corrected before implementation changes the allowlist. A renamed or moved
function with the same oversized body remains the same migration item.

## Exact CMake source sets

The final ordered sets are:

```cmake
set(MICROS_KERNEL_ADDRESS_SPACE_SOURCES
    kernel/address_space.c
    kernel/address_space_validation.c
)

set(MICROS_USER_ADDRESS_SPACE_CORE_SOURCES
    kernel/user_address_space_core.c
    kernel/user_address_space_vm_plan.c
)

set(MICROS_USER_ADDRESS_SPACE_SOURCES
    kernel/user_address_space.c
    kernel/user_address_space_runtime.c
    kernel/user_address_space_table.c
    kernel/user_address_space_validation.c
    kernel/user_address_space_mapping.c
    kernel/user_address_space_mutation.c
    kernel/user_address_space_handoff.c
    kernel/user_address_space_tty.c
    kernel/user_address_space_vm_scratch.c
    kernel/user_address_space_vm_freeze.c
    kernel/user_address_space_vm_teardown.c
    kernel/user_address_space_activation.c
)

set(MICROS_USER_EXECUTION_SOURCES
    kernel/user_execution.c
    kernel/user_execution_validation.c
    kernel/user_execution_trap.c
)

set(MICROS_USER_ADDRESS_SPACE_HOST_TEST_SOURCES
    tests/host/user_address_space_core_test.c
    tests/host/user_address_space_owner_test.c
    tests/host/user_address_space_vm_plan_test.c
)

set(MICROS_USER_ADDRESS_SPACE_QEMU_TEST_SOURCES
    kernel/user_address_space_test.c
    kernel/user_address_space_test_fixture.c
    kernel/user_address_space_test_lifecycle.c
    kernel/user_address_space_test_mapping.c
    kernel/user_address_space_test_validation.c
)

set(MICROS_USER_EXECUTION_QEMU_TEST_SOURCES
    kernel/user_execution_test.c
    kernel/user_execution_test_fixture.c
    kernel/user_execution_test_prepare.c
    kernel/user_execution_test_trap.c
    kernel/user_execution_test_lifecycle.c
)
```

The main kernel target expands the four production sets once. `endpoint_test`
expands the portable core and host-test sets once. The existing conditional
QEMU configurations expand the corresponding test set once. Existing
assembly sources remain in their current target/configuration blocks.

No source listed in a set is also listed directly by the same consumer.

## Structural source-layout gate

Implementation adds:

```text
tests/host/test_kernel_address_space_source_layout.py
```

The accepted design is intentionally Red before any implementation path or
CMake set exists.

### First Green

The first implementation commit:

1. adds the structural test;
2. adds all exact target source and private-header paths;
3. adds the seven exact ordered CMake source sets;
4. changes existing consumers to expand the sets exactly once;
5. adds only declarations or minimal translation-unit includes to new
   paths; and
6. records the exact 19 moved public/test symbols in the symbol-owner
   migration map and all 23 current long functions in the long-function
   migration allowlist.

Runtime behavior remains in the current source owners at this point. Empty
translation-unit scaffolding is temporary and may be removed only by moving
the accepted responsibility into that path.

### Permanent assertions

The test verifies:

- every accepted path exists and no unapproved sibling is added to a source
  set;
- source-set ordering matches this document;
- every accepted consumer expands each required set exactly once;
- no set member is duplicated by a direct source listing;
- private headers contain declarations but no function body;
- no production source includes a test-private header;
- no source textually includes a `.c` file;
- every stable public/test symbol not in the migration map is defined by its
  accepted owner;
- every symbol still in the migration map remains in its recorded current
  owner rather than moving through an unapproved intermediate owner;
- production and test modules do not define parallel copies of the shared
  scratch or test context;
- current CTest/custom-target/workflow identities and exact pass markers
  remain unchanged;
- every new source/header is classified by the validation plan; and
- every function above the review signal appears in the exact migration
  allowlist until it receives the required semantic disposition.

The symbol-owner migration map is initially:

| Current owner | Final owner | Symbols |
| --- | --- | --- |
| `kernel/user_address_space.c` | `kernel/user_address_space_mutation.c` | `micros_user_address_space_allocate_page`, `micros_user_address_space_release_page`, `micros_user_address_space_test_fail_after_allocations` |
| `kernel/user_address_space.c` | `kernel/user_address_space_mapping.c` | `micros_user_address_space_lookup`, `micros_user_address_space_translate`, `micros_user_address_space_inventory` |
| `kernel/user_address_space.c` | `kernel/user_address_space_handoff.c` | `micros_user_address_space_prepare_wired_handoff`, `micros_user_address_space_complete_wired_handoff` |
| `kernel/user_address_space.c` | `kernel/user_address_space_activation.c` | `micros_user_address_space_activate`, `micros_user_address_space_activate_kernel` |
| `kernel/user_address_space.c` | `kernel/user_address_space_validation.c` | `micros_user_address_space_validate` |
| `kernel/user_address_space_core.c` | `kernel/user_address_space_vm_plan.c` | `micros_user_address_space_prepare_vm_table_plan` |
| `kernel/user_execution.c` | `kernel/user_execution_validation.c` | `micros_user_execution_validate_return`, `micros_user_execution_validate_context` |
| `kernel/user_execution.c` | `kernel/user_execution_trap.c` | `micros_user_execution_capture_trap`, `micros_user_execution_store_context`, `micros_user_execution_install_return_frame` |
| `kernel/user_execution_test.c` | `kernel/user_execution_test_trap.c` | `micros_user_execution_test_pre_capture`, `micros_user_execution_handle_test_trap` |

The implementation test encodes those as 19 literal
`(current path, final path, symbol)` entries. A commit may delete an entry
only when that symbol moves directly to its final owner.

The long-function migration allowlist is a literal tuple of path/function
pairs. A commit may delete entries from either migration collection but may
not add or replace entries without revising the Accepted design. The final
implementation requires both collections to be empty.

## Validation-plan ownership

Every new path must fail closed until both `tools/validation_plan.py` and
`tests/host/test_validation_plan.py` assign it the exact existing evidence
class:

| New path family | Required inherited classification |
| --- | --- |
| `kernel/address_space_validation.c` and `kernel/address_space_internal.h` | Exact workflows currently selected for `kernel/address_space.c` |
| `user_address_space_runtime/table/validation/mapping/mutation/handoff/tty/vm_scratch/vm_freeze/vm_teardown/activation.c` and `user_address_space_internal.h` | Exact workflows currently selected for `kernel/user_address_space.c` |
| `user_address_space_vm_plan.c` | Exact workflows currently selected for `kernel/user_address_space_core.c` |
| `user_execution_validation/trap.c` and `user_execution_internal.h` | Exact shared-QEMU/full-tier ownership currently selected for `kernel/user_execution.c` |
| `user_address_space_test_fixture/lifecycle/mapping/validation.c` and its private header | Only the existing `test-qemu-user-address-space` ownership of `kernel/user_address_space_test.c` |
| `user_execution_test_fixture/prepare/trap/lifecycle.c` and its private header | Only the existing `test-qemu-user-execution` ownership of `kernel/user_execution_test.c` |
| `user_address_space_owner_test.c`, `user_address_space_vm_plan_test.c`, and host private header | The existing native `endpoint_test`/`endpoint-profile` ownership and current slow IPC-model selection of `tests/host/user_address_space_core_test.c` |
| `test_kernel_address_space_source_layout.py` | Fast host Python unit ownership, with no QEMU workflow |

The implementation updates path-policy tests before a new path can silently
select no evidence. It must compare inherited workflow sets for equality,
not merely require one representative workflow.

Because `CMakeLists.txt` and the execution family are shared QEMU/full-tier
triggers, the final implementation PR plan is expected to select the
existing complete QEMU matrix. That authoritative PR plan runs once. Its
selected workflows are not also invoked separately.

## Green extraction sequence

Exactly one dependency-ready slice is active at a time.

### Slice 1: structural ownership

- Add the structural test, exact paths, private-header skeletons, CMake sets,
  and validation-plan classifications.
- Preserve all behavior in the current files.
- Keep the complete 19-entry symbol-owner map and 23-entry long-function
  allowlist.

### Slice 2: portable owner and VM-plan logic

- Move owner/leaf classification and VM plan construction to their accepted
  production owners.
- Split the native owner and VM-plan suites while preserving runner order.
- Remove the three corresponding host/portable long-function entries.
- Validate through the existing native evidence.

### Slice 3: kernel root, runtime lease, and table primitives

- Extract kernel-root validation.
- Introduce the explicit runtime view and sole scratch lease.
- Extract low-level address/PTE/table/bitmap primitives without changing
  callers.
- Remove `tree_and_table_ownership_are_valid` from the allowlist only after
  its semantic phases are visible.

### Slice 4: validation, mapping, root lifecycle, and bootstrap mutation

- Extract complete private-root validation.
- Extract mapping lookup, translation, inventory, and traversal.
- Reduce `user_address_space.c` to create/destroy lifecycle.
- Extract allocate/release transaction phases.
- Run the existing narrow address-space QEMU evidence for behavior changes
  in this slice.

### Slice 5: handoff and exact exceptional mappings

- Extract wired handoff, TTY UART mapping, VM scratch, freeze, teardown, and
  activation.
- Preserve the acyclic cross-family dependency and non-failing commit
  boundaries.
- Run the existing address-space and handoff evidence selected for these
  paths.

### Slice 6: execution production modules

- Extract canonical execution validation and trap/context mutation.
- Retain stack lifecycle and post-handoff binding commit in
  `user_execution.c`.
- Preserve every public guard and error order.
- Run the existing user-execution evidence once for the completed slice.

### Slice 7: target evidence

- Split the address-space QEMU fixture and suites.
- Split the execution QEMU fixture, preparation, trap, and lifecycle suites.
- Preserve exact top-level order, trap callback behavior, markers, and
  shutdown.
- Remove every remaining migration entry.
- Run the final authoritative PR validation plan once and obtain an
  independent static review without routine test reruns.

No slice starts before its predecessor is green and reviewable.

## Acceptance criteria

The implementation is complete only when:

1. all exact target paths and source sets match this document;
2. the public headers and behavior contracts are unchanged;
3. private headers contain declarations only and the concrete dependency
   graph is acyclic;
4. the shared reachability/release scratch pair has one owner and one
   explicit lease path;
5. every accepted public symbol resides in its owner;
6. the 19-entry symbol-owner map and 23 long-function migration items are
   empty after every symbol reaches its final owner and every function gains
   its required semantic disposition;
7. native and QEMU evidence identities, ordering, markers, and timeouts are
   unchanged;
8. every new path has exact inherited validation-plan ownership;
9. the authoritative PR validation plan passes once without duplicate
   workflow execution; and
10. an independent static review reports no substantive issue.

## Non-goals

This correction does not:

- change a public header, enum, layout, callback, syscall, protocol, or ABI;
- alter address-space, frame-owner, execution, scheduler, trap, PM, VM, TTY,
  or IPC behavior;
- add general page faults, copy-on-write, `fork`, `mmap`, file-backed paging,
  ASIDs, SMP, shootdown, migration, recovery, or compatibility machinery;
- decompose sources outside the named family;
- create a generic module framework or broad internal utility library;
- add another executable, CTest, QEMU target, workflow, marker, timeout, or
  routine test execution; or
- treat physical lines as a substitute for task cohesion.

# Kernel VM-Handoff Source Organization

This document defines the exact behavior-preserving source decomposition for
the kernel VM-handoff state, operation-12 syscall adapter, static boot
snapshot, native evidence, and retained address-space handoff target
scenario. It applies ADR-0051 to the fourth correction identified by the
repository source-module audit.

Implementation must not begin until
[ADR-0055](../adr/0055-kernel-vm-handoff-source-decomposition.md) is Accepted
and its design pull request is merged.

## Reviewed base

The reviewed source base is:

```text
b47d56e2ece2253a3ab2a4e9ac8ddc8cab56da37
```

The fixed local MINIX baseline and required `micros` adaptations are recorded
in
[the research note](../research/minix-kernel-vm-handoff-source-organization.md).

## Current inventory

The audit signal covers seven files:

| File | Physical lines | Functions | Longest function |
| --- | ---: | ---: | --- |
| `kernel/vm_handoff_core.c` | 2,152 | 70 | `micros_vm_handoff_state_validate`, 168 |
| `kernel/vm_handoff_syscall.c` | 1,742 | 31 | `micros_vm_handoff_handle_captured_user_ecall`, 202 |
| `kernel/vm_snapshot.c` | 349 | 7 | `micros_vm_snapshot_prepare`, 127 |
| `kernel/address_space_handoff_test.c` | 3,758 | 43 | `micros_address_space_handoff_runtime_run_self_test`, 1,745 |
| `tests/host/vm_handoff_syscall_test.c` | 2,124 | 82 | `reset_fixture`, 165 |
| `tests/host/vm_handoff_test.c` | 935 | 22 | `test_decodes_load_commands`, 126 |
| `tests/host/vm_bootstrap_test.c` | 720 | 12 | `initialize_build_fixture`, 164 |
| **Total** | **11,780** | **267** | **13 functions above 120 lines** |

The numeric values are review signals. The boundaries below follow ABI,
authority, state transition, transaction, adapter, snapshot, fixture, and
scenario ownership.

## Behavioral preservation

The decomposition preserves all Accepted behavior in ADR-0040, ADR-0045, and
ADR-0050.

### Stable interfaces

These headers remain byte-for-byte stable:

```text
kernel/vm_handoff_core.h
kernel/vm_handoff_syscall.h
kernel/vm_snapshot.h
kernel/address_space_handoff_test.h
```

No enum, command, structure, field, token, result, error, public declaration,
or test-hook signature changes.

### ABI and output publication

- Operation 12 and its nine commands keep their exact register shapes.
- Command decode still rejects unknown commands, upper bits, reserved
  registers, alignment errors, invalid fixed sizes, invalid addresses,
  permissions, and zero tokens in the current order.
- Decoders build local candidates and publish outputs only after complete
  success.
- Input and output fixed user objects remain at most two translated chunks,
  and both complete physical access plans are retained before mutation.
- Input bytes are copied into kernel-owned storage before portable preflight
  or mutation.
- A returning failure leaves every output byte unchanged.
- Output bytes are written only after all accepted non-failing commits, using
  the retained prevalidated output plan; that final write performs no
  translation or other fallible work.

### VM handoff and load state

- The static VM identity, boot summary, prepared/handed-off phase, and fatal
  self-fault classification remain exact.
- Complete state validation remains read-only and verifies every mapping
  record, token, generation, owner phase, scratch state, record overlap, and
  single active loading/frozen transaction.
- Load begin consumes the next nonzero token only at commit.
- Scratch map validates portable load state, frame claim, and active VM-root
  mapping before ordered commit.
- Scratch commit validates exact recorded VA/frame and clears the active-root
  alias before publishing the page entry.
- Freeze validates the copied plan, PM reservation, complete data/table sets,
  frame owners, deterministic root construction, and output object before the
  accepted commit order.
- Prepared, unprepared, activated, final commit, exit detach, abort, and exit
  release keep their exact token/phase transitions.
- Abort and exit release validate every fallible address-space and ownership
  operation before the first non-failing commit.

### Static snapshot

- One aligned kernel-owned `micros_vm_boot_info` candidate remains the sole
  snapshot storage.
- Preparation binds manifest entry, process generation, endpoint, root,
  complete mapping inventory, allocator geometry, frame ownership, VM role,
  and digest.
- Early invalid-argument rejection occurs before candidate reset and leaves
  both the existing snapshot storage and caller result unchanged.
- After valid arguments trigger candidate reset, any later preparation
  failure clears the complete candidate and leaves the caller result
  unchanged.
- Current validation re-inventories every mapping in order and verifies the
  exact authoritative allocator/ownership state without mutation.

### Syscall authority and failure ordering

- The exact current VM process, thread, endpoint, profile, and kernel-operation
  privilege are validated before command handling.
- `VM_READY` remains available only while VM is starting in bootstrap and
  ownership is `BOOTSTRAP`.
- TTY mapping remains available only at its exact launcher/TTY handoff phase.
- Dynamic commands remain available only after launcher seal and VM/ownership
  handoff.
- Recoverable ABI, state, unauthorized, and memory-fault results remain
  stable.
- Runtime, ownership, address-space, TTY, IPC, PM, or object corruption
  remains fatal with the existing diagnostic boundary.
- No handler performs fallible work after its current authoritative commit
  begins.

### Evidence

- Native VM bootstrap, portable handoff, and syscall adapter tests retain
  their current case order and assertions.
- The retained address-space handoff image preserves every setup, snapshot,
  corruption, resident IPC/grant, real user-syscall, TTY mapping, trap,
  marker, and shutdown behavior.
- Source decomposition creates no new executable, CTest, workflow, marker,
  timeout, or duplicate behavioral execution. One structural Python test is
  added to existing host-unit discovery.

## Portable VM-handoff state layout

| Path | Owned responsibility |
| --- | --- |
| `kernel/vm_handoff_core.c` | Common owner/identity predicates, mapping-record shape, record overlap/token uniqueness, page-plan digest, complete state validation, and private transition primitives |
| `kernel/vm_handoff_abi.c` | Command recognition and all operation-12 register decoders |
| `kernel/vm_handoff_boot.c` | Static state preparation, summary matching, TTY mapping identity, irreversible boot commit, and VM self-fault classification |
| `kernel/vm_handoff_load.c` | Load-begin preflight and load-begin commit |
| `kernel/vm_handoff_scratch.c` | Scratch-map preflight/bind/finish and scratch-commit preflight/commit |
| `kernel/vm_handoff_freeze.c` | Freeze request validation, mapping-generation selection, action/result construction, and frozen-state commit |
| `kernel/vm_handoff_commit.c` | Prepared/unprepared/activated transitions and final load-commit preflight/commit |
| `kernel/vm_handoff_release.c` | Pre/post-freeze abort, exit-detached transition, exit-release preflight, and record release |
| `kernel/vm_handoff_internal.h` | Sole private portable-state contract |

`vm_handoff_core.c` is read-only relative to accepted authoritative state
except for private pure candidate helpers. It does not own a command commit.
Transition modules may depend on core predicates and validation; no transition
module calls another transition module.

## Stable production public-symbol ownership

| Owner | Public symbols |
| --- | --- |
| `vm_handoff_core.c` | `micros_vm_handoff_state_validate`, `micros_vm_load_page_plan_digest` |
| `vm_handoff_abi.c` | `micros_vm_handoff_decode`, `micros_vm_handoff_decode_command`, `micros_vm_handoff_decode_tty_mapping`, `micros_vm_load_begin_decode`, `micros_vm_scratch_map_decode`, `micros_vm_scratch_commit_decode`, `micros_vm_load_freeze_decode`, `micros_vm_load_abort_decode`, `micros_vm_load_commit_decode`, `micros_vm_exit_release_decode` |
| `vm_handoff_boot.c` | `micros_vm_tty_mapping_matches`, `micros_vm_handoff_state_prepare`, `micros_vm_handoff_summary_matches`, `micros_vm_handoff_commit_prevalidated`, `micros_vm_self_fault_classify` |
| `vm_handoff_load.c` | `micros_vm_load_begin_preflight`, `micros_vm_load_begin_commit_prevalidated` |
| `vm_handoff_scratch.c` | `micros_vm_scratch_map_preflight`, `micros_vm_scratch_map_commit_prevalidated`, `micros_vm_scratch_map_bind_prevalidated`, `micros_vm_scratch_map_finish_prevalidated`, `micros_vm_scratch_commit_preflight`, `micros_vm_scratch_commit_commit_prevalidated` |
| `vm_handoff_freeze.c` | `micros_vm_load_freeze_preflight`, `micros_vm_load_freeze_commit_prevalidated` |
| `vm_handoff_commit.c` | `micros_vm_load_mark_prepared`, `micros_vm_load_mark_unprepared`, `micros_vm_load_mark_activated`, `micros_vm_load_commit_preflight`, `micros_vm_load_commit_commit_prevalidated` |
| `vm_handoff_release.c` | `micros_vm_load_abort_preflight`, `micros_vm_load_abort_commit_prevalidated`, `micros_vm_load_mark_exit_detached`, `micros_vm_exit_release_preflight`, `micros_vm_exit_release_commit_prevalidated` |

## Static snapshot layout

| Path | Owned responsibility |
| --- | --- |
| `kernel/vm_snapshot.c` | Sole aligned snapshot storage, VM-role discovery, wired staging, mapping collection, boot-object build, and result publication |
| `kernel/vm_snapshot_validation.c` | Current manifest/binding/mapping re-inventory and allocator/ownership authority validation |
| `kernel/vm_snapshot_internal.h` | Private binding and mapping callback declarations shared by preparation and validation |

`micros_vm_snapshot_prepare` remains in `vm_snapshot.c`.
`micros_vm_snapshot_validate_current` moves to
`vm_snapshot_validation.c`.

## Privileged syscall-adapter layout

| Path | Owned responsibility |
| --- | --- |
| `kernel/vm_handoff_syscall.c` | Public captured-`ecall` facade, command decode, common runtime acquisition, phase selection, and dispatch |
| `kernel/vm_handoff_syscall_runtime.c` | Result shaping, panic/failure diagnostics, VM authorization, bootstrap/post-handoff phase gates, PM reservation checks, and shared error translation |
| `kernel/vm_handoff_syscall_memory.c` | Fixed user-object translation plan plus prevalidated read/write |
| `kernel/vm_handoff_syscall_boot.c` | `VM_READY` and exact TTY UART mapping adapters |
| `kernel/vm_handoff_syscall_load.c` | `LOAD_BEGIN` and `LOAD_COMMIT` adapters |
| `kernel/vm_handoff_syscall_scratch.c` | `SCRATCH_MAP` and `SCRATCH_COMMIT` adapters |
| `kernel/vm_handoff_syscall_freeze.c` | `LOAD_FREEZE` fixed-object, PM, ownership, address-space, and state orchestration |
| `kernel/vm_handoff_syscall_release.c` | `LOAD_ABORT` and `EXIT_RELEASE` orchestration |
| `kernel/vm_handoff_syscall_internal.h` | Sole private syscall-adapter contract |

The public
`micros_vm_handoff_handle_captured_user_ecall` symbol remains in
`vm_handoff_syscall.c`.

The operation adapters do not call one another. They depend downward on
runtime/memory helpers and the public portable transition modules. The facade
does not duplicate operation logic.

## Production dependency graph

The only permitted internal edges are:

```text
vm_handoff_abi -> vm_handoff_core
vm_handoff_boot -> vm_handoff_core
vm_handoff_load -> vm_handoff_core
vm_handoff_scratch -> vm_handoff_core
vm_handoff_freeze -> vm_handoff_core
vm_handoff_commit -> vm_handoff_core
vm_handoff_release -> vm_handoff_core

vm_snapshot_validation -> vm_snapshot private binding predicate

vm_handoff_syscall_boot -> vm_handoff_syscall_runtime
vm_handoff_syscall_load -> vm_handoff_syscall_runtime
vm_handoff_syscall_load -> vm_handoff_syscall_memory
vm_handoff_syscall_scratch -> vm_handoff_syscall_runtime
vm_handoff_syscall_freeze -> vm_handoff_syscall_runtime
vm_handoff_syscall_freeze -> vm_handoff_syscall_memory
vm_handoff_syscall_release -> vm_handoff_syscall_runtime
vm_handoff_syscall -> vm_handoff_syscall_runtime
vm_handoff_syscall -> every operation adapter
```

Each operation adapter also depends only on the existing public subsystem
mechanisms required by that command. Portable state modules never include the
syscall-private header or call privileged adapters. Snapshot modules never
call an operation adapter.

## Native portable-state evidence

The existing `vm_bootstrap_test` executable continues to invoke one portable
handoff suite through `micros_vm_handoff_test_run()`.

| Path | Owned responsibility |
| --- | --- |
| `tests/host/vm_handoff_test.c` | Suite entry and exact current 11-case order |
| `tests/host/vm_handoff_test_fixture.c` | Valid summary, register arguments, handed-off state, dynamic process, freeze plan, append/fill helpers, and output-preservation helpers |
| `tests/host/vm_handoff_abi_test.c` | READY, TTY, load, scratch, freeze, abort, commit, and exit-release decode cases |
| `tests/host/vm_handoff_boot_test.c` | Static state prepare/summary/commit, invalid-state preservation, and VM self-fault classification |
| `tests/host/vm_handoff_load_test.c` | Load lifecycle, pre/post-freeze abort, failure preservation, stack bounds, and oversized-record rejection |
| `tests/host/vm_handoff_test_internal.h` | Private fixture and case-runner declarations |

The suite entry remains the only public test symbol in this family and keeps
the current short-circuit order.

## Native VM-bootstrap evidence

| Path | Owned responsibility |
| --- | --- |
| `tests/host/vm_bootstrap_test.c` | Executable entry and exact current case order, including the portable handoff suite |
| `tests/host/vm_bootstrap_test_fixture.c` | Sole aligned boot-info/allocator/ownership context, valid-info initialization, authoritative build fixture, zero-storage checks, and error helpers |
| `tests/host/vm_bootstrap_abi_test.c` | ABI layout, valid snapshot, finalize atomicity, malformed input, and misalignment cases |
| `tests/host/vm_bootstrap_build_test.c` | Authoritative-state build, role assignment, candidate clearing, duplicate mapping, temporary owner, and stale-authority cases |
| `tests/host/vm_bootstrap_test_internal.h` | Private context and case-runner declarations |

Exactly one static `vm_bootstrap_test_context` owns mutable fixture state.

## Native syscall-adapter evidence

| Path | Owned responsibility |
| --- | --- |
| `tests/host/vm_handoff_syscall_test.c` | Executable entry and exact current 17-case order |
| `tests/host/vm_handoff_syscall_test_fixture.c` | Sole mutable context, reset, argument builders, command invocation, panic capture, load-page setup, and freeze-object construction |
| `tests/host/vm_handoff_syscall_test_boot.c` | TTY mapping success, authorization, phase, notification, and uncleared-event scenarios |
| `tests/host/vm_handoff_syscall_test_load.c` | Load begin, freeze failures, fatal invariants, complete commit/exit, and all abort scenarios |
| `tests/host/vm_handoff_syscall_test_runtime_backend.c` | Bootstrap, VM, PM, object, endpoint, privilege, frame-ledger, and panic linker adapters |
| `tests/host/vm_handoff_syscall_test_boot_backend.c` | TTY, PLIC, UART, wired handoff, TTY address-space, and IPC-notification linker adapters |
| `tests/host/vm_handoff_syscall_test_load_backend.c` | Fixed user translation, frame claim/release/freeze, scratch, freeze-construction, and teardown linker adapters |
| `tests/host/vm_handoff_syscall_test_internal.h` | Private context, scenario, builder, and adapter declarations |

The fixture source defines exactly one static
`vm_handoff_syscall_test_context`. Required linker-signature adapters obtain
that state only through the fixture accessor; no backend defines duplicate
mutable state.

The final backend symbol owners are exact:

| Owner | Linker-adapter symbols |
| --- | --- |
| `vm_handoff_syscall_test_runtime_backend.c` | `micros_bootstrap_runtime_state`, `micros_bootstrap_runtime_authoritative_state`, `micros_bootstrap_runtime_validate`, `micros_bootstrap_runtime_validate_vm_prepared`, `micros_bootstrap_runtime_fail`, `micros_bootstrap_control_find_binding`, `micros_vm_handoff_runtime_authoritative_state`, `micros_vm_handoff_runtime_state`, `micros_vm_handoff_runtime_validate`, `micros_pm_control_runtime_state`, `micros_pm_control_state_validate`, `micros_pm_control_runtime_validate`, `micros_ipc_runtime_authoritative_registry`, `micros_frame_ownership_runtime_ledger`, `micros_process_resolve`, `micros_thread_resolve`, `micros_thread_ipc_state_is_clear`, `micros_ipc_thread_has_staged_kernel_notification`, `micros_endpoint_resolve_active`, `micros_privilege_profile_allows_kernel_operation`, `micros_trap_panic_entry` |
| `vm_handoff_syscall_test_boot_backend.c` | `micros_tty_handoff_runtime_authoritative_state`, `micros_tty_handoff_runtime_prepare_mapped`, `micros_tty_handoff_runtime_commit_mapped_prevalidated`, `micros_tty_handoff_runtime_validate`, `micros_plic_validate`, `uart_console_handoff_is_quiesced`, `micros_user_address_space_complete_wired_handoff`, `micros_user_address_space_validate`, `micros_user_address_space_prepare_tty_uart_mapping`, `micros_user_address_space_commit_tty_uart_mapping`, `micros_ipc_prepare_kernel_notification`, `micros_ipc_commit_kernel_notification_prevalidated` |
| `vm_handoff_syscall_test_load_backend.c` | `micros_user_address_space_translate`, `micros_frame_ownership_runtime_vm_claim_preflight`, `micros_frame_ownership_runtime_vm_claim_commit_prevalidated`, `micros_frame_ownership_runtime_vm_release_transferable_preflight`, `micros_frame_ownership_runtime_vm_release_transferable_commit_prevalidated`, `micros_frame_ownership_runtime_vm_freeze_preflight`, `micros_frame_ownership_runtime_vm_freeze_commit_prevalidated`, `micros_frame_ownership_runtime_vm_release_process_preflight`, `micros_frame_ownership_runtime_vm_release_process_commit_prevalidated`, `micros_user_address_space_prepare_vm_scratch_mapping`, `micros_user_address_space_commit_vm_scratch_mapping`, `micros_user_address_space_prepare_vm_scratch_removal`, `micros_user_address_space_commit_vm_scratch_removal`, `micros_user_address_space_prepare_vm_freeze`, `micros_user_address_space_construct_vm_freeze_prevalidated`, `micros_user_address_space_install_vm_freeze_prevalidated`, `micros_user_address_space_prepare_vm_teardown`, `micros_user_address_space_commit_vm_teardown_prevalidated` |

These 21, 12, and 18 symbols are part of the accepted owner map.

## Retained address-space handoff target evidence

| Path | Owned responsibility |
| --- | --- |
| `kernel/address_space_handoff_test.c` | Public self-test entry, ordered phase orchestration, SIE save/restore, failure-stage report, and final Boolean result |
| `kernel/address_space_handoff_test_fixture.c` | Sole explicit test context, byte/pattern helpers, process/page identities, PTE/frame lookup, and user-context construction |
| `kernel/address_space_handoff_test_setup.c` | IPC/grant runtime initialization, reserved/live process creation, roots/pages/endpoints/threads, TTY bootstrap fixture, execution payloads, and initial grants |
| `kernel/address_space_handoff_test_snapshot.c` | Page-table/user/registry snapshots plus VM snapshot preparation and current validation |
| `kernel/address_space_handoff_test_handoff.c` | Pre-handoff access, failure-atomic handoff rejection, stale snapshot, exact wired staging, and ownership commit |
| `kernel/address_space_handoff_test_runtime.c` | Post-handoff lookup/translation/activation, resident IPC/grants, recoverable failures, owner/PTE corruption, and mutation revocation |
| `kernel/address_space_handoff_test_syscall.c` | Real grant-syscall script, command arming/transition/effect validation, and before/after syscall hooks |
| `kernel/address_space_handoff_test_tty.c` | Exact TTY UART mapping, device authority rejection, TTY release, and device-grant failure scenarios |
| `kernel/address_space_handoff_test_trap.c` | Production entry, scheduler actor validation, control `ecall`, process switching, and supervisor return |
| `kernel/address_space_handoff_test_internal.h` | Private target context, phase, script, and scenario declarations |

The fixture source defines exactly one static
`micros_address_space_handoff_test_context`. Every scenario receives it
explicitly. Externally invoked syscall/trap hooks obtain the same context from
the fixture accessor.

Stable target-test public-symbol ownership is:

| Owner | Symbols |
| --- | --- |
| `address_space_handoff_test.c` | `micros_address_space_handoff_runtime_run_self_test` |
| `address_space_handoff_test_syscall.c` | `micros_address_space_handoff_test_before_ecall`, `micros_address_space_handoff_test_after_dispatch`, `micros_address_space_handoff_test_after_return` |
| `address_space_handoff_test_trap.c` | `micros_address_space_handoff_test_enter_production`, `micros_address_space_handoff_test_handle_trap` |

Assembly entry points and saved/restored-state objects remain unchanged.

## Stable evidence identities

The implementation preserves:

- native target `vm_bootstrap_test` and CTest `vm-bootstrap`;
- native target `vm_handoff_syscall_test` and CTest
  `vm-handoff-syscall`;
- `test-qemu-vm-handoff` and its exact pass record;
- `test-qemu-address-space-handoff` and both exact pass records;
- `test-qemu-vm-scratch-preemption` and its exact pass record;
- VM ready-early and both VM self-fault panic workflows;
- the current timeouts, generated fixture inputs, assembly entry points, and
  validation-plan ownership; and
- all downstream workflows selected by shared VM-handoff production paths.

No new behavioral execution identity is introduced.

## Build ownership

`CMakeLists.txt` defines exact ordered source sets:

```text
MICROS_VM_HANDOFF_CORE_SOURCES
MICROS_VM_HANDOFF_SYSCALL_SOURCES
MICROS_VM_SNAPSHOT_SOURCES
MICROS_VM_HANDOFF_CORE_HOST_TEST_SOURCES
MICROS_VM_BOOTSTRAP_HOST_TEST_SOURCES
MICROS_VM_HANDOFF_SYSCALL_HOST_TEST_SOURCES
MICROS_ADDRESS_SPACE_HANDOFF_QEMU_TEST_SOURCES
```

The accepted consumers are:

- `vm_bootstrap_test` expands core, portable-core-test, and bootstrap-test
  sets once;
- `pm_test` expands the core set once;
- `vm_handoff_syscall_test` expands core, syscall, and syscall-test sets once;
- `micros` expands core, syscall, and snapshot sets once; and
- `MICROS_BUILD_ADDRESS_SPACE_HANDOFF_TEST` expands the QEMU-test set once
  beside the existing assembly source.

No split source is also listed directly beside its source-set expansion.

## Validation-plan ownership

`tools/validation_plan.py` records path-classification equivalents:

- every new portable core source and `vm_handoff_internal.h` inherits
  `kernel/vm_handoff_core.c`;
- every new syscall-adapter source and private header inherits
  `kernel/vm_handoff_syscall.c`;
- every new snapshot source and private header inherits
  `kernel/vm_snapshot.c`;
- every new target-test source and private header inherits
  `kernel/address_space_handoff_test.c`;
- every new native syscall-test source and private header inherits
  `tests/host/vm_handoff_syscall_test.c`;
- every new portable-state test source and private header inherits
  `tests/host/vm_handoff_test.c`; and
- every new bootstrap-test source and private header inherits
  `tests/host/vm_bootstrap_test.c`.

The structural source-layout test is added to `HOST_UNIT_ONLY_INPUTS`.
Equivalent paths must produce command-identical fast and pull-request plans.

## Structural Red gate

Implementation begins with:

```text
tests/host/test_kernel_vm_handoff_source_layout.py
```

The first Green slice adds exact paths, declarations-only private headers,
ordered source sets, and empty translation units while runtime behavior stays
in current owners.

The gate verifies:

- exact production and test paths;
- exact source-set membership and one expansion per accepted consumer;
- declarations-only private headers;
- no textual C inclusion or production inclusion of test-private headers;
- exact final production, snapshot, target-hook, and host-backend symbol
  ownership;
- exactly one mutable context owner per test family and one aligned snapshot
  owner;
- unchanged public headers, evidence identities, case/scenario ordering,
  markers, timeouts, and assembly symbols;
- exact validation-plan inheritance;
- an exact 92-entry public/test symbol-owner migration map; and
- an exact 13-entry long-function migration allowlist.

Both migration collections may only shrink and must be empty in the final
implementation. Moving or renaming an unchanged long function does not
satisfy the gate.

## Required function disposition

Every current function above 120 physical lines is non-atomic.

| Function and current lines | Required subtask outline |
| --- | --- |
| `micros_vm_handoff_state_validate` (`kernel/vm_handoff_core.c`, 168) | State identity/phase; each mapping record and unique token/physical set; single loading/frozen slot; idle transaction; loading transaction and scratch; frozen transaction |
| `handle_tty_mapping_command` (`kernel/vm_handoff_syscall.c`, 143) | Phase gate; complete runtime validation; launcher/VM event-clear gate; request identity; address/TTY/notification preflight; ordered non-failing commit |
| `handle_load_freeze_command` (`kernel/vm_handoff_syscall.c`, 159) | Input fixed-object translation/copy; output translation; portable freeze preflight; PM reservation; data-frame derivation; ownership preflight; address-space prepare/construct; ordered ownership/install/state commit; output write |
| `handle_load_abort_command` (`kernel/vm_handoff_syscall.c`, 177) | Portable abort preflight; optional scratch-removal preflight; frozen address/ownership release preflight or loading transferable release preflight; ordered address/ownership/state commit |
| `micros_vm_handoff_handle_captured_user_ecall` (`kernel/vm_handoff_syscall.c`, 202) | Argument and command decode; runtime authority acquisition; common invariant validation; caller authorization; bootstrap/post-handoff phase selection; command dispatch; stable result |
| `micros_vm_snapshot_prepare` (`kernel/vm_snapshot.c`, 127) | Argument/candidate reset; manifest-binding and unique VM discovery; wired staging; ordered address-space/mapping inventory; allocator/ledger boot build; result publication; complete candidate clear on failure |
| `arm_handoff_syscall` (`kernel/address_space_handoff_test.c`, 302) | Grant create; copy-from; copy-to; validate/zero; recoverable-error; revoke; stale-token command families |
| `transition_handoff_syscall` (`kernel/address_space_handoff_test.c`, 126) | Exact next-command table, actor switch selection, and terminal supervisor return |
| `micros_address_space_handoff_runtime_run_self_test` (`kernel/address_space_handoff_test.c`, 1,745) | Runtime setup; process/root/page/TTY setup; execution/grant setup; pre-handoff access; failure-atomic handoff and snapshot; post-handoff resident operations; recoverable/corruption checks; mutation revocation and real user syscalls; TTY UART/device authority; final validation and cleanup |
| `reset_fixture` (`tests/host/vm_handoff_syscall_test.c`, 165) | Bootstrap/VM/PM/TTY state; process/thread/endpoint records; operation/injection controls; user-memory reset |
| `test_decodes_load_commands` (`tests/host/vm_handoff_test.c`, 126) | Load begin; scratch map; scratch commit; freeze; abort/commit; exit release; invalid reserved-output preservation |
| `initialize_build_fixture` (`tests/host/vm_bootstrap_test.c`, 164) | Allocator/ledger initialization; owner construction; frame allocation/release; complete wired-set staging; address-space records; mapping records |
| `test_build_failure_clears_candidate` (`tests/host/vm_bootstrap_test.c`, 125) | Missing wired target; duplicate physical mapping; temporary owner; post-build authority staleness |

## Implementation sequence

The implementation proceeds as small green commits:

1. Add the structural Red gate, exact source sets, private-header skeletons,
   empty target paths, and complete migration collections.
2. Split ABI decoding, portable core validation, static boot handoff, and
   native ABI/boot tests.
3. Split dynamic load, scratch, freeze, commit, release, and native lifecycle
   tests.
4. Split snapshot preparation/current validation and bootstrap build/ABI
   evidence.
5. Split syscall runtime/memory, static boot, load, scratch, freeze, and
   release adapters plus native syscall fixtures/backends/scenarios.
6. Split retained address-space handoff fixture, setup, snapshot, handoff,
   runtime, real-syscall, TTY, and trap evidence; empty both migration
   collections.
7. Run the authoritative pull-request validation plan once and obtain the
   independent implementation review.

During extraction, run only the narrow existing native or target evidence for
the active slice. Before review, run
`tools/validation_plan.py --tier pr --base <review-base> --execute` once. Do
not separately rerun workflows selected by that plan.

## Non-goals

This design does not authorize:

- public API, ABI, enum, command, structure, token, phase, result, error, or
  behavior changes;
- new VM, PM, TTY, grant, frame, address-space, or scheduler authority;
- page-fault delivery, fork, copy-on-write, paging, recovery, or SMP;
- changes to generated VM fixtures, service images, wire formats, markers,
  timeouts, or shutdown behavior;
- decomposition of kernel PM control, VM service, PM service, or unrelated
  source-audit units;
- new behavioral executables, CTests, workflows, or duplicate execution
  beyond the authorized structural source-layout test; or
- mechanical splitting based only on line count.

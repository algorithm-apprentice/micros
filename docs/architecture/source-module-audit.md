# Repository Source-Module Audit

## Purpose

This audit applies
[ADR-0051](../adr/0051-responsibility-based-source-modules.md) after the
portable VFS decomposition. It classifies the remaining source-organization
review signals and creates bounded follow-up units. It does not authorize one
repository-wide refactor.

The audited base is:

```text
9d46b55ce2d1675838eeb37955a724943fda0a3c
```

## Method

The inventory uses only files tracked by Git with C, header, assembly, or
Python source suffixes. Build directories, generated build trees, and other
untracked output are excluded. This avoids the false multi-thousand-line
results produced when tools such as `cloc` include `build/`. These suffixes
are the repository's current executable implementation and test languages.
Declarative build metadata such as `CMakeLists.txt` is outside ADR-0051's
implementation/test source scope and its numeric review signals.

The review signals remain:

- more than 1,500 physical lines in one implementation or test file;
- more than 25 function definitions;
- any function longer than 120 physical lines;
- mixed authority, lifecycle, protocol, fixture, or scenario ownership even
  below those numeric signals.

At the audited base:

| Signal | Count |
| --- | ---: |
| Files with at least one signal | 118 |
| Files over 1,500 physical lines | 42 |
| Files over 25 function definitions | 53 |
| Functions over 120 physical lines | 268 |

These counts locate review work; they do not define module or function
boundaries.

## Cohesion rule

A file owns one cohesive authority, resource lifecycle, protocol layer,
algorithm, fixture family, or model. A function owns one task.

When a task has meaningful subtasks, its top-level function should read as an
ordered outline of named helper calls. Validation, reservation, externally
visible mutation, rollback, and result publication are separate subtasks when
they have distinct invariants or failure boundaries.

Atomicity is semantic, not numeric. A function is atomic only when further
extraction would split one invariant, algorithm, or commit boundary without
revealing a separately understandable task. Therefore:

- a mixed 30-line function still requires decomposition;
- a cohesive function over 120 lines may remain only with an explicit
  algorithmic or invariant-based rationale;
- helper extraction must improve the visible task hierarchy rather than
  manufacture wrappers around individual expressions.

Tests follow the same rule. One scenario may remain one public test, but its
fixture construction, setup, action, failure injection, assertions, and
cleanup should be visible as named phases instead of one long script.

## Classification

The 118 signal files fall into 18 correction units plus eight cohesive
exceptions. Each correction unit is a separate reviewed design and
implementation pair. Its design must define exact file ownership and function
phases before source movement begins.

### Immediate next-touch corrections

These units are on the current Step 14 path. Their implementation must precede
new behavior in the same signaled sources.

| Unit | Signal files | Required outcome | Task pair |
| --- | ---: | --- | --- |
| VFS tests | 8 | Apply the exact [VFS test source organization](vfs-test-source-organization.md): separate shared fixtures, core behavior, spawn transactions, service protocols, replayable model logic, target scenarios, and fixture generation before adding exit-cleanup coverage. | `vfs-test-decomposition-design` / `vfs-test-decomposition-implementation` |
| Kernel address spaces | 7 | Apply [ADR-0053](../adr/0053-kernel-address-space-source-decomposition.md) and the exact [kernel address-space source organization](kernel-address-space-source-organization.md): separate ordinary root/mapping ownership, complete validation, VM scratch, freeze/teardown, activation, execution validation, and their target scenarios while retaining one scratch owner and an acyclic dependency direction. | `kernel-address-space-decomposition-design` / `kernel-address-space-decomposition-implementation` |
| Frame ownership | 4 | Apply [ADR-0054](../adr/0054-frame-ownership-source-decomposition.md) and the exact [frame-ownership source organization](frame-ownership-source-organization.md): separate allocator-ledger validation, bootstrap handoff, VM claim/freeze/release transitions, and model/target scenarios. | `frame-ownership-decomposition-design` / `frame-ownership-decomposition-implementation` |
| Kernel VM handoff | 7 | Apply [ADR-0055](../adr/0055-kernel-vm-handoff-source-decomposition.md) and the exact [kernel VM-handoff source organization](kernel-vm-handoff-source-organization.md): separate ABI decoding, static handoff, dynamic load/scratch/freeze/commit/exit state, syscall adaptation, snapshot ownership, and native/target evidence. | `kernel-vm-handoff-decomposition-design` / `kernel-vm-handoff-decomposition-implementation` |
| Kernel PM control | 4 | Apply [ADR-0056](../adr/0056-kernel-pm-control-source-decomposition.md) and the exact [kernel PM-control source organization](kernel-pm-control-source-organization.md): separate ABI and validation, reservation, execution preparation, activation, exit, syscall adaptation, and native evidence while retaining one authoritative target state and one mutable context per test family. | `kernel-pm-control-decomposition-design` / `kernel-pm-control-decomposition-implementation` |
| VM service | 7 | Apply [ADR-0057](../adr/0057-vm-service-source-decomposition.md) and the exact [VM service source organization](vm-service-source-organization.md): separate boot/image planning, complete-state validation, frame selection, load/exit lifecycle, service adapters, fixtures, target evidence, and generation tooling while preserving one authoritative context per evidence family. | `vm-service-decomposition-design` / `vm-service-decomposition-implementation` |
| PM service | 7 | Apply [ADR-0058](../adr/0058-pm-service-source-decomposition.md) and the exact [PM service source organization](pm-service-source-organization.md): separate common identity, codec, validation, spawn/rollback, exit/wait, request/layout, service, fixture/model, kernel report, and generator ownership. | `pm-service-decomposition-design` / `pm-service-decomposition-implementation` |

The VFS exit-cleanup implementation depends on the VFS test correction. The
later PM/VFS/VM coordinator depends on the six PM/VM/kernel correction
implementations above. This prevents new lifecycle behavior from being added
to the currently mixed sources.

### Before the next change to each subsystem

These units do not block unrelated Step 14 work. They become mandatory before
the next behavior-bearing change to their own subsystem.

| Unit | Signal files | Required outcome | Task pair |
| --- | ---: | --- | --- |
| RAMFS service | 7 | Apply Accepted [ADR-0066](../adr/0066-ramfs-source-decomposition.md) and the exact [RAMFS source contract](ramfs-source-organization.md), only after the separately reviewed design merges: expose portable, native/reference, target/report, and generator tasks while preserving behavior and closed validation eligibility. | `ramfs-source-decomposition-design` / `ramfs-source-decomposition-implementation` |
| Kernel IPC | 11 | Apply Accepted [ADR-0059](../adr/0059-kernel-ipc-source-decomposition.md) and the exact [kernel IPC source organization](kernel-ipc-source-organization.md): separate queue/completion/token/event/deadlock/rendezvous/close owners, captured syscall adaptation, nine evidence contexts, independent reference operations, and retained target scripts. Implement only after the independently reviewed design merges. | `kernel-ipc-decomposition-design` / `kernel-ipc-decomposition-implementation` |
| Kernel endpoints | 4 | Apply Accepted [ADR-0067](../adr/0067-kernel-endpoint-source-decomposition.md) and the exact [endpoint source contract](kernel-endpoint-source-organization.md) only after separate design merge: separate representation/identity, profile and complete validation, publication/source-only/close, native/model, and target tasks while preserving authority and real closed-profile eligibility. | `kernel-endpoint-decomposition-design` / `kernel-endpoint-decomposition-implementation` |
| Kernel scheduler | 6 | Apply Accepted [ADR-0062](../adr/0062-kernel-scheduler-source-decomposition.md) and the exact [scheduler source contract](kernel-scheduler-source-organization.md) only after reviewed design merge: separate ready queues, IPC transactions, complete validation, planning/commit/accounting, selected completion and return, independent fixtures/models, and target evidence without changing behavior. | `kernel-scheduler-decomposition-design` / `kernel-scheduler-decomposition-implementation` |
| Kernel objects | 3 | Follow Accepted [ADR-0068](../adr/0068-kernel-object-source-decomposition.md) and the exact [object source contract](kernel-object-source-organization.md), only after reviewed design merge: separate representation/identity, process/thread/context/hart, base validation, native/model and target tasks while retaining distinct identities, original storage and real validation-profile eligibility. | `kernel-object-decomposition-design` / `kernel-object-decomposition-implementation` |
| Kernel bootstrap | 10 | Apply Accepted [ADR-0060](../adr/0060-kernel-bootstrap-source-decomposition.md) and the exact [bootstrap source contract](kernel-bootstrap-source-organization.md) only after design merge: separate manifest/profile validation, preparation and rollback, diagnostics, captured control, boot entry, launcher, fixtures, reference replay and shared descriptor phases without changing behavior. | `kernel-bootstrap-decomposition-design` / `kernel-bootstrap-decomposition-implementation` |
| Trap and syscall routing | 6 | Apply Accepted [ADR-0061](../adr/0061-trap-and-syscall-source-decomposition.md) and the exact [trap/syscall source contract](trap-syscall-source-organization.md) only after design merge: separate routed context, user/supervisor interrupt, syscall, fault, return, bounded test hooks, and existing runtime evidence while preserving register custody and authority. | `trap-syscall-decomposition-design` / `trap-syscall-decomposition-implementation` |
| Grant data plane | 7 | Follow Accepted [ADR-0069](../adr/0069-grant-data-plane-source-decomposition.md) and the exact [grant data-plane source contract](grant-data-plane-source-organization.md) only after separate design merge: preserve the cohesive registry while exposing range/copy, captured syscall, fixture/reference/model and real target-script tasks. | `grant-data-plane-decomposition-design` / `grant-data-plane-decomposition-implementation` |
| TTY | 8 | Proposed [ADR-0070](../adr/0070-tty-function-and-evidence-decomposition.md) and the exact [TTY source contract](tty-source-organization.md) separate runtime state/validation/transitions, service request owners, independent evidence and generator phases while preserving cohesive terminal/UART/control state. Implementation stays pending until separate design review and merge. | `tty-function-decomposition-design` / `tty-function-decomposition-implementation` |

### Tooling correction

| Unit | Signal files | Required outcome | Task pair |
| --- | ---: | --- | --- |
| QEMU runner tooling | 2 | Separate outcome parsing, command construction, serial driving, CLI construction, and host tests without changing the command interface or accepted markers. | `qemu-tooling-decomposition-design` / `qemu-tooling-decomposition-implementation` |
| ELF checker tooling | 2 | Apply Accepted [ADR-0063](../adr/0063-user-elf-checker-source-decomposition.md) and the exact [user ELF checker source contract](user-elf-checker-source-organization.md), after the separately reviewed design merges: separate parsing, structure, service policy, instructions, CLI, fixtures, and case discovery without changing accepted inputs, outputs, or the command interface. | `elf-tooling-decomposition-design` / `elf-tooling-decomposition-implementation` |

The selected ELF tooling correction precedes the application host checker
within `elf-spawn-app-runtime`. It exposes existing representation and
structural owners before that consumer expands the family, rather than
duplicating service-checker logic. It does not add a runtime-DAG node or block
unrelated Step 14 work. Design #125 and implementation #126 are independently
reviewed and merged. The application-runtime consumer #127 is also merged;
the final [ELF spawn integration](elf-spawn-integration.md) composes those
checked owners rather than reopening the source-only correction.

The independent validation-plan deduplication task is not part of this source
decomposition. The
[validation planner](../../tools/validation_plan.py) remains a cohesive
exception below; its execution policy is governed by ADR-0032.

## Complete signal ownership

Every signal file belongs to exactly one unit or exception.

### VFS tests

`tests/host/vfs_model_test.c`, `tests/host/vfs_spawn_test.c`,
`tests/host/vfs_test.c`, `tests/host/vfs_service_test.c`,
`tests/host/vfs_test_fixture.c`, `tests/qemu/vfs_service_probe.c`,
`kernel/vfs_service_test.c`, and
`tools/generate_vfs_service_fixture.py`.

### Kernel address spaces

`kernel/user_address_space.c`, `kernel/address_space.c`,
`kernel/user_address_space_core.c`, `kernel/user_execution.c`,
`kernel/user_address_space_test.c`, `kernel/user_execution_test.c`, and
`tests/host/user_address_space_core_test.c`.

### Frame ownership

`kernel/frame_ownership.c`, `kernel/frame_ownership_test.c`,
`tests/host/frame_ownership_test.c`, and
`tests/host/frame_allocator_test.c`.

### Kernel VM handoff

`kernel/vm_handoff_core.c`, `kernel/vm_handoff_syscall.c`,
`kernel/vm_snapshot.c`, `kernel/address_space_handoff_test.c`,
`tests/host/vm_handoff_syscall_test.c`, `tests/host/vm_handoff_test.c`, and
`tests/host/vm_bootstrap_test.c`.

### Kernel PM control

`kernel/pm_control_core.c`, `kernel/pm_control_syscall.c`,
`tests/host/pm_control_test.c`, and
`tests/host/pm_control_execution_test.c`.

### VM service

`servers/vm/vm_core.c`, `servers/vm/vm_image.c`,
`lib/vm/vm_boot_validate.c`, `kernel/vm_scratch_preemption_test.c`,
`tests/host/vm_core_test.c`, `tests/qemu/vm_server.c`, and
`tools/generate_vm_scratch_preemption_fixture.py`.

### PM service

`servers/pm/pm_core.c`, `servers/pm/pm_service.c`,
`lib/spawn/spawn_request.c`, `tests/host/pm_test.c`,
`tests/host/spawn_abi_test.c`, `kernel/pm_service_test.c`, and
`tools/generate_pm_service_fixture.py`.

### RAMFS service

`servers/ramfs/ramfs_core.c`, `tests/host/ramfs_model_test.c`,
`tests/host/ramfs_test.c`, `tests/host/ramfs_seed_test.c`,
`tests/qemu/ramfs_service_vfs.c`, `kernel/ramfs_service_test.c`, and
`tools/generate_ramfs_service_fixture.py`.

### Kernel IPC

`kernel/ipc.c`, `kernel/ipc_syscall.c`, `kernel/ipc_test.c`,
`kernel/ipc_syscall_test.c`, `kernel/ipc_ecall_test.c`,
`tests/host/ipc_model_test.c`, `tests/host/ipc_notify_test.c`,
`tests/host/ipc_reply_test.c`, `tests/host/ipc_close_test.c`,
`tests/host/ipc_call_test.c`, and `tests/host/ipc_operation_test.c`.

### Kernel endpoints

`kernel/endpoint.c`, `kernel/endpoint_test.c`,
`tests/host/endpoint_test.c`, and `tests/host/endpoint_model_test.c`.

The Accepted
[exact endpoint contract](kernel-endpoint-source-organization.md) partitions
all 98 original definitions into 25 C sources and four private contracts,
with 90 moves and ten required long-function dispositions. Design #135 and
implementation #136 are independently reviewed and merged. The complete
fresh author plan, actual native/smoke/TTY-image reuse and identical
validated/reviewed squash tree close this correction.

### Kernel scheduler

`kernel/scheduler_core.c`, `kernel/scheduler.c`, `kernel/scheduler_test.c`,
`kernel/scheduler_invalid_test.c`, `tests/host/scheduler_model_test.c`, and
`tests/host/scheduler_test.c`.

### Kernel objects

`kernel/kernel_objects.c`, `kernel/object_model_test.c`, and
`tests/host/kernel_objects_test.c`.

At the endpoint-merged base `4eb1c4d`, these sources contain 4,426 lines,
76 definitions and eight long functions. Accepted
[ADR-0068](../adr/0068-kernel-object-source-decomposition.md) and the exact
[object contract](kernel-object-source-organization.md) name 23 C owners,
three private headers/sets, 72 original moves, 22 necessary portable private
helpers, eight long and four shorter task dispositions. The design also
preserves the original complete object-source hash through a bounded inverse
and avoids a retained-oracle import cycle through unchanged shared inspection.
Design #137 and implementation #138 are independently reviewed and merged.
All 23 real owners close both migrations as literal empty tuples; 128 current
definitions have a maximum of 92 physical lines. Original body/type/storage
and complete byte-source/recipe oracles remain enforced. Full fresh
author acceptance, actual three-profile same-producer reuse, identical
reviewed squash tree and obsolete local/remote branch cleanup close this
correction.

### Kernel bootstrap

`kernel/bootstrap_runtime.c`, `kernel/bootstrap_manifest.c`,
`kernel/bootstrap_syscall.c`, `kernel/bootstrap_image.c`, `kernel/main.c`,
`tests/host/bootstrap_manifest_test.c`, `tests/host/bootstrap_control_test.c`,
`tests/host/fdt_test.c`, `tests/qemu/bootstrap_launcher.c`, and
`tools/generate_bootstrap_fixture.py`.

### Trap and syscall routing

`kernel/trap.c`, `kernel/syscall.c`, `kernel/user_runtime_test.c`,
`tests/qemu/user_runtime_service.c`, `tests/host/runtime_test.c`, and
`tools/generate_user_runtime_fixture.py`.

### Grant data plane

`kernel/grant_copy.c`, `kernel/grant_syscall.c`, `kernel/grant_test.c`,
`kernel/grant_syscall_test.c`, `tests/host/grant_test.c`,
`tests/host/grant_copy_test.c`, and
`tests/host/grant_syscall_core_test.c`.

At object-merged base `6269d924`, those seven files contain 8,018 physical
lines, 95 definitions and fourteen long functions. Accepted
[ADR-0069](../adr/0069-grant-data-plane-source-decomposition.md) and the exact
[grant data-plane contract](grant-data-plane-source-organization.md) name
22 C owners, three private headers/sets, four consumer operands, 65 moves,
fourteen long and five shorter task dispositions. Registry/core/runtime/ABI
and public boundaries stay unchanged. The fixed MINIX trace, actual
fixture/script/model state and independent validation dimensions precede
implementation. Independent complete design review is clean. That design
contained documentation only and merged separately as #139.
Implementation started from that outcome with immutable inventories and
structural Red. Its current source outcome has all 22 real owners, 217
definitions bounded by 115 physical lines, all 65 original moves and all
nineteen semantic dispositions, with literal-empty migrations and immutable
original body/type/data inventories. Narrow native/model and real target
checks are green. Complete current 47-command fresh author acceptance,
actual same-original-producer three-profile reuse and complete independent
implementation review passed. Implementation #140 is merged with identical
reviewed squash tree, main fast-forward and exact local/remote branch cleanup.

### TTY

`kernel/tty_handoff_runtime.c`, `kernel/uart_console_test.c`,
`servers/tty/tty_service_core.c`, `tests/host/tty_model_test.c`,
`tests/host/tty_handoff_test.c`, `tests/host/tty_control_syscall_test.c`,
`tests/host/tty_control_test.c`, and
`tools/generate_tty_service_fixture.py`.

At grant-merged base `ac94a211`, these contain 5,803 C lines, 156 C
definitions, seven long C functions and one 195-line Python renderer.
Proposed [ADR-0070](../adr/0070-tty-function-and-evidence-decomposition.md)
and the [exact TTY contract](tty-source-organization.md) define 26 C owners,
five private headers/sets, eight consumer operands and 116 original moves.
The runtime state leaf prevents a facade/validation cycle; existing terminal,
UART, handoff/control, public ABI and test-double interfaces remain intact.
This is documentation-only design; its paired implementation stays pending.

### QEMU runner tooling

`tools/run_qemu_smoke.py` and `tests/host/test_run_qemu_smoke.py`.

### ELF checker tooling

`tools/check_user_elf.py` and `tests/host/test_check_user_elf.py`.

## Cohesive exceptions

The following files trigger a numeric review signal but currently retain one
cohesive responsibility and contain no function over 120 lines:

| File | Rationale |
| --- | --- |
| `servers/vfs/vfs_service_core.c` | One byte-exact VFS service codec and dispatch layer; ADR-0051 already records this exception. |
| `servers/vfs/vfs_state.c` | One complete VFS state-validation responsibility. |
| `kernel/bootstrap_control_core.c` | One portable bootstrap-control state machine. |
| `kernel/grant.c` | One generation-safe grant-registry lifecycle; copy and syscall responsibilities already live elsewhere. |
| `kernel/bootstrap_memory.c` | One bootstrap memory and typed-ownership runtime adapter. |
| `servers/tty/tty_core.c` | One portable TTY state machine. |
| `tools/validation_plan.py` | One fail-closed change-aware planning tool with short task-level functions. |
| `tests/host/test_validation_plan.py` | One focused validation-plan test module whose individual cases remain short. |

All 118 in-scope tracked implementation/test source files that trigger at
least one numeric review signal appear exactly once above, either in a
correction unit or as a cohesive exception.

An exception is not permanent immunity. A future change that introduces a
second responsibility, obscures the task hierarchy, or creates a non-atomic
function requires a new review and may activate a decomposition task.

## Sequencing rule

Only one correction pair is active at a time. Each design:

1. confirms the current responsibility and function-task boundaries;
2. records exact non-goals and behavior-preservation invariants;
3. names the target files and private dependency direction;
4. lists every non-atomic function and its required subtask outline;
5. defines structural and existing semantic validation;
6. receives independent design review before implementation starts.

Each implementation then moves or extracts the minimum reviewed boundary,
keeps every permanent commit green, receives one independent implementation
review, and is squash-merged as one task outcome.

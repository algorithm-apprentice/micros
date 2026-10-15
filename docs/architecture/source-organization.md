# Responsibility-Based Source Organization

This document defines the current source-organization architecture for
`micros`. The normative decision is
[ADR-0051](../adr/0051-responsibility-based-source-modules.md).

## Objective

Every production and test module should have one cohesive responsibility.
File size is useful evidence, but a god file is defined by mixed authority:
one file implements several independently testable state machines, resource
owners, or protocol layers that can change for different reasons.

The repository migrates existing mixed files incrementally. A decomposition
must preserve behavior unless a separately reviewed design explicitly changes
it.

## Module boundary rules

Use these rules when adding or reorganizing code:

1. Name the state, resource, protocol, or algorithm that the module owns.
2. Keep wire adapters and top-level dispatch separate from portable state
   transitions.
3. Keep public API declarations separate from private cross-module contracts.
4. Pass owned state explicitly instead of introducing mutable global context.
5. Export only helpers required by another responsibility. Keep all other
   functions file-local.
6. Preserve distinct architectural objects even when they share one fixed
   backing allocation.
7. Prefer a small facade over a central implementation file that forwards and
   also owns every operation.
8. Keep tests organized by behavior or state machine rather than mirroring one
   production god file.

## Review signals

The following are mandatory review signals, not automatic correctness
failures:

- more than 1,500 physical source lines in one implementation file;
- more than 25 top-level function definitions in one implementation file;
- a function longer than 120 physical lines;
- one file owning more than one independently testable protocol or lifecycle;
- a function combining validation, reservation, externally visible mutation,
  and rollback without named phase helpers;
- unrelated tests requiring unrelated fixtures in the same test source.

When a signal is present, the design or review must either split the
responsibility or explain why the code is one intrinsically cohesive
algorithm. Line-count-only splitting, anonymous `util` modules, and helpers
that hide authority behind globals do not satisfy this rule.

## Function boundaries

A function is one task. When that task contains independently nameable
subtasks, the function should orchestrate named helpers in the order those
subtasks occur. A reader should be able to understand the task outline from
the function names and control structure before reading each helper's
implementation.

A function is atomic only when further extraction would split one cohesive
algorithm, invariant, or commit boundary rather than expose a meaningful
subtask. Physical line count does not determine that boundary:

- a short function that mixes validation, reservation, mutation, and rollback
  still requires decomposition;
- a longer cohesive algorithm may remain intact when splitting it would hide
  rather than clarify its invariant;
- the 120-line signal requires review but never justifies mechanical
  line-count splitting.

Named helpers must expose domain work rather than merely wrap syntax. They
must not hide authority in mutable globals, duplicate ownership logic, or
reorder validation, backend calls, publication, rollback, or token
consumption.

A function should normally perform one of the following:

- decode and validate one request;
- reserve resources;
- commit one state transition;
- roll back one transition;
- translate one protocol result;
- implement one cohesive algorithm.

Large request handlers should separate preflight from commit so exact error
precedence and failure atomicity remain visible. Helper extraction must not
reorder validation, backend calls, reference changes, grant revocation, or
token consumption.

## VFS target layout

The first migration target is the portable VFS core. Its accepted runtime
behavior remains defined by
[ADR-0049](../adr/0049-vfs-service-and-application-io-protocol.md) and
[ADR-0050](../adr/0050-post-handoff-elf-spawn.md). The organizational baseline
is [the MINIX VFS source-organization note](../research/minix-vfs-source-organization.md).

The target production layout is:

| File | Owned responsibility |
| --- | --- |
| `vfs_core.c` | Public portable facade, application dispatch, PM-control dispatch, notification dispatch, and result shaping |
| `vfs_state.c` | Initialization, canonical whole-state validation, and validation-only helpers |
| `vfs_ownership.c` | Fixed-table lookup, slot reservation, descriptor/open-file/vnode reference primitives, and reference release |
| `vfs_transfer.c` | Checked client access plus VFS-owned direct-grant and bounce-page transfer primitives |
| `vfs_namespace.c` | One-time RAMFS root mount, path lookup, vnode integration, open/create, mkdir, and chdir |
| `vfs_io.c` | Close, regular-file read/write, getdents, and common application transfer routing |
| `vfs_console.c` | TTY submission, collection, retry, cancellation, pending-operation state, notification handling, and test drain |
| `vfs_process.c` | Process attach, trusted descriptor sharing, detach orchestration, and process-owned cleanup |
| `vfs_spawn.c` | Image and descriptor preparation, hidden commit/revert, abort, release, and spawn-token helpers |
| `vfs_internal.h` | Minimal private declarations shared only by the portable-core responsibility modules |
| `vfs_core.h` | Stable public portable VFS state and API |
| `vfs_service_core.c` | Byte-exact service-message codec and message-to-portable-facade dispatch |
| `vfs_service_core.h` | Stable private contract shared by the service loop and native service tests |
| `vfs_service.c` | Production IPC receive/reply loop and target backend adapter |

The private dependency direction is:

- `vfs_ownership.c` and `vfs_transfer.c` are low-level leaves;
- `vfs_namespace.c` depends only on ownership and transfer primitives;
- `vfs_console.c` depends only on ownership and transfer primitives;
- `vfs_io.c` may depend on ownership, transfer, namespace, and console
  primitives;
- `vfs_process.c` may depend on ownership and console cleanup primitives;
- `vfs_spawn.c` may depend on ownership, transfer, and namespace primitives;
- `vfs_state.c` may use read-only ownership queries but invokes no operation
  handler;
- `vfs_core.c` depends on state validation and the responsibility handlers;
- `vfs_service_core.c` depends only on `vfs_core.h` and the public portable
  facade;
- `vfs_service.c` depends on `vfs_service_core.h` plus target runtime and IPC
  adapters.

Cycles between responsibility modules are not allowed. Orchestration belongs
in the higher-level module; lower-level modules expose narrow primitives.

`vfs_internal.h` is not a service-layer header.
`vfs_service_core.c` and `vfs_service.c` must not include it.

`vfs_state.c` may inspect every public state object to enforce complete
invariants, but it must not invoke operation handlers. This is a validation
dependency, not ownership of those operations.

`vfs_service_core.c` currently has 34 functions and therefore triggers explicit
review. It remains one module because all of those helpers encode, decode, or
dispatch the same byte-exact service protocol and own no VFS resource state.
Its two oversized orchestration functions are still split as required below.

## VFS migration invariants

The first decomposition must preserve:

- every public declaration, enum value, capacity, and structure layout in
  `vfs_core.h`;
- application, PM-control, RAMFS, and TTY wire formats;
- endpoint, generation, token, grant, reference, and pending-operation
  validation;
- exact error precedence and no-mutation-on-failure behavior;
- second-hop grant creation and revocation ownership;
- executable-image, hidden-publication, rollback, abort, and release
  transitions;
- test-only interfaces and their compile-time guards;
- single-threaded, fixed-capacity, allocation-free v0.1 operation.

Cross-file functions use the `vfs_` prefix and are declared only in
`vfs_internal.h`. The implementation must not duplicate a helper to avoid a
proper internal dependency.

## Current VFS function disposition

At the reviewed base commit, the following VFS functions exceed the 120-line
review signal. None is an intrinsically cohesive algorithm, so every entry is
a required split. The listed phases are part of the accepted implementation
boundary; implementation may choose concise helper names but may not combine
the phases again.

| Function and current lines | Required phase split |
| --- | --- |
| `validate_open_files` (`vfs_core.c:389-511`, 123) | Per-entry shape; descriptor references; aggregate open-file and vnode reference counts |
| `validate_pending` (`vfs_core.c:585-737`, 153) | Pending request/result shape; process and descriptor ownership; backend page, grant, and debt invariants |
| `validate_spawn` (`vfs_core.c:979-1124`, 146) | Stage metadata; image/grant/page ownership; descriptor snapshot and hidden-child publication invariants |
| `handle_open` (`vfs_core.c:2077-2338`, 262) | Common request/path preflight; existing-node open; create path; descriptor/open-file publication |
| `handle_regular_read` (`vfs_core.c:2452-2576`, 125) | Range/grant preflight; RAMFS call and result validation; client copy and position commit |
| `handle_getdents` (`vfs_core.c:2773-2921`, 149) | Range/grant preflight; RAMFS call; record translation and copy; cursor commit |
| `handle_console_write` (`vfs_core.c:3042-3162`, 121) | Transfer preparation; client copy and TTY submission; pending-operation publication |
| `handle_transfer` (`vfs_core.c:3164-3370`, 207) | Common caller/descriptor preflight; zero-length completion; regular-file route; console route |
| `handle_mkdir` (`vfs_core.c:3372-3567`, 196) | Target lookup; parent/name derivation; RAMFS create call; result and reference cleanup |
| `prepare_spawn_image` (`vfs_core.c:4777-5080`, 304) | Transaction/token/path preflight; executable lookup and sizing; image transfer; prepared-stage commit |
| `prepare_spawn_descriptors` (`vfs_core.c:5082-5321`, 240) | Request/source-policy preflight; slot and reference reservation; descriptor metadata commit |
| `micros_vfs_handle_request` (`vfs_core.c:5735-5863`, 129) | Common application preflight; operation dispatch; result and final-state validation |
| `micros_vfs_service_build_pm_control_result` (`vfs_service_core.c:847-978`, 132) | Common reply header; operation-specific payload encoding; final canonical validation |
| `micros_vfs_service_handle_message` (`vfs_service_core.c:1283-1422`, 140) | Caller/message classification; decode and portable dispatch; reply encoding or notification completion |

## Implementation sequence

The implementation should remain green after each coherent commit:

1. Add the private interface and extract state, ownership, and transfer
   primitives without changing behavior.
2. Extract namespace and regular-I/O handling.
3. Extract console and process-lifecycle handling.
4. Extract the spawn transaction and reduce `vfs_core.c` to the facade.
5. Complete every required function split in the disposition table while
   preserving validation and commit order.

The implementation may adjust this move order when required to keep the
dependency graph acyclic, but it may not change the target ownership or mix
new VFS behavior into the refactor.

## Repository adoption

VFS is the first application of the policy because its core currently contains
the largest number of independent responsibilities and Step 14 still requires
additional lifecycle behavior.

The VFS decomposition is merged. The
[repository source-module audit](source-module-audit.md) classifies every
in-scope implementation and test review-signal file at its audited base
commit, records cohesive exceptions, and defines separate dependency-ordered
design and implementation task pairs. Existing large files are technical debt
to assess; this policy does not authorize one broad repository rewrite.

The first audit correction is the VFS test-source design. Its exact fixture,
behavior, protocol, model, target, tooling, private-contract, and function
boundaries are defined in
[VFS Test Source Organization](vfs-test-source-organization.md). The design
preserves the current test executables and workflows; it does not equate a
source-file split with an additional test job.

The next audit correction is the kernel address-space and execution-family
design. Its exact kernel-root, user-root, VM-transaction, execution,
private-contract, native-test, and QEMU-test boundaries are defined in
[Kernel Address-Space Source Organization](kernel-address-space-source-organization.md).
The design retains one bounded validation/release scratch owner, preserves
the existing public headers and evidence identities, and places wired
handoff above both validation families to keep the concrete module graph
acyclic.

The following correction is the frame-ownership family. Its exact
allocator-ledger validation, bootstrap mutation, one-way handoff, VM claim,
freeze, release, native-model, and target-scenario boundaries are defined in
[Frame-Ownership Source Organization](frame-ownership-source-organization.md).
The design keeps complete validation below every mutation owner, preserves
the public ledger contract and evidence identities, and retains one explicit
mutable context owner per split test family.

The next correction is the kernel VM-handoff family. Its exact register ABI,
portable boot/load/scratch/freeze/commit/release state, snapshot, privileged
adapter, native-test, and retained target-scenario boundaries are defined in
[Kernel VM-Handoff Source Organization](kernel-vm-handoff-source-organization.md).
The design keeps portable state below privileged adapters, preserves exact
transaction and failure ordering, and retains one mutable context owner per
evidence family.

The following correction is the kernel PM-control family. Its exact operation-
13 ABI, complete validation, reservation, execution preparation, activation,
exit, privileged adapter, and native-evidence boundaries are defined in
[Kernel PM-Control Source Organization](kernel-pm-control-source-organization.md).
The design keeps validation below lifecycle transitions, portable state below
privileged adapters, activation separate from rollbackable preparation, and
one authoritative mutable owner for target state and each native fixture
family.

The next correction is the VM service family. Its exact boot validation,
binary planning, portable state validation, frame selection, load/exit,
service-adapter, host-evidence, scratch-preemption, and fixture-generator
boundaries are defined in
[VM Service Source Organization](vm-service-source-organization.md). The
design keeps validation below lifecycle mutation, frame selection separate
from ELF/stack policy, one mutable context per evidence family, and common
service startup separate from scenario-specific protocols.

The shared IPC correction is defined by Accepted
[ADR-0059](../adr/0059-kernel-ipc-source-decomposition.md) and
[Kernel IPC Source Organization](kernel-ipc-source-organization.md).
It separates queue, completion, token, notification, deadlock, rendezvous,
close, syscall, reference-model, and target-script responsibilities while
preserving existing authority and execution identities. Complete endpoint/
object validation and the authoritative IPC runtime remain in their current
owners. Implementation is blocked until that design is independently
reviewed, Accepted, and merged.

The scheduler correction is defined by Accepted
[ADR-0062](../adr/0062-kernel-scheduler-source-decomposition.md) and
[Kernel Scheduler Source Organization](kernel-scheduler-source-organization.md).
It separates portable queue/transaction/validation/planning/accounting,
target return/completion/interrupt/idle adaptation, and independent native
and target evidence. State/fixture leaves stay below entry/action/projection
owners; original assembly custody, automatic witnesses, execution identities,
and four-dimensional validation classification remain unchanged.
Implementation starts only after the separate design is independently
reviewed, Accepted, and merged.

The selected user ELF checker correction is defined by Accepted
[ADR-0063](../adr/0063-user-elf-checker-source-decomposition.md) and its exact
[source contract](user-elf-checker-source-organization.md).
It separates binary representation/decoding, structural checks, service
policy, bounded instruction inspection, CLI, and private host evidence while
preserving the old facade, error order, fixture bytes, discovery identities,
and transitive validation ownership. Its independent design review is clean;
implementation remains pending the separate design merge.

The selected RAMFS correction is defined by Proposed
[ADR-0066](../adr/0066-ramfs-source-decomposition.md) and its exact
[source contract](ramfs-source-organization.md). It separates portable
filesystem tasks, native fixtures/cases, independent reference/projection,
target peer/report, and generator phases while preserving ADR-0048 behavior,
storage lifetimes, evidence identities, and closed validation eligibility.
Implementation remains pending clean independent design review, Acceptance,
and the separate design merge.

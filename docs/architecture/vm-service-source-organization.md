# VM Service Source Organization

This document defines the exact behavior-preserving source decomposition for
the VM service's boot import, portable state, frame selection, image and stack
planning, load/exit lifecycle, native evidence, service adapter, scratch-
preemption target scenario, and fixture generator. It applies ADR-0051 to the
sixth correction identified by the repository source-module audit.

The source-only correction below is merged. Its preserved interfaces and
evidence remain the historical baseline. The later Accepted ADR-0050
coordinator adds ordinary VM control, a production-owned service/backend
layer, and native service evidence as mapped in
[the coordinator implementation](pm-spawn-coordinator.md). Those new owners
borrow the existing state and buffers; they do not change the boot/core/image
ABI or replace the preserved synthetic and scratch adapters.
The existing VM native fixture now has its own shared source set so that the
new slow composed model can borrow its sole context. The `vm-core` CTest
retains its original six-case prefix and appends eight service-adapter cases;
the separate model is authorized by ADR-0050, not by the source-only
correction below.

Implementation must not begin until
[ADR-0057](../adr/0057-vm-service-source-decomposition.md) is Accepted and its
design pull request is merged.

## Reviewed base

The reviewed source base is:

```text
d3cb32b031ea7494ec00220cf879df96eb668907
```

The fixed local MINIX baseline and required `micros` adaptations are recorded
in
[the research note](../research/minix-vm-service-source-organization.md).

## Current inventory

The audit signal covers seven files:

| File | Physical lines | Functions | Longest function |
| --- | ---: | ---: | --- |
| `servers/vm/vm_core.c` | 2,351 | 46 | `vm_core_validate_state`, 474 |
| `servers/vm/vm_image.c` | 547 | 15 | `micros_vm_elf_plan`, 206 |
| `lib/vm/vm_boot_validate.c` | 704 | 13 | `micros_vm_boot_validate`, 343 |
| `kernel/vm_scratch_preemption_test.c` | 834 | 11 | `micros_vm_scratch_preemption_test_handle_trap`, 170 |
| `tests/host/vm_core_test.c` | 1,110 | 23 | `test_load_lifecycle`, 282 |
| `tests/qemu/vm_server.c` | 1,164 | 26 | `pm_test_load_child`, 179 |
| `tools/generate_vm_scratch_preemption_fixture.py` | 266 | 5 | `render_fixture`, 184 |
| **Total** | **6,976** | **139** | **11 functions above 120 lines** |

The numeric values are review signals. The boundaries below follow boot-object
validation, frame-database authority, deterministic frame selection, image and
stack planning, load and exit transactions, service adaptation, fixture
ownership, scenario phases, and generator tasks.

## Behavioral preservation

The decomposition preserves all Accepted behavior in ADR-0045 and ADR-0050.

### Stable interfaces

These headers remain byte-for-byte stable:

```text
include/micros/vm_bootstrap.h
servers/vm/vm_core.h
servers/vm/vm_image.h
kernel/vm_scratch_preemption_test.h
kernel/vm_scratch_preemption_test_fixture.h
```

No boot-object field, runtime state, mapping record, frame state, ELF or stack
plan, protocol code, result, token, command, marker, generated symbol, public
function, test hook, or target ABI changes.

### Authority and state

- VM remains the sole user-space owner of ordinary post-handoff frame
  selection and application mapping policy.
- The kernel remains the sole owner of frame-owner transitions, scratch PTE
  mutation, child page-table construction, mapping generations, load tokens,
  and privileged release.
- PM remains the sole lifecycle coordinator and exact caller of the VM
  service's private load/commit/exit protocol.
- VFS remains the sole executable-path and descriptor authority and the
  grantor of immutable executable/request snapshots.
- The fixed VM boot-information object remains the initial frame database. No
  second per-frame authority is introduced.
- One portable `micros_vm_state` retains one globally serialized loader
  transaction plus at most one mapping record per process slot.

### Transaction and failure ordering

The split preserves:

- complete boot-object validation before state initialization;
- caller input copies before lifecycle mutation;
- complete ELF and stack validation before `LOAD_BEGIN`;
- deterministic lowest-address frame selection before kernel mutation;
- one scratch map/fill/commit at a time in ascending child-VA order;
- complete freeze-plan construction before kernel freeze;
- exact local mirroring after successful kernel transitions;
- reverse rollback for every preactivation failure;
- activated load commit before clearing the loader transaction;
- exit release only from loader `IDLE` and one exact active mapping record;
- stable expected VM results for malformed, capacity, executable, and
  too-large input;
- fatal treatment of trusted-grant, kernel/VM, token, owner, or phase
  disagreement; and
- no success-shaped fallback after a non-failing kernel commit.

## VM portable-state layout

| Path | Owned responsibility |
| --- | --- |
| `servers/vm/vm_core.c` | Common byte/range/identity/digest primitives, initial-free bitmap primitives, and VM-state initialization |
| `servers/vm/vm_validation.c` | Complete boot-database immutability, frame/record agreement, transaction, and phase validation |
| `servers/vm/vm_frames.c` | Managed-frame translation, record membership, deterministic lowest-frame selection, and selected-set validation |
| `servers/vm/vm_load.c` | Input preparation, process/load binding, page iteration, scratch state, page fill, freeze, abort, and activated load commit |
| `servers/vm/vm_exit.c` | Active-record exit-release begin and completion |
| `servers/vm/vm_internal.h` | Sole declarations-only private portable-state contract |

Complete validation is read-only. Frame-selection helpers do not mutate
lifecycle state. Load and exit depend downward on validation and frame
selection but do not call one another.

`vm_core.c` owns no load or exit transition after the split. It retains the
common primitives used by at least two accepted owners and the public
`micros_vm_state_initialize` entry.

## Accepted bounded complete boot-database validation

Accepted
[ADR-0077](../adr/0077-bounded-complete-vm-boot-database-validation.md)
preserves the complete `vm_validation.c` authority while correcting the
measured fixed-capacity execution shape before VM readiness.

`vm_core_boot_database_is_immutable()` remains a read-only private predicate
inside complete state validation. Before any count-derived access, it requires
the already matched `managed_frame_count` to be nonzero and at most
`MICROS_VM_MAX_MANAGED_FRAMES`; matching header/summary corruption is not a
range proof. It then computes the identical imported digest through explicit
segments:

- immutable bytes before and after the zeroed header digest field;
- active frame states canonicalized from one loaded initial-free bitmap word
  per 64-frame chunk; and
- a proven-zero inactive frame-state tail advanced through the FNV digest with
  logarithmic exponentiation.

The same owner directly rejects nonzero inactive frame-state bytes, high bits
in the partial initial-free bitmap word, and every later nonzero bitmap word.
The full-word scan begins exactly at `managed_frame_count / 64` for an aligned
boundary or the following word for a partial boundary, then advances
contiguously through the fixed bitmap capacity. It makes no call to
`vm_core_initial_frame_is_free()` from the canonical digest path.

The public VM state, boot object, digest, frame encodings, service interface,
and validation call graph remain unchanged. `vm_core.c` retains the shared
bitmap query for unrelated frame and lifecycle owners. No validation cache,
epoch, duplicate boot database, or generalized digest framework is introduced.

The separate implementation follows that contract with four file-local owners
for bounded byte digestion, logarithmic zero advancement, active word/chunk
canonicalization, and inactive bitmap-tail validation. The existing complete
validator remains the only caller-facing owner.

## Stable public VM-core symbol ownership

| Owner | Public symbols |
| --- | --- |
| `vm_core.c` | `micros_vm_state_initialize` |
| `vm_validation.c` | `micros_vm_state_validate` |
| `vm_load.c` | `micros_vm_load_prepare`, `micros_vm_load_bind`, `micros_vm_load_next_page`, `micros_vm_load_scratch_mapped`, `micros_vm_load_fill_scratch`, `micros_vm_load_scratch_committed`, `micros_vm_load_freeze_plan`, `micros_vm_load_frozen`, `micros_vm_load_aborted`, `micros_vm_load_committed` |
| `vm_exit.c` | `micros_vm_exit_release_begin`, `micros_vm_exit_release_complete` |

The public declarations and state layouts remain in byte-identical
`vm_core.h`.

## Image, stack, and ELF layout

| Path | Owned responsibility |
| --- | --- |
| `servers/vm/vm_image.c` | Shared little-endian, zero/copy, checked-add, and range-overlap primitives |
| `servers/vm/vm_stack.c` | Canonical spawn-request stack planning and final child-stack construction |
| `servers/vm/vm_elf.c` | Fixed RISC-V ELF64 header, segment, range, permission, and page-plan validation |
| `servers/vm/vm_image_internal.h` | Declarations-only private binary-planning contract |

`micros_application_stack_plan` and `micros_application_stack_build` move to
`vm_stack.c`. `micros_vm_elf_plan` moves to `vm_elf.c`. The stable
`vm_image.h` remains the sole public contract.

Stack planning never calls ELF planning. ELF planning never calls stack
planning. Both depend only on common image primitives and Accepted spawn/ABI
constants.

## Boot-object validation

`lib/vm/vm_boot_validate.c` remains the cohesive fixed boot-information
validator and digest owner. It is not made into a general VM-state module.

`micros_vm_boot_validate` becomes a short orchestration over named phases:

1. header, capacity, size, reserved-field, and scalar identity validation;
2. physical and managed range canonicalization;
3. address-space and mapping slice/owner validation;
4. frame-state, independent-count, and zero-tail validation; and
5. digest recomputation and success-only summary publication.

The file remains one source because those phases validate one immutable object
under one public error namespace. No helper becomes a second boot database or
runtime frame authority.

## Native VM-core evidence

The existing `vm_core_test` executable and `vm-core` CTest remain unchanged.
`tests/host/vm_core_test.c` becomes the sole suite entry and retains this exact
case order:

1. mapping-record and portable-state ABI;
2. boot-information import;
3. complete load lifecycle;
4. abort paths;
5. capacity and invariant rejection; and
6. staged-input and immutable-database behavior.

The final sources are:

| Path | Owned responsibility |
| --- | --- |
| `tests/host/vm_core_test.c` | Sole `main`, exact case order, and final output |
| `tests/host/vm_core_test_fixture.c` | Sole mutable context, boot-object construction, request/ELF builders, digest helpers, and run-to-frozen setup |
| `tests/host/vm_core_boot_test.c` | ABI and boot-import evidence |
| `tests/host/vm_core_load_test.c` | Complete page/scratch/freeze/commit lifecycle evidence |
| `tests/host/vm_core_abort_test.c` | Pre- and post-freeze abort evidence |
| `tests/host/vm_core_validation_test.c` | Capacity, corruption, staged-input, and immutable-database evidence |
| `tests/host/vm_core_test_internal.h` | Declarations-only private fixture and case contract |

Exactly one mutable `vm_core_test_context` owns boot information, portable VM
state, executable/request buffers, and scratch page. Snapshot copies remain
local values.

## VM service target layout

The current target VM service remains the real service used by the existing
QEMU integration gates. The source split does not create another service ELF.

| Path | Owned responsibility |
| --- | --- |
| `tests/qemu/vm_server.c` | `micros_service_main`, common phase order, compile-time scenario selection, and receive-loop dispatch |
| `tests/qemu/vm_server_fixture.c` | Exported bootstrap configuration/boot object, sole mutable service context, byte codecs, service lookup, and configuration validation |
| `tests/qemu/vm_server_handoff.c` | Operation-12 handoff summary and token-bound launcher readiness |
| `tests/qemu/vm_server_probe.c` | Retained handoff grant-copy probe |
| `tests/qemu/vm_server_tty.c` | Console-map notification validation and one-shot UART mapping |
| `tests/qemu/vm_server_scratch.c` | Scratch-preemption driver request, free-frame selection, real scratch map/commit, and stale-load trigger |
| `tests/qemu/vm_server_pm.c` | PM-test synthetic child load, freeze, commit, exit release, reply codec, and exact PM-only state |
| `tests/qemu/vm_server_internal.h` | Declarations-only private service/scenario contract |

Exactly one mutable `vm_server_context` owns portable VM state and
compile-time test state. The required exported
`micros_bootstrap_service_config` and `micros_vm_boot_info` objects remain
separate exact ABI objects in `vm_server_fixture.c`.

The common facade does not implement scenario protocols. Scenario modules do
not call one another. Compile-time guards, ordinary startup order, handoff,
readiness, self-fault points, scenario dispatch, receive-loop behavior, and
all existing test-only traps remain exact.

The current functions move to these owners:

| Owner | Current functions |
| --- | --- |
| `vm_server_fixture.c` | `vm_server_clear_bytes`, `vm_server_bytes_are_zero`, `vm_server_write_u32_le`, one consolidated `vm_server_read_u64_le`, `vm_server_read_u32_le`, `vm_server_find_service`, `vm_server_validate_configuration` |
| `vm_server_handoff.c` | `vm_server_send_ready`, `vm_server_complete_handoff` |
| `vm_server_probe.c` | `vm_server_serve_probe` |
| `vm_server_tty.c` | `vm_server_notification_is_canonical`, `vm_server_map_tty_uart` |
| `vm_server_scratch.c` | `vm_server_find_free_frame`, `vm_server_run_scratch_preemption` |
| `vm_server_pm.c` | every `pm_test_*` helper and `pm_test_serve_request` |
| `vm_server.c` | `micros_service_main` |

The two current conditional definitions of `vm_server_read_u64_le` become one
private fixture helper with identical decoding.

## Scratch-preemption kernel evidence

The retained scratch-preemption QEMU kernel scenario is split into:

| Path | Owned responsibility |
| --- | --- |
| `kernel/vm_scratch_preemption_test.c` | Public launch and before/after timer/syscall hooks |
| `kernel/vm_scratch_preemption_test_fixture.c` | Expected services, profiles, policies, runtime configuration, and sole mutable scenario context |
| `kernel/vm_scratch_preemption_test_runtime.c` | Panic boundary, SATP/current-thread helpers, and complete runtime-state validation |
| `kernel/vm_scratch_preemption_test_report.c` | Final stale-fault, mapping, ownership, and baseline success report |
| `kernel/vm_scratch_preemption_test_trap.c` | Trap-stage classification and state transitions |
| `kernel/vm_scratch_preemption_test_internal.h` | Declarations-only private scenario contract |

The public `micros_vm_scratch_preemption_test_handle_trap` symbol moves to the
trap owner. The other four public hooks remain in the facade. One explicit
`vm_scratch_preemption_test_context` replaces the separate phase, timer,
physical-address, load-token, and process globals.

Scenario phase order, ordinary timer/scheduler switching, inactive-root
validation, VM reactivation, exact fault PC/VA, marker, and 45-second timeout
remain unchanged.

## Fixture generator

`tools/generate_vm_scratch_preemption_fixture.py` remains one CLI because it
produces one generated C fixture from four checked ELFs. Its long
`render_fixture` function becomes a short orchestration over:

- validated launcher/VM/PM/driver descriptors and required symbols;
- resident-page limits;
- segment/image table rendering;
- exact four-entry manifest rendering; and
- exported trap/resume/fault address rendering.

The command-line arguments, error prefix, generated includes, image IDs,
manifest tuple/order, user-page limits, symbol names, and output bytes remain
stable. No reusable framework or second generator module is added.

## Dependency direction

The permitted production direction is:

```text
vm_image -> no VM planning owner
vm_stack -> vm_image
vm_elf -> vm_image
vm_frames -> vm_core common state/bitmap primitives
vm_validation -> vm_core common + vm_frames + public image planners
vm_load -> vm_core common + vm_validation + vm_frames
           + public image planners
vm_exit -> vm_core common + vm_validation + vm_frames
vm_core initialization -> boot validator + file-local common/bitmap primitives
```

`vm_validation.c` calls no load or exit transition. `vm_load.c` and
`vm_exit.c` do not call one another. Image/stack/ELF modules do not include
the service private header.

The target service facade may call each scenario owner. Scenario owners call
only shared fixture/runtime helpers and the production runtime/syscall APIs;
they do not call sibling scenario handlers.

Scratch-preemption target validation remains below trap/report orchestration.
The report and trap modules do not duplicate mutable scenario state.

## Exact public/test owner migration

The structural migration map contains 23 entries:

- 13 moving public VM-core functions;
- three moving public stack/ELF functions;
- one moving public scratch-preemption trap hook; and
- six moving native VM-core test cases.

The following remain in their current owners:

- `micros_vm_state_initialize`;
- `micros_vm_boot_digest` and `micros_vm_boot_validate`;
- the four non-trap public scratch-preemption hooks;
- `micros_service_main`; and
- the native test `main`.

Static helper ownership in the VM service and scratch-preemption scenario is
verified separately by exact owner tables and dependency checks.

## Current long-function disposition

Every current over-limit function is a required semantic split. None receives
an algorithmic exception.

| Function and current lines | Required task outline |
| --- | --- |
| `vm_core_validate_state` (474) | Base state/boot immutability; local frame counts; per-record shape/entry ownership; cross-record uniqueness; loader-phase-specific validation |
| `micros_vm_load_prepare` (197) | Argument/alias preflight; ELF and stack planning; deterministic frame selection; candidate construction; input snapshot publication |
| `micros_vm_load_bind` (145) | Token/record collision preflight; table-entry construction; data-entry construction; local reservation and loading publication |
| `micros_vm_elf_plan` (206) | ELF header/table preflight; segment decoding; cross-segment range/permission/contiguity checks; final ordered page plan |
| `micros_vm_boot_validate` (343) | Header/capacity checks; ranges; address spaces/mappings; frame states/counts/zero tail; digest and summary publication |
| `micros_vm_scratch_preemption_test_handle_trap` (170) | Common trap identity; peer-hold transition; timer/preemption transition; VM-reactivation transition; stale-fault success transition |
| `report_success` (133) | Authority acquisition; exact fault/context; load/mapping record; ownership/PTE; final baseline and marker |
| `initialize_boot_info` (127) | Header and ranges; address spaces/mappings; frame-state population; finalize/digest |
| `test_load_lifecycle` (282) | Prepare/bind; ordered page actions; scratch fill/commit; freeze; activated commit and final state |
| `pm_test_load_child` (179) | PM request/load-begin validation; ordered scratch page construction; freeze plan/result; reply publication |
| `render_fixture` (184) | Descriptor/symbol validation; limits; images; manifest; exported addresses |

The final implementation must remove all 11 entries from the long-function
migration collection. Renaming or moving an unchanged long function does not
satisfy the gate.

## Build ownership

`CMakeLists.txt` defines these exact ordered reusable source sets:

```text
MICROS_VM_BOOT_VALIDATION_SOURCES
  lib/vm/vm_boot_validate.c

MICROS_VM_IMAGE_SOURCES
  servers/vm/vm_image.c
  servers/vm/vm_stack.c
  servers/vm/vm_elf.c

MICROS_VM_CORE_SOURCES
  servers/vm/vm_core.c
  servers/vm/vm_validation.c
  servers/vm/vm_frames.c
  servers/vm/vm_load.c
  servers/vm/vm_exit.c

MICROS_VM_CORE_HOST_TEST_SOURCES
  tests/host/vm_core_test.c
  tests/host/vm_core_test_fixture.c
  tests/host/vm_core_boot_test.c
  tests/host/vm_core_load_test.c
  tests/host/vm_core_abort_test.c
  tests/host/vm_core_validation_test.c

MICROS_VM_SERVICE_QEMU_SOURCES
  tests/qemu/vm_server.c
  tests/qemu/vm_server_fixture.c
  tests/qemu/vm_server_handoff.c
  tests/qemu/vm_server_probe.c
  tests/qemu/vm_server_tty.c
  tests/qemu/vm_server_scratch.c
  tests/qemu/vm_server_pm.c

MICROS_VM_SCRATCH_PREEMPTION_QEMU_TEST_SOURCES
  kernel/vm_scratch_preemption_test.c
  kernel/vm_scratch_preemption_test_fixture.c
  kernel/vm_scratch_preemption_test_runtime.c
  kernel/vm_scratch_preemption_test_report.c
  kernel/vm_scratch_preemption_test_trap.c
```

The existing consumers are:

| Consumer | Required expansions |
| --- | --- |
| `vm_bootstrap_test` | Boot-validation set once |
| `spawn_abi_test` | Image set once |
| `vm_core_test` | Boot-validation, image, core, and host-test sets once |
| `micros_vm_server` | Boot-validation, image, core, and QEMU-service sets once |
| `micros` | Boot-validation set once |
| `MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST` target sources | Scratch-preemption QEMU set once |

No split source is listed directly beside its source set. No executable,
CTest, user ELF target, generated fixture, custom target, or workflow is
renamed.

## Validation ownership

Every new path inherits the exact validation classification of its current
monolithic owner through `tools/validation_plan.py`.

- Runtime validation/frame/load/exit paths inherit
  `servers/vm/vm_core.c`.
- Stack/ELF/private-image paths inherit `servers/vm/vm_image.c`.
- Host evidence paths inherit `tests/host/vm_core_test.c`.
- VM service scenario paths inherit `tests/qemu/vm_server.c`.
- Scratch-preemption kernel scenario paths inherit
  `kernel/vm_scratch_preemption_test.c`.
- The structural test itself is host-unit-only.

Existing unknown/full-tier ownership remains fail-closed and is not narrowed
opportunistically by this source correction.

## Stable evidence identities

The implementation preserves:

- native executable `vm_core_test` and CTest `vm-core`;
- native executables/CTests `vm_bootstrap_test`/`vm-bootstrap` and
  `spawn_abi_test`/`spawn-abi`;
- the real `micros_vm_server` ELF and its 160-page resident limit;
- `test-qemu-vm-handoff`, `test-qemu-pm-service`,
  `test-qemu-vm-scratch-preemption`, `test-qemu-tty`,
  `test-qemu-ramfs`, `test-qemu-vfs`, `test-qemu-vm-ready-early`,
  `test-qemu-vm-self-fault`, and
  `test-qemu-vm-self-fault-sealed`;
- every current marker, expected panic, timeout, self-fault point, scratch
  assembly hook, and service-loop order;
- generator command-line flags, output path, manifest/image IDs, and exported
  symbol names; and
- current validation-plan and CI identities.

No split creates another behavioral executable, CTest, user ELF, custom
target, workflow, marker, timeout, generated fixture, or duplicate execution.

## Structural Red gate

Implementation begins with:

```text
tests/host/test_vm_service_source_layout.py
```

The fail-closed gate verifies:

- exact accepted production, host, QEMU, kernel-scenario, private-header, and
  generator family paths;
- exact ordered CMake source sets and one accepted expansion per consumer;
- no direct raw split-source listings;
- declarations-only private headers and no textual source inclusion;
- byte-identical stable headers;
- exact public/test function ownership and empty 23-function migration;
- exact service/host/scratch mutable-state owners;
- exact production, service-adapter, and target-scenario dependency maps;
- recursive repository-local quoted-include closure;
- no C function above 120 lines in that closure;
- no Python function above 120 lines in the fixture generator;
- exact host case order, service phase order, generated symbol identities,
  target/workflow/marker/timeout identities, and validation inheritance; and
- empty final 11-function long migration.

Family inventory and state/context assertions are unconditional. Dependency
checks use identifier references across each owner's complete local include
closure, with stable declaration-only headers treated explicitly.

The implemented gate retains the initial migration inventories for review
history, but both active migration collections are empty. It also checks
file-scope object declarations, including initialized and inline-aggregate
definitions, so a differently named global cannot bypass context ownership.
The existing generator test compares its complete synthetic fixture against
the pre-extraction SHA-256 output baseline.

## Implementation sequence

The implementation proceeds as small green commits:

1. add the structural Red gate, exact source sets, private-header skeletons,
   and complete migration collections;
2. split shared image primitives, stack planning, ELF planning, boot-validator
   phases, and matching native fixture/boot evidence;
3. split frame selection and complete portable-state validation;
4. split load and exit transactions plus load/abort/validation native cases;
5. split the VM service facade, fixture, handoff, probe, TTY, scratch, and PM
   scenario adapters;
6. split the scratch-preemption kernel fixture/runtime/report/trap owners and
   decompose generator rendering phases;
7. empty both migration collections and run one authoritative pull-request
   validation plan; and
8. obtain independent implementation review.

Each intermediate commit remains green and runs only the narrow existing
evidence for its active slice. The final
`tools/validation_plan.py --tier pr --base <review-base> --execute` result is
authoritative. Workflows already selected by that plan are not rerun
separately.

## Non-goals

This source correction does not authorize:

- public boot, VM-core, image, stack, ELF, service, PM/VM, or operation-12 ABI
  changes;
- new frame policy, mapping state, loader phase, protocol result, grant path,
  page-table behavior, or error precedence;
- page faults, demand paging, file-backed mappings, fork, copy-on-write,
  shared memory, reclaim, overcommit, recovery, live update, or SMP;
- decomposition of kernel operation 12, PM control, VFS, PM service, scheduler,
  IPC, endpoint, grant, frame, or address-space owners assigned elsewhere;
- a new VM service executable, fixture format, workflow, marker, timeout, or
  CI job; or
- mechanical line-count splitting without cohesive task boundaries.

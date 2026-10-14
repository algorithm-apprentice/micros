# Kernel Bootstrap Source Organization

This is the source contract for the bootstrap correction in
[the source-module audit](source-module-audit.md).
[ADR-0060](../adr/0060-kernel-bootstrap-source-decomposition.md) is Accepted
and its design merged as PR #119, commit
`531b191529c53f900dee1cb4b642bb85d58138c7`.

Implementation is in progress. The structural Red gate, nine source sets,
private-header/source skeletons and validation inheritance are integrated.
Portable common/profile/manifest validation and runtime transitions now have
their reviewed owners and named phases. The active migrations retain 54
symbols and 24 long functions in the other existing owners; remaining
skeletons do not claim that the final decomposition is complete.

## Reviewed base and scope

The reviewed implementation base is:

```text
49706a8ef8ef2296c42dc1ecadef0df375d2ee2f
```

This is the merged production coordinator outcome, PR #118. The correction
changes source ownership, private helper boundaries, and named function phases,
not the manifest, boot algorithm, privilege policy, readiness protocol, syscall
ABI, target behavior, or evidence identity.

The ten audited sources currently contain 12,621 physical lines:

| Current source | Lines | Responsibility mixed in the current owner |
| --- | ---: | --- |
| `kernel/bootstrap_runtime.c` | 2,006 | Storage, preparation, resident image/configuration access, rollback, diagnostics, validation, publication, launch |
| `kernel/bootstrap_manifest.c` | 1,720 | Manifest/profile validation, topology, portable runtime transitions |
| `kernel/bootstrap_syscall.c` | 1,289 | Common authority, console begin, release, readiness reply, sealing, failure |
| `kernel/bootstrap_image.c` | 288 | One cohesive image-catalog validator with a long per-image orchestration |
| `kernel/main.c` | 590 | Boot foundation, diagnostics, all compile-time component dispatch, shutdown |
| `tests/host/bootstrap_manifest_test.c` | 2,608 | Fixtures, manifest domains, runtime cases, independent reference and replay |
| `tests/host/bootstrap_control_test.c` | 1,912 | Fixtures, decoder, lifecycle, sealing/notification, image catalog |
| `tests/host/fdt_test.c` | 1,116 | Serialized blob construction, format and memory-policy cases, TAP entry |
| `tests/qemu/bootstrap_launcher.c` | 737 | Exported image objects, independent view validation, control transport, serial readiness policy |
| `tools/generate_bootstrap_fixture.py` | 355 | Shared descriptor validation and one bootstrap fixture rendering |

The fixed behavioral evidence and source-organization adaptations are in
[the bootstrap source study](../research/minix-bootstrap-source-organization.md).
The existing [RS/SEF study](../research/minix-rs-sef-bootstrap.md) remains the
authority for its runtime classification ledger.

## Preservation boundary

Preserve all Accepted behavior in ADR-0043, ADR-0044, ADR-0045, ADR-0046,
ADR-0047, ADR-0049, and ADR-0050, including the currently extended
capacity-seven objects and six-service production chain.

The following existing interfaces remain byte-for-byte stable:

```text
include/micros/bootstrap.h
include/micros/bootstrap_control.h
include/micros/fdt.h
kernel/bootstrap_runtime.h
kernel/bootstrap_image.h
kernel/bootstrap_syscall.h
kernel/bootstrap_control_internal.h
kernel/bootstrap_test.h
kernel/bootstrap_test_fixture.h
tests/qemu/bootstrap_protocol.h
tests/qemu/bootstrap_launcher_control.h
```

The implementation's first structural gate records hashes from the reviewed
base, not dynamically computed expected values from the current worktree.
It must also preserve the complete current contents of
`kernel/bootstrap_control.c` and `kernel/bootstrap_control_core.c`.

The latter is a directly coupled cohesive exception: its 27 functions implement
one bounded portable control-state lifecycle, have a longest body of 119 lines,
own no target singleton, and already expose distinct preflight/commit
boundaries. Moving that state machine is not required to separate the ten
audited owners. Its bounded inline lookup in the existing private header is
preserved explicitly; it is not permission to add bodies to new private
headers.

There is no change to:

- validation, diagnostic, syscall-result, or malformed-field precedence;
- one-shot initialization, exact profiles, generations, or endpoint visibility;
- two-pass preparation, image/stack zeroing, patch/readback, or `fence.i`;
- rollback order, baseline checks, or consumed generations;
- VM, PM, console role binding and retained deadline order;
- release publication, token-bound ready acknowledgment, or sealing;
- UART ownership, PLIC gates, panic behavior, markers, or timeouts;
- public function signatures or generated fixture interfaces; or
- target, CTest, preset, workflow, model, and image-budget identities.

## Portable manifest ownership

| Final owner | Responsibility |
| --- | --- |
| `kernel/bootstrap_manifest.c` | Public manifest validator facade and ordered validation orchestration |
| `kernel/bootstrap_manifest_profiles.c` | Profile-table shape, immutable standard profiles, role/profile relationships |
| `kernel/bootstrap_manifest_common.c` | Shared byte/name/alignment primitives required by at least two portable owners |
| `kernel/bootstrap_transitions.c` | Portable runtime initialize/release/ready/complete/classification lifecycle |
| `kernel/bootstrap_manifest_internal.h` | Declarations-only portable helper and automatic validation-candidate contract |
| `kernel/bootstrap_image.c` | Existing image-catalog API and named per-image validation phases |

The manifest facade retains `micros_bootstrap_manifest_validate` and
`micros_bootstrap_manifest_validate_detailed`. It prepares an automatic
candidate, validates completely, and publishes the output plan only on
success. Diagnostic publication remains exactly where the current validator
defines it; malformed argument/alignment rejection does not manufacture a
diagnostic.

Move these six public functions together to `bootstrap_transitions.c`:

```text
micros_bootstrap_runtime_initialize
micros_bootstrap_runtime_release
micros_bootstrap_runtime_release_retaining_deadline
micros_bootstrap_runtime_accept_ready
micros_bootstrap_runtime_complete
micros_bootstrap_classify_ready
```

Their candidate-copy and failure-preservation behavior stays unchanged.
The portable runtime is not the privileged target runtime singleton.

Profile validation and topology invoke no transition. The independent launcher
validator does not call the kernel manifest implementation.

## Privileged preparation and runtime

| Final owner | Responsibility |
| --- | --- |
| `kernel/bootstrap_runtime.c` | Public prepare, controller publication, launch orchestration |
| `kernel/bootstrap_runtime_state.c` | Sole target runtime context, existing state accessors, active-controller query, shared target primitives |
| `kernel/bootstrap_preparation.c` | Configuration/policy preflight, ordered service preparation, held-thread preparation, VM/PM/TTY role binding |
| `kernel/bootstrap_preparation_image.c` | Image/stack/manifest pages, exact verification, user-range transfer, service-table/configuration patch and readback |
| `kernel/bootstrap_preparation_rollback.c` | Reverse record release, preparation unwind, reset and final baseline validation |
| `kernel/bootstrap_runtime_validation.c` | Complete read-only target and prepared-VM validation |
| `kernel/bootstrap_diagnostics.c` | Existing diagnostic strings, one-shot failure record/panic, deadline checking |
| `kernel/bootstrap_runtime_internal.h` | Declarations-only context, preparation records and narrow cross-owner contracts |

One private context consolidates only these currently separate mutable
statics:

```text
bootstrap_state
validation_plan
image_infos
prepared_bindings
preparation_records
preparation_order
preparation_baseline
preparation_diagnostic
prepared_vm_snapshot
failure_record_emitted
```

The context owner returns a private pointer; all other owners receive an
explicit context or a read-only borrow. Existing public state accessors keep
their exact null-before-initialization behavior and return the same one live
control-state member. No second runtime, preparation ledger, image catalog,
frame database, current process/thread, or reset mechanism is added.

The preparation records and baseline retain their existing fields and meaning.
Consolidation must not change the ordering of clearing diagnostics, resetting
VM/PM/TTY preparation state, reading the allocator, validating catalog/policies/
manifest, initializing IPC/grants, or capturing the rollback baseline.

The runtime prepare outline is:

1. argument and one-shot shape preflight;
2. IRQ save and exact preparation reset;
3. catalog, policy and manifest validation;
4. IPC/grant initialization and baseline capture;
5. complete two-pass service preparation;
6. VM snapshot, PM-control and TTY-handoff preparation in that order;
7. scheduler initialization;
8. control-state preparation and optional console binding; and
9. the existing single IRQ restoration and returning result.

The service-preparation outline retains:

1. copying the validated preparation order;
2. each exact process/root/image/stack/manifest-view/endpoint preparation;
3. per-record binding publication after exact process resolution;
4. complete configuration patch/readback followed by held-thread preparation;
5. instruction synchronization; and
6. root/owner validation.

Every failed phase uses the current diagnostic and reverse cleanup, including
the current partially prepared record. Rollback cannot allocate, publish a
service, rewind generations, or substitute a successful result.

Controller publication preserves its current preflight and no-recoverable-
failure commit. Do not turn impossible post-publication conditions into
ordinary returning errors while extracting helpers.

The diagnostic owner observes the service state before setting failed phase,
sets both existing phase fields, and emits at most one failure record.
Deadline checks preserve the console check before the ordinary starting-service
deadline check and preserve all current detail values.

Move the existing public accessors/query to the state owner, the two public
validators to the validation owner, and the three failure/deadline entry
points to the diagnostic owner. No public declaration moves or changes.

## Captured bootstrap syscall ownership

| Final owner | Responsibility |
| --- | --- |
| `kernel/bootstrap_syscall.c` | Retained public entry, argument/decode/authority/runtime preflight and command dispatch |
| `kernel/bootstrap_syscall_common.c` | Existing result/error mapping, authority/role predicates and fatal boundary helpers |
| `kernel/bootstrap_syscall_release.c` | Console-begin and service-release transactions |
| `kernel/bootstrap_syscall_ready.c` | Token-bound readiness acknowledgment and captured completion |
| `kernel/bootstrap_syscall_complete.c` | Launcher sealing and captured return selection |
| `kernel/bootstrap_syscall_failure.c` | Launcher failure semantics, exact reason/detail validation and nonreturning report |
| `kernel/bootstrap_syscall_internal.h` | Declarations-only handler/helper contract |

All transaction plans remain automatic. No new syscall state owner, command,
register, permission bit, profile, or caller-selected authority is introduced.
The common facade preserves:

```text
argument invariant
-> ABI decode
-> authoritative state/registry resolution
-> exact controller authorization
-> complete runtime validation
-> grants resolution
-> command handler
```

Command owners do not call sibling command handlers. They depend downward on
the common predicates, existing portable control, and existing privileged
mechanisms.

Console-begin phases expose its existing identity/phase checks, VM/TTY/owner
validation, UART plan, retained-deadline plan, notification preflight,
prevalidated quiesce/deadline/UART/console/notification commit, and final
invariant check. Release keeps the ordinary versus retained-console-deadline
branches and exact result mapping.

Ready phases expose role/TTY planning, portable ready plan, current-context
capture and IPC guard, expected-caller reply with guard rollback on rejection,
ready/TTY commit, no-message completion, and current-IPC commit. Successful
reply acceptance precedes the existing local ready commit exactly as today.

Complete preserves the exact portable seal plan, current-IPC preflight/guard,
scheduler removal, source-only endpoint transition, prevalidated state and PM
event commit, and final invariant validation.

## Boot entry and compile-time evidence dispatch

| Final owner | Responsibility |
| --- | --- |
| `kernel/main.c` | Sole `kernel_main` entry and short ordered boot outline |
| `kernel/boot_foundation.c` | Existing object/trap initialization, FDT discovery/report, allocator/ownership/MMU/PLIC preparation and markers |
| `kernel/boot_component_tests.c` | Existing compile-time component branches, exact order, marker strings and returned-test panic fallbacks |
| `kernel/boot_internal.h` | Declarations-only automatic boot-context/phase contract |

The boot context is automatic in the original `kernel_main` invocation. It
borrows the existing allocator, ledger, report, registry and hart objects and
owns the original FDT map. It does not introduce mutable global boot/current
state or move register-sensitive execution capture out of its existing owner.

Preserve initialization and marker order, every `MICROS_BUILD_*` condition,
the order of branches that return and branches that never return, and exact
shutdown/reset-return fallback. Component helpers do not call siblings or
move implementation out of the mechanisms they exercise.

This is source extraction, not a runtime test registry or a new boot framework.
The source-only correction does not touch trap/syscall mechanism policy.

## Launcher source ownership

| Final owner | Responsibility |
| --- | --- |
| `tests/qemu/bootstrap_launcher.c` | Sole service entry, automatic launcher context, serial service loop and complete fallback |
| `tests/qemu/bootstrap_launcher_manifest.c` | Independent manifest-view/configuration validation and lowest-ID order |
| `tests/qemu/bootstrap_launcher_protocol.c` | Readiness/console message validation and exact failure classification |
| `tests/qemu/bootstrap_launcher_common.c` | Three existing exported ABI/data objects, byte/name and bounded lookup helpers |
| `tests/qemu/bootstrap_launcher_control.c` | Existing console wrapper plus unchanged private operation-11/fatal-report transport |
| `tests/qemu/bootstrap_launcher_internal.h` | Declarations-only automatic context and helper contract |

The three existing exported objects are:

```text
micros_bootstrap_service_config
micros_bootstrap_launcher_rodata
micros_bootstrap_launcher_data
```

Their types, qualifiers, initial bytes, symbols and roles remain exact. The
manifest pointer, order and local service-state arrays remain automatic in the
original entry invocation. They may be fields of one automatic context, never
a second global manifest or service registry.

Preserve the independent validation subset, current shape-detail order, source
resolution, and early/duplicate/foreign classification. In particular, payload
service/source detail checks remain conditional on the same starting-service
match. Console begin and mapped-event receipt still precede release; readiness
receipt and acceptance precede the next release. The loop never gains polling,
retries, sleeps, concurrent readiness, or kernel-manifest helper calls.

## Native evidence ownership

The existing entry files retain `main`, exact case order and output:

```text
tests/host/bootstrap_manifest_test.c
tests/host/bootstrap_control_test.c
tests/host/fdt_test.c
```

| Planned source family | Responsibility |
| --- | --- |
| `bootstrap_manifest_fixture.c` | Shared manifest-only input construction and automatic fixture types |
| `bootstrap_manifest_acceptance_test.c` | Contract, valid manifest and rejection cases |
| `bootstrap_manifest_topology_test.c` | Mapping capacity and topology cases |
| `bootstrap_manifest_tty_test.c` | Existing TTY role/profile contract case |
| `bootstrap_runtime_test.c` | Portable runtime and retained-deadline cases |
| `bootstrap_runtime_model.c` | Existing replay entry, input/production dispatch, trace and comparison |
| `bootstrap_runtime_reference.c` | Independent reference transitions/classification/projection |
| `bootstrap_control_fixture.c` | Separate control fixture construction and automatic setup/completion helpers |
| `bootstrap_control_abi_test.c` | Valid/malformed decoder and failure-detail cases |
| `bootstrap_control_lifecycle_test.c` | Bounded binding lookup and control lifecycle cases |
| `bootstrap_control_completion_test.c` | Seal rejection, IPC dependencies and PM notification cases |
| `bootstrap_control_image_catalog_test.c` | Existing catalog case and one static-lifetime three-page fixture context |
| `fdt_test_fixture.c` | Serialized FDT constants, options, builders and named blob phases |
| `fdt_header_test.c` | Binary/header/block/token/property validation cases |
| `fdt_memory_test.c` | Memory/reservation/node policy and range-capacity cases |

All names above are under `tests/host/`. New private headers are
`bootstrap_manifest_test_internal.h`, `bootstrap_runtime_model_internal.h`,
`bootstrap_control_test_internal.h`, and `fdt_test_internal.h`. They contain
types, constants, case/helper prototypes and existing assertion macros only.
Production never includes them.

Do not unify the different manifest/control profile builders merely because
both currently use a static helper called `profile`. Preserve their separate
input policies. Existing automatic fixture and reference/snapshot lifetimes
remain automatic. The three current mutable static `text`, `rodata`, and
`data` pages gain one explicitly owned static-lifetime catalog context;
no other mutable evidence singleton is introduced.

The manifest entry retains these nine cases:

```text
test_manifest_contract
test_manifest_validation
test_manifest_rejections
test_vm_mapping_capacity
test_three_service_topology
test_tty_service_contract
test_runtime_transitions
test_retained_runtime_deadline
test_replayable_runtime_model
```

The control entry retains these ten cases:

```text
test_valid_commands
test_malformed_commands
test_binding_lookup_rejects_oversized_count
test_control_transitions
test_pm_completion_rejections_are_atomic
test_completion_rejects_launcher_ipc_dependencies
test_pm_completion_queues_sealed_event_once
test_pm_completion_wakes_only_any_receiver
test_pm_completion_preserves_specific_receiver
test_image_catalog
```

The FDT entry retains its current 27 identifiers and their exact `tests[]`
order, TAP labels, short-circuit behavior per case and aggregate exit status.
Its case registry remains in `fdt_test.c`, not in a second suite entry.

The exact FDT case ownership is:

```text
fdt_header_test.c
  test_rejects_null_arguments
  test_accepts_nonzero_property_padding
  test_rejects_truncated_header
  test_rejects_bad_magic
  test_rejects_incompatible_version
  test_rejects_total_size_beyond_buffer
  test_rejects_overlapping_blocks
  test_rejects_unterminated_reservation_map
  test_rejects_unknown_structure_token
  test_rejects_bad_property_name_offset
  test_rejects_oversized_property_name
  test_rejects_excessive_tree_depth

fdt_memory_test.c
  test_parses_memory_and_reservations
  test_rejects_unsupported_root_cells
  test_rejects_partial_reg_tuple
  test_rejects_memory_name_with_conflicting_type
  test_skips_disabled_named_memory
  test_skips_disabled_device_type_memory
  test_rejects_range_overflow
  test_rejects_memory_range_capacity_overflow
  test_disabled_memory_does_not_consume_capacity
  test_rejects_reserved_memory_cell_mismatch
  test_rejects_nonempty_reserved_memory_ranges
  test_rejects_reserved_memory_as_usable_memory
  test_rejects_duplicate_device_type
  test_rejects_dynamic_reserved_memory
  test_requires_at_least_one_memory_range
```

The case registry's interleaving remains its reviewed-base order, not the
grouping order in this ownership inventory.

The runtime model keeps seed `0x42535452504c4159`, 4,096 steps, the current
xorshift shifts/draw placement, `step % 16` operation schedule, randomized
readiness classification, reference error/class comparisons, bytewise
projected-runtime comparison, and the final-16-operation failure trace.
Reference decisions call no production-private or extracted implementation
helper. It remains in the existing native test tier; no new slow model,
executable, CTest or workflow is created.

## Long-function migration

Every current over-120-line function in the ten-source scope must be split
into meaningful phases; there is no long-function exception in that scope.

| Current function | Lines | Required visible phases |
| --- | ---: | --- |
| `prepare_services` | 176 | Order setup; one record's resource preparation/binding; configuration/thread pass; final root/owner validation |
| `micros_bootstrap_runtime_prepare` | 227 | Argument/one-shot reset; immutable preflight; mechanism init/baseline; services; role bindings; scheduler/control-state preparation; one unwind/IRQ result |
| `stable_service_profiles_match` | 134 | Exact core-service, I/O-service and inert application profile checks, preserving current order and tuples |
| `validate_profile_relationships` | 263 | Controller; target closure; role-profile identities; fixed standard profiles/kernel operations; per-entry relationship checks |
| `micros_bootstrap_manifest_validate_detailed` | 503 | Argument/header/table; ordered per-entry shape/identity/profile/image/accounting; aggregate/role constraints; topology; success-only plan publication |
| `handle_console_begin` | 215 | Identity/phase; VM/TTY/owner validity; mapping/deadline/event plans; prevalidated commit; final invariant |
| `handle_release` | 127 | Binding/role; ordinary versus console release; expected/fatal result classification; final runtime validation |
| `handle_accept_ready` | 194 | Role/ready planning; context/guard; exact reply and rollback; ready/TTY/no-message commit; final captured result |
| `micros_bootstrap_handle_captured_user_ecall` | 146 | Argument/decode; authority/runtime/grant preflight; exact command dispatch |
| `validate_image` | 178 | Image scalar/config/optional VM object; ordered segments and containment/zero bytes; aggregate image info |
| `kernel_main` | 436 | Objects/traps; FDT; memory/owners/MMU/PLIC; ordered component dispatch; shutdown |
| launcher `validate_manifest` | 210 | Header/tail; entry/identity/role/budget; independent topology; final complete selection |
| launcher `micros_service_main` | 229 | Startup/view/config; local states; one serial console/release/readiness/accept transaction; complete fallback |
| generator `_validated_descriptor` | 129 | Validated ELF/symbols/loads; exact config region; optional VM boot region; descriptor publication |
| `test_tty_service_contract` | 400 | Input construction; standard profiles; accepted plan; role/target/config assertions |
| `test_manifest_validation` | 229 | Fixture; baseline acceptance; plan assertions; diagnostic/config assertions |
| `test_manifest_rejections` | 261 | Ordered header/entry/profile/image/resource mutations and atomic-output checks |
| `test_three_service_topology` | 554 | Profile/input construction; topology/prerequisite checks; exact configuration assertions |
| `test_replayable_runtime_model` | 250 | Reset/scheduled operation; production/reference dispatch; randomized classification; trace and complete comparison |
| `test_malformed_commands` | 141 | Ordered register/command/detail mutations and unchanged output assertions |
| `setup_control_fixture` | 174 | Clear inputs; manifest/profiles/plan; exact objects/endpoints/threads; bindings; control-state preparation |
| `test_control_transitions` | 315 | Setup; release/ready; atomic denials; complete/seal assertions |
| `test_pm_completion_rejections_are_atomic` | 135 | Valid baseline; ordered rejection mutations; complete sentinel/state preservation |
| `test_completion_rejects_launcher_ipc_dependencies` | 142 | Dependency setup; exact rejection; complete preservation |
| `test_pm_completion_wakes_only_any_receiver` | 127 | Receiver setup; seal plan/commit; exact wake and unrelated receiver assertions |
| `test_image_catalog` | 166 | Static input pages/catalog; accepted metadata; ordered malformed-catalog/output-preservation cases |
| FDT `build_fixture` | 268 | Reservation map; root/memory nodes; reserved-memory subtree; strings/header/offset finalization |

The first structural gate records exactly these 27 entries. A rename, a move,
or formatting compression does not discharge an entry. The final active
long-function migration is empty.

## Generator contract

`tools/generate_bootstrap_fixture.py` remains one module and CLI. Preserve its
four currently shared helpers `_format_bytes`, `_validated_descriptor`,
`_render_segment_bytes`, and `_render_image`, including signatures, dictionary
keys and returned content. Existing VM, PM, scratch, TTY, RAMFS and VFS
generators import those helpers.

Preserve `render_fixture`, `parse_arguments`, `main`, flags, error prefix,
output path behavior, emitted includes, three image IDs, manifest entry
order/tuples, resident-page arithmetic and probe-report symbol.
For identical checked input images, the complete generated bytes remain
identical. Splitting descriptor validation introduces named private phases,
not a second generator framework or policy.

## Dependency direction and private interfaces

Portable common primitives are lowest. Profiles and transitions depend on
common primitives, never on each other; manifest orchestration invokes
profile validation. Image catalog validation remains a separate lower
read-only owner.

Target context/common access is below preparation image I/O, rollback,
read-only validation and diagnostic owners. Preparation orchestrates its
resource/role phases; the facade orchestrates preparation/publication/launch.
Rollback performs only reverse release and baseline checks. Read-only
validation invokes no preparation, publication, reset, failure-record mutation,
or lifecycle transition.

Captured syscall facade depends on common and command owners. Command owners
do not call siblings. All use the existing portable control and privileged
mechanisms; no kernel policy migrates to launcher and no launcher policy
migrates into the kernel.

The launcher uses only its own context/common, independent validation,
protocol and private control transport plus supported runtime calls.
It includes no kernel-private header. Native reference code depends only on
its model contract and supported public data layouts.

All new cross-file helpers have one declared owner and subsystem-specific
names. File-local helpers remain local. New private headers are declarations-
only and contain no mutable object definitions, inline algorithms, or textual
source inclusion. The implementation gate checks recursive local includes,
not only direct includes or names in the entry source.

## Build ownership

Define explicit ordered source sets for:

```text
MICROS_BOOTSTRAP_MANIFEST_SOURCES
  kernel/bootstrap_manifest.c
  kernel/bootstrap_manifest_common.c
  kernel/bootstrap_manifest_profiles.c
  kernel/bootstrap_transitions.c

MICROS_BOOTSTRAP_IMAGE_SOURCES
  kernel/bootstrap_image.c

MICROS_BOOTSTRAP_RUNTIME_SOURCES
  kernel/bootstrap_runtime.c
  kernel/bootstrap_runtime_state.c
  kernel/bootstrap_preparation.c
  kernel/bootstrap_preparation_image.c
  kernel/bootstrap_preparation_rollback.c
  kernel/bootstrap_runtime_validation.c
  kernel/bootstrap_diagnostics.c

MICROS_BOOTSTRAP_SYSCALL_SOURCES
  kernel/bootstrap_syscall.c
  kernel/bootstrap_syscall_common.c
  kernel/bootstrap_syscall_release.c
  kernel/bootstrap_syscall_ready.c
  kernel/bootstrap_syscall_complete.c
  kernel/bootstrap_syscall_failure.c

MICROS_BOOT_FOUNDATION_SOURCES
  kernel/main.c
  kernel/boot_foundation.c
  kernel/boot_component_tests.c

MICROS_BOOTSTRAP_LAUNCHER_SOURCES
  tests/qemu/bootstrap_launcher.c
  tests/qemu/bootstrap_launcher_common.c
  tests/qemu/bootstrap_launcher_manifest.c
  tests/qemu/bootstrap_launcher_protocol.c
  tests/qemu/bootstrap_launcher_control.c

MICROS_BOOTSTRAP_MANIFEST_HOST_TEST_SOURCES
  tests/host/bootstrap_manifest_test.c
  tests/host/bootstrap_manifest_fixture.c
  tests/host/bootstrap_manifest_acceptance_test.c
  tests/host/bootstrap_manifest_topology_test.c
  tests/host/bootstrap_manifest_tty_test.c
  tests/host/bootstrap_runtime_test.c
  tests/host/bootstrap_runtime_model.c
  tests/host/bootstrap_runtime_reference.c

MICROS_BOOTSTRAP_CONTROL_HOST_TEST_SOURCES
  tests/host/bootstrap_control_test.c
  tests/host/bootstrap_control_fixture.c
  tests/host/bootstrap_control_abi_test.c
  tests/host/bootstrap_control_lifecycle_test.c
  tests/host/bootstrap_control_completion_test.c
  tests/host/bootstrap_control_image_catalog_test.c

MICROS_FDT_HOST_TEST_SOURCES
  tests/host/fdt_test.c
  tests/host/fdt_test_fixture.c
  tests/host/fdt_header_test.c
  tests/host/fdt_memory_test.c
```

Each contains the respective complete owner family above once. Unchanged
portable control decoder/core sources stay explicitly linked as before.
Every current consumer expands only its required sets once:

- `micros`: portable manifest, image, privileged runtime/syscall, boot entry;
- `bootstrap_test`: portable manifest and manifest/runtime/model native cases;
- `bootstrap_control_test`: portable manifest, image, unchanged decoder/control
  core/mechanisms, and control native cases;
- `fdt_test`: unchanged parser and the complete FDT native family; and
- `micros_bootstrap_launcher`: complete launcher family and unchanged runtime.

No raw split-source listing remains beside its source set. Any required
test-only include-root addition is limited to the existing FDT target and
does not modify compiler, sanitizer or target policy.

## Validation ownership and acceptance

New paths inherit the current owner's exact declared classification:

| Baseline owner | Required inheritance |
| --- | --- |
| `kernel/bootstrap_manifest.c` | Existing bootstrap cross-gate class, all 27 currently declared workflows |
| `kernel/bootstrap_runtime.c` | Same existing 27-workflow cross-gate class |
| `kernel/bootstrap_syscall.c` | Same existing 27-workflow cross-gate class |
| `kernel/bootstrap_image.c` | Same existing 27-workflow cross-gate class |
| `kernel/main.c` | Full tier and complete 37-workflow matrix |
| `tests/qemu/bootstrap_launcher.c` | Its exact current 12-workflow launcher/VM/PM/scratch/TTY/RAMFS/VFS/failure class |
| `tools/generate_bootstrap_fixture.py` | Its same current 12-workflow class |
| Each of the three native entry sources | Existing unknown/full-tier classification, without narrowing it to host-only |

All baseline owners currently select no separate slow-model or image workflow
through these per-path classes. Preserve that metadata; final aggregate
coverage may be larger because CMake and shared build inputs remain fail-closed.
The new structural test itself is explicitly host-unit-only.

The implementation starts with one structural Red gate:

```text
tests/host/test_kernel_bootstrap_source_layout.py
```

It checks exact family/header/source-set inventory and consumers, stable
interface hashes, all 60 moving public/test-case owners, retained entry
symbols, exact private/helper owners, context and static/automatic storage
lifetimes, dependency direction across recursive includes, exact case/model/
dispatch ordering, all evidence and generator identities, validation
inheritance, and the exact 27-function long migration. Both active migration
collections must be empty in the final implementation.

The 60-symbol moving inventory is six portable runtime functions, eight target
runtime state/validation/diagnostic functions, nine manifest/runtime native
cases, ten control native cases, and 27 FDT cases. The public syscall entry,
kernel entry, launcher entry, existing native mains, manifest validators and
image-catalog API retain their existing facade owners.

Each coherent extraction runs only the narrow existing evidence for its
active responsibility. Targeted evidence never weakens an existing assertion,
reference decision, model count, marker or timeout.
Before review, the author stages the complete change, proves no unstaged or
untracked task file remains, records HEAD/tree/exact commands/results, and
runs one authoritative PR plan. Its current shared main/CMake ownership
requires complete native validation, all 37 QEMU workflows, all existing
image workflows and documentation/diff gates. Selected commands are not
duplicated separately.

The independent implementation reviewer inspects the complete extraction,
cross-owner visibility, commit/failure ordering, preserved oracle and exact
author evidence without routinely rerunning suites.

## Implementation sequence and non-goals

After the design merge, use a fresh implementation branch and green slices:

1. structural Red gate, exact sets/private contracts and initial migrations;
2. portable manifest/profile/transition/image phases and native manifest cases;
3. privileged context, resident preparation/configuration and reverse cleanup;
4. complete runtime validation, diagnostics, prepare/publication/launch outline;
5. captured syscall common/command phases;
6. automatic boot outline and exact component dispatch;
7. launcher context/validation/protocol/control phases and generator phases;
8. control/FDT native fixtures, cases and unchanged reference replay;
9. empty migrations and one complete author PR validation; and
10. independent implementation review and justified bounded corrections.

This correction adds no bootstrap state, manifest feature, privilege,
recovery/reset, service, application runtime, dynamic spawn behavior, directory
layout, test execution, generated image, workflow or compatibility layer.
Unrelated subsystem decompositions and validation-plan deduplication remain
their own tasks.

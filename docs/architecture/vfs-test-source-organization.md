# VFS Test Source Organization

This document defines the accepted responsibility split for the VFS test
sources before Step 14 exit-cleanup coverage is added. The normative decision
is [ADR-0052](../adr/0052-vfs-test-source-decomposition.md). The fixed
behavioral baseline is
[the MINIX VFS test-organization note](../research/minix-vfs-test-organization.md).

## Objective

The VFS test layers must remain complete while their source ownership becomes
readable:

- one shared native fixture family owns simulated grant, RAMFS, and TTY
  behavior;
- native core tests are grouped by state, process, namespace, regular I/O,
  directory, and console behavior;
- native spawn tests are grouped by image, descriptor, and hidden-publication
  transaction ownership;
- service tests are grouped by protocol, configuration, and dispatch
  ownership;
- the model separates reference state, comparison, operation drivers,
  pressure cases, and trace scheduling;
- the target probe separates client IPC from filesystem and console
  scenarios;
- the kernel hook exposes its report-authentication, terminal-state, and
  shutdown phases; and
- the fixture generator exposes validation, limit calculation, and rendering
  phases.

The split preserves the current evidence graph. It does not create more test
executables or cause the same suite to run under additional CTest or workflow
identities.

## Reviewed base

The reviewed repository base is:

```text
25cc6729bfbfa998392069d98245180e2abc8cba
```

The eight source signals contain 12,788 physical lines:

| File | Lines | Functions | Longest function |
| --- | ---: | ---: | ---: |
| `tests/host/vfs_model_test.c` | 2,880 | 59 | 300 |
| `tests/host/vfs_spawn_test.c` | 2,770 | 40 | 198 |
| `tests/host/vfs_test.c` | 2,063 | 19 | 498 |
| `tests/host/vfs_service_test.c` | 1,721 | 23 | 374 |
| `tests/host/vfs_test_fixture.c` | 1,533 | 34 | 194 |
| `tests/qemu/vfs_service_probe.c` | 985 | 28 | 131 |
| `kernel/vfs_service_test.c` | 495 | 4 | 129 |
| `tools/generate_vfs_service_fixture.py` | 341 | 4 | 268 |

These measurements trigger review. They do not determine the target
boundaries.

## Preservation invariants

The implementation must preserve:

- every current assertion and rejection case;
- the current short-circuit case order, including the initialization-to-mount
  state handoff in `vfs_test`;
- exact application, RAMFS, TTY, and PM-control message bytes;
- exact error precedence and no-mutation-on-failure checks;
- fixture grant generations, permissions, reference accounting, injected
  failures, and observable counters;
- spawn lifecycle, image, descriptor, publication, rollback, and token
  assertions;
- the VFS model seed, 8,192 transitions, 32-step cycle, random draw count,
  operation encoding, failure trace, coverage requirements, and complete
  state comparison;
- the target application's section layout, one aligned transfer buffer,
  ready/report protocol, input trigger, expected input, pass marker, failure
  codes, and drain request;
- the kernel fixture's profiles, service order, scheduler policy, report
  authentication, final-state validation, UART-drain bound, and SBI shutdown;
- fixture-generator inputs, CLI, validation errors, generated C bytes, image
  identifiers, manifest ordering, page limits, and report-symbol address;
- target names `vfs_test`, `vfs_model_test`, `vfs_service_test`, and
  `micros_vfs_service_probe`;
- CTest names `vfs-core`, `vfs-model`, and `vfs-service-core`; and
- workflows `test-unit-fast`, `test-vfs-model`,
  `build-vfs-service-image`, `test-qemu-vfs`, and the existing pull-request
  validation tier.

The source move must not alter production VFS sources or public headers under
`include/micros/`.

## Stable evidence boundaries

| Evidence | Stable executable or workflow | Source effect |
| --- | --- | --- |
| Native core and spawn behavior | `vfs_test` / `vfs-core` | More source modules, one unchanged test execution |
| Native service codec and dispatch | `vfs_service_test` / `vfs-service-core` | More source modules, one unchanged test execution |
| Replayable complete-state model | `vfs_model_test` / `vfs-model` / `test-vfs-model` | More source modules, one unchanged seed and run |
| Standalone service image | `build-vfs-service-image` | No production source or image-policy change |
| Isolated target integration | `micros_vfs_service_probe` / `test-qemu-vfs` | More probe sources, one unchanged guest scenario |

No new CTest or workflow is added merely to mirror a source file. Source
cohesion and execution cadence are independent decisions.

## Shared native fixture layout

The retained test-only public fixture contract is
`tests/host/vfs_test_fixture.h`. It intentionally exposes fixture state used
by white-box assertions. It is not a supported production interface.

| File | Owned responsibility |
| --- | --- |
| `tests/host/vfs_test_fixture.h` | Test-visible fixture storage, injection controls, counters, and public fixture operations |
| `tests/host/vfs_test_fixture_internal.h` | Private declarations shared only by the fixture implementation modules |
| `tests/host/vfs_test_fixture.c` | Fixture initialization, endpoint binding, callback wiring, and whole-fixture reset |
| `tests/host/vfs_test_fixture_grant.c` | Application-grant validation/copy, backend-grant creation/revocation, lookup, and public grant inspection |
| `tests/host/vfs_test_fixture_ramfs.c` | Node identity, path lookup, create, read, write, getdents, putnode, deterministic seeding, and public path/file helpers |
| `tests/host/vfs_test_fixture_tty.c` | Submit-read, submit-write, cancel, collect, queued input, completion, writable output, and drain behavior |

The private dependency direction is:

- the grant module is a leaf;
- the RAMFS and TTY modules may use private grant lookup;
- the facade wires callbacks but does not reimplement their behavior; and
- no fixture module may include a core, spawn, service, or model private
  header.

Fixture callers continue to pass `struct vfs_test_fixture *` explicitly. The
split must not create duplicate module-global fixture state.

## Native core layout

| File | Owned responsibility |
| --- | --- |
| `tests/host/vfs_test.c` | `vfs_test` entry point, explicit target context, and the exact current short-circuit case order |
| `tests/host/vfs_test_suites.h` | Target-private declarations for responsibility-owned core case/phase runners and `micros_vfs_spawn_test_run()` |
| `tests/host/vfs_core_test_fixture.h` | Private core-test context and request-helper declarations |
| `tests/host/vfs_core_test_fixture.c` | Core-test context initialization, request construction, and the corrupting RAMFS wrapper |
| `tests/host/vfs_core_state_test.c` | Public constants, initialization, and complete-state corruption rejection |
| `tests/host/vfs_core_process_test.c` | Trusted attach, descriptor sharing, detach, process-owned cleanup, and trusted-result publication |
| `tests/host/vfs_core_namespace_test.c` | Root mount, path preflight, mkdir, create, exclusive create, and working-directory behavior |
| `tests/host/vfs_core_io_test.c` | Open, close, regular read/write, access checks, backend-result validation, and backend-reference release |
| `tests/host/vfs_core_directory_test.c` | Directory-record translation, cursor commit, EOF, and grant-failure atomicity |
| `tests/host/vfs_core_console_test.c` | Console read/write, writable retry, completion races, detach cleanup, grant failures, and test drain |

Each behavior file exports only the case or semantic-phase runners required by
`vfs_test_suites.h`. `vfs_test.c` invokes them in the existing order and
passes one explicit context, so the current initialization-to-mount handoff
and short-circuit failure order remain unchanged. All subordinate helpers
remain file-local. The private fixture owns common setup; behavior files own
their assertions.

Future VFS exit-cleanup native cases enter
`vfs_core_process_test.c` for process-owned descriptor/vnode release and
`vfs_core_console_test.c` for pending TTY cancellation or completion debt.
They must not reopen `vfs_test.c` as a behavior owner.

## Native spawn layout

| File | Owned responsibility |
| --- | --- |
| `tests/host/vfs_spawn_test.c` | Existing `micros_vfs_spawn_test_run()` entry and exact current short-circuit spawn-case order |
| `tests/host/vfs_spawn_test_fixture.h` | Private spawn-test context, request builders, and inspection contract |
| `tests/host/vfs_spawn_test_fixture.c` | Executable/request construction, fixture initialization, control-request builders, and shared counts |
| `tests/host/vfs_spawn_image_test.c` | Image prepare/release, executable policy, trusted-init path policy, image grants, snapshots, capacity, tokens, and prepared-state validation |
| `tests/host/vfs_spawn_descriptor_test.c` | Descriptor prepare/abort, reference reservation, process-slot capacity, corruption, rollback, and parent-detach exclusion |
| `tests/host/vfs_spawn_publication_test.c` | Hidden child commit, immutability, revert, release, and trusted-init hidden publication |

`vfs_test_suites.h` owns the declaration of
`micros_vfs_spawn_test_run()` instead of repeating an ad hoc prototype.
Responsibility modules export only the case or phase runners needed by the
spawn aggregator, which retains the current order.

Image, descriptor, and publication modules share only the private spawn
context. They do not export operation-specific helpers to one another.

## Native service layout

| File | Owned responsibility |
| --- | --- |
| `tests/host/vfs_service_test.c` | `vfs_service_test` entry point, explicit target context, and exact current short-circuit case order |
| `tests/host/vfs_service_test_suites.h` | Target-private declarations for responsibility-owned service case/phase runners |
| `tests/host/vfs_service_test_fixture.h` | Private service-test context, endian access, message builders, and configuration constructors |
| `tests/host/vfs_service_test_fixture.c` | Shared fixture initialization and byte-level test construction |
| `tests/host/vfs_service_application_protocol_test.c` | Public constants plus application request/result codec behavior |
| `tests/host/vfs_service_backend_protocol_test.c` | RAMFS and TTY request/result codec behavior |
| `tests/host/vfs_service_pm_control_protocol_test.c` | PM-control command decoding, reserved bytes, and result encoding |
| `tests/host/vfs_service_configuration_test.c` | Production and test service-configuration validation |
| `tests/host/vfs_service_dispatch_test.c` | Application calls, notifications, drain authorization, and PM-control lifecycle dispatch |

Future `EXIT_PROCESS` codec cases belong in the PM-control protocol module.
Future service-level exit dispatch belongs in the dispatch module.

The test fixture may construct messages, but it must not own expected protocol
semantics. Those assertions remain in the behavior modules.

Service behavior modules export only the case or phase runners required by
`vfs_service_test_suites.h`; subordinate helpers remain file-local.

## Replayable model layout

| File | Owned responsibility |
| --- | --- |
| `tests/host/vfs_model_test.c` | Model entry point, fixed step count, and final pass line |
| `tests/host/vfs_model_internal.h` | Private model state, coverage, operation identifiers, and cross-module declarations |
| `tests/host/vfs_model_context.c` | Seeded random source, trace recording, failure reporting, request construction, grant cleanup, and model initialization |
| `tests/host/vfs_model_reference.c` | Independent reference tables, path metadata, reference counts, and state transitions |
| `tests/host/vfs_model_compare.c` | Complete production/reference comparison and transition counting |
| `tests/host/vfs_model_process_operations.c` | Attach, detach, cycle completion, and process-lifecycle operation drivers |
| `tests/host/vfs_model_file_operations.c` | Malformed/foreign requests, mkdir, open/create/share, read/write/close, chdir, and getdents drivers |
| `tests/host/vfs_model_console_operations.c` | Console read/write, completion, cancellation, writable retry, debt clearing, and drain drivers |
| `tests/host/vfs_model_pressure.c` | Descriptor, vnode, process, backend-capacity, and fatal-backend cases |
| `tests/host/vfs_model_trace.c` | Exact 32-phase scheduling, operation recording, pressure cadence, and coverage validation |

The model private header may contain shared structures and the existing
failure-check macro, but no function bodies. The reference model must not call
production transition helpers.

`vfs_model_trace.c` draws random input exactly once per model step and passes
that value to semantic phase helpers. Helper extraction must not add, remove,
or reorder random draws.

Future modeled exit cleanup belongs in the process-operation and reference
modules, with scheduling owned by the trace module.

## Target evidence layout

### Application probe

| File | Owned responsibility |
| --- | --- |
| `tests/qemu/vfs_service_probe.c` | Boot configuration validation, readiness, scenario ordering, failure reporting, and final report |
| `tests/qemu/vfs_service_probe_internal.h` | Private probe context, result type, and cross-module declarations |
| `tests/qemu/vfs_service_probe_client.c` | Byte helpers, result decoding, grants, VFS calls, open/path/close/transfer/read/write client operations |
| `tests/qemu/vfs_service_probe_filesystem.c` | Seeded-file read, create/write/reopen, absolute/relative path behavior, and root directory enumeration |
| `tests/qemu/vfs_service_probe_console.c` | Marker-triggered console write/read and the no-authority drain request |

The entry module owns one static aligned probe context and passes it
explicitly. The split must not add another transfer page or mutable hidden
context.

### Kernel hook

`kernel/vfs_service_test.c` remains one file because it owns one isolated
kernel scenario configuration and terminal report hook. Its trap handler is
decomposed into named phases:

1. match the breakpoint report address and required runtime state;
2. authenticate the current thread as the configured application;
3. classify the report code or panic with the exact existing reason;
4. validate sealed bootstrap, VM handoff, TTY handoff, PLIC, grant, UART
   mapping, and runtime invariants;
5. verify every service is ready;
6. wait for the bounded physical UART drain; and
7. request SBI shutdown and remain non-returning.

The ordering is authoritative. Report classification precedes terminal-state
validation, and shutdown follows physical drain.

### Fixture generator

`tools/generate_vfs_service_fixture.py` remains one focused tool. Its
`render_fixture()` function becomes an orchestration outline over:

1. validated descriptor construction;
2. VM/non-VM metadata and report-symbol validation;
3. UART overlap and fixed resident-limit validation;
4. dynamic launcher/VM/PM/TTY and aggregate limit calculation;
5. segment and image-table rendering;
6. manifest rendering; and
7. report-address rendering and final assembly.

The public `render_fixture()` signature and CLI remain unchanged.

## Required function disposition

Every current function over the 120-line review signal is non-atomic. The
implementation must replace each with the listed semantic outline. Helper
names may become more concise, but phases may not be combined again.

| Function and current lines | Required subtask outline |
| --- | --- |
| `fixture_lookup` (`tests/host/vfs_test_fixture.c:342-469`, 128) | Resolve grant/start/root; validate canonical path; choose absolute or relative start; traverse one component at a time; enforce trailing-directory semantics; acquire the final backend reference and response |
| `fixture_tty_call` (`tests/host/vfs_test_fixture.c:978-1171`, 194) | Common preflight and injected result; submit read; submit write; cancel; collect; operation dispatch |
| `mount_attach_share_and_detach_are_atomic` (`tests/host/vfs_test.c:252-405`, 154) | Rejected and successful root mount; attach argument and idempotence cases; descriptor sharing and collision; first detach; final detach and absent-process idempotence |
| `open_read_close_and_validation_are_exact` (`tests/host/vfs_test.c:524-769`, 246) | Open seeded file; backend-grant capacity rejection; successful read; revoked-grant and EOF behavior; access/range/malformed checks; close and backend-reference release; version/type/caller rejection |
| `backend_reply_validation_is_operation_specific` (`tests/host/vfs_test.c:832-993`, 162) | Invalid read result; corrupt read count; invalid write result; invalid getdents result |
| `create_write_mkdir_and_chdir_are_exact` (`tests/host/vfs_test.c:995-1287`, 293) | Create directory and duplicate rejection; create/open file; write and no-space rollback; exclusive-create cases; change directory; relative reopen/read; close and restore root |
| `getdents_translation_and_cursor_are_exact` (`tests/host/vfs_test.c:1289-1437`, 149) | Build/open directory; translate first records; prove grant-failure cursor atomicity; translate remaining records and end cursor; prove EOF skips stale-grant access; range rejection |
| `console_async_and_cleanup_are_exact` (`tests/host/vfs_test.c:1439-1936`, 498) | Delayed read completion; prequeued read completion; direct write completion and event-mask rejection; busy write writable retry; detach before read completion; detach after read completion; detach during write completion; detach during writable wait; grant and request-ID capacity failures |
| `pm_control_requests_are_exact` (`tests/host/vfs_service_test.c:268-519`, 252) | Decode prepare-image variants; decode descriptor prepare; decode hidden commit/revert; decode abort/release; decode exit; reject caller/type/version/command/reserved/reply-token errors without output mutation |
| `pm_control_results_are_exact` (`tests/host/vfs_service_test.c:558-699`, 142) | Encode prepare-image payload; encode descriptor token; encode zero-payload commands; encode non-OK result; reject invalid result, command, command-specific payload, and reply token without output mutation |
| `backend_protocol_is_exact` (`tests/host/vfs_service_test.c:819-1192`, 374) | Encode each RAMFS request family; decode RAMFS results and rejected peer/operation cases; encode TTY submit/cancel/collect; decode TTY result and rejected reserved/count cases |
| `service_dispatches_pm_control` (`tests/host/vfs_service_test.c:1464-1704`, 241) | Dispatch image prepare; descriptor prepare; hidden commit and revert; descriptor abort and image release; reject foreign caller, malformed payload, and unknown command |
| `run_vnode_pressure` (`tests/host/vfs_model_test.c:2093-2230`, 138) | Initialize pressure state; seed deterministic files; attach processes; reserve baseline vnodes; fill remaining vnode capacity; count active vnodes; prove no-space and validate final state |
| `execute_step` (`tests/host/vfs_model_test.c:2509-2808`, 300) | Draw random input once; execute process setup phases; namespace phases; regular-I/O phases; directory phases; console phases; pressure/failure phase; detach and finish the cycle |
| `successful_prepare_and_release_are_exact` (`tests/host/vfs_spawn_test.c:402-525`, 124) | Seed executable and request; prepare and validate transaction state; validate VM-target grants; release and verify complete zeroization/reference cleanup |
| `public_descriptor_prepare_and_abort_are_exact` (`tests/host/vfs_spawn_test.c:527-724`, 198) | Prepare image; reserve descriptor snapshot and references; reject duplicate prepare/release/parent detach; abort and verify rollback; reprepare with next token; final abort and release |
| `reserved_process_slot_and_capacity_are_exact` (`tests/host/vfs_spawn_test.c:1276-1403`, 128) | Prepare image; fill public process capacity; reserve hidden child slot; prove public attach remains full; abort reservation; fill final slot; reject descriptor preparation atomically; release image |
| `expected_prepare_failures_roll_back` (`tests/host/vfs_spawn_test.c:1843-1978`, 136) | Missing path; backend no-space; non-executable file; directory; empty executable; oversized executable, each as an independent rollback case |
| `trusted_init_and_path_bounds_are_exact` (`tests/host/vfs_spawn_test.c:2100-2234`, 135) | Trusted-init success/release; reject relative trusted path; reject trusted descriptor actions; reject overlong path before backend lookup |
| `prepared_snapshot_is_independent_from_vnode_size` (`tests/host/vfs_spawn_test.c:2429-2561`, 133) | Prepare image snapshot; open backing file for write; grow backing vnode through application I/O; prove prepared bytes/size remain immutable; release image and close descriptor |
| `probe_test_filesystem` (`tests/qemu/vfs_service_probe.c:737-867`, 131) | Read seeded motd; create directory and note; write and close note; reopen/read by absolute path; chdir and read by relative path; restore root |
| `micros_vfs_service_test_handle_trap` (`kernel/vfs_service_test.c:366-495`, 130) | Match report trap; authenticate reporter; classify report; validate terminal subsystem state; validate readiness; drain UART; shut down |
| `render_fixture` (`tools/generate_vfs_service_fixture.py:30-297`, 268) | Build descriptors; validate roles/metadata; calculate fixed and aggregate limits; render segments/images; render manifest; render report address; assemble output |

No current over-limit function receives an algorithmic exception.

## Private contract rules

- Test-family private headers live under `tests/` and are not installed.
- A private header contains declarations and shared types, not function
  bodies.
- A behavior module exports only the case or semantic-phase runners declared
  by its target-private orchestration header.
- Target-private orchestration headers contain declarations only and preserve
  the existing invocation order; they do not own fixture or protocol logic.
- Cross-family dependencies use the public fixture contract, not another
  family's private header.
- Production sources must not include any new test-private header.
- Source files are compiled normally; `.c` inclusion is forbidden.
- State is passed explicitly. Source movement must not introduce new mutable
  process-global test state.
- Generic `utils`, `helpers`, or `common` modules are not accepted names.
  Each shared module must state the domain responsibility it owns.

## Build integration

`CMakeLists.txt` defines exact test source sets:

```text
MICROS_VFS_TEST_FIXTURE_SOURCES
MICROS_VFS_CORE_TEST_SOURCES
MICROS_VFS_SPAWN_TEST_SOURCES
MICROS_VFS_MODEL_TEST_SOURCES
MICROS_VFS_SERVICE_TEST_SOURCES
MICROS_VFS_PROBE_SOURCES
```

The target composition is:

- `vfs_test`: portable VFS sources, spawn request support, shared fixture,
  core-test sources, and spawn-test sources;
- `vfs_model_test`: portable VFS sources, spawn request support, shared
  fixture, and model sources;
- `vfs_service_test`: portable VFS sources, spawn request support,
  `vfs_service_core.c`, shared fixture, and service-test sources; and
- `micros_vfs_service_probe`: existing report assembly plus all probe sources.

The exact source sets are listed once in CMake. Targets consume the named
sets rather than repeating individual paths.

## Structural test

Implementation starts with a local Red change to
`tests/host/test_vfs_test_source_layout.py`. The test then becomes green in
the first coherent source-set commit. It verifies:

- every accepted source and private header exists;
- each CMake source set has exact ordered membership;
- each consumer uses the complete required sets exactly once;
- native fixture sources are shared by all three host VFS targets;
- core and spawn sources are used only by `vfs_test`;
- model sources are used only by `vfs_model_test`;
- service sources are used only by `vfs_service_test`;
- all probe sources are linked into `micros_vfs_service_probe`;
- private headers contain no static function bodies;
- production sources include no test-private header; and
- every function above the review signal appears in one exact migration
  allowlist keyed by source path and function name;
- no unrecorded over-limit function appears during extraction; and
- the allowlist only shrinks and is empty in the final implementation.

The structural test does not impose a repository-wide line limit.

## Validation ownership

`tools/validation_plan.py` and
`tests/host/test_validation_plan.py` must name every new VFS test source:

- all shared fixture, native core, spawn, service, model, target probe,
  kernel hook, and generator paths continue to select `test-qemu-vfs`;
- shared fixture and model paths continue to select `test-vfs-model`; and
- no new path is silently treated as an unknown host-only file.

This is ownership parity, not an additional test run. The validation planner
still deduplicates selected workflow commands.

## Implementation sequence

The implementation proceeds as small green slices:

1. Add the structural Red test locally, then commit the exact CMake source
   sets, orchestration contracts, initial private contracts, and an exact
   migration allowlist containing the 23 documented functions. The structural
   test and current native targets must be green.
2. Split the shared grant, RAMFS, TTY, and facade fixture ownership.
3. Split native core state, process, namespace, I/O, directory, and console
   behavior.
4. Split spawn fixture, image, descriptor, and publication behavior.
5. Split service fixture, protocol, configuration, and dispatch behavior.
6. Split model context, reference, comparison, process, file, console,
   pressure, and trace ownership without changing the trace.
7. Split the target probe, decompose the retained kernel trap handler, split
   generator phases, empty the migration allowlist, and complete
   validation-plan ownership.

Move order may change only to keep a commit buildable and the dependency graph
acyclic. It may not change the accepted final ownership.

## Validation plan

During extraction, run only the narrow existing gate that covers the active
slice. Do not run QEMU or the complete pull-request tier after every move.

Before independent review, generate and inspect the authoritative
pull-request plan:

```sh
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier pr --base main
```

The plan must select complete native validation, the standalone VFS image, and
the QEMU VFS gate for the completed change. Execute that plan once:

```sh
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier pr --base main --execute
```

Do not separately rerun a workflow already selected by the authoritative
plan. If a required VFS gate is absent, correct validation ownership before
review rather than compensating with an undocumented manual run.

The author records the actual plan and results. The independent reviewer
performs static source and design analysis and does not routinely rerun these
suites.

## Non-goals

This decomposition does not add:

- VFS exit cleanup or any new test scenario;
- a new test framework, assertion macro family, parameterized runner, or
  repository-wide fixture abstraction;
- new executables, CTest names, workflows, model operations, seeds, markers,
  or timeouts;
- production VFS, RAMFS, TTY, PM, grant, bootstrap, or runtime changes;
- wire, state-layout, capacity, manifest, profile, or scheduler-policy
  changes;
- dynamic allocation, concurrency, recovery, or multi-filesystem behavior;
  or
- mechanical splitting solely to meet a line count.

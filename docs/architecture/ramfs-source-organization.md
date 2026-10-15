# RAMFS Source Organization

This is the implementation-ready contract for the seven RAMFS signal files
in [the source-module audit](source-module-audit.md).
[ADR-0066](../adr/0066-ramfs-source-decomposition.md) is Accepted after clean
independent design and bounded correction review. The separate design merged
as PR #133, commit `cac6282edbf303fc81bcffdb3387b23e70e6da17`.
Implementation follows the required final source map below; explicit owner
and semantic migrations distinguish pending extraction from completed work.

## Base, goal, and authority

```text
reviewed micros base: 4290ca21c06ac88b299d694c236b774896cb6171
fixed MINIX reference: 4db99f4012570a577414fe2a43697b2f239b699e
design task: ramfs-source-decomposition-design
implementation task: ramfs-source-decomposition-implementation
dependency-ready predecessor: repository-module-size-audit
```

Expose existing state validation, codec, node ownership, namespace, regular
I/O, directory, fixture, reference-model, scenario, and report tasks.
The [fixed organization trace](../research/minix-ramfs-source-organization.md)
supplements, not replaces, the existing
[behavioral classification ledger](../research/minix-vfs-mfs-filesystem-protocol.md).
Accepted [ADR-0048](../adr/0048-ramfs-service-and-vfs-filesystem-protocol.md)
remains the complete runtime contract; ADR-0051 governs private boundaries.

This adds no filesystem operation, runtime dependency node, wire/API/layout,
capacity, policy, allocation, concurrency, recovery, cache, retry, init,
shell, command, directory migration, or general refactoring framework.

| Original signal source | Lines | C definitions | Functions above 120 lines |
| --- | ---: | ---: | ---: |
| `servers/ramfs/ramfs_core.c` | 1,997 | 45 | 5 |
| `tests/host/ramfs_test.c` | 1,834 | 30 | 5 |
| `tests/host/ramfs_seed_test.c` | 880 | 20 | 2 |
| `tests/host/ramfs_model_test.c` | 2,108 | 54 | 2 |
| `tests/qemu/ramfs_service_vfs.c` | 1,139 | 36 | 1 |
| `kernel/ramfs_service_test.c` | 496 | 4 | 1 |
| `tools/generate_ramfs_service_fixture.py` | 293 | N/A | `render_fixture` |

The existing lexical C-definition helper supplies the 189-definition,
sixteen-long-function inventory. The Python function receives a semantic
split independently of C counts.

## Stable neighbors

Keep these artifacts byte-for-byte unchanged:

```text
include/micros/ramfs.h
servers/ramfs/ramfs_core.h
servers/ramfs/ramfs_seed.h
servers/ramfs/ramfs_seed.c
servers/ramfs/ramfs_service.c
servers/ramfs/ramfs_embedded_seed.h
servers/ramfs/seed.json
tools/generate_ramfs_seed.py
tools/embed_ramfs_seed.py
tests/host/test_generate_ramfs_seed.py
tests/host/test_embed_ramfs_seed.py
tests/qemu/ramfs_service_protocol.h
tests/qemu/ramfs_service_report.S
kernel/ramfs_service_test.h
kernel/ramfs_service_test_fixture.h
CMakePresets.json
```

Also freeze the complete committed membership, modes, and bytes beneath
`servers/ramfs/seed/`. Do not add or regenerate committed seed inputs.
The cohesive 417-line seed parser and 352-line actual service adapter are
not signaled mixed owners and are not split.

Public entry points, supported enum/value identities, structure layouts,
assertions, report symbols/instructions, budget/checker options, generated
seed bytes, and all current runtime command lines remain unchanged. Source
locations in assertions/panics follow the true new owner; do not spoof
`__FILE__`, `__LINE__`, or assembly labels.
The split model's shared failure leaf receives the caller's `__FILE__`
alongside its existing line argument. The existing `MODEL_EXPECT` supplies
both at the call site so a moved fixture logger cannot pair its own filename
with another module's line. The failure format and complete trace are
unchanged; this is private source-location transport, not a new error policy.

## Exact source sets and private contracts

Define six complete ordered CMake sets, exactly 32 C sources:

```text
MICROS_RAMFS_CORE_SOURCES
  servers/ramfs/ramfs_core.c
  servers/ramfs/ramfs_codec.c
  servers/ramfs/ramfs_ownership.c
  servers/ramfs/ramfs_state.c
  servers/ramfs/ramfs_transfer.c
  servers/ramfs/ramfs_namespace.c
  servers/ramfs/ramfs_io.c
  servers/ramfs/ramfs_directory.c

MICROS_RAMFS_HOST_TEST_SOURCES
  tests/host/ramfs_test.c
  tests/host/ramfs_test_fixture.c
  tests/host/ramfs_protocol_test.c
  tests/host/ramfs_namespace_test.c
  tests/host/ramfs_io_test.c
  tests/host/ramfs_directory_test.c

MICROS_RAMFS_SEED_HOST_TEST_SOURCES
  tests/host/ramfs_seed_test.c
  tests/host/ramfs_seed_test_fixture.c
  tests/host/ramfs_seed_boundary_test.c
  tests/host/ramfs_seed_corruption_test.c

MICROS_RAMFS_MODEL_HOST_TEST_SOURCES
  tests/host/ramfs_model_test.c
  tests/host/ramfs_model_fixture.c
  tests/host/ramfs_model_reference.c
  tests/host/ramfs_model_projection.c
  tests/host/ramfs_model_protocol.c
  tests/host/ramfs_model_operations.c

MICROS_RAMFS_QEMU_PEER_SOURCES
  tests/qemu/ramfs_service_vfs.c
  tests/qemu/ramfs_service_vfs_fixture.c
  tests/qemu/ramfs_service_vfs_ramfs.c
  tests/qemu/ramfs_service_vfs_scenario.c
  tests/qemu/ramfs_service_vfs_tty.c

MICROS_RAMFS_QEMU_REPORT_SOURCES
  kernel/ramfs_service_test.c
  kernel/ramfs_service_test_fixture.c
  kernel/ramfs_service_test_report.c
```

The six new private headers are exactly:

```text
servers/ramfs/ramfs_internal.h
tests/host/ramfs_test_internal.h
tests/host/ramfs_seed_test_internal.h
tests/host/ramfs_model_internal.h
tests/qemu/ramfs_service_vfs_internal.h
kernel/ramfs_service_test_internal.h
```

Headers contain types, constants, prototypes, narrow extern const
declarations, and only the existing assertion/failure macros. No object
definitions, inline algorithms, textual C includes, new executable macros,
or ad hoc extern declarations are allowed. Directory-qualified includes
disambiguate native and target evidence. Cross-file functions gain the
matching `ramfs_`, `ramfs_test_`, `ramfs_seed_test_`, `ramfs_model_`,
`vfs_`, or `ramfs_test_` family prefix; supported `micros_` names do not change.
Local helpers remain static. Tests and service adapters never include the
portable private header.

### Portable ownership

Original names below identify exact definitions, not permission to duplicate
helpers. New phase helpers stay with their owning task.

| Owner beneath `servers/ramfs/` | Original definitions |
| --- | --- |
| `ramfs_core.c` | `protocol_result`, `micros_ramfs_handle_call`: public call orchestration, not operation ownership. |
| `ramfs_codec.c` | `zero_bytes`, `bytes_are_zero`, `read_u16_le`, `read_u32_le`, `read_u64_le`, `write_u32_le`, `write_u64_le`, `request_type_is_known`, `result_is_defined`, `micros_ramfs_decode_request`, `micros_ramfs_build_result`, `prepare_reply`: canonical byte/message shaping. |
| `ramfs_ownership.c` | `endpoint_is_valid`, `mode_is_canonical`, `node_is_directory`, `node_is_regular`, `micros_ramfs_handle`, `decode_handle`, `initialize_empty_node`, `node_handle`, `resolve_node`, `resolve_referenced_node`, `names_equal`, `name_equals_bytes`, `find_free_node`, `find_free_block`, `handle_mount`, `handle_putnode`: fixed node/block identity and counted ownership. |
| `ramfs_state.c` | `validate_state`, `micros_ramfs_state_validate`, `micros_ramfs_state_initialize`: initialization and complete invariants. |
| `ramfs_transfer.c` | `copy_grant`: the existing single checked callback boundary shared by namespace and both data paths. |
| `ramfs_namespace.c` | `find_child`, `is_ancestor`, `path_components_are_bounded`, `handle_lookup`, `handle_create_or_mkdir`: bounded names/tree traversal and namespace publication. |
| `ramfs_io.c` | `stage_read`, `handle_read`, `handle_write`: sparse staging and regular-file mutation. |
| `ramfs_directory.c` | `write_directory_record`, `directory_entry_for_cursor`, `handle_getdents`: fixed records and cursor progression. |

Codec primitives and ownership queries are leaves. Ownership may use byte
primitives, but no codec routine calls an ownership handler. Validation may
use read-only identity/name/type queries and seed decoding; it never calls an
operation. Transfer uses the existing I/O callback and VFS endpoint only.
Namespace, I/O, and directory use validation, ownership, codec, and transfer
leaves, not one another. The facade orchestrates all handlers and final
validation; no lower module calls it. `prepare_reply` owns only output shaping,
not dispatch. There is no new global production state.

### Native functional and seed evidence

| Owner beneath `tests/host/` | Original definitions |
| --- | --- |
| `ramfs_test.c` | `main`: exact seven-case short-circuit and output. |
| `ramfs_test_fixture.c` | All 22 original helpers before the first case: four LE helpers, `test_copy`, `load_seed`, `reset_bus`, `request`, five operation request builders, `reply_result`, `reply_error_fields_are_zero`, `call_expect`, `failure_preserves_state`, both initialization helpers, `write_grant_bytes`, `lookup_path`, `create_node`. |
| `ramfs_protocol_test.c` | `seed_state_is_exact_and_invariants_fail_closed`, `protocol_mount_and_result_layout_are_exact`. |
| `ramfs_namespace_test.c` | `lookup_semantics_and_reference_limits_are_exact`, `handles_create_mkdir_and_capacity_are_exact`, `putnode_and_handle_validation_are_exact`. |
| `ramfs_io_test.c` | `read_write_sparse_and_block_capacity_are_exact`. |
| `ramfs_directory_test.c` | `getdents_cursor_and_records_are_exact`. |
| `ramfs_seed_test.c` | `main`: exact seven-case short-circuit and digest output. |
| `ramfs_seed_test_fixture.c` | All twelve original helpers: three LE writers, `load_seed`, `refresh_digest`, `initialize_header`, `initialize_entry`, four seed builders, `expect_rejection`. |
| `ramfs_seed_boundary_test.c` | `production_seed_validates`, `exact_capacity_boundaries_validate`, `argument_validation_is_fail_closed`. |
| `ramfs_seed_corruption_test.c` | `header_corruption_is_rejected`, `record_name_and_reserved_corruption_is_rejected`, `parent_and_mode_corruption_is_rejected`, `data_and_capacity_corruption_is_rejected`. |

The fixtures are below entry and cases; sibling cases never call one
another. Existing test codec helpers remain independent of production
private helpers. Each case borrows only its family's sole fixture and passes
it explicitly to phases. No helper reaches back into the `main` owner.

### Independent model

| Owner beneath `tests/host/` | Original definitions |
| --- | --- |
| `ramfs_model_test.c` | `run_model`, `main`: initialization, scripted/capacity/random selection, coverage, and output. |
| `ramfs_model_fixture.c` | `write_u32_le`, `write_u64_le`, `load_seed`, `model_failure`, `model_copy`, `record_operation`; original model, separate seed storage, and const `model_io`. |
| `ramfs_model_reference.c` | All twenty `initialize_reference_node`, `initialize_reference`, and `reference_*` definitions: independent expected transitions and handle/path/record math. |
| `ramfs_model_projection.c` | `compare_state`: complete production/reference validation and comparison. |
| `ramfs_model_protocol.c` | `read_u32_le`, `read_u64_le`, six request builders, `execute_call`, `prepare_bus`, and `execute_mount`, `execute_lookup`, `execute_create`, `execute_write`, `execute_read`, `execute_getdents`, `execute_putnode`, `execute_stale`, `execute_malformed`. |
| `ramfs_model_operations.c` | `next_random`, `build_absolute_path`, `random_live_slot`, `random_referenced_nonroot_slot`, `execute_random_operation`, `execute_scripted_operation`. |

Fixture/failure/trace and the two shared byte writers are leaves.
Reference directory-record rendering and protocol request construction use
the same existing byte-writer bodies through that leaf; reference never
calls a protocol action or reply reader. Reference transitions use only
their own representation and public protocol constants, never production
private functions or projected production values. Projection may call read-only
`reference_handle`; reference transitions never call projection.
Protocol actions apply independent reference transitions, execute the real
call, compare reply/storage, and project complete state. Operations call
those actions. The driver owns model reset and execution selection; lower
owners never call it. Preserve `execute_call`'s comparison and no-space
coverage timing, not just a driver-level final comparison.

### Target peer, report, and generator

| Owner | Original responsibility |
| --- | --- |
| `tests/qemu/ramfs_service_vfs.c` | `vfs_report_and_stop`, `micros_service_main`: exact entry outline and terminal failure reporting. |
| `tests/qemu/ramfs_service_vfs_fixture.c` | Nine byte/LE helpers, `vfs_endpoint_is_canonical`, `vfs_validate_configuration`, `vfs_send_ready`; sole peer storage, initialized word, and configuration ABI object. |
| `tests/qemu/ramfs_service_vfs_ramfs.c` | All thirteen request/result/call/grant/mount/lookup/create/transfer/getdent/putnode/version helpers from `vfs_initialize_ramfs_request` through `vfs_malformed_version_is_exact`. |
| `tests/qemu/ramfs_service_vfs_scenario.c` | `vfs_directory_record_matches`, `vfs_enumerate_root`, `vfs_run_filesystem_scenario`. |
| `tests/qemu/ramfs_service_vfs_tty.c` | All six TTY result/call/event/collect/drain/marker helpers. |
| `kernel/ramfs_service_test.c` | `micros_ramfs_service_test_launch`: existing non-returning launch only. |
| `kernel/ramfs_service_test_fixture.c` | Original const `profiles`, `expected_services`, `policies`, and `runtime_config`. |
| `kernel/ramfs_service_test_report.c` | `ramfs_test_failure`, `ramfs_test_uart_is_physically_drained`, `micros_ramfs_service_test_handle_trap` and its authenticated report phases. |
| `tools/generate_ramfs_service_fixture.py` | Keep all three original Python definitions at their existing owner; split `render_fixture` into descriptor, metadata/budget, image, manifest, and report rendering tasks. |

Peer fixture helpers are below RAMFS and TTY adapters; the scenario uses the
RAMFS adapter, and only entry sequences configuration, reports, scenario,
ready, TTY, and final report. TTY does not invoke filesystem scenario work.
Kernel report code never calls fixture launch. Kernel launch uses one const
configuration declaration; no second runtime configuration is built.
The generator reuses the current bootstrap descriptor/render helpers and
keeps its CLI/import/exception behavior; no new generator module is needed.

## Storage and lifetime

Consolidation means rehoming existing bytes, not adding state or snapshots:

| Family/owner | Exact existing mutable storage | Required lifetime/reset |
| --- | --- | --- |
| Functional fixture | `seed_image`, `seed_image_size`, `state`, `state_snapshot`, `corrupt_state`, `storage_snapshot`, `bus` | One static fixture; only existing seed load, bus reset, initialization, assignment, and snapshot sites modify it. Const `test_io.context` still points to that fixture's actual bus. |
| Seed fixture | `seed_image`, `mutated_image`, `seed_image_size` | One separate static fixture; preserve each copy, header builder clear, mutation, digest refresh, and rejected-plan observation. |
| Model fixture | Existing `struct ramfs_model model` and separate `seed_image`/`seed_image_size` | Preserve the complete model schema and the separate load-once seed lifetime. `memset` resets only the running model, never its loaded seed. Const `model_io.context` points to that same running model. |
| Target peer fixture | `vfs_ramfs_buffer[4096]`, `vfs_tty_buffer[256]`, `vfs_ramfs_endpoint`, `vfs_tty_endpoint` | One zero-initialized BSS context, retaining extents and original explicit clears/endpoint publication. |

The model's seed pair may form its own two-field fixture object, not become
fields of the cleared running model. No duplicate image, production state,
reference table, bus, snapshot, or data arena is introduced.

Keep these two target definitions separate from the BSS context, with their
exact type, volatility, linkage where supported, and initializers:

```text
volatile struct micros_bootstrap_service_config
    micros_bootstrap_service_config
static volatile uint64_t vfs_initialized_data = 0x52414d4653564653
```

Aggregating the initialized word with large zero buffers would change
file-backed ELF data and is forbidden. The six-service kernel profile,
expected-service, policy, and runtime configuration tables remain const.

Automatic result/message/handle/path arrays, rejected seed plans, case
locals, validator `seen_blocks`, traversal cursors, write ranges, model
expected fields, and report binding/current-handle observations retain
automatic duration. Named phase-work records may transport existing scalar
values or borrowed pointers, not retain them after return or duplicate
complete state. Only entry/case roots obtain a fixture; ordinary phase
helpers receive it explicitly.

## Required semantic outlines

All sixteen existing over-limit C definitions must expose the following
ordered work. Every resulting function is at most 120 physical lines; an
owner move, rename, or statement compression alone is not completion.

| Original function | Lines | Required ordered phases |
| --- | ---: | --- |
| core `validate_state` | 275 | Arguments/header/scratch policy; per-node empty or live shape; root or bounded parent/name/sibling checks; directory or regular block witnesses; live/root totals; link totals; physical-owner/zero-free-block closure and allocation total. |
| core `handle_lookup` | 209 | Referenced start/root, confinement, range; grant copy; complete string/component validation; confined traversal; trailing-directory/refcount and scratch-tolerant validation; scratch clear and reference/result publication. |
| core `handle_create_or_mkdir` | 167 | Referenced parent/type/mode/range; free slot and parent-link preflight; grant copy; exact name and duplicate checks; scratch-tolerant validation; existing node/link/count publication and scratch cleanup. |
| core `handle_write` | 133 | Referenced node/type/range; missing-block capacity; grant copy; scratch-tolerant validation; current input-order block reservation and data/size mutation; scratch clear and result. |
| core `micros_ramfs_handle_call` | 128 | Arguments/full-state validation; decode and protocol failure; caller and mount-state checks; operation dispatch; invariant-error propagation; full-state validation and reply. |
| functional `protocol_mount_and_result_layout_are_exact` | 230 | Before-mount rejection and malformed/caller/token cases; one successful mount/root pin and repeated mount; mounted caller/node/reference/mode cases; per-operation reserved bytes; all result encodings. |
| functional `lookup_semantics_and_reference_limits_are_exact` | 293 | Absolute/relative/empty/parent references; confined root; non-descendant rejection; trailing/missing/complete-path bounds; non-directory start variants; malformed bytes/ranges; grant failure; reference overflow. |
| functional `handles_create_mkdir_and_capacity_are_exact` | 257 | Create result/reference and duplicate; mkdir/link accounting; non-directory/unreferenced parent; mode/range/name errors; grant failure; exact reset, 61-directory fill, no-copy exhaustion. |
| functional `read_write_sparse_and_block_capacity_are_exact` | 307 | Initial file/write/read/EOF; sparse zero region; cross-block update; rejected-copy snapshots; directory/range failures; exact reset and maximum-size boundary; exact reset, 63-block fill, second-file no-copy exhaustion. |
| functional `getdents_cursor_and_records_are_exact` | 140 | Complete dot/dotdot/seed record bytes; one-record cursor progression; create file/directory and continuation; EOF no-copy/no-write; type/cursor/record-size/grant failures. |
| seed `header_corruption_is_rejected` | 164 | Size/magic/version/layout; entry-count and reserved metadata; data capacity and exact image-size boundaries; digest corruption. |
| seed `record_name_and_reserved_corruption_is_rejected` | 140 | Entry/unused-record reserved bytes; root/empty/overlong name; NUL/slash/tail bytes; dot/dotdot; duplicate sibling name. |
| model `reference_lookup` | 136 | Reference/root/range and complete path validation; independent confined traversal; trailing type and reference publication. |
| model `execute_scripted_operation` | 141 | Explicit steps 0-5 setup; steps 6-15 sparse/directory/reference/stale/malformed/grant/remount/path-bound proofs; unchanged false default. |
| peer `vfs_run_filesystem_scenario` | 199 | Mount/version and seed lookup; wrong-rights failure then exact seed/EOF reads; stale handle and seed release; mkdir/lookup/create and over-put rejection; sparse write/read, enumeration, and exact releases. |
| kernel `micros_ramfs_service_test_handle_trap` | 172 | Existing observations; breakpoint/PC/current-VFS authentication; resumable pre-ready report; exact failure-code dispatch; sealed/runtime/TTY/grant invariants; all-ready and physical drain; successful shutdown. |

Shorter `micros_ramfs_state_initialize`, `handle_read`, and `handle_getdents`
also expose their existing preflight, staging/building, validation, and
publication boundaries. `compare_state` exposes header/scratch, every node,
and every physical-block projection. These are named task outlines, not
additional original long migrations.

The Python renderer preserves this exact outline: validate six descriptors
in launcher/VM/PM/TTY/RAMFS/VFS order; validate VM-only metadata, report symbol,
TTY overlap, and RAMFS budget; calculate per-service and aggregate limits;
render segment bytes and six images; render the existing manifest entries
in VFS/launcher/RAMFS/TTY/PM/VM order; render report address; join once.
Each phase is at most 120 lines and retains literal output ordering.

### Failure and mutation boundaries

Preserve every predicate, argument, write, error value, short-circuit,
break/continue/goto meaning, and immediate caller exit. Returning a helper
error or false must not let its caller continue a success path.

`validate_state(state, true)` requires clear scratch. Operation precommit
validation deliberately uses `false` while staged bytes exist. Preserve
the distinction, the complete automatic block witness, and the stronger
existing `parent_slot < index` invariant, not merely generic acyclicity.

Lookup validates all component lengths before searching, including an
overlong component after a missing name. Empty relative paths still require
a directory start. Root confinement, trailing separators, final overflow,
clear-before-reference-publication, and every scratch-clear error exit stay
exact. Create reserves no new authority before its existing preflight;
mkdir returns no created-node reference.

Read stages sparse zeros and copies out before result publication.
Write retains capacity-before-copy, copy-before-validation, zeroed new
blocks, input-order allocation, and size publication. Its existing
invariant-only `find_free_block` failure after mutation remains explicit:
do not invent rollback or silently convert it to an ordinary failure.
Initialization likewise keeps post-publication validation and complete
zero-on-invariant-failure. The facade preserves protocol-error reply
precedence before caller checks, terminal invariant errors, and final
validation before reply. Failed operations retain complete state and
destination snapshots wherever the existing oracle requires them.

## Evidence identities

The functional `main` keeps the seven case functions in the exact table order
above, ending with putnode/handle validation, first-failure short-circuit,
exit statuses, and `RAMFS_CORE_TEST_PASS`. Seed `main` keeps boundary cases,
then header/name/parent/data corruption in the current order and retains
`RAMFS_SEED_TEST_PASS size=%zu digest=0x%016llx`.

The independent model keeps seed `0x52414d46534d4f44`, 8,192 steps,
64-bit xorshift shifts 13/7/17, all draw sites and ordering, and complete
trace encoding/FNV update. Steps 0-15 retain their existing script. Steps
`7900 <= step < 7965` issue exactly 65 one-byte writes at successive
4,096-byte logical offsets; they consume no new random draws.
All thirteen coverage fields retain their check order: mount at least two;
absolute lookup, relative lookup, create, mkdir, read, write, getdents,
putnode, stale, malformed, grant failure, and no-space all nonzero.
Keep every reply field, whole-bus, node/block/data, scratch, and reference
comparison and the complete failure operation trace. The reviewed base's
actual successful trace, recorded before extraction, is `0x79bfaa8d631a40bf`.
Require the same trace afterward; do not derive its expected value from
rewritten code.

The peer entry retains configuration, pre-ready report, filesystem scenario,
ready acknowledgment, TTY pass marker/collect/drain, and final report in that
order. Every created grant is revoked even when its IPC call fails.
Scenario handles/results remain automatic. Kernel reporting authenticates
the original breakpoint PC and current VFS thread generation. Pre-ready
proves STARTING with zero grants and advances `sepc` by four; final reporting
proves all services ready, sealed bootstrap, handed-off VM, real TTY ownership
and UART mapping, zero grants, runtime validators, and physical drain.

Keep the complete literal `MICROS_RAMFS_TEST_PASS` record, protocol report and
failure constants, report assembly, workflow timeouts, six-service/image
101-106 tuples, one-stack-page options, 192-page RAMFS resident limit, and
4,096-page aggregate limit unchanged. The generator produces byte-identical
text and identical ordered failures for identical input images. Freeze
independently derived base-output digests and explicit small fixtures;
do not require Git, local MINIX, or network at test runtime.

## Build, coverage, and recipe integration

Every current core consumer expands `MICROS_RAMFS_CORE_SOURCES` once:
`ramfs_test`, `ramfs_model_test`, and the production
`micros_ramfs_service` bootstrap-user target. Keep the existing
`ramfs_seed.c` operand alongside it. `ramfs_seed_test` retains only its seed
parser plus the seed evidence set. VFS native/model fixture shims do not
link real RAMFS today; do not add that dependency.

Each native executable expands its corresponding evidence set once.
The actual RAMFS test peer expands the peer set beside unchanged report
assembly. Only the existing RAMFS kernel-test image adds the report set
beside the generated fixture. Preserve includes, C17, warning flags,
sanitizer compile/link options, dependencies, working directories, CTest
arguments/labels, build options, post-link checks, and generated-file rules.
No object library, target, CTest, preset, or workflow is added.

Use six literal rows in existing `PATH_CLASSIFICATION_EQUIVALENTS`, one for
each original C root and its split sources/private header, and register only
`tests/host/test_ramfs_source_layout.py` as host-unit-only.
Preserve these four independent metadata dimensions:

| Original owner | Full tier | QEMU | Separate slow model | Image |
| --- | --- | --- | --- | --- |
| `servers/ramfs/ramfs_core.c` | no | RAMFS, VFS, ELF-spawn | `test-ramfs-model` | `build-ramfs-service-image` |
| `tests/host/ramfs_test.c` | yes | empty | empty | empty |
| `tests/host/ramfs_seed_test.c` | yes | empty | empty | empty |
| `tests/host/ramfs_model_test.c` | no | RAMFS, VFS, ELF-spawn | `test-ramfs-model` | empty |
| `tests/qemu/ramfs_service_vfs.c` | no | RAMFS, VFS, ELF-spawn | empty | empty |
| `kernel/ramfs_service_test.c` | no | RAMFS, VFS, ELF-spawn | empty | empty |

Here the three QEMU names mean their existing `test-qemu-` workflows.
The unchanged generator retains its eleven existing QEMU owners and empty
model/image metadata. No selector function, full-tier fallback, command
builder, execution, or reuse algorithm changes.

The retained trap/syscall planner oracle may project out only these exact
six alias rows and the one new structural-gate literal after independently
checking their contents; its original planning AST remains frozen.
That gate also freezes a complete CMake reconstruction after removing its
own source sets. Its one expected CMake digest may follow the proven RAMFS
source-set change only after the independent reviewed-base reconstruction
below succeeds. Keep its original consumer checks and all other expectations;
do not discard the new sets or arbitrary recipe changes in that oracle.

### Closed recipe declaration maintenance

ADR-0064/0065's `RECIPE_DIGESTS["CMakeLists.txt"]` currently admits SHA-256
`afb07b0e891e11c677782570ac5a5310dd3cd3d2418c5b741555fe0263a04877`.
The source-set move must not leave all three supported workflows ineligible.

The structural Red freezes the complete base recipe and all existing
consumer multiplicities. Its reconstruction removes only the exact six
new set declarations and replaces their exact consumer operands by original
source operands. Apart from the explicitly bounded insertion whitespace,
the restored recipe must hash to the immutable base digest. No other
command, variable, condition, target, compiler flag, dependency, generated
input, checker, workflow, or timeout can be normalized away.

After that proof, update only the literal CMake digest in
`tools/validation_context.py` to the actual complete new recipe digest.
Add direct tests of actual declared-byte equality and rejection of
unreviewed recipe bytes. Preserve the toolchain and QEMU-runner digests,
profile name, three-workflow inventory, closure algorithms, complete
source/tool/environment identities, strict receipt checks, and fail-closed
fallback. This is maintenance of a separately reviewed exact declaration,
not generalized source-list normalization during reuse.

The implementation's current full producer plus bounded actual reuse
acceptance must demonstrate native, smoke, and TTY-image receipts remain
eligible after the source move. Use the existing documented fresh/reuse CLI;
the reuse acceptance is additional evidence for this coupled declaration,
not a replacement for the current full author gate. Unsupported selected
workflows still execute fresh. No older-HEAD receipt is carried across merge.

## Structural enforcement and execution

`tests/host/test_ramfs_source_layout.py` reuses existing lexical/CMake helpers.
It checks exact sets, headers, consumers and flags; stable neighbors;
all 189 original-definition owners; narrow declarations and recursive
include/call direction; exact contexts, const/volatile/initialized/ABI and
automatic lifetimes; sixteen C plus one Python semantic outlines; complete
original case/script/model/generator identities; metadata inheritance;
and bounded recipe reconstruction/declaration maintenance.

Exactly 180 original C definitions move; nine remain at root owners
(core two, functional one, seed one, model two, peer two, kernel one).
Record all 180 moves in a frozen literal owner inventory. Record sixteen
long C functions and the Python renderer in a separate frozen semantic
inventory. During green extraction only explicit pending entries may retain
their reviewed-base owners/bodies. Both active collections are literal empty
tuples before review.

Use immutable base-derived body/condition/call/effect records. Private
renaming and verified typed transport of existing locals may be normalized;
predicates, arguments, effects, order, caller exits, and automatic scope
must not be erased. Small independent positive/negative fixtures must catch
wrong VFS or generation checks, scratch-policy flips, lost clear/cleanup,
parent-order weakening, dropped state/data comparisons, lost grant revoke,
reordered commit, reset of loaded seed, changed initialized storage, and
callee-local success incorrectly replacing caller failure. This supplements
real semantic evidence and full review, not a general C interpreter.

The narrow implementation loop is:

```bash
python3 -m unittest tests.host.test_ramfs_source_layout
cmake --preset host-debug
cmake --build build/host-debug --target ramfs_seed_test ramfs_test ramfs_model_test
ctest --test-dir build/host-debug --output-on-failure -R '^ramfs-(seed|core|model)$'
python3 -m unittest tests.host.test_generate_ramfs_service_fixture
```

Retain `ramfs-seed`, `ramfs-core`, `ramfs-model`,
`test-ramfs-model`, `build-ramfs-service-image`, and `test-qemu-ramfs`,
plus downstream VFS and ELF-spawn integrations. Run only gates invalidated
by each coherent extraction. Before implementation review stage the whole
change, prove tracked/untracked task cleanliness, record HEAD/tree/exact
commands/results, and execute one authoritative PR plan. Shared CMake
ownership currently selects complete native validation, all 38 QEMU
workflows, four images, docs/structure, and three diff checks. Do not
separately duplicate its canonical workflows except the explicitly bounded
real reuse acceptance above.

## Implementation sequence and phase barrier

After the documentation-only design is independently reviewed, Accepted,
and separately merged, start a fresh implementation branch from current main:

1. Structural Red, frozen base/trace/output inventories, exact private
   declarations/source sets/classification, and initial literal migrations.
2. Portable codec/ownership/validation/transfer and operation owners, with
   explicit failure and staging/commit phases; narrow green native evidence.
3. Native functional/seed fixtures and semantic cases; independent model
   fixture/reference/projection/actions/script while preserving reset/draws.
4. Target peer fixture/adapters/scenario/TTY, const kernel fixture and
   authenticated report, generator rendering phases; real image/QEMU proof.
5. Empty migrations, exact recipe declaration update/proof, one complete
   current author plan and bounded reuse acceptance, independent read-only
   implementation review and justified corrections.

This design authors no source skeletons, executable tests/models, harnesses,
build scaffolding, or reusable prototypes.

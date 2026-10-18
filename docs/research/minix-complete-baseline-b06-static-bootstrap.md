# B06 Static-Bootstrap Baseline Audit

## Status

```text
audit unit:       B06 static bootstrap
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; four blocking findings; not certified
implementation:   forbidden during complete-audit phase
```

B06 audits boot descriptors, image/manifest/profile/policy validation,
deterministic topology, complete static resource preparation, rollback,
controller publication, service release, readiness, deadlines, console
gating, fatal diagnostics, launcher sealing, and boot orchestration.

## Exact primary path manifest

B06 has 33 primary paths in the pinned tree.

### Public contracts

| Path | Responsibility |
| --- | --- |
| `include/micros/bootstrap.h` | Manifest, image-info, plan, runtime, diagnostics, and service configuration ABI |
| `include/micros/bootstrap_control.h` | Operation-11 command/failure request and decoder ABI |

### Boot, manifest, preparation, runtime, and control owners

| Path | Responsibility |
| --- | --- |
| `kernel/boot/boot_component_tests.c` | Compile-time component dispatch and exact target evidence order |
| `kernel/boot/boot_foundation.c` | Object/trap, FDT, allocator/ownership, MMU, PLIC, and shutdown orchestration |
| `kernel/boot/boot_internal.h` | Automatic boot context and ordered boot phases |
| `kernel/boot/bootstrap_control.c` | Operation-11 scalar decode and failure-detail shape |
| `kernel/boot/bootstrap_control_core.c` | Portable control state, release, ready, complete, and validation lifecycle |
| `kernel/boot/bootstrap_control_internal.h` | Binding/control state and ready/complete plans |
| `kernel/boot/bootstrap_diagnostics.c` | Stable fatal record, deadline checks, and one-shot failure |
| `kernel/boot/bootstrap_image.c` | Generated image-catalog validation |
| `kernel/boot/bootstrap_image.h` | Image segment/catalog representation |
| `kernel/boot/bootstrap_manifest.c` | Manifest facade, entry validation, aggregate roles, and topology |
| `kernel/boot/bootstrap_manifest_common.c` | Fixed-name, byte, and alignment primitives |
| `kernel/boot/bootstrap_manifest_internal.h` | Manifest candidate and private validation contracts |
| `kernel/boot/bootstrap_manifest_profiles.c` | Profile table and role/profile relationship validation |
| `kernel/boot/bootstrap_preparation.c` | Policy/configuration validation, service preparation, roles, and control state |
| `kernel/boot/bootstrap_preparation_image.c` | Image/stack/view allocation, verification, configuration patch/readback |
| `kernel/boot/bootstrap_preparation_rollback.c` | Reverse resource release and baseline restoration |
| `kernel/boot/bootstrap_runtime.c` | Prepare, controller publication, and nonreturning launch |
| `kernel/boot/bootstrap_runtime.h` | Target bootstrap configuration and runtime interface |
| `kernel/boot/bootstrap_runtime_internal.h` | Static context, records, baseline, and preparation contracts |
| `kernel/boot/bootstrap_runtime_state.c` | Sole runtime context, byte helpers, state accessors, controller query |
| `kernel/boot/bootstrap_runtime_validation.c` | Complete runtime and prepared-VM validation |
| `kernel/boot/bootstrap_syscall.c` | Captured bootstrap syscall preflight and command dispatch |
| `kernel/boot/bootstrap_syscall.h` | Captured operation-11 adapter |
| `kernel/boot/bootstrap_syscall_common.c` | Error maps, controller authority, role gates, fatal helpers |
| `kernel/boot/bootstrap_syscall_complete.c` | Launcher sealing transaction |
| `kernel/boot/bootstrap_syscall_failure.c` | Launcher-submitted fatal classification/detail |
| `kernel/boot/bootstrap_syscall_internal.h` | Private captured-command contracts |
| `kernel/boot/bootstrap_syscall_ready.c` | Token-bound readiness acknowledgment and captured completion |
| `kernel/boot/bootstrap_syscall_release.c` | Console begin and ordinary/console service release |
| `kernel/boot/bootstrap_transitions.c` | Portable initialize/release/ready/complete/classify transitions |
| `kernel/boot/main.c` | Sole ordered kernel entry outline |

## Cross-owner source dependencies

| Path family | B06 operation | Primary/later owner |
| --- | --- | --- |
| `arch/riscv64/**`, `kernel/platform/**`, FDT and panic | B06-O01/O10 foundation and fatal output | B01 |
| allocator, ownership, kernel/user address spaces | B06-O03/O07 preparation, rollback, device mapping | B02 |
| objects and scheduler | B06-O03-O08 process/thread/context/admission/removal | B03 |
| endpoint and IPC | B06-O04-O08 publication, readiness reply, notifications, source-only seal | B04 |
| grants and user execution | B06-O03/O08 initialization, launcher dependency, context preparation | B05 |
| `kernel/vm/**`, `kernel/pm/**`, `kernel/tty/**` | B06-O03/O06-O09 role gates and retained state | B07-B09 |
| launcher ELF/protocol/evidence paths | B06-O05-O09 user-space controller behavior | B14 |

## Fixed MINIX source evidence

The B06 comparison uses only local source:

- `minix/include/minix/type.h`
- `minix/include/minix/rs.h`
- `minix/include/minix/ipc.h`
- `minix/include/minix/sef.h`
- `minix/kernel/table.c`
- `minix/kernel/main.c`
- `minix/kernel/system/do_privctl.c`
- `minix/servers/rs/table.c`
- `minix/servers/rs/type.h`
- `minix/servers/rs/const.h`
- `minix/servers/rs/main.c`
- `minix/servers/rs/utility.c`
- `minix/servers/rs/request.c`
- `minix/servers/rs/manager.c`
- `minix/servers/rs/update.c`
- `minix/lib/libsys/sef.c`
- `minix/lib/libsys/sef_init.c`
- `minix/servers/pm/main.c`
- `minix/servers/sched/main.c`
- `common/lib/libc/arch/arm/string/memcpy.S`
- `common/lib/libc/arch/arm/string/memset.S`
- `common/lib/libc/string/memcpy.c`
- `common/lib/libc/string/memset.c`

## Logical variables

| Variable | Meaning |
| --- | --- |
| `n` | active static service/manifest entries |
| `ncap` | static service capacity, pinned at 7 |
| `f` | installed privilege profiles |
| `i` | generated service images |
| `u` | prepared user pages across all services |
| `z` | image/configuration/stack bytes copied, cleared, or verified |
| `Vctl` | complete bootstrap control-state relation validation |
| `Vboot` | complete target bootstrap runtime validation |
| `Vep` | B04 complete endpoint/object/IPC validation work |
| `Vgrant` | B05 complete grant relation validation work |
| `Rmem` | B02 complete address-space validation/lookup/translation work |
| `Fown` | B02 complete frame-ownership validation work |
| `Dvm` | B07 VM role/runtime validation work |
| `Dtty` | B09 TTY role/runtime validation work |

## Validation cost definitions

One control-state validation:

1. runs complete object validation and `Vep`;
2. visits every binding;
3. resolves each binding endpoint through public resolution, invoking `Vep`;
4. scans transition entries to match each binding; and
5. after sealing, scans all thread slots and performs binding/transition
   searches for staged launcher acknowledgments.

```text
Vctl = O(
    Vep
    + n * Vep
    + n^2
    + sealed * tcap * n
)
```

One complete target-runtime validation adds:

```text
Vboot = O(
    Vctl
    + Fown
    + Vgrant
    + Dtty
    + n * 3*Rmem
)
```

The `3*Rmem` term represents one complete address-space validation plus
execution-context validation whose PC and stack checks each perform a
complete lookup. Cross-owner constants and nested validators remain those
recorded by B02-B05.

## Operation ledger

### B06-O01 Fixed boot identity and inhibited service state

MINIX has one kernel boot-image table in module order. Kernel startup creates
every process/endpoint identity, records the boot module range, and leaves
ordinary services inhibited. RS later installs exact policy while each target
remains inhibited.

`micros` uses one immutable pointer-free manifest plus a generated image
catalog. It prepares process, root, image, stack, context, reserved endpoint,
and held scheduling state for every service before publishing the controller.

| Difference | Classification |
| --- | --- |
| Fixed boot identity separated from live lifecycle state | Baseline parity |
| Exact profile installation while the target is held | Baseline parity |
| Generation-safe reserved endpoint hidden before release | Compatible extension |
| Complete preparation before one-way VM handoff | Required adaptation |
| Capacity seven while production remains the six-service chain | Compatible evidence-bound extension |

### B06-O02 Image, manifest, profile, policy, and topology validation

MINIX walks the fixed privilege table and, for each service, linearly looks up
the corresponding boot-image, system-policy, and device-policy entries.
Boot initialization is therefore `O(n^2)` when the parallel table sizes grow
together. Table order is the release order.

`micros` validates generated images, fixed names, IDs, process/profile/image
uniqueness, exact expected tuples, role/profile closure, page budgets, policy
rows, and an explicit prerequisite graph. It selects the lowest dependency-
ready service by scanning all entries for each output position.

```text
MINIX fixed boot policy initialization:
    O(n^2 + policy bitmap work)
micros manifest/catalog/policy/topology validation:
    O(n^2 * fixed_name_bytes + n*i + n*f + f^2*profile_name_bytes + z)
```

The explicit graph and lowest-ID tie break are compatible extensions. Their
quadratic work remains inside the fixed reference's boot-wide `O(n^2)`
table-lookup envelope. This is not a blocking finding.

### B06-O03 Static resource preparation, image installation, and rollback

MINIX kernel startup associates preloaded boot modules with process records.
VM/RS complete mappings, privileges, scheduling, and service runtime state
before allowing execution.

`micros` processes the validated order in two stages:

1. create each exact process/root, map/copy/verify image pages, create/verify
   stack, map the launcher manifest view, and reserve the endpoint;
2. patch/read back every 128-byte service configuration, create/prepare each
   thread, execute `fence.i`, validate every root/owner, and prepare VM/PM/TTY
   role state.

```text
table/image/policy lookups and service-table construction: O(n^2)
mapping and teardown: inherited B02 costs over u pages
byte copy/clear/verification: O(z)
rollback: reverse service/page order plus inherited B02/B03/B04 release costs
```

The two-pass held preparation, complete-page zeroing, verification, and reverse
failure cleanup are required adaptations or compatible safety extensions.
Per-page complete address-space validation and capacity-wide release confirm
B02-F001/F002/F005/F006 rather than creating duplicate B06 findings.

### B06-O04 Portable control state and controller publication

The portable state records the immutable manifest, derived plan, one runtime
entry and binding per service, and controller identities. Controller
publication initializes transitions, installs the exact profile/endpoint,
admits the prepared thread, and changes phase to running.

MINIX boot release directly uses the current table entry and corresponding
`rproc` slot. Its readiness path maps the source process slot through
`rproc_ptr` in `O(1)`.

`micros` finds bindings and runtime entries by linear service-ID scans and
copies the complete `O(n)` transition object for release and ready plans.

```text
MINIX selected boot record: O(1)
micros selected runtime/binding record: O(n)
micros release/ready transition snapshot: O(n)
```

See B06-F001.

### B06-O05 Service release and readiness deadline

MINIX walks its boot table, starts scheduler policy, clears privilege
inhibition, and sends `RS_INIT`. It records initialization timing state, but
fresh boot collects the remaining results through untimed receives before RS
arms its periodic alarm. Services not marked synchronous may initialize
concurrently.

`micros` permits only the exact next service. It validates prerequisites,
computes one absolute deadline, atomically publishes the endpoint/profile and
scheduler state, and permits no second starting service.

| Difference | Classification |
| --- | --- |
| Exact release after policy installation | Baseline parity |
| Guest-owned absolute static-bootstrap deadline without sleeps | Required adaptation |
| Explicit prerequisite graph and exact-generation publication | Compatible extension |
| One starting service at a time | Staged substitution; SOM-01 and SOM-23 |

Local transition work is `O(n)` because of B06-O04. Target command cost also
includes the complete validation terms below.

### B06-O06 Token-bound readiness result and acknowledgment

MINIX SEF calls one service-specific initialization callback and sends its
result to RS. Ordinary services use `sendrec` and block until RS replies. Fresh
VM uses a one-time asynchronous result with no reply to avoid the VM/RS boot
cycle. RS indexes the sender's runtime slot, checks initializing state, and
clears the initialization flag.

`micros` validates one versioned message in the launcher, then operation 11
prepares the exact transition, replies only to the token-bound caller, commits
ready state, stages no-message syscall completion, and returns through the
captured scheduler path.

| Difference | Classification |
| --- | --- |
| Ordinary-service result plus reply gate before startup continues | Baseline parity |
| One-shot reply token and exact endpoint generation | Compatible extension |
| Fresh VM uses the same token-bound call after the separate ownership commit instead of MINIX's asynchronous no-reply result | Required adaptation and staged transport substitution; SOM-23 |
| VM and TTY readiness role gates | Required adaptation |
| No general SEF callback/restart/live-update layer | Staged substitution; SOM-01 |

The pair reply path inherits B03/B04 scheduler/IPC transition and stack costs.

### B06-O07 Console begin and console-owner release

MINIX boot policy contains device identity and later driver/RS ownership.
`micros` adds the accepted QEMU `virt` adaptation: exact VM/PM readiness,
one UART mapping plan, retained deadline, early-console quiesce, VM
notification, TTY release, TTY ownership commit, and role-gated readiness.

This is a required platform/boot-cycle adaptation. Its local plan work is
bounded; complete VM, TTY, ownership, address-space, endpoint, IPC, and PLIC
validation contributes `Dvm`, `Dtty`, `Fown`, `Rmem`, and `Vep`.

### B06-O08 Launcher completion and source-only sealing

MINIX RS remains the live recovery manager. `micros` instead verifies that the
launcher has no runnable IPC state or active grant dependency, checks staged
acknowledgments, removes it from scheduling, changes its endpoint to
source-only, clears the controller binding, optionally notifies PM, and seals
bootstrap authority.

```text
local completion scans:
    O(tcap * n + gcap + n)
plus Vctl, endpoint/IPC/scheduler plans, and command validation
```

The retained source-only launcher is a staged substitution owned by SOM-01.
The exact cleanup scans are an explicit required adaptation for revoking this
temporary authority boundary; ordinary valid-path overvalidation remains
B06-F002/F003.

### B06-O09 Fatal failure, malformed detail, and deadline checks

MINIX fresh boot treats a non-OK initialization result as fatal, but it does
not enforce the later periodic RS timeout while collecting boot results.
After boot, RS checks initialization/liveness deadlines and applies
restart/update policy. `micros` enforces an absolute static-bootstrap deadline,
records one stable failure line, and panics because recovery is not
dependency-ready.

Deadline checking examines the TTY handoff first, then the one ordinary
starting service. Diagnostic lookup is `O(n)`.

| Difference | Classification |
| --- | --- |
| Fatal fresh-boot non-OK initialization result | Baseline parity |
| Absolute guest-owned static-bootstrap timeout | Required adaptation |
| Stable reason/service/endpoint/phase/state/detail record | Compatible extension |
| Fatal failure without restart, replica, or live update | Staged substitution; SOM-01 |

### B06-O10 Kernel boot outline and compile-time component dispatch

`kernel_main` owns one automatic 1,368-byte boot context and preserves the
ordered object/trap, FDT, memory/MMU/PLIC, component, nested-trap, and shutdown
phases. Component routing is compile-time evidence selection; ordinary builds
do not gain a runtime test registry.

Platform/memory mechanisms are audited by B01/B02. B06 classifies only the
static-bootstrap orchestration boundary as baseline parity with required
RISC-V/OpenSBI adaptations.

## Ordinary command validation cadence

Before every valid operation-11 command, the syscall adapter:

1. resolves and authorizes the exact controller through endpoint/profile
   public resolution;
2. runs `Vboot`; and
3. resolves the authoritative grant registry.

Command owners then repeat complete checks:

```text
ordinary release:
    2*Vboot + 2*Vctl + scheduler/endpoint command work
accept ready:
    2*Vboot + Vctl + IPC/scheduler reply work
complete:
    2*Vboot + 2*Vctl + O(tcap*n + gcap) + IPC/scheduler work
console begin:
    2*Vboot + Dvm + Dtty + Fown + Rmem + IPC work
```

The first `Vboot` is common preflight. Successful handlers invoke another
`Vboot`, while portable control helpers invoke `Vctl` before and sometimes
after mutation.

See B06-F002 and B06-F003.

## Difference summary

| Area | Result |
| --- | --- |
| Fixed identity, inhibition, exact release | Baseline parity with generation/profile extensions |
| Explicit graph, image checks, page budgets | Compatible/required adaptations within the reference boot-wide complexity envelope |
| Static service preparation before handoff | Required adaptation |
| Per-service runtime indexing/transition mutation | Blocking divergence |
| Complete bootstrap runtime validation on each command | Blocking divergence |
| Repeated nested validation before/after one transition | Blocking divergence |
| Serial readiness and fatal service loss | Staged substitution |
| Byte transfer and clearing | Blocking divergence |

## Time, stack, and auxiliary-space envelope

Persistent B06-owned target state is one 3,688-byte
`bootstrap_runtime_context`. It includes:

```text
bootstrap control state:        2,288 bytes
manifest:                       1,408 bytes (inside control state)
runtime transitions:             384 bytes (inside control state)
seven preparation records:     7 * 56 bytes
seven bindings:                7 * 56 bytes
seven image infos:             7 * 56 bytes
```

Representative automatic storage is:

```text
kernel boot context:           1,368 bytes
control-state candidate:       2,288 bytes
manifest candidate:              152 bytes
image-info candidate array:      392 bytes
runtime transition candidate:    384 bytes
caller + nested ready plans:     912 bytes
caller + nested complete plans: 1,040 bytes
service configuration verify:    256 bytes plus 56-byte service table
```

Ready acknowledgment may additionally nest the B03/B04 5,120-byte pair
transition workspace. All control commands nest B02-B05 validator witnesses;
console commands also nest B07/B09 plans. No current evidence proves the
complete valid/error envelope against the 16 KiB supervisor stack.

Prepared image, stack, manifest-view, VM boot-information, and service
configuration bytes reside in their owned frames or static objects rather
than automatic supervisor storage.

## Staged omissions

| ID | B06 relation |
| --- | --- |
| SOM-01 | DS/RS/SEF discovery, callbacks, heartbeat, restart, replica, live update, recovery, and launcher replacement |
| SOM-02 | User-space scheduler policy after bootstrap assignment |
| SOM-03 | Dynamic drivers, device tables, and shared IRQ routing |
| SOM-06 | Production init is created by the later spawn/process path |
| SOM-23 | MINIX asynchronous initialization transport and overlapping service startup |
| SOM-24 | RS selective/timed receive filters used by the later control plane |

## Evidence gaps

| ID | Missing evidence | Owner |
| --- | --- | --- |
| B06-E001 | No comparison/lookup counter records manifest, catalog, policy, profile, topology, and service-table work | B14 |
| B06-E002 | No transition counter distinguishes direct service-slot access from repeated binding/runtime scans and full-state copies | B14 |
| B06-E003 | No command trace counts `Vboot`, `Vctl`, and cross-owner validator invocations for release/ready/complete/console | B14 |
| B06-E004 | No boot copy/fill work counter distinguishes byte, word, and block paths over image, stack, manifest, configuration, and state sizes | B14 |
| B06-E005 | No complete nested operation-11/IPC/scheduler/validator stack evidence exists for the 16 KiB supervisor stack | B14 |
| B06-E006 | No overlapping asynchronous initialization, restart, or recovery evidence exists because SOM-01/SOM-23 are absent | B14 after the owning designs |

## Findings register

### B06-F001: Runtime service lookup and transitions are capacity-linear

- Severity: Medium
- `micros` evidence:
  - `runtime_entry` / `runtime_entry_const`
  - `find_runtime_entry`
  - `find_binding_mutable`
  - `micros_bootstrap_control_find_binding_bounded`
  - complete transition snapshots in release/ready/complete
- MINIX evidence:
  - table-relative `rproc` slot during boot release
  - endpoint-slot `rproc_ptr[who_p]` during readiness
- Difference: MINIX boot release and readiness already possess the exact
  runtime record in `O(1)`; `micros` repeatedly scans `n` entries by service
  ID and copies the complete transition object for one local state change.
- Impact: each release, ready classification/acceptance, and seal substep
  gains `O(n)` work independent of the selected service.
- Smallest correction boundary: retain service IDs and deterministic order
  while adding one validated service-ID/manifest-index/runtime-index map and
  path-local failure-atomic plans that mutate only the selected record.
- Dependencies: B14 transition operation counts.

### B06-F002: Every bootstrap control command runs whole-system validation

- Severity: High
- `micros` evidence:
  - `preflight_runtime`
  - `micros_bootstrap_runtime_validate`
  - `micros_bootstrap_control_validate`
- MINIX evidence: local initializing-slot, privilege, and result checks on the
  selected service; post-boot deadline enforcement is a separate periodic
  table scan
- Difference: an ordinary controller command validates all bindings,
  endpoints, objects, frame ownership, grants, TTY state, address spaces, and
  execution contexts before performing one local transition.
- Impact: each command pays `Vboot`, including `n*Vep`, `Vgrant`,
  `n*3Rmem`, and inherited cubic/capacity-wide terms from B02-B05.
- Smallest correction boundary: keep complete bootstrap validation as an
  explicit diagnostic operation; command preflight validates runtime
  readiness, exact controller authority, selected binding/transition, and
  only the cross-owner resources that command consumes.
- Dependencies: B02-F002/F005, B03-F001/F005, B04-F001/F002, B05-F001.

### B06-F003: Successful commands repeat complete validation before and after commit

- Severity: High
- `micros` evidence:
  - `control_release` pre/post control validation
  - `finish_release`
  - `micros_bootstrap_control_prepare_ready`
  - ready post-commit runtime validation
  - complete pre/post control/runtime validation
  - console post-commit runtime validation
- MINIX evidence: one selected-service preflight, transition, result/reply,
  and local state update
- Difference: the common `Vboot` preflight is followed by one or more nested
  `Vctl` passes and another `Vboot` after a successful local transition.
- Impact: valid release/ready/complete/console commands pay the multiples
  recorded in the command-cadence table, compounding every earlier audit
  finding.
- Smallest correction boundary: one path-local preflight plan plus a
  non-failing commit; perform whole-system validation only at explicit
  diagnostic/acceptance boundaries.
- Dependencies: B06-F001, B06-F002, and the cited B02-B05 validators.

### B06-F004: Bootstrap copy and clear operations are byte loops

- Severity: Medium
- `micros` evidence:
  - `bootstrap_runtime_copy_bytes`
  - `bootstrap_runtime_clear_bytes`
  - manifest/control byte helpers
  - per-page image/stack/view/configuration preparation
- MINIX evidence:
  - architecture `memcpy`/`memset` word/block algorithms
  - boot-image/process-table bulk copies
- Difference: image pages, stack pages, manifest/configuration objects, and
  complete state snapshots use source-level byte iteration with no explicit
  alignment or block-transfer algorithm.
- Impact: boot copies and clears `O(z)` bytes with no structural or
  generated-code evidence of the reference word/block path; the largest
  static-service images amplify the constant-factor regression.
- Smallest correction boundary: independently provide standard-defined RV64
  alignment/block copy and fill primitives for nonoverlapping boot storage,
  with byte tails and unchanged zeroing/verification/failure behavior.
- Dependencies: B14 copy/fill work evidence and the B05-F006 runtime-memory
  correction where a shared mechanism is appropriate.

## Unit conclusion

B06 primary-path and operation coverage is complete for the pinned tree. The
audit adds B06-F001 through B06-F004 as blocking findings,
confirms B02-F001/F002/F005/F006, B03-F001/F005, B04-F001/F002, and
B05-F001, and records SOM-01, SOM-02, SOM-03, SOM-06, SOM-23, and SOM-24.

B06 is **audited but not certified**. Independent review corrected fresh-boot
VM acknowledgment and timeout classifications, then removed one stale
deadline claim from the findings evidence. Final re-review reported B06
complete. No B06 correction may start during the read-only audit.

## References

- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)
- [B02 memory audit](minix-complete-baseline-b02-memory.md)
- [B03 object/scheduler audit](minix-complete-baseline-b03-objects-scheduler.md)
- [B04 endpoint/IPC audit](minix-complete-baseline-b04-endpoint-ipc.md)
- [B05 grant/user-execution audit](minix-complete-baseline-b05-grants-user-execution.md)
- [MINIX RS/SEF bootstrap study](minix-rs-sef-bootstrap.md)
- [MINIX bootstrap source-organization study](minix-bootstrap-source-organization.md)

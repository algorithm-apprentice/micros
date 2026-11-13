# MINIX Selected VM Validation Study

## Fixed inputs

```text
micros source: 463edf502c8f47f37e55dbc2e0c6d4d47aa345d8
micros tree:   d085ddb6eac3fd67710fe0eaabfd08b202d9344e
MINIX source:  4db99f4012570a577414fe2a43697b2f239b699e
```

Both repositories were checked clean at these revisions. MINIX research
uses the local `/Users/alephling/Repo/minix` checkout read-only.
BOOT03 #270 is the completed preferred predecessor. VM01's actual
prerequisites remain BOOT02 #268, MEM04 #228 and GRANT01 #259.

## Fixed MINIX behavior and mechanisms

| Local reference | Observed responsibility |
| --- | --- |
| `minix/servers/vm/main.c:105-186` | Receive one message, resolve its exact caller, bound/map the call number, check that process's ACL and dispatch; reply remains tied to that source. |
| `utility.c:84-94` | Endpoint-slot selection bounds the index, compares the full stored endpoint and requires the selected `VMF_INUSE` record. |
| `region.c:615-645` | The selected process's AVL predecessor lookup checks the requested offset against the returned region; physical-block selection stays within that region. |
| `exit.c:60-94` | Resolve the named process, require its announced EXITING state, then free that process and clear its slot. |
| `minix/kernel/system/do_vmctl.c:16-104` | Resolve the exact target endpoint before selected VMCTL state changes. The memory-request queue retains its genuinely consumed traversal/filtering. |
| `minix/servers/vm/vm.h:7-9` and `sanitycheck.h:8-54` | The fixed build sets `SANITYCHECKS=0`; whole-table/page/allocator sanity machinery is separate and disabled, not an ordinary required precondition. |

The baseline is selected identity, process/region/request state and actual
consumed resource work. It does not imply constant time for allocator,
region/tree, queue or teardown operations. Endpoint-slot admission is
constant; region lookup follows its AVL structure; release traverses the
selected process's resources. Auxiliary state is local to that operation,
not a repeated copy or certification of the imported transport object.

No MINIX source is copied. Fixed arrays, exact generations, direct grants,
one protected kernel commit, the VM scratch exception and token-bound
freeze are existing `micros` adaptations, not MINIX ABI emulation.

## Current ordinary service trace

`vm_core_validate_state` in `servers/vm/vm_validation.c` checks the whole
boot digest and inactive tails, current input-plan canonicality, all local
frame states, all mapping records and global counts/uniqueness.
Active phases also recompute deterministic lowest-frame selection.
The existing ADR-0077 chunked/logarithmic digest is still complete.

| Caller in `servers/vm/` | Direct complete calls |
| --- | ---: |
| `vm_load.c::micros_vm_load_prepare` | 2 |
| `micros_vm_load_bind` | 2 |
| `micros_vm_load_next_page` | 1 |
| `micros_vm_load_scratch_mapped` | 2 |
| `micros_vm_load_fill_scratch` | 2 |
| `micros_vm_load_scratch_committed` | 2 |
| `micros_vm_load_freeze_plan` | 1 |
| `micros_vm_load_frozen` | 2 |
| `micros_vm_load_aborted` | 3 |
| `micros_vm_load_committed` | 2 |
| `vm_exit.c::micros_vm_exit_release_begin` | 2 |
| `micros_vm_exit_release_complete` | 2 |
| `vm_service_core.c::service_instance_is_valid` | 1 public complete wrapper call |
| Ordinary total | 24 |

Retain the public diagnostic wrapper's call and the service initializer's
complete validation. `micros_vm_state_initialize` separately performs
complete boot import before constructing the original bitmap/state.

The service adapter has an important ordering dependency:
`service_release_frozen` calls the kernel backend before invoking local
abort/commit. Its current instance-level complete check supplies the
before-backend safety boundary. A header-only replacement followed by
validation only in the later local transition would be wrong.

Actual frame construction and collision work remain distinct:
`vm_core_select_lowest_frames`, `load_record_identity_is_available`,
`selected_frame_is_available` and exit's matching-record search are not
complete-validator calls to delete. Their record-count/range assumptions
must become explicit when the global precondition goes away.

## Current kernel and transitive trace

The fixed production source has 28 direct calls to
`micros_vm_handoff_state_validate` and five to
`micros_vm_handoff_runtime_validate`.

Nine portable dynamic caller rows are replaced:

| Owner | Caller |
| --- | --- |
| `vm_handoff_load.c` | `micros_vm_load_begin_preflight` |
| `vm_handoff_scratch.c` | `micros_vm_scratch_map_preflight`, `micros_vm_scratch_commit_preflight` |
| `vm_handoff_freeze.c` | `micros_vm_load_freeze_preflight` |
| `vm_handoff_commit.c` | `load_record_transition`, `micros_vm_load_commit_preflight` |
| `vm_handoff_release.c` | `micros_vm_load_abort_preflight`, `micros_vm_load_mark_exit_detached`, `micros_vm_exit_release_preflight` |

The shared transition row owns prepared, unprepared and activated changes;
one call site is not three independent validator invocations per operation.
Three further direct state checks belong to memory's
`require_vm_scratch_state`, `preflight_vm_freeze` and
`preflight_vm_teardown`.

`vm_handoff_syscall.c::require_post_handoff_runtime` adds four direct
complete boundaries: BOOT runtime, VM runtime, PM runtime and VM-root
validation. Together with the service and portable/memory rows, these
form the forty-call replacement ledger.

The sixteen retained kernel-state caller rows are thirteen PM-owned rows,
the cold summary-match check, complete VM runtime validation and complete
scratch-authority acquisition. The four retained VM-runtime rows are cold
BOOT preparation, TTY mapping, PM runtime and VM-fault diagnostics.
Tests/terminal acceptance consumers also remain.

### Hidden memory reentry

The three visible memory-state calls are not the complete closure.
`validate_all_process_roots_locked` visits each root through
`validate_locked`; its `load_validation_context` obtains full VM scratch
authority, reentering the whole kernel VM validator.

The five affected VM-plan walk sites are:

- scratch mapping and removal's two all-root validations;
- freeze's all-root validation;
- teardown's all-root validation; and
- freeze installation's final single-root validation.

Removing their walks belongs to later work. VM01 instead needs the same
walks with operation-scoped committed exception descriptions after real
VM authority. Complete diagnostic APIs keep the full getters.
MEM05's bootstrap-only collector is not a handed-off VM substitute.

Freeze installation is a valid transient: owners/root are committed before
kernel VM state becomes FROZEN. Its final memory certificate cannot rerun
PM/VM command admission expecting the already advanced phase.
Scratch bind/owner/PTE/fence/phase ordering likewise remains unchanged.

### Hidden PM reentry

`micros_pm_control_runtime_validate` first validates the current reservation
and scans all managed owners for live processes. Its reservation owner
performs a whole object audit and conditionally reenters complete VM state.
Replacing only the visible VM runtime call would leave those costs.

The current PM relation matrix distinguishes empty/loading, frozen,
prepared, activated-pending and stopped/detached exit states. It already
uses exact selected process/record/frame counts and kinds in addition to
the broad audits. Those real predicates must remain in a narrow VM-command
entry, while full PM APIs and PM commands retain their existing checks.
The activated-pending helper checks the last reservation/load/process
relationship; this outcome must not invent a different activation policy.

## Safety assumptions to make explicit

The current frame translators trust `managed_range_count`, multiplication,
compact offsets and managed-frame bounds established by complete boot
validation. The initial-free bitmap query directly indexes its word.
Selected admission must establish active geometry and frame bounds first.

Both service and kernel physical-membership helpers loop over stored
`entry_count`. Without the old complete validator, a malformed scanned
record could escape its array. Bound consumed headers/counts before these
searches; do not translate invalid metadata into absence or capacity.

Service abort and exit completion resolve frame indices in their store
loops. Their existing complete preflight makes those resolutions safe.
The replacement must prove the whole selected cleanup set before its
first store, not merely validate each next element after prior mutation.

Service input staging, backend result checks, fixed user-object access
plans, owner-specific errors and one-shot publication remain.
The complete diagnostic still owns immutable history, global record/frame
agreement, unvisited storage and lowest-selection recomputation.
No new persistent valid bit or cached admission is necessary.

## Work, storage and evidence implications

Use active range count, selected record/page entries, request/ELF-plan work,
actual allocator/search prefix, owned frames and retained root traversal
as distinct variables. Remove complete VM invocation costs, not the
separate allocator, collision, lookup or root algorithms.
VM02-VM06 retain their finding ownership.

Existing artifacts were inspected read-only:

| ELF-spawn VM image | Existing resident footprint including stack | Limit |
| --- | ---: | ---: |
| Ordinary | 149 pages | 160 |
| Observed | 152 pages | 160 |

The actual service context is 201,632 bytes; the boot object is 364,704
bytes. These are current measurements, not permission to increase them.
New selected predicates must fit the existing state/layout, stack and
image limits; test observations also consume real image space.

The existing generator already declares the VM context's exact image
symbol/address/size. `elf_spawn_read_object` and `read_process` bind that
descriptor to a real service generation, checked RW translation and
bounded copies. Reuse this owned readback for the VM producer rather than
adding a transport framework.

The real VM control path has four message classes; dynamic operation 12 has
seven classes. The existing ELF-spawn guest exercises load, frozen rollback,
activation commit and exit. It can supply separate authenticated kernel
and VM producer evidence from one run. Existing stack/terminal ordering
must remain: authenticate/read required state before UART seizure.

## Classification

| Mechanism | Classification |
| --- | --- |
| Selected caller, transaction and process/record admission | Baseline parity |
| Distinct generation/token/profile/root and direct-grant authority | Required isolation adaptation |
| Fixed plans, complete preflight and non-failing commits | Compatible safety extension |
| Complete cold import and explicit diagnostics | Compatible diagnostic extension |
| Existing allocator/collision/root work | Separately owned VM02-VM04 correction, not hidden constant work |
| Complete VM audit before/after ordinary transitions | B07-F001 divergence requiring this separate implementation |

This is design research only. No replacement code, model, fixture,
observation harness, build scaffold or executable prototype was authored.

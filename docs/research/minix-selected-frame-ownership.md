# MINIX Selected Frame-Ownership Baseline

## Status and source pins

This is the fixed-source MEM03 study for B02-F002 and B02-E002 under
[Accepted ADR-0103](../adr/0103-selected-frame-ownership-validation.md).
It records the design baseline, not implementation evidence.
MEM03 implementation remains pending the separate design merge.

The current source is independently reviewed EV01-C04 implementation #214:

```text
micros commit: 8927f9956eb270962517b19715a12bc93a5fa466
micros tree:   9f7802dfdfc84034b626068b5c160b90a645dad7
MINIX commit:  4db99f4012570a577414fe2a43697b2f239b699e
```

The complete `arch`, `kernel`, `include`, `lib`, `servers` and `user` source
trees are byte-identical to merged MEM02 #212. EV01-C04 changed only evidence
metadata, its tests and documentation.

MEM03's runtime prerequisite remains MEM01 #210. The separate evidence
prerequisite is now complete: design #213 and implementation #214 add
granular current-source bindings without changing any source guard.
The inventory has 4,282 facts in seventeen families; all 328 new bindings
are active. Four earlier claims and thirty earlier retirements remain
usable without author journals.

## Fixed MINIX mechanism

The inspected reference is the existing local checkout at
`/Users/alephling/Repo/minix`. Its revision and inspected VM source
cleanliness were verified before tracing.

| Source | Symbol or representation | Mechanism and scope |
| --- | --- | --- |
| `minix/servers/vm/region.h` | Virtual region, physical block | A region owns an offset-indexed array of physical-region references; a physical block owns its physical address, reference count and referencing-region list |
| `minix/servers/vm/phys_region.h` | Physical region | One selected relation carries its block, parent region, offset and next reference |
| `minix/servers/vm/region.c` | `physblock_get` | Checks the selected page offset, directly indexes the region array and verifies the returned relation's offset |
| `minix/servers/vm/region.c` | `physblock_set` | Mutates one selected relation and the owning process's memory totals |
| `minix/servers/vm/pb.c` | `pb_link`, `pb_reference` | Installs one selected physical relation and increments that block's reference count |
| `minix/servers/vm/pb.c` | `pb_unreferenced` | Decrements the selected block, unlinks its exact relation, invokes the memory-type release at zero references, and clears the selected mapping relation |
| `minix/servers/vm/utility.c` | `vm_isokendpt` | Checks one decoded slot, exact endpoint generation and live-process flag |
| `minix/servers/vm/main.c` | VM dispatch | Resolves the exact caller and checks its operation authority before dispatch |
| `minix/servers/vm/vm.h` | `SANITYCHECKS` | Disabled in the fixed configuration |
| `minix/servers/vm/sanitycheck.h` | `SANITYCHECK`, `PT_SANE` | Separate diagnostic machinery; the whole-state macro additionally has an explicit false condition even in its enabled branch |
| `minix/servers/vm/region.c` | `map_sanitycheck` | Independent traversal of process, region and physical-reference relationships |
| `minix/servers/vm/alloc.c` | `mem_sanitycheck` | Separate physical-memory diagnostic work |

This does not make every MINIX memory operation constant-time.
After a region is selected, page-offset access and selected total/reference
updates are constant work. Unlinking a non-head physical reference walks
that block's list and costs `O(r)` for its `r` references. Region selection,
allocation, page-table work and memory-type callbacks have separate costs.
No global physical-ownership or complete process/scheduler audit is inserted
into each selected metadata access.

MINIX has no equivalent dense kernel table carrying a generation-bound
semantic owner for every managed frame. Its VM owns allocation and region
policy; privileged mapping mechanisms remain in the kernel.
The `micros` mirror therefore remains an explicit isolation/safety adaptation,
not a structure to claim was copied from MINIX.

## Current `micros` representation and costs

The current mechanism retains:

- the single availability bitmap and MEM01 lower-bound allocation cursor;
- immutable compact managed-frame indices over sparse FDT geometry;
- one eight-byte exact typed owner and one handoff-target byte per frame;
- exact owned/per-kind counts and allocator free count;
- immutable range snapshots bound to one allocator pointer;
- separate `BOOTSTRAP` and `HANDED_OFF` phases;
- exact process slot/generation identities;
- bounded VM claim, freeze and release plans; and
- one-hart external serialization with SIE clear in target mutation paths.

Let `p` be active managed frames, `P = 262144` the frame capacity, `g` active
managed ranges, `G = 96` the range capacity, `k = 8` the fixed semantic owner
classes, and `q` a selected transition's frame count. `Vobj` denotes the
complete kernel-object/scheduler validator, not a constant-time handle
resolution.

| Current operation | Work and retained source |
| --- | --- |
| Physical-address/index translation | `O(g)` through immutable range snapshots |
| Ordinary owner lookup | Complete active/inactive geometry checks through `G`, then selected address/owner lookup: `O(G + g)` |
| Typed bootstrap allocation/release | MEM01 availability search plus complete geometry through `G`; these wrappers do not themselves invoke the complete owner-table audit |
| Complete portable ownership validation | Active owners/targets, complete inactive tail, bitmap words, cursor prefix and counts: `O(P + p + G)` |
| Complete target ownership validation | Portable complete audit plus `Vobj` and active process-bound owner resolution |
| Count one process's owners | Active `p` scan after geometry validation |
| VM point/set preflight | Complete portable audit before selected frame checks; distinctness scans have their separate existing costs |
| Wired-set preparation | Complete audit, a fixed-capacity selection-bitmap walk and an active commit walk |
| Handoff completion | Complete audit, an active transition/count preflight and an active non-failing commit |

The sparse compact-index lookup is the existing ADR-0017/0020 adaptation.
Its `g` work must remain visible; removing inactive `G-g` checks does not
make it constant-time. Likewise, removing a frame audit does not remove a
caller's independently invoked object validator, private-root walk,
process-owner enumeration or selected-set algorithm.

The complete validator already has ADR-0076's word-aware active scan and
direct inactive owner/target scan. MEM01 separately added a cursor-prefix
check. Those changes reduced primitive cost, not the ordinary invocation
scope which MEM03 owns.

## Complete-audit call census

The current production-source census contains 26 direct call sites in
eighteen C owners. This count excludes the two public validator definitions
themselves, and includes the two compile-guarded allocator self-test calls.
Other explicit native and `kernel/tests` diagnostics are outside this
production-source census and remain evidence, not removal candidates.

| Owner | Enclosing function | Calls |
| --- | --- | ---: |
| `kernel/memory/address_space_validation.c` | `validation_authority_is_valid` | 1 |
| `kernel/memory/bootstrap_memory.c` | `micros_frame_ownership_runtime_initialize` | 1 |
| Same owner | `micros_frame_ownership_runtime_complete_handoff` | 1 |
| Same owner | `micros_frame_ownership_runtime_validate` | 1 |
| Same owner | `micros_frame_ownership_runtime_release_process` | 1 |
| Same owner | `micros_bootstrap_memory_run_self_test`, test-guarded | 2 |
| `kernel/memory/frame_ownership_bootstrap.c` | `micros_frame_ownership_release_process_set` | 1 |
| `kernel/memory/frame_ownership_handoff.c` | `micros_frame_ownership_prepare_wired_process_user_set` | 1 |
| Same owner | `micros_frame_ownership_complete_handoff` | 1 |
| `kernel/memory/frame_ownership_validation.c` | `micros_frame_ownership_vm_transition_preflight` | 1 |
| `kernel/memory/user_address_space.c` | `micros_user_address_space_create` | 1 |
| `kernel/memory/user_address_space_validation.c` | `load_validation_context` | 1 |
| `kernel/memory/user_address_space_handoff.c` | `micros_user_address_space_prepare_wired_handoff` | 1 |
| Same owner | `micros_user_address_space_complete_wired_handoff` | 1 |
| `kernel/memory/user_address_space_vm_scratch.c` | `micros_user_address_space_prepare_vm_scratch_mapping` | 1 |
| `kernel/memory/user_address_space_vm_freeze.c` | `validate_vm_freeze_address_spaces` | 1 |
| `kernel/boot/boot_foundation.c` | `boot_initialize_memory` | 1 |
| `kernel/boot/bootstrap_preparation.c` | `validate_prepared_services` | 1 |
| `kernel/boot/bootstrap_preparation_rollback.c` | `bootstrap_preparation_rollback_preparation` | 1 |
| `kernel/boot/bootstrap_runtime_validation.c` | `micros_bootstrap_runtime_validate` | 1 |
| `kernel/boot/bootstrap_syscall_release.c` | `validate_console_begin_runtime` | 1 |
| `kernel/vm/vm_bootstrap.c` | `micros_vm_boot_build` | 1 |
| Same owner | `micros_vm_boot_validate_authority` | 1 |
| `kernel/vm/vm_handoff_runtime.c` | `micros_vm_handoff_runtime_validate` | 1 |
| `kernel/pm/pm_control_runtime_core.c` | `micros_pm_control_runtime_validate_reservation` | 1 |

The census must not be handled by replacing every function named `validate`.
Initialization, final kernel-map proof, explicit whole-ledger diagnostics,
one-time prepared-service publication, rollback-baseline verification and
test-only evidence have different roles from ordinary selected operations.
The design must enumerate exact retained and migrated boundaries.

`validate_prepared_services` is called once after the complete preparation
batch, not once for each service. Its role differs from
`micros_bootstrap_runtime_validate`, which composes ordinary runtime checks.
Rollback's final resource proof is an error-boundary diagnostic.

## Consumer assumptions that cannot be dropped

### Selected VM transitions

The existing claim and frame-set helpers resolve physical addresses and
compare expected owner classes/generations, but rely on the preceding
complete audit for bitmap/owner agreement, valid handoff state and several
scalar/count invariants. Removing that call without replacing those selected
checks would let corrupted input reach a non-failing commit.

Every selected allocator word, owner and target must therefore be checked
before its plan is published. Existing exact process handles, distinctness
checks, arithmetic bounds and non-failing commit order remain obligations.

### Bootstrap defensive rollback

Typed allocation deliberately has a transient interval after low-level
allocation where the bitmap is set and the old owner is still `FREE`.
The free count and cursor have advanced but owner totals have not.
A validator for stable owner/bit/count agreement cannot be invoked blindly
inside that interval.

The current defensive mismatch path releases the selected bit and restores
the exact saved cursor. Active geometry must be established before the
low-level mutation so rollback resolves the same physical frame. Existing
MEM01 byte-exact rollback evidence remains required.

### Whole-active handoff

`micros_frame_ownership_complete_handoff` recomputes and publishes per-kind
counts from active records. Merely checking aggregate totals before that
pass could silently repair inconsistent old per-kind counts.

A correction must independently prove all consumed active owner counts,
bits, targets, phase restrictions and cursor requirements before the first
mutation. This is a whole-active-set operation, not a one-record lookup.
Excluding unused capacity is different from omitting active state.

Wired-set preparation must still prove that its selected bitmap is exactly
the required process-user set. Its existing full-capacity bitmap walk and
per-process/root orchestration belong to MEM05, not an implicit MEM03
algorithm rewrite.

### VM boot snapshots

`authoritative_frame_state` classifies every active frame but does not itself
check every owner identity/reserved field; its callers currently rely on the
complete ownership preflight. Both snapshot build and authority validation
consume the whole active ledger.

A scalar-only replacement is insufficient for those two call sites.
Active record/count/geometry authority must remain complete, while the
pointer-free output object's own capacity, zero-tail, mapping and digest
checks remain unchanged. VM's separate whole-database cost findings stay
with their existing correction owners.

### Selected process liveness

`micros_process_resolve` already checks one registry initialization boundary,
bounded slot, live state and exact generation without calling the complete
registry validator. The portable ledger itself does not own process
liveness. Its target consumers must retain exact live-process resolution
where that relation supplies authority.

Moving a complete object audit out of a frame-runtime wrapper is not
permission to remove another subsystem's separately owned object checks.

### Complete corruption diagnostics

The original complete validator remains the independent check for every
active and inactive owner/target, every allocator bit, both geometry tails,
all counts and the cursor prefix. The target diagnostic additionally checks
all process-bound owner generations.

Existing geometry tests already distinguish active mutation preflight from
inactive diagnostic corruption: inactive range and bitmap corruption are
asserted through the complete validator. Every original complete-diagnostic
case must remain, with no sampling, repair, reduced capacity or cached
success.

## Correction ownership

| Difference or obligation | Classification and owner |
| --- | --- |
| Selected metadata indexing, exact caller identity and local mutation order | Baseline parity to preserve |
| Sparse FDT geometry and compact frame indices | Existing required adaptation under ADR-0017/0020; expose its active-range cost |
| Exact generation-bound typed ownership, selected bit/target agreement and failure-atomic plans | Compatible safety extension when its cost stays within the selected operation's envelope |
| Whole-capacity owner/range tails and complete object audit on ordinary ownership paths | Divergence requiring correction, MEM03 / B02-F002 |
| Explicit complete corruption diagnosis | Compatible diagnostic extension, with its full cost reported separately |
| Complete private-root validation before selected lookup/activation | Remaining divergence, MEM04 / B02-F005 |
| Fixed-capacity release sets and repeated per-process/handoff scans | Remaining divergences, MEM05 / B02-F006/F007 |
| All-process VM mapping checks and pairwise selected-frame searches | Remaining divergences, VM02 / B02-F008/F009 |
| Other explicit scheduler, endpoint, grant, bootstrap, PM and VM validators | Retain their separate finding owners; MEM03 cannot declare their costs solved |

## Evidence constraints for the pending design

B02-E002 requires real primitive observations which distinguish active
frames, inactive frames, active ranges, inactive ranges, bitmap words and
object/scheduler work. The existing collector has one active capture and a
fixed 27-owner by fourteen-kind ABI; it does not carry an extra active/inactive
dimension.

The design must specify how those distinctions are measured without changing
that ABI, assigning memory work to a false owner, double-adding inclusive
captures or estimating callee work from caller counts.

Required semantic guards include exact selected corruption rejection,
unchanged state/output on failure, legitimate allocation rollback,
generation-safe process release, wired/transferable transitions and unchanged
complete diagnostics. Ordinary successful operations and explicitly requested
audits need separately stated scopes; a missing requested mapping must not
hide corruption inside the still-required selected process-root proof.

The separately merged EV01-C04 keys now permit granular admission planning.
They do not authorize a runtime change. The pending design must name every
affected source identity and precise current helper/observation adaptation,
retain historical native modes and original MEM01/MEM02 work bounds, and
prove that candidate payloads remain disjoint from independent inspectors.

No runtime, executable test, model, observation, generator, build scaffold or
prototype has been authored for MEM03.

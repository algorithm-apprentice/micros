# Selected Frame Ownership and Explicit Complete Audits

## Status and outcome

This is the Accepted MEM03 contract under
[ADR-0103](../adr/0103-selected-frame-ownership-validation.md).
The [fixed-source study](../research/minix-selected-frame-ownership.md)
records the MINIX baseline, complete caller census and consumer assumptions.
No implementation is authorized until independent design review, Acceptance
and separate merge.

The fixed source is merged EV01-C04 #214:

```text
commit: 8927f9956eb270962517b19715a12bc93a5fa466
tree:   9f7802dfdfc84034b626068b5c160b90a645dad7
MINIX:  4db99f4012570a577414fe2a43697b2f239b699e
```

Runtime source is unchanged from MEM02 #212. The actual runtime prerequisite
is MEM01 #210; separately merged #213/#214 supply the precise evidence keys.
Only MEM03 design is active. Implementation remains pending.

The outcome closes only B02-F002/B02-E002: ordinary ownership operations
check active metadata and the records they consume, rather than auditing
the whole configured ledger and object registry. Complete diagnostics remain
complete, separately callable and independently tested.

## Preserved authority and non-goals

Preserve:

- one allocator bitmap as availability authority;
- one dense typed ledger as semantic ownership authority;
- immutable compact managed geometry and exact allocator identity;
- the MEM01 cursor, lowest-address allocation and exact rollback;
- every owner encoding, generation, phase, capacity and public structure;
- the existing bounded VM and release plans;
- exact live-process resolution in the owning target operation;
- external one-hart serialization and SIE-clear mutation;
- success-only output/plan publication and non-failing commits;
- complete portable and target corruption diagnostics; and
- all original native/model/target/stack acceptance.

No persistent validation cache, epoch, new mutable global, second owner
table, dynamic allocation, general transaction framework, VM policy,
mapping index, page-table policy, capacity or budget change is introduced.

MEM04 retains complete private-root walks on ordinary translation/activation.
MEM05 retains process-owner scans, fixed-capacity selection-bitmap walks and
repeated root/handoff orchestration. VM02 retains all-process mapping and
pairwise frame uniqueness work. Other explicit object, scheduler, endpoint,
grant, bootstrap, PM and VM validators keep their existing owners.
Removing an inherited frame audit does not certify those costs.

## Three validation scopes

Let `p` be active managed frames, `P = 262144`, `g` active managed ranges,
`G = 96`, and `k = 8` fixed semantic owner classes.

### Active metadata

Add the portable read-only entry:

```c
enum micros_frame_ownership_error
micros_frame_ownership_validate_metadata(
    const struct micros_frame_ownership *ownership
);
```

It proves:

1. the original initialization and allocator-pointer identity;
2. an initialized allocator and a recognized ownership phase;
3. nonzero bounded equal allocator/ledger managed counts and range counts;
4. bounded free/owned counts and cursor, with `free + owned == managed`;
5. zero `FREE` owner count, bounded per-kind counts and an overflow-safe
   exact sum of the eight counts equal to owned;
6. no forbidden process-user/temporary count in `HANDED_OFF`; and
7. every active snapshot/current range pair has identical values, aligned
   nonzero extents, checked arithmetic, sorted nonoverlapping physical
   ranges, contiguous compact offsets and exact total managed coverage.

Validate scalar bounds before indexing either object. Use subtraction-based
count/extent checks where an unchecked sum could overflow.

One new active-geometry helper in `frame_ownership.c` may establish both
descriptors' geometry in one simultaneous active-range pass. It must not
inspect an unused descriptor or scan the bitmap/cursor prefix.
Keep the original complete geometry helpers unchanged for diagnostics.
There is no remembered successful result.

Work is `O(g + k)` and auxiliary storage is fixed scalar state.
The existing physical-address lookup still costs `O(g)`; this additional
active check does not worsen that accepted compact-index envelope.
It is not described as constant work over the range count.

### Selected record

Add one private, read-only cross-owner helper in
`frame_ownership_validation.c`, declared in the existing private header:

```c
enum micros_frame_ownership_error
micros_frame_ownership_selected_record(
    const struct micros_frame_ownership *ownership,
    uint64_t frame_index,
    struct micros_frame_owner *owner
);
```

It reads the exact selected dense index and returns the checked owner
through a success-only output.

Its caller has completed active-metadata preflight and keeps serialization
through the eventual read or commit. The helper still bounds its selected
index before reading an owner, target or bitmap word. It proves:

- exact valid owner kind, reserved byte and identity fields;
- the allocation bit is set exactly when the owner is not `FREE`;
- the current handoff target is valid for that owner;
- a free owner has no target;
- `HANDED_OFF` has no selected target, process-user or temporary owner; and
- a selected free bit cannot lie below the existing lower-bound cursor.

The cursor supplies only a rejection invariant, never availability authority.
The actual bitmap remains decisive.

The helper does not resolve a process through a different object registry,
walk unrelated frame records, inspect an inactive tail, allocate storage,
repair a record or retain authority after its operation ends.

Keep its contract private: an index-only record check is not independent
physical-address or live-process authority. Existing operation plans retain
the selected indices, exact expected owners and validated count deltas.
No new public plan layout or persistent proof token is needed.

### Whole active set

Add the portable read-only entry:

```c
enum micros_frame_ownership_error
micros_frame_ownership_validate_active(
    const struct micros_frame_ownership *ownership
);
```

This is only for an operation whose input domain is the whole active ledger.
It performs active-metadata preflight, the original cursor-prefix proof and
the original word-aware active-frame scan, then independently compares every
observed total/per-kind count with metadata.

Reuse `validate_active_frames` and `allocated_prefix_is_valid` without
changing their bodies. Do not call `inactive_state_is_zero`, the complete
geometry helper or the full validator from this entry.

Work is `O(p + g + k)` plus at most the active cursor-prefix bitmap words.
The existing eight-count witness and scalar totals are fixed automatic
storage. No capacity-sized automatic object or new BSS appears.

### Complete diagnostics

Keep these existing entries complete:

```text
micros_frame_ownership_validate
micros_frame_ownership_runtime_validate
```

Keep their bodies, active/inactive/prefix helper bodies and complete geometry
predicates unchanged. The target diagnostic retains complete object and
process-generation validation. Every active and inactive owner/target,
geometry tail, allocator bit, count and cursor invariant remains checked.

No diagnostic trusts a previous fast-path result. No corruption is repaired.
Its `O(P + p + G + Vobj)` target cost is explicit and is measured separately.

## Target metadata authority

Add:

```c
enum micros_frame_ownership_error
micros_frame_ownership_runtime_validate_metadata(
    const struct micros_kernel_objects *objects
);
```

It requires the original ready runtime and SIE-clear condition, validates
portable active metadata, and checks that the supplied non-null object
registry is the canonical ready registry. It does not invoke
`micros_kernel_objects_validate` or scan all frame owners.

The operation consuming a process-bound frame still resolves the exact live
process generation. Existing `micros_process_resolve` provides a direct
checked slot/generation/live-state lookup; it is not replaced by a raw array
pointer or a count.

For target handoff completion, a separate read-only active-owner liveness
pass resolves every process-bound active record through that canonical
registry. The subsequent portable whole-active preflight proves complete
record/count/target validity before mutation. The complete diagnostic's
existing `validate_process_owners` remains unchanged and is not weakened
with a mode parameter.

## Operation preflight and publication

### Bootstrap allocation and exact release

Keep existing argument, initialization, phase and requested-owner ordering.
Replace ordinary complete geometry checks with active metadata.

Allocation still calls the real low-level allocator and checks its selected
frame before installing an owner. Its transient bit-set/owner-free interval
is deliberate. Do not invoke the stable selected-record helper during that
interval. Preserve exact owner/target checks, count headroom, physical-index
agreement, defensive low-level release and restoration of the saved cursor.
Publish the owner/counts and caller address only on success.

Release proves the stable selected record and exact expected owner before
the real low-level release. Preserve cursor maintenance, zero owner/target
publication and exact count decrements. No fallible check follows successful
authoritative release.

### Lookup and process-owner counting

Lookup performs active metadata, the existing physical-to-index resolution
and selected record validation before publishing the owner.
It remains read-only in both valid phases, including lookup of `FREE`.

Process counting retains its active-frame enumeration and original owner
encoding checks. Before counting a record naming the selected process,
verify the selected bit/target/phase relation. Preserve its exact handle
matching and success-only count output. This remains `O(p + g)` work;
MEM05 owns replacing that enumeration.

Target process release still resolves the exact process, requires its owner
count to be zero and invokes the real object release. Only the inherited
complete frame/object audit becomes target metadata validation.

### Plan staging and selected release sets

Single-frame handoff planning proves the selected stable record and exact
current owner before changing its one target. A malformed existing record
or target is rejected, not silently repaired by replacing the plan.

Selected process release retains the existing bitmap scan, summary and
commit algorithm. Replace its complete preflight with metadata and validate
every selected row before accumulating the exact release deltas.
Inactive selected indices still return the existing unmanaged error.
Keep the original count-underflow/capacity checks and non-failing commit.
This does not claim to solve the retained `P`-bit selection scan.

### VM claim, freeze and release

The existing handed-off transition preflight becomes metadata validation
after the same phase check.

Claim, transferable rollback, freeze and process release must validate each
selected stable owner/bit/target relation before comparing its required
class/generation and publishing any existing plan. Retain all argument,
alignment, physical-range, distinctness, count and capacity checks.

Do not change pairwise distinctness algorithms, plan layouts, frame ordering,
mapping tokens or commit order. All plans remain operation-local and are
consumed only while the same serialization holds.

### Whole-active handoff and VM snapshots

Use `validate_active`, not scalar metadata alone, in:

- `micros_frame_ownership_prepare_wired_process_user_set`;
- `micros_frame_ownership_complete_handoff`;
- `micros_vm_boot_build`; and
- `micros_vm_boot_validate_authority`.

These operations consume the whole active ledger. Independent old count
agreement is required before handoff recomputes/publishes counts, preventing
silent repair. VM snapshot classification must not lose owner identity or
reserved-field validation that its existing classifier assumes.

Retain the existing selection, output zeroing, mapping inventory, digest,
target-plan, count construction and commit algorithms. The snapshot object's
own complete inactive-tail rules remain unchanged.

## Exact complete-audit boundaries

Of the 26 direct call sites in the source study, retain exactly these eight:

| Source/function | Reason |
| --- | --- |
| `bootstrap_memory.c:micros_frame_ownership_runtime_initialize` | One-shot initialized-ledger diagnostic |
| `address_space_validation.c:validation_authority_is_valid` | Final one-shot kernel-map proof; MEM02 still requires its one complete owner audit |
| `boot_foundation.c:boot_initialize_memory` | Ownership-ready publication diagnostic |
| `bootstrap_preparation.c:validate_prepared_services` | One final audit after the complete hidden-service batch |
| `bootstrap_preparation_rollback.c:bootstrap_preparation_rollback_preparation` | Final rollback-baseline diagnostic |
| `bootstrap_memory.c:micros_frame_ownership_runtime_validate` | Complete target diagnostic's portable audit |
| Both calls in guarded `micros_bootstrap_memory_run_self_test` | Existing isolated diagnostic evidence |

The other eighteen call sites migrate according to the operation scopes
above. Explicit native and `kernel/tests` complete audits remain unchanged.

This boundary is not a debug-build switch. Ordinary operations always enforce
their selected invariants; explicit diagnostics always enforce the complete
ledger. One-time boot publication and rollback checks are named audit
boundaries, not an implicit full scan on each lookup or mutation.

User-root validation continues to validate the entire selected process root,
including orphan/duplicate ownership, all private PTEs and shared kernel
entries. Only unrelated global ledger capacity is removed from its inherited
preflight. A missing requested leaf still cannot hide corruption elsewhere
inside that selected root.

## Failure scope and serialization

Valid-input results, public error enums, exact generation rules and output
preservation stay unchanged.

Common metadata corruption is an invariant failure before dependent indexing
or mutation. A malformed selected record is also an invariant failure before
expected-owner or transition authority can be published. Wrong but valid
owners, stale handles, phase denial, absent mappings and invalid caller
arguments retain their existing operation-specific classifications.

The intentional corrupt-state changes are explicit:

- an ordinary operation need not diagnose an unselected active frame or an
  unrelated inactive owner, target, range or bitmap tail;
- it may not use that omission to ignore corrupted metadata or a record it
  actually consumes;
- lookup and single-frame operations gain explicit selected bit/target/phase
  agreement where they formerly relied on an outer full audit; and
- cheap malformed scalar/count state may now produce `INVARIANT` before a
  later exhaustion or wrong-owner result that depended on already-corrupt
  input.

  For the new portable validation entries, null ledger is `ARGUMENT`,
  unrecognized initialization magic is `NOT_INITIALIZED`, and malformed
  remaining metadata or phase is `INVARIANT`. The selected-record helper also
  rejects a null output as `ARGUMENT` and an out-of-managed index as `UNMANAGED`
  before any selected array access. Its remaining record failures are
  `INVARIANT`; its output changes only on success.

  The target metadata wrapper preserves this sequence: readiness, SIE,
  portable metadata, then canonical object-registry identity. A null or foreign
  registry is `ARGUMENT`; it cannot substitute equal contents for canonical
  pointer authority.

  Complete diagnostics still reject all original corrupt states. Selected
operations do not promise to repair them or make a damaged system globally
valid.

Keep SIE clear and the one-hart external serialization from metadata/record
preflight through every retained plan, mutation or publication. This is not
an SMP protocol and introduces no retained epoch or pin.

## Production and evidence ownership

Production changes are confined to:

```text
include/micros/frame_ownership.h
include/micros/frame_ownership_runtime.h
kernel/memory/frame_ownership_internal.h
kernel/memory/frame_ownership.c
kernel/memory/frame_ownership_validation.c
kernel/memory/frame_ownership_bootstrap.c
kernel/memory/frame_ownership_handoff.c
kernel/memory/frame_ownership_vm_claim.c
kernel/memory/frame_ownership_vm_release.c
kernel/memory/bootstrap_memory.c
kernel/memory/user_address_space.c
kernel/memory/user_address_space_validation.c
kernel/memory/user_address_space_handoff.c
kernel/memory/user_address_space_vm_scratch.c
kernel/memory/user_address_space_vm_freeze.c
kernel/boot/bootstrap_runtime_validation.c
kernel/boot/bootstrap_syscall_release.c
kernel/vm/vm_bootstrap.c
kernel/vm/vm_handoff_runtime.c
kernel/pm/pm_control_runtime_core.c
```

Keep core geometry/encoding below validation, and validation below mutation
owners. The private header contains declarations only. The existing
`frame_ownership_vm_freeze.c` can consume corrected shared preflight without
a source change. No new runtime source owner or mutable global is required.

The new closed evidence owners are:

```text
tests/host/selected_ownership_test.c
tests/host/selected_ownership_test.h
tests/host/selected_ownership_source.py
tests/host/selected_ownership_evidence.py
tests/host/selected_ownership_authority.py
tests/host/test_selected_ownership.py
tools/generate_selected_ownership_fixture.py
tools/check_selected_ownership.py
```

The native fixture uses `frame_allocator.c` and the real
`MICROS_FRAME_OWNERSHIP_SOURCES`, `MICROS_KERNEL_OBJECT_CORE_SOURCES`,
`MICROS_SCHEDULER_CORE_SOURCES`, `MICROS_ENDPOINT_CORE_SOURCES` and
`MICROS_IPC_CORE_SOURCES` groups. It additionally compiles the complete
`pm_control_core.c`, `pm_control_validation.c`,
`pm_control_runtime_core.c` and `vm_handoff_core.c` owners.
These satisfy the real selected PM entry's portable dependency closure;
no endpoint, IPC, object or ownership algorithm is replaced by a test double.

For bootstrap runtime extraction, admit only explicit native readiness/SIE
inputs, the sole fixture-owned allocator/ledger and one canonical getter for
the real portable object registry. Pointer identity and selected object
resolution remain production checks. Host evidence does not claim CSR,
firmware, `satp` or real boot initialization behavior.

## Actual primitive evidence

The unchanged EV02 collector has one active capture and a 27-by-fourteen
matrix. Use five independently compiled observation slices:

```text
aggregate
active-frame
inactive-frame
active-geometry
inactive-geometry
```

Each slice compiles the same actual source-stage bodies and fixture inputs;
only its independently declared passive observation subset differs.
The aggregate includes all declared work. The four component slices measure
their own actual record primitives and are never added to the inclusive
aggregate.

This introduces no runtime observation switch, false owner assignment,
nested capture, new collector dimension or inferred callee cost.
Object/scheduler work is recorded at its actual unchanged primitive sites.
Original active/inactive loops and every new helper's executed primitive
closure must be declared before production changes.

Within memory work, count each actual owner or target array read/store as a
record visit, each selected range descriptor as a range visit and each
bitmap word read or read-modify-write as a word load. Component slices
partition those declared sites by active/inactive frame or geometry domain.
The memory complete-validator dimension counts the two existing complete
portable/target diagnostic entries, not the new active-domain operation
preflight. Other owners' complete entries retain their actual primitive
annotations.

Use producer base `0x4d454d3033000000`, add channel IDs 1 through 5 in the
order above, and add `0x100` for current rather than baseline source.
Case IDs 1 through 99 are strictly increasing capture labels within each
execution. Keep complete 27-by-fourteen snapshots and exact declared owner
masks. The ordinary executable emits no work snapshot.

A sixth ordinary executable compiles the same source-stage bodies without
observation statements or collector linkage. Complete semantic/model
assertions run in every executable. Compare their canonical case/result/
logical-output transcripts, not ASLR-dependent native pointer bytes.
Within each process, failures preserve byte-exact full snapshots and valid
mutations match independently applied expected state updates.

Add one `selected-ownership` CTest to both native tiers, orchestrating the
six declared executables. They are six build products, not six duplicate
behavioral CTests. The complete native count therefore advances from 42 to
43; no target workflow is added.

Keep explicit fixed/current source/header dependencies, ordinary reusable
profile eligibility and existing CMake parser policy. Generated translations
and executables are outputs, never source authority.

### Fixed valid cases

Use physical base `0xc0000000` with no reservations for dense cases:

```text
managed frames: 1, 63, 64, 65, 257, 4096, 262143, 262144
```

Add one 96-range case using sixteen reversed eleven-page memory intervals,
twelve-page stride and reservations at page offsets 1, 3, 5, 7 and 9 in
each interval. The real allocator derives 96 one-frame components.

For each of those nine geometries, run nine independently reset cases:

1. one bootstrap temporary allocation;
2. free-owner lookup at the final managed frame;
3. exact release of one allocated temporary frame;
4. one process-user wired-target preparation;
5. handed-off claim of the final free frame;
6. target release of one rootless, frameless live process;
7. the real idle PM reservation-validation entry;
8. explicit complete portable audit; and
9. explicit complete target audit.

The PM case uses actual process slot 2, its real initial thread, service ID 3,
the exact PM profile and endpoint derived from that process generation.
Prepare state through the existing real API; use no active reservation and
`vm == NULL`. Its independently owned explicit object validation remains.

Add twelve VM set cases: for managed counts 65 and 262144 and data counts
1 and 16, execute freeze, transferable release and process release with the
existing four-table requirement for freeze/process release. Data indices
are `0..q-1`; table indices are `q..q+3`. Transferable rollback releases only
its `q` data frames. Setup uses real claim/freeze operations outside capture.

The two process-release bitmap cases select indices `{0,64}` at count 65
and `{0,63,64}` at count 262144. Allocate only the first 65 frames in setup;
selected frames have the exact process-user owner and intervening frames
are kernel-retained. Do not allocate the entire maximum-capacity prefix
merely to reach a high selected index.

Use those same two geometries and owner sets for wired-stage and complete
handoff, giving four cases. Index 1 is additionally planned kernel-retained
to transferable. Wired-stage begins with unplanned user frames; complete
handoff begins after actual wired staging.

The order is nine operations for each dense geometry, then the sparse
geometry; VM cases by managed count, data count and the three listed
operations; the two bitmap cases; then stage/complete for each handoff
geometry. This gives 99 valid cases and 495 observation snapshots per source
stage, plus 99 ordinary semantic outcomes.

A one-frame point-operation fixture is valid because it builds no Sv39
tree. It is not used for a multi-frame freeze. Every setup uses actual
initialization/mutation APIs outside the measured operation and proves the
complete initial invariant.

### Required measured bounds

Let `w = ceil(p/64)` and `n = q+4` for freeze/process release or `n = q`
for transferable release. The following are independently fixed upper bounds,
not estimates copied from a corrected execution. A component's other
unlisted work is still retained in the aggregate report.

| Case category | Current memory primitive envelope |
| --- | --- |
| Allocate, free lookup, exact release, one-frame plan, one-frame claim | Active frame visits at most 16; active geometry range visits at most `8g+16`; bitmap word loads at most 8 for these declared setups; each aggregate record/index/range dimension at most `64g+256` |
| Selected VM set | Active frame visits at most `16n+32`; bitmap loads at most `8n+16`; active geometry visits at most `8(g+1)(n+1)`; each aggregate record/index/range dimension at most `64(g+1)(n+1)+16n^2+256` |
| Process-release bitmap | Active frame visits at most `16q+32`; active geometry at most `8g+16`; bitmap loads at most `4P+8q+16`, retaining the existing selection walk |
| Empty target process release | Active frame visits at most `4p+32`; active geometry at most `8g+16`; bitmap loads at most 8; aggregate record visits at most `16p+64g+512` |
| Idle PM reservation validation | No active owner/target frame visit or bitmap load; active geometry at most `8g+16`; aggregate memory record/index/range dimension at most `64g+256` |
| Whole-active wired stage/handoff | Active frame visits at most `32p+64`; aggregate memory record visits at most `64p+64g+1024`; stage bitmap loads at most `4P+16w+64`, complete-handoff loads at most `16w+64` |

For every nondiagnostic case require zero inactive frame and inactive
geometry visits. Require zero complete memory-diagnostic calls.
The target process-release case also requires zero complete object/scheduler
audit calls. The idle PM case retains its explicit complete object audit:
its object/scheduler measurements must equal the fixed original execution,
not be silently omitted to obtain memory Green. Its public complete
kernel-object validator entry is observed exactly once in both stages.

For ordinary point operations also require:

- no complete frame audit;
- no inactive frame or inactive geometry visits;
- selected owner/target work bounded independently of `P` and `G`;
- active range work `O(g)`, explicitly counted rather than called constant;
- bitmap words for a selected stable record bounded independently of `p`;
  allocation retains its separately measured MEM01 search term; and
- no inherited complete object/scheduler audit in the target frame-runtime
  release path.

Selected `q`-frame VM plans retain `O(qg)` address resolution and their
existing pairwise distinctness term. Do not declare VM02 solved.

Process counting retains its measured active `p` visits. Process bitmap
release retains its `P`-bit scan. Whole-active transitions retain active
`p + g` work, including exact counts and prefix words, but no inactive-ledger
or inactive-geometry audit. All retained costs remain in aggregate reports.

For the declared all-free, cursor-zero diagnostic setup, portable audit
visits exactly `2p` active owner/target records and `2(P-p)` inactive records.
Target audit additionally reads each active owner in its liveness scan,
giving `3p` active frame visits. Both retain `3g` active geometry descriptor
visits and `2(G-g)` inactive descriptor visits. Complete allocator-bitmap
loads are `4096 + (p mod 64 != 0)` in the portable scan.
The target additionally retains real object/scheduler work.

These exact counts refer to the declared valid all-free setup, not an
early-returning corruption case. Diagnostic semantics and source bodies
remain unchanged. They are preservation guards, not qualifying Red failures.

The pre-implementation declaration expands these fixed formulas over the
fixed case matrix and includes every actual selected helper/inherited
primitive site. It may not learn an expectation from corrected candidate
output, omit a new callee or raise a bound after failure.

### Corruption, atomicity and ordinary controls

Cover independent selected owner fields, allocation-bit mismatch, invalid
target, wrong phase, stale/foreign process generation, invalid active
geometry/count/cursor bounds, underflow/overflow deltas, duplicate and
overlapping VM inputs, malformed selection bitmaps and exact exhaustion.
Retain valid empty transferable rollback and empty process-release sets;
zero-length success is not permission to skip their common phase/metadata
preflight. Freeze still requires a nonempty data set.

Prove complete owner/target/bitmap/geometry inactive-tail detection remains
available while an unrelated inactive corruption does not require an
ordinary selected operation to perform that audit. Also distinguish an
unselected active-record corruption from a selected record or corruption
inside the still-completely-validated selected user root.

Preserve actual low-level allocation defensive rollback, exact cursor
restoration, output/plan sentinels, selected owner transitions and full
diagnostic postconditions. Whole-active count corruption must fail rather
than be repaired at handoff.

Require strict source-stage/passive inverses, missing/duplicate/misplaced
observation rejection, complete transcript/snapshot membership, ordinary
observer absence and unchanged original MEM01/MEM02 work/semantic bounds.
A compile failure, missing helper, malformed record or incomplete run is
never qualifying Red.

## Granular source authority

Preserve all 4,282 current complete facts and seventeen fingerprints.
Declare one eight-fact MEM03 family from independent fixed-source,
Accepted-contract, recipe and oracle inputs. Its eventual total is 4,290
facts in eighteen families; this is a requirement, not a current inventory
claim.

Only 38 current transport identities may retire: fourteen still-active
kernel-directory file identities, eleven MEM01 current function clauses,
three MEM01 definition-set clauses and ten MEM02 native-input identities.
Do not retire an original skeleton merely because functions are added.
Do not own any of the four already retired MEM01 memory files again.

The exact key suffixes are:

```text
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/bootstrap_runtime_validation.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/bootstrap_syscall_release.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_handoff.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_internal.h/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_vm_claim.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_vm_release.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/pm_control_runtime_core.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_handoff.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_validation.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_vm_freeze.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space_vm_scratch.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/vm_bootstrap.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/vm_handoff_runtime.c/2
MEM01/definitions/kernel/memory/bootstrap_memory.c
MEM01/definitions/kernel/memory/frame_ownership.c
MEM01/definitions/kernel/memory/frame_ownership_validation.c
MEM01/function/kernel/memory/bootstrap_memory.c/micros_frame_ownership_runtime_complete_handoff
MEM01/function/kernel/memory/bootstrap_memory.c/micros_frame_ownership_runtime_release_process
MEM01/function/kernel/memory/frame_ownership_bootstrap.c/collect_process_release_summary
MEM01/function/kernel/memory/frame_ownership_bootstrap.c/micros_frame_ownership_allocate
MEM01/function/kernel/memory/frame_ownership_bootstrap.c/micros_frame_ownership_release
MEM01/function/kernel/memory/frame_ownership_bootstrap.c/micros_frame_ownership_release_process_set
MEM01/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_count_process
MEM01/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_lookup
MEM01/function/kernel/memory/frame_ownership_validation.c/micros_frame_ownership_vm_transition_preflight
MEM01/function/kernel/memory/frame_ownership_validation.c/resolve_vm_data_frames
MEM01/function/kernel/memory/frame_ownership_validation.c/resolve_vm_table_frames
MEM02/native-input/include/micros/frame_ownership.h
MEM02/native-input/include/micros/frame_ownership_runtime.h
MEM02/native-input/kernel/memory/bootstrap_memory.c
MEM02/native-input/kernel/memory/frame_ownership.c
MEM02/native-input/kernel/memory/frame_ownership_bootstrap.c
MEM02/native-input/kernel/memory/frame_ownership_handoff.c
MEM02/native-input/kernel/memory/frame_ownership_internal.h
MEM02/native-input/kernel/memory/frame_ownership_validation.c
MEM02/native-input/kernel/memory/frame_ownership_vm_claim.c
MEM02/native-input/kernel/memory/frame_ownership_vm_release.c
```

Every `MEM01/` or `MEM02/` suffix is prefixed exactly with:

```text
tests/host/memory_native_source_bindings.py:native-source-admission:
```

The twenty production paths plus these six paths form the complete
26-path candidate envelope:

```text
tests/host/bootstrap_allocation_source.py
tests/host/kernel_layout_source.py
tests/host/kernel_mapping_source.py
tests/host/memory_native_source_bindings.py
tests/host/test_kernel_directory_layout.py
tools/generate_kernel_mapping_fixture.py
```

The literal binding owner remains byte-identical to #214. The five legacy
adapters must be independently specified, installed before actual Red and
frozen by exact finite inverses against #214. Their presence does not relax
an assertion before the MEM03 transition permits it. Only the twenty production
files may change during the actual correction attempt.

All other existing inputs and the eight new evidence owners are immutable
within that attempt. Complete-current record conservation is independent of
any historical view. Shared inspectors, collector policy, transition rules
and CMake/planner inputs remain outside the candidate closure.

The old `BASE_FRAME_VALIDATION_SHA256` is historical restoration evidence,
not another current assertion to retire: its restoration call is already
gated by the completed MEM01 bounded-validator retirement.
A same-named TTY `BASELINE_BODIES/5/micros_bootstrap_runtime_validate`
identity belongs to the unchanged native TTY test double, not the production
bootstrap validator. Neither is included in this transition.

## Existing evidence adaptations

Admit only finite, independently declared changes:

- per-key MEM01 function/definition admission and new actual helper primitive
  sites, preserving old literal declarations and historical native mode;
- per-input MEM02 admission at both generator and source consumers, retaining
  every unaffected byte comparison and actual current dependencies;
- kernel-directory assertion delegation for the fourteen new file keys;
- exact native recipe/source-owner/Python-runtime registration;
- the two new portable public symbols and one target declaration, while
  preserving layouts and original symbol owners; and
- current rejection controls in the C4 authority regression using unchanged
  allocator source/header subjects after its original examples legitimately
  retire. Its complete 328-record fixed-reference proof remains unchanged.

Current code is always compiled as current. A legacy source projection may
prove that an adapter preserves unrelated historical policy; it may not
compile an old algorithm as a replacement for new runtime source.

The bootstrap read-only dependency gate may recognize the new read-only
metadata entry without admitting an upward mutation. Freeze that fixture
change before actual Red. No old hash or family fingerprint is rebaselined.

## Actual Red, Green and recertification

Declare three independent oracles:

| Oracle | Actual discriminator |
| --- | --- |
| `mem03-selected-record-work` | Existing portable lookup and VM point/set entries expose original inactive geometry/frame work and corrected selected checks |
| `mem03-runtime-owner-work` | Actual target-runtime process release, under explicit native environment bindings, exposes original complete ledger and `Vobj` work |
| `mem03-consumer-audit-boundaries` | Actual PM reservation-validation entry exposes inherited frame audit; a closed source/call ledger proves all eighteen consumer migrations and eight retained direct boundaries |

Every planned retirement key must have explicit oracle coverage. Baseline
semantic and explicit-diagnostic guards must pass while each named oracle
fails for its declared original work divergence. Current semantic,
corruption, transcript and work evidence must all pass.

Coverage is exact:

- the selected-record oracle owns the four kernel-directory handoff/private-
  header/VM-claim/VM-release keys, nine MEM01 bootstrap/validation function
  keys, two MEM01 core/validation definition-set keys and eight MEM02 inputs
  other than bootstrap runtime and its public runtime header: 23 keys;
- the runtime-owner oracle owns the two MEM01 runtime functions, bootstrap-
  runtime definition set and the MEM02 bootstrap-runtime/runtime-header
  inputs: five keys; and
- the consumer-boundary oracle owns the ten remaining kernel-directory
  user-root/bootstrap/VM/PM caller identities.

After separate design merge, canonical author intents are:

```text
${MICROS_PYTHON} -E -S -B tools/check_selected_ownership.py --red
${MICROS_PYTHON} -E -S -B tools/check_selected_ownership.py --green
${MICROS_PYTHON} -E -S -B tools/check_selected_ownership.py --recertify
```

These are future implementation commands, not files claimed to exist now.
The driver binds fixed references, immutable input/source/tool membership,
actual executions and per-unit results under ADR-0082. A staged completion
claim cannot suppress an original assertion.

The exact affected-unit commands are:

| Unit | Canonical recertification command |
| --- | --- |
| B01 | `cmake --workflow --preset test-qemu-frame-allocator` |
| B02 | `cmake --workflow --preset test-qemu-frame-ownership` |
| B03 | `cmake --workflow --preset test-qemu-scheduler` |
| B04 | `cmake --workflow --preset test-qemu-ipc` |
| B05 | `cmake --workflow --preset test-qemu-user-runtime` |
| B06 | `cmake --workflow --preset test-qemu-bootstrap-launcher` |
| B07 | `cmake --workflow --preset test-qemu-vm-handoff` |
| B08 | `cmake --workflow --preset test-qemu-pm-service` |
| B09 | `cmake --workflow --preset test-qemu-tty` |
| B10 | `cmake --workflow --preset test-qemu-ramfs` |
| B11 | `cmake --workflow --preset test-qemu-vfs` |
| B12 | `cmake --workflow --preset test-qemu-elf-spawn` |
| B14 | The actual `--recertify` invocation above, including the complete current native oracle replay, retained MEM01/MEM02 evidence, authority/source/recipe and Python-owner checks |

Provisional suppression is limited to the exact 38 keys and current
validated Green context. Clear it on every exit. Failure or interruption
publishes no usable terminal event. Only complete success derives
`RETIREMENT_READY`.

Run the unchanged fifty-command fresh author producer afterward:

```text
${MICROS_PYTHON} -E -S -B tools/validation_plan.py --tier full --execute --producer
```

It includes user-root, wired-handoff, VM-scratch and all other original target
acceptance, 43 native CTests after the one new case, all 44 static
configurations/113 outputs/ten observed scopes and explicit blocked B13.
Preserve the three original reusable-profile eligibility contracts.

Independent implementation review precedes the first permanent commit.
Verify ordinary committed authority without journals and exact
reviewed/committed/remote/preview/squash tree equality, then clean only the
completed task refs.

This design phase contains no executable test, model, observation, generator,
build scaffold, prototype or runtime/header edit.

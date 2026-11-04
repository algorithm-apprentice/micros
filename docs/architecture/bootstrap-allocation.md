# Bootstrap Allocation and Range Validation

## Status and outcome

This is the Accepted MEM01 contract under
[ADR-0100](../adr/0100-minix-bootstrap-allocation.md). Its
[fixed-source study](../research/minix-bootstrap-allocation.md) records the
MINIX phase distinction, current work defect and directly dependent mutation
paths. It closes only B01-F006/B01-E006 and B02-F001/B02-E001 after separate
implementation acceptance.

The restarted design base is EV01-C03 #208, commit
`bcace37522e5beb101ef5278889a2f87742f5721`, tree
`36110108d7839460b2f384761bb9aaa540e82444`. EV04 is the actual runtime
prerequisite. The original design's evidence-binding blocker is closed by
separate #205/#208 under ADR-0098; #206/#207 under ADR-0099 preserve ordinary
current trace acceptance without widening historical baseline authority.
Runtime source and type/header bytes at that design base remain identical
to #204. Design #209 is separately reviewed and merged as
`82f0b229afd2f396b0fdd8fc24b9bec00793fe97`, tree
`76be2a6fb534fab324ef20960314263a7ebaa1bc`.
Only the dependent MEM01 implementation is active. Its complete acceptance
still requires the actual native/target work and authority evidence below.

## Retained authority and non-goals

Keep 4096-byte frames; 16 memory, 80 reservation and 96 managed-range
capacities; 262144 managed-frame capacity; unsigned 64-bit half-open
arithmetic; and the existing error enumeration and public function
signatures.

Keep:

- inward memory and outward reservation alignment;
- sorting, union merging, reservation subtraction and compact bitmap offsets;
- complete inactive metadata clearing and initialization publication last;
- `[0, __kernel_end)` exclusion and reclamation of the consumed FDT blob;
- lowest-address allocation and deterministic lowest released-frame reuse;
- unchanged outputs and allocation/owner state on returning failure;
- immutable typed-ownership geometry, exact generations and original counts;
- SIE-clear target mutation and irreversible ownership handoff;
- the current VM boot-information and IPC ABIs; and
- all original native/model/QEMU behavior, diagnostics and resource limits.

No allocator cache, contiguous-run request, heap, dynamic free list,
mapping/topology correction, table-frame index, complete-validation cadence
change, new target workflow, runtime counter or unrelated source decomposition
is introduced. MEM03 and B07-F003 remain open with their current owners.

## Representation and invariant

Insert exactly one `uint64_t allocation_cursor` immediately before
`allocated_bitmap` in `struct micros_frame_allocator`. Earlier field offsets
and all capacities remain unchanged; the bitmap moves by eight bytes and the
structure grows by one scalar. No public wire object embeds or exports it.

The existing grant-syscall and address-space-handoff fixtures copy and compare
the metadata prefix ending at `offsetof(..., allocated_bitmap)`, then the
active bitmap words. This placement keeps the new search state inside those
unchanged failure-atomicity snapshots. Appending it after the bitmap would
silently omit it. No assembly or external wire format uses a literal
allocator-bitmap offset.

Write `N = managed_frame_count` and `c = allocation_cursor`. Valid search
state requires:

1. `0 <= c <= N`;
2. every active bitmap bit with index less than `c` is set;
3. bits at or above `c` retain their existing free/allocated meaning;
4. no inactive high bit or later bitmap word is set.

The cursor need not itself name a free bit. It is a lower search bound,
not a second free set, physical address, ownership identity or proof that
the next bit is available. The bitmap remains authoritative.

The immutable managed descriptors remain sorted, nonoverlapping physical
intervals with contiguous logical bitmap offsets. Physical gaps do not
become available and cannot be crossed by interpreting one bitmap word as
one physically contiguous allocation.

## State transitions

### Initialization

Retain the complete existing argument/error and canonicalization order.
Set the new cursor to zero only in the non-failing initialized-state commit.
Every constructor failure leaves the entire destination object unchanged,
including a preexisting cursor value. Successful initialization still clears
every unused range entry and every bitmap word before publishing
`initialized = true`.

The typed ledger's pristine-allocator check additionally requires cursor
zero. This is a directly introduced invariant, not another initialization
phase or a mutable geometry snapshot.

### Allocation

Retain null-output/null-allocator/uninitialized rejection first. Reject an
out-of-bound managed-range count, managed-frame count, free count or new
cursor as `INVARIANT` before using those values. A valid zero free count
retains the immediate `EXHAUSTED` result without a bitmap scan. Before any descriptor-derived
bitmap access, validate its required geometry extent.

For a valid nonempty allocator:

1. keep the cursor and all prospective results local;
2. visit managed descriptors in their original ascending order;
3. skip a descriptor whose logical end is at or below the cursor;
4. search from the greater of that descriptor's offset and the cursor;
5. fetch allocation words and mask off positions before the search start
   and beyond this descriptor's end;
6. skip a masked word with no free bit in its admitted interval;
7. choose the lowest free bit of the first word that contains one;
8. derive the physical address from that same descriptor and checked local
   frame index; and
9. commit the actual bit, decrement the free count, set the cursor to the
   selected logical index plus one, then publish the physical address.

Mask construction must not shift by 64. A word shared by two descriptors
cannot publish a frame belonging to the other descriptor. No speculative
load beyond the active last word is allowed.

Within a nonempty selected word, bounded scalar bit selection is permitted.
It does not replace whole-word skipping with one load per preceding frame.
No compiler optimization or optional ISA bit instruction is required to meet
the source-level bound.

All searches and prospective-address checks precede mutation. Exhaustion or
an invariant failure leaves the cursor, bitmap, counts and output unchanged.
There is no silent cursor reset, wraparound fallback or repair.

### Release

Preserve argument/initialization, alignment, managed-range membership and
allocated-bit rejection order. Before commit, validate every cursor/index
bound needed by the new state update and retain coherent count arithmetic.
Validate the managed-range count before iterating its descriptor array.

After the existing release checks succeed, clear the actual bit, increment
the free count and set `c = min(c, released_index)`. No later fallible work
is added. Failed release leaves the complete object unchanged.

### Typed rollback and bulk release

`micros_frame_ownership_allocate` saves the cursor before calling the
portable allocator. A defensive post-allocation mismatch must undo the
allocated bit/count and restore that exact saved cursor before returning
the existing invariant error. Releasing the selected frame alone is not
sufficient: a search may have advanced past previously allocated words.

The existing immutable-geometry check and serialized call guarantee that
the just-allocated frame is releasable in that rollback. An unexpected
rollback failure remains an invariant failure, never a successful release
or fabricated complete rollback.

The process-set release commit must route its selected bit clears through
the existing `micros_frame_ownership_allocator_mark_free` primitive.
That primitive clears the bit and lowers the cursor; it does not change
counts, owner records, handoff targets or error policy.

The same primitive already serves VM release commits. Its cursor update is
privileged mirror maintenance, not VM frame-selection policy. Existing VM
claim/freeze commits only set bits, so they cannot invalidate the lower-bound
invariant and do not acquire a new search operation.

### Complete validation

The geometry helper rejects `c > N` before any dependent indexing.
The existing complete ownership validator additionally proves the allocated
prefix `[0, c)` using complete words plus, when needed, one lower-bit mask.

Put that directly introduced check in a separate file-local helper outside
`validate_active_frames` and `inactive_state_is_zero`. Those two accepted
algorithms, their exact token identities and their complete active/inactive
coverage remain unchanged. The new check is read-only and does not repair
the cursor.

Do not invoke the complete prefix audit on every successful portable or
typed allocation. Correct mutation maintains the invariant; the existing
complete-validator entry remains the diagnostic authority. Its current
invocation cadence is not changed by MEM01.

## Bootstrap range validity

Keep `bootstrap_state_is_valid` as the target's private state validator.
Its read-only checks must establish:

- a non-null decoded-memory-map input before reading any of its counts;
- initialized state and nonzero managed count;
- every memory/reservation/managed count within its original capacity;
- free count at most the managed count and cursor at most the managed count;
- nonempty aligned managed intervals, checked physical ends and contiguous
  logical offsets;
- sorted, nonoverlapping managed geometry and exact final managed count;
- complete interval coverage by the original decoded FDT memory union;
- no intersection with either original FDT reservation source or the
  pre-kernel/kernel exclusion;
- exact bitmap population and free-count agreement;
- no allocated frame when `require_all_free` is true;
- zero inactive range entries, partial-word high bits and later bitmap
  words; and
- the new allocated-prefix cursor invariant.

Bounds are checked before loops or accesses that consume them. Range and
count additions/multiplications are checked rather than relying on wraparound.

The existing complete-interval coverage and exclusion checks remain. Remove
only the later loop that re-runs `managed_frame_is_allowed` for every frame.
The selected-frame helper remains available to the original target self-test;
its checks on the four actually returned test frames are not removed.

The bitmap pass loads each required active or inactive word once. Count active
bits by bounded scalar word arithmetic, not an out-of-line per-frame bit
helper. Explicitly mask the partial active word, reject inactive high bits,
and examine every later word. The counter must record actual loads rather
than labeling a 64-iteration reload loop as one word operation.

## Responsibility and footprint

Only these existing production owners change:

| Path | Sole permitted change |
| --- | --- |
| `include/micros/frame_allocator.h` | Insert the search scalar before the bitmap and document its invariant; preserve all existing field meanings, declarations and constants |
| `kernel/memory/frame_allocator.c` | Initialize, bound, consume and rewind search state; implement admitted word search |
| `kernel/memory/bootstrap_memory.c` | Range/word bootstrap validity and exact test-only work observations |
| `kernel/memory/frame_ownership.c` | Cursor shape/pristine checks and shared free-bit cursor maintenance |
| `kernel/memory/frame_ownership_bootstrap.c` | Exact cursor rollback and shared primitive use in process-set release |
| `kernel/memory/frame_ownership_validation.c` | One separately named complete cursor-prefix check |

No production owner moves and no new production file, global, public
function or cross-module header is added. Private helpers remain file-local.
The existing frame-owner private declaration for the free-bit primitive is
sufficient.

Keep all unrelated functions, signatures, CPP scopes, immutable data and
static object ownership in those sources fixed. Independently freeze that
retained register from #208 before any implementation edit. Actual
diagnostic file operands retain the accepted logical path maps; this
behavioral correction's edited source owns its actual current source lines,
not a synthetic `#line` or copied old source-location result.

The original function bodies permitted to change, apart from precisely
reversible observations, are exactly:

- allocator `commit_initialized_state`, `micros_frame_allocator_allocate`
  and `micros_frame_allocator_release`;
- bootstrap adapter `bootstrap_state_is_valid`;
- owner core `allocator_geometry_is_valid`, `allocator_is_pristine` and
  `micros_frame_ownership_allocator_mark_free`;
- bootstrap ownership `micros_frame_ownership_allocate` and
  `commit_process_release`; and
- complete ownership `micros_frame_ownership_validate`.

New file-local helpers may only expose the admitted word search/count,
bounded bootstrap geometry/bitmap checks and cursor-prefix verification.
Freeze their names, signatures, call sets and primitive sites before
production edits. All other original definitions, including range
normalization/subtraction, selected-frame self-test checks, owner encodings,
handoff and VM transition policies, remain byte/token exact after observation
and include transport.

The selected-frame helper may gain only the existing
`MICROS_BUILD_FRAME_ALLOCATOR_TEST` definition guard when removal of its
ordinary per-frame caller would otherwise leave an unused static function.
Its signature/body and target self-test calls remain unchanged. The native
source fixture binds this exact guard explicitly rather than silently
omitting the selected-frame semantic checks.

## Work and storage bounds

The native oracles use logical counts, not elapsed time. Let
`C = memory_range_capacity + reserved_range_capacity + managed_range_capacity`;
complete inactive-range clearing/checking contributes `O(C)`, not zero work:

| Operation | Required envelope |
| --- | --- |
| Range initialization | Retain `O(m^2 + r^2 + mr + C + P/64)` and its current fixed candidate arrays |
| Whole-interval FDT verification | At most `m + g` union-coverage iterations, each visiting at most `m` raw memory records; at most `g*r` raw reservation visits |
| Bootstrap bitmap/count/cursor validity | `O(C + P/64)` metadata work and bounded scalar work per word |
| One allocation | `O(g + scanned_words + 64)`; no restart over the already certified allocated prefix |
| Monotone first `a` allocations | `O(a*g + a + P/64)`; one-range initially free captures require one search-word load per successful allocation, plus an actual commit read if read-modify-write is retained |
| One release | Existing `O(g)` membership search plus constant bit/count/cursor mutation |
| Typed rollback | Existing release work plus one saved/restored eight-byte cursor |
| Complete owner validity | Retain all original work; add at most `O(P/64)` directly introduced prefix checking |

Observe every declared bitmap input load, including an actual read-modify-
write commit read. The initially free single-range oracle permits at most
`2a` complete-operation bitmap loads and at most `64a` selected-bit probes;
it never silently subtracts commit work. A retained already-loaded word may
satisfy the same bound without another array read.

Arbitrary release/allocation sequences do not receive a constant amortized
claim. A released low frame can require later scanning of a long allocated
suffix; count those actual words. The MINIX VM cache is not present and no
cache-based average claim is borrowed.

The complete typed allocation cost still includes existing geometry checks
over active and inactive managed-range capacity. No retained full ownership,
process, mapping or service validation cost is claimed closed by this task.

Persistent overhead is exactly one scalar per allocator. There is no
depth-sized or frame-sized new automatic object, second bitmap or free-page
vector. Temporary search/count state is fixed scalar storage. Existing
initialization arrays and every current 4 KiB/16 KiB stack limit remain
subject to the full compiler/link/target gate.

## Native and target evidence

The implementation adds exactly these evidence owners:

```text
tests/host/bootstrap_allocation_test.c
tests/host/bootstrap_allocation_test.h
tests/host/bootstrap_allocation_source.py
tests/host/bootstrap_allocation_evidence.py
tests/host/bootstrap_allocation_authority.py
tests/host/test_bootstrap_allocation.py
tools/generate_bootstrap_allocation_fixture.py
tools/check_bootstrap_allocation.py
```

These eight files form the closed implementation evidence boundary.

### Actual code under the native fixture

The source fixture compiles the actual allocator and the complete fixed
`MICROS_FRAME_OWNERSHIP_SOURCES`, `MICROS_KERNEL_OBJECT_CORE_SOURCES` and
`MICROS_SCHEDULER_CORE_SOURCES` memberships already used by the native
ownership test. Freeze those memberships from #208; do not stub the actual
handoff/VM release dependencies used by coherence guards.
For the bootstrap adapter, it extracts only the complete exact
definitions of the selected pure validity helpers from the actual source,
not handwritten equivalents. The baseline uses the fixed #208 Git bodies
and their public/private headers; current mode reads the current regular
source owners and headers.
The native harness may select the declared baseline/current layout at
compile time, but must not add a cursor field to the baseline or read one
there. The logical availability/owner model does not depend on either
representation.

Passive observations are materialized only in private generated native
translations. Their exact inverse recovers the selected fixed/current
source bytes and binds every declared primitive occurrence and enclosing
compound. The production files contain no observation state or calls, so
ordinary and unrelated already-counted targets cannot acquire MEM01 work.

The closed original helper set is `range_end`, `ranges_intersect`,
`fdt_memory_union_covers`, `intersects_fdt_reservations`,
`managed_frame_is_allowed` and `bootstrap_state_is_valid`. Current mode may
add only the file-local word-count, bitmap-validity and managed-interval
helpers explicitly declared before Red. Their complete call closure must
be included; an undeclared helper or omitted effect fails the fixture.

The native-only adapter binds the selected allocator object and two numeric
linker-bound inputs. It does not alter any selected production body, claim
to execute CSR/MMIO code or provide a second validation algorithm. The
generated translation unit and executable are outputs under a private build
directory, not expected-value inputs. Check native LP64 integer/pointer
representation and use only numeric boundary conversion, never a synthetic
physical-memory dereference.

Use ASan/UBSan and the existing test-only collector. Static fixture storage
owns allocator snapshots and measurement state; do not put those complete
objects on a 4 KiB target stack.

### Observations and independent oracles

Only the dedicated native evidence target enables both
`MICROS_TEST_WORK_COUNTS=1` and `MICROS_TEST_BOOTSTRAP_MEMORY_COUNTS=1`.
Reject every other explicitly defined MEM01 mode value. Other targets,
including already-counted diagnostics, remain uninstrumented for MEM01.

Observe actual `KERNEL_MEMORY` range visits, index probes, comparisons and
bitmap-word loads at their primitive sites. The immutable declaration binds
each source/function, primitive occurrence, enclosing compound and placement
before candidate edits. The inverse removes only these observations and
their exact include/guard additions. It must restore fixed #208 baseline
bodies without allowing a production algorithm change.

Register exactly three discriminating oracles:

| Oracle ID | Test ID | Property |
| --- | --- | --- |
| `mem01-bootstrap-range-work` | `tests.host.bootstrap_allocation_evidence.BootstrapAllocationOracles.test_bootstrap_range_work` | Whole-interval safety with bounded range work and word-based bitmap validation, without per-frame FDT rechecks |
| `mem01-allocator-prefix-work` | `tests.host.bootstrap_allocation_evidence.BootstrapAllocationOracles.test_allocator_prefix_work` | Retained prefix progress, range-bound word masks and no quadratic bitmap-probe total for a successful fixed-range allocation prefix |
| `mem01-owner-cursor-composition` | `tests.host.bootstrap_allocation_evidence.BootstrapAllocationOracles.test_owner_cursor_composition` | Actual typed allocation/release compositions obey the same search bound while preserving owner/count/rollback state |

Every measured case has its own declared producer/capture, exact owner mask,
complete 27-by-14 snapshot and actual execution result. Unknown dimensions,
overflow, missing/duplicate producers, partial results or unexpected nonzero
owners fail. Do not count independent semantic assertions as production
algorithm work or omit an invoked production helper from a claimed bound.

Common inputs cover unsorted/overlapping/touching/partial ranges, exclusions,
physical gaps, multiple descriptors sharing one bitmap word, 1/63/64/65 and
maximum/maximum-minus-one managed counts, exhaustion, lower-index release,
allocated-word skips and final partial words. Choose a fixed bounded
allocation prefix large enough to fail the original quadratic algorithm;
maximum-capacity representation coverage does not require quadratically
exhausting every baseline frame.

The required discriminating matrix is fixed independently:

| Family | Input and operation schedule |
| --- | --- |
| Dense validity | `N` in `{1,63,64,65,257,4096,262143,262144}`; raw memory starts at `0x80000000`, kernel start/end are `0x80200000`/`0x80400000`, and the memory end is `0x80400000 + 4096*N`; no FDT reservation. Validate pristine all-free state and live state after allocating `min(N,33)` frames. Preparation is outside the measurement. |
| FDT split/ordering | Memory interval `i` starts at `0x80400000 + 4096*10*i` and contains nine pages, for `0 <= i < 16`. Its odd-offset pages 1/3/5/7 are holes. Split the 64 holes equally between the two FDT reservation sources and reverse each raw input array. Validate the resulting 80 one-page managed intervals, including shared bitmap words. |
| Portable capacity split | Memory interval `i` starts at `0x80400000 + 4096*12*i` and contains eleven pages, for `0 <= i < 16`. Its odd-offset pages 1/3/5/7/9 are reservations. The portable constructor receives all 80 reservations and must produce 96 one-page managed intervals. This is not misrepresented as a 64-reservation FDT case. |
| Monotone allocation | Use every dense `N` above, allocate `a = min(N,512)` frames in one capture and compare every returned physical address to the independently declared ascending sequence. Count all search and commit bitmap reads. |
| Typed composition | Repeat the same dense allocation-prefix schedule through exact `KERNEL_TEMPORARY` ownership, with initialization and full diagnostic assertions outside the measured operation. Retain the exact mutation guards below, and distinguish their costs from the allocation-prefix claim. |

Use the fixed reference model to derive expected availability and addresses,
not the candidate's cursor. Additional semantic/adversarial cases must be
declared before Red and remain immutable through the attempt. Every family
executes in both baseline and current mode; only genuinely new cursor-layout
corruption guards are current-only.

The required mutation guards are:

1. With 4096 managed frames and the first 512 allocated, release indexes
   `255,63,511,0,127,64` in that order. The next six allocations must return
   `0,63,64,127,255,511`. Check all counts, owners and the cursor invariant.
2. With 257 managed frames, allocate the first 128 as `PROCESS_USER` for
   exact process handle `{slot=0,generation=1}`. Release the selected
   process bitmap `{0,64,127}`, then require those three addresses in order
   through the normal typed allocator. The existing process-set preflight
   still scans its full capacity; this is a coherence guard, not a MEM05
   work improvement.
3. With the first 65 of 257 frames allocated, inject the existing defensive
   failure by placing a nonfree ledger owner at the next free index 65.
   In current mode set the cursor to 64, a valid lower bound behind the
   allocated bit at 64. The failed typed allocation must preserve the
   complete allocator, ledger and caller output byte for byte, including
   that cursor. Repair only the deliberately injected fixture corruption
   afterward and verify normal allocation of index 65. The new-layout
   assignment is a declared current-only fixture input, not a fabricated
   baseline field or a changed logical availability map.
4. Exercise the existing VM claim/release and freeze/process-release
   transitions with lower-index returns. Their prevalidated free-bit
   primitive must rewind the cursor, while counts, owner generations,
   phase restrictions and publication order remain the existing contract.

Report the work-bounded allocation capture separately from diagnostic
assertions and mutation-coherence guards. No unmeasured process-set or
complete-validator scan is folded into a claimed constant-time release.

Retained and current-only guards cover exact lowest results, unchanged
outputs, denied release, all-free versus live validation, inactive corruption,
invalid cursor bounds and free-below-cursor corruption, typed defensive
rollback, process-set release, and VM release mirror consistency. A current-
only cursor-layout case is not a discriminating baseline Red.

The original `frame-allocator` and `frame-ownership` executions and model
sequences remain. Add one native `bootstrap-allocation` case to both native
tiers through its exact dedicated target, preserving every existing recipe
and original CMake expectation through a finite inverse.

Actual target acceptance retains `test-qemu-frame-allocator` at 128 MiB and
256 MiB, ownership/handoff/MMU regressions and the complete existing target
matrix. No native numeric-bound fixture substitutes for real FDT/linker,
owned-page, mapping or shutdown acceptance.

## Six-key authority transition

Use transition `MEM01-bootstrap-allocation`, task `MEM01`, primary unit `B02`,
finding set exactly `{B01-F006, B02-F001}`.

Affected and planned-retirement keys are exactly:

```text
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_allocator.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/bootstrap_memory.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_bootstrap.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_validation.c/2
tests/host/kernel_layout_source.py:data-entry:BOUNDED_FRAME_VALIDATION_SHA256
```

The corrected #208 inventory was inspected before restarting this proposal.
Every selected fact has exactly its physical C owner and
`tests/host/kernel_layout_source.py` as payload, with primary B02.
The two `bootstrap` file rows retain consumers B02/B05/B06/B14; the other
four rows retain B02/B14. The existing mapping rules are not changed.
Their source closure contains five C files plus the evidence owner; adding
the header and narrow directory adapter through unit inputs yields exactly
the eight candidate paths below. CMake and planner inputs remain immutable
and disjoint. No task-specific closure override is needed.

Keep every original literal and reviewed pin. Retiring the last key only
authorizes the changed input to the earlier bounded-validator adapter; it
does not authorize replacing actual current source with an archived body.
The final file assertion is a separately named retirement, not an implicit
consequence of skipping the earlier adapter.

The bootstrap-range oracle covers the bootstrap file identity. The prefix
oracle covers the allocator identity. The composed-owner oracle covers the
three owner-file identities and the bounded-validator input assertion.
Semantic/corruption/rollback guards independently cover the directly coupled
new state and every retained function/type/data boundary.

All other facts stay active, including `BASE_FRAME_VALIDATION_SHA256`, both
active/inactive token digests, the shared C inspector, include/owner/recipe
invariants and all prior COPY01/PLAT01 claims. Preserve all 3,938 complete
current facts and fourteen fingerprints before adding the new family.
This includes the Accepted ADR-0098 authority-core fingerprint
`152ba765d3a332961b59e88a7725f4a823c335bb62b97ba09c662bb84e1098fd`,
not its superseded pre-binding metadata view.

The only mutable candidate paths in one recorded attempt are the six
production paths above and these two narrow assertion adapters:

```text
tests/host/kernel_layout_source.py
tests/host/test_kernel_directory_layout.py
```

Every other tracked file in the merged design tree, plus the eight declared
evidence additions, belongs to the immutable envelope. Finalize native
recipes, ownership registration, test mappings and directly related
implementation documentation before recording Red.

Add one closed `bootstrap-allocation-baseline` family with exactly these
eight facts, using the existing owner-prefixed identity spelling:

| Owner | Kind/scope | Class |
| --- | --- | --- |
| `bootstrap_allocation_authority.py` | `design-anchor:MEM01` | `TRANSPORT_IDENTITY` |
| `bootstrap_allocation_source.py` | `source-membership:MEM01` | `PERMANENT_INVARIANT` |
| `bootstrap_allocation_source.py` | `artifact-contract:MEM01-recipes` | `ARTIFACT_CONTRACT` |
| `bootstrap_allocation_source.py` | `state-work-contract:MEM01` | `PERMANENT_INVARIANT` |
| `bootstrap_allocation_source.py` | `source-projection:MEM01` | `TRANSPORT_IDENTITY` |
| `bootstrap_allocation_authority.py` | `baseline-oracle:mem01-bootstrap-range-work` | `BASELINE_ORACLE` |
| `bootstrap_allocation_authority.py` | `baseline-oracle:mem01-allocator-prefix-work` | `BASELINE_ORACLE` |
| `bootstrap_allocation_authority.py` | `baseline-oracle:mem01-owner-cursor-composition` | `BASELINE_ORACLE` |

Both owners are under `tests/host/`. The resulting inventory has 3,946 facts
in fifteen families. Independently derive complete payload/unit/value
records from this separately merged contract and the fixed reference before
editing an extractor or evidence owner, never from candidate output.

The task-specific authority helper handles only these six keys and delegates
all other decisions to the unchanged prior authority chain. It is a leaf at
module import time. Do not alter the previous declarations, journal formats
or transition rules to make the new task pass.

## Actual execution and recertification

The prospective author interface accepts only:

```text
${MICROS_PYTHON} -E -S -B tools/check_bootstrap_allocation.py --red
${MICROS_PYTHON} -E -S -B tools/check_bootstrap_allocation.py --green
${MICROS_PYTHON} -E -S -B tools/check_bootstrap_allocation.py --recertify
```

The merged design's actual commit/tree binds task, source, commands, oracle
coverage and the complete input inventory. No caller-supplied command,
source root, expected result, identity set or snapshot can authorize a run.

Freeze the complete independent declarations before editing extractors or
production. Actual Red builds and runs the fixed #208 bodies under the same
immutable fixture/measurement inputs. Each of the three named oracles must
fail for its work divergence while original semantic execution succeeds.
Missing fields, compile/link errors, invalid observations or a broken harness
are errors, not qualifying Red.

Green builds the actual clean staged candidate and passes all three oracles
and accompanying guards. Recheck source/tool/compiler/runtime/input identity
before and after each execution and publication. Any immutable-input change
renews Red and Green; post-Green source drift prevents recertification.

Recertification replays Green, then enters one exact root/plan/PID provisional
scope for only the six named assertions. Clear it on every exit. Keep prior
claims separate, and record each actual unit result:

| Unit | Canonical recertification command |
| --- | --- |
| B01 | `cmake --workflow --preset test-qemu-frame-allocator` |
| B02 | `cmake --workflow --preset test-qemu-address-space-handoff` |
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
| B14 | The actual `--recertify` invocation above, including in-process retained authority/source/recipe checks and oracle replay |

Every unit binds all eight candidate paths and the same complete immutable
envelope. Do not infer completion from an aggregate-only exit code.

Only complete success publishes the hash-chained `AUTHOR_RECERTIFIED` event
and derives `RETIREMENT_READY`. A failed/interrupted transaction publishes no
usable terminal event and restores ordinary assertion policy.

Before the first reviewed implementation commit, only that current terminal
journal can authorize the six retirements. After independent review and
commit, an identical committed declaration supports ordinary journal-free
source checks; it is not a new execution receipt.

## Implementation acceptance

After the design separately merges:

1. declare and independently freeze source/recipe/primitive/oracle authority;
2. obtain actual discriminating baseline Red with original semantic guards;
3. implement the one scalar, complete mutation maintenance and range/word
   validity;
4. pass actual current native work, semantic, access and rollback evidence;
5. execute the granular affected-unit recertification transaction;
6. run the complete current fresh author plan, retaining all 50 commands,
   44 static configurations/113 ELF outputs, ten observed scopes and blocked
   B13 admission;
7. obtain independent implementation review of the exact green tree and
   actual journal;
8. commit and verify ordinary committed authority without author journals;
9. publish and squash only the identical reviewed tree; and
10. clean only the completed task's local/remote refs.

The separately merged design contained documentation only and passed its
required structure/three-layer checks. Implementation evidence is produced
by the [documented actual commands](../development/building.md#bootstrap-allocation-work-and-authority-evidence);
scaffold availability alone is not a qualifying Red, current Green or
completed retirement.

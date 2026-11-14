# VM Frame Allocation

## Status and scope

This is the Accepted VM04 contract under
[ADR-0138](../adr/0138-minix-vm-frame-allocation.md). MEM01 #210 and
VM03 #276 satisfy its actual predecessors. The
[fixed study](../research/minix-vm-frame-allocation.md) owns provenance,
reference behavior and measured current resource limits.
Independent design review is clean and design #277 is separately merged.
Implementation is active; B07-F003 remains open until its complete author
acceptance, independent implementation review and merge.

Close B07-F003 only: replace repeated lowest-address construction with the
reference current-bitmap/cache/retained-origin allocator. Kernel bootstrap
allocation, frame-owner/PTE mechanisms, VM05 movement, VM06 snapshot
lifetime and LIFE01 cross-owner lifecycle remain separate.

No syscall, wire version, application capacity, mapping-record layout,
public lifecycle signature or VM/kernel ownership boundary changes.
Private VM state and local reservation representation change as specified
below. There is no compatibility or migration path for an unshipped layout.

## One availability plane and typed local state

The existing compact managed-frame coordinate remains distinct from the
physical address. It covers at most 262,144 frames across the existing
checked, sorted managed ranges.

Reuse the 4,096 existing 64-bit bitmap words as current availability:
one means locally FREE; zero means unavailable. Do not retain another
immutable initial-free bitmap or allocate an address-span-sized bitmap.
The complete inactive bitmap tail and unused high bits remain zero.

The exact VM-local frame byte domain becomes:

| Meaning | Local byte | Available bit | Initial eligibility |
| --- | ---: | ---: | --- |
| Active FREE frame | `0x01` | 1 | Free in the imported pool |
| Kernel/self/service static frame | Original `0x02`/`0x03`/`0x04` | 0 | Not eligible |
| Dynamic transferable frame | `0x85` | 0 | Free in the imported pool |
| Local load reservation, table or data | `0x86` | 0 | Free in the imported pool |
| Dynamic process-wired frame | `0x87` | 0 | Free in the imported pool |
| Dynamic process page table | `0x88` | 0 | Free in the imported pool |
| Inactive frame-array tail | `0x00` | 0 | Not a managed frame |

Every other byte is malformed. In particular, a tagged FREE/static value
or an untagged dynamic class is not another accepted representation.
Only the four exact tagged dynamic values carry initial-free provenance.

The version-1 boot object and `micros_vm_frame_state` wire enum do not
change. Initialization first validates that original object, clears the
existing VM state and builds the current-free bitmap from its original
FREE bytes. It leaves those bytes untagged and unchanged. Existing cold
handoff, digest and independent boot-validator consumers therefore retain
the original object before any load reservation.

Reservation changes FREE to the tagged local reservation and clears its
available bit. Later dynamic transitions retain the tag. Release writes
the original untagged FREE value and sets the bit. Static frame classes
are never rewritten by the allocator.

The ordinary initial-eligibility predicate uses this exact local byte
domain, while every selected frame also checks its current availability
relation. Complete diagnostics map tagged dynamic bytes back to original
FREE for the existing digest calculation, retain static bytes, and verify
the bitmap independently against current classes. They retain the original
immutable header/ranges/mappings, full inactive-tail and global corruption
coverage.

This is maintained typed data, not a remembered validation result.
Consistent private-data corruption is not new kernel authority: the real
kernel still rejects a frame whose actual owner does not permit the
requested transition. Complete diagnostics independently reject disagreement
with the imported digest and global record/frame sets.

## Cache and origin

Add only:

- twenty 32-bit compact frame indexes;
- one 32-bit active cache count; and
- one 32-bit retained scan origin.

Twenty is the maximum existing four-table-plus-sixteen-data load, not a
limit on managed/free frames. Cache overflow never loses an available
frame: it remains in the complete bitmap.

The origin is in `[0, managed_frame_count]`; the upper endpoint is the
initial unset sentinel. It is not MEM01's lower-bound cursor.
An allocated bit may sit at the origin, and no lower-prefix invariant
is asserted.

Initialization seeds the cache in reverse managed-range order and ascending
frame-index order within each range, stopping cache insertion at twenty.
Bitmap initialization still covers every active frame. This specializes
MINIX's reverse-chunk import to the existing canonical sparse geometry.

Before access, validate cache count, origin, active geometry and every
consumed index. Common selected-state admission includes the cache-count
and origin bounds, including before the service's first backend call;
individual cache entries are checked only when consumed.
Pop from the active end without clearing its backing slot.
For a valid index:

1. require an eligible local byte and consistent available-bit/current-class
   relation;
2. if allocated/reserved, discard that stale hint and continue; and
3. if FREE, select and reserve the actual frame.

A malformed index/class/bit relationship is `INVARIANT`, not a stale hint
or reason to fall back silently. Inactive cache slots are not authority
and need not be zero after a pop. Active slots may contain stale allocated
eligible frames; no eager cross-record removal index is introduced.

## Descending search

If the cache supplies no page, start at the retained origin when it is
below the managed count, otherwise at the highest managed index.
Search down to zero. Skip a bitmap word whose admitted portion is zero;
within a nonempty word select the highest available bit.

Mask the starting partial word and the managed upper boundary before using
bits. Do not shift by 64, read an inactive word or turn a negative index
into an unsigned array access. Select actual bitmap bits, not free counts.

If the first search fails, retry from the managed upper bound as MINIX
does. If both fail, return `NO_SPACE`. A success records the selected index
itself as the new origin, including cache hits.

Only single pages are allocated. Sparse-range gaps cannot accidentally
become physically contiguous output: compact indexes are translated through
the existing checked binary geometry owner. No low-memory, alignment,
contiguous-run or allocation-clear flag is added.

## Failure-atomic load reservation

Preparation still validates the complete ELF/request/stack before selecting
frames. Its existing private helper owns the temporary table/data arrays.

Reserve `4 + page_count` single pages in reference allocation order.
During this private, non-callback phase, selected bits are cleared and local
bytes become load-reserved. This makes duplicate or stale cached entries
unable to allocate the same frame twice.

Retain the selected indexes/addresses in the existing temporary frame
arrays, plus only scalar old cache count/origin and populated count.
Rollback coordinates must already be valid; rollback cannot depend on
rereading a malformed cache entry or unchecked geometry.
Allocation never overwrites cache-array values. On any batch, geometry,
ordering-workspace or capacity failure:

- restore every tentatively reserved frame to untagged FREE and available;
- restore the exact original active cache count and origin;
- preserve the cache-array values; and
- return the original error without publishing a transaction or invoking a
  backend.

The existing expected prepare-failure input clearing remains. There is no
bitmap copy, cache-array rollback copy, new journal or persistent reservation
plan. Failed private sorting must still leave a known permutation of the
selected set usable for rollback.

After all pages exist, sort that set by physical address with the existing
exclusive VM02 key workspace. The lowest four **within the selected set**
become root/level-1/image/stack tables; the remaining addresses map to data
pages in ascending child-VA order. This preserves the strict existing
record representation without restoring globally lowest selection.
Release the workspace before transaction publication or any backend call.

Publish the same retained description and `INPUT_COPIED` phase only after
the complete local set is reserved and its canonical ordering is ready.
The kernel still owns every frame as FREE at this point. This is a
VM-local reservation, not a kernel ownership change.

## Lifecycle and authority

| Boundary | Local allocation obligation | Unchanged external boundary |
| --- | --- | --- |
| Preparation success | All table/data bytes load-reserved; all corresponding bits zero; one complete retained description | No `LOAD_BEGIN`, scratch mapping or kernel owner change yet |
| Bind | Check all selected reservations and existing record membership; publish exact returned process/token into the record | `LOAD_BEGIN` remains rootless/frame-free and result disagreement remains fatal |
| Scratch map success | Selected data reservation becomes tagged transferable; its bit stays zero | Actual kernel FREE claim, exact VM root/leaf and map fence |
| Scratch commit | Clear progress/scratch state without making the frame available | Exact PTE removal and local invalidation |
| Freeze | Reserved tables and transferable data become their tagged process classes; bits stay zero | Actual selected owner/root certificate, generation and protected publication |
| `INPUT_COPIED` abort | Release the complete local reserved set before clearing transaction/input storage | No kernel abort is required for a not-yet-bound load |
| Loading/frozen abort | Preflight all selected local indexes/classes/bits before backend and local stores, then release the complete set | Existing scratch removal, root teardown, fence and kernel-owner release order |
| Load commit | Retain unavailable process frames; clear only transient transaction/input state | Existing activated mapping commit |
| Exit | Preflight selected record/set, then free and cache each frame only after kernel release | Existing exact token/generation detach and owner/PTE release |

Release sets each available bit even if the cache is full. Append indexes
in the existing ascending selected-record order while space remains.
It does not lower the origin. Reject double release or a class/bit mismatch
before any store.

Successful rollback/exit may change these private allocation hints. Exact
resource return still means the same free/owned frame and mapping sets,
no scratch/transaction/grant leak and all original external resource
witnesses. Rejected operations and failed reservation batches preserve
their complete prior state; no exception is made for invalid hints.

The kernel ledger, PM reservation, actual VM caller/root, generation,
mapping/exit tokens and all fences remain authoritative. Neither a cache
entry nor a free bit authorizes a syscall by itself.

## Selected checks and complete diagnostics

Ordinary allocation no longer constructs a global mapping-record membership
view per batch or starts at frame zero. It consumes the maintained current
bitmap, selected class and actual cache/search prefix.

The service's existing before-copy record-header gate remains. Bind retains
its complete selected membership preflight before record publication.
Unvisited foreign record corruption is not allocation authority; global
record/bitmap disagreement is complete-diagnostic scope.

Replace diagnostic reallocation through `vm_core_selected_frames_match`
with explicit set/class/bitmap equality. An advanced cache/origin cannot
reproduce the historical selected set.

Complete phase counts become:

- `INPUT_COPIED`: `4 + page_count` local reservations, no loading record;
- `LOADING`: four table reservations plus unstarted data reservations,
  and exactly completed/mapped data as transferable;
- `FROZEN`/active: the same exact process table/wired sets, unavailable; and
- idle after release: no transient reservations or transferable frames.

Keep exact transaction/record uniqueness, all current frame-state counts,
original-class digest, active cache index/eligibility checks, bitmap/class
equivalence and inactive bitmap/frame tails. A global diagnostic is
read-only and never repairs a hint or bitmap.

VM03's per-page nonmovement bounds, immutable prepared description and
pre-write stack protection remain. Do not restore complete VM/BOOT/PM
audits, all-root enumeration, source-plan reconstruction or allocator
replay to ordinary page steps.

## Work and storage evidence

Let `m` be active managed frames, `r` active ranges, `q = 4 + u <= 20`,
`C = 20`, and bitmap word width `W = 64`.

- Cache-top selection is constant policy work plus the existing
  `O(log(r+1))` compact-index translation. A batch discards at most the
  originally active `C` cache entries; each pop is counted.
- Fallback has at most two descending searches per selected page.
  Search-word visits are bounded by `2*q*ceil(m/W)` and selected nonempty-word
  bit inspection by `2*q*W`. No process-record-capacity multiplier appears.
- With sufficient valid cache entries, `q` allocations have exactly `q`
  cache pops and zero fallback-search word visits.
- Initial import is `O(m + r)` plus the existing fixed storage clearing;
  release is `O(q)` plus existing geometry lookup.
- Canonical selected-set ordering retains the VM02 fixed-width radix bound
  and its actual counted cleanup, separately from allocator probes.
- Stack/ELF planning, input/page movement and once-per-boundary membership
  work remain separately disclosed, not labelled allocator work.

These are algorithmic bounds, not elapsed-time thresholds. Native evidence
must distinguish cached, stale-prefix, same-word, empty-word, wrap/retry,
fragmented, sparse-range and exhausted paths and compare exact choices with
an independent simple reference state machine.

Reuse the existing 32-KiB bitmap and shared key workspace. New production
metadata is at most 128 bytes including alignment; the defined cache,
count and origin occupy 88 bytes before layout padding. No new capacity
buffer, automatic bitmap/cache copy, heap, VLA or recursive search is
permitted. Keep ADR-0087's preparation-helper lifetime.

The current limiting observed VM has only one resident page of image
headroom. All 160-page VM, four-KiB user/16-KiB kernel stack and deadline
budgets remain unchanged. Actual compiler/linked evidence must prove fit;
the proposed field arithmetic is not a measured future `sizeof` or image
certificate.

## Acceptance and implementation sequence

After separate design merge:

1. Establish actual unchanged-runtime native selection/work and same-guest
   allocator Red, then implement representation, initialization and the
   reference cache/scan reservation mechanism.
2. Integrate bind/scratch/freeze/abort/exit and complete diagnostic set
   accounting; prove partial-batch rollback and real kernel/local agreement.
3. Complete current fixture/reference/primitive qualifications and the
   original composed/stack gates, then obtain fresh independent review
   before any permanent implementation commit.

Keep all existing VM and memory cases, model seeds/counts and target
scenarios. Mechanistically update exact physical expectations for the new
Accepted selection policy; never capture candidate output as the oracle.
Use the existing native families and original ELF-spawn guest, not a new
transport or suite with reduced coverage.

Required focused controls include malformed cache count/origin/index,
stale entries, bitmap/class/tag disagreement, initial static-frame
ineligibility, partial-word bounds, no-space after a partial batch,
exact cache/origin restoration on failure, repeated reuse, active-record
coexistence, sparse geometry, and all local/kernel release phases.
Retain original complete diagnostic corruption classes in their new
representation, including original-class/digest and inactive tails.

The original composed guest must preserve its resource witness, exact
VM03 three ELF plans/nine request decodes, zero ordinary global audits/root
enumeration and five selected root certificates while measuring actual
allocator cache/search work. Preserve scratch preemption and every
44-configuration/113-ELF/ten-observed-scope stack obligation; B13 stays
blocked.

Current metadata and artifact ownership updates belong here under
ADR-0123. Run one sufficient final author plan, not a second correction
recertification execution. The separate design contained documentation only.

## Current implementation and evidence owners

| Responsibility | Owner |
| --- | --- |
| Current bitmap, typed eligibility and cache seed | `vm_core.h` / `vm_core.c`; the wire boot enum and initial frame bytes remain unchanged. |
| Cache, descending word scan, batch reservation and local release | `vm_frames.c`; existing temporary arrays and key workspace retain the selected set, while scalar count/origin rollback preserves failed batches. |
| Lifecycle integration | `vm_load.c` / `vm_exit.c`; local reservation precedes input publication, and actual kernel effects retain their original order. |
| Selected and complete proofs | `vm_validation.c`; selected class/bit checks and bounded metadata stay separate from full cache/bitmap/digest/record equality. |
| Native allocator/reference evidence | Existing `selected_vm_test.c` / `selected_vm_evidence.py`, including original cases, malformed/stale cache, partial rollback, boundary/maximal word scans and before-backend header rejection. |
| Complete mixed lifecycle reference | Existing `elf_spawn_model_reference.c` / `elf_spawn_model_compare.c`; independent cache/origin, allocation/release and active-hint agreement retain the original seed and transition count. |
| Same-guest work and resource equality | Existing `vm_command_evidence.py` and `elf_spawn_test_service_compare.c`; actual cache/search primitives and validated hint differences do not waive exact availability, mapping, input or boot-object equality. |

The [build guide](../development/building.md#validation-planning) owns
commands. The implementation PR owns exact Red/Green, final-tree and review
records rather than duplicating them here.

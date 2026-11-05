# Kernel Mapping Topology and Direct Table Identity

## Status and outcome

This is the Accepted MEM02 contract under
[ADR-0101](../adr/0101-minix-kernel-mapping-topology.md).
The [fixed-source study](../research/minix-kernel-mapping-topology.md)
records the MINIX and Sv39 mechanisms, current source and retained costs.
It closes only B02-F003/B02-F004 and B02-E003 after separate implementation
acceptance.

The fixed source base is merged MEM01 #210:

```text
commit: 3f7e33026f92c8c1765ce00c2b03b5c2d6855197
tree:   08f31a8b1dfc639c8b826ea65d092327fea159a1
MINIX:  4db99f4012570a577414fe2a43697b2f239b699e
```

Design #211 is independently reviewed and separately merged as
`e7d936d6d26e372a8794bc5fd8feb6d976fcd529`, tree
`4f520e80e7f7a7490f67d438029c5d7ff77abcc0`.
Implementation #212 is independently reviewed and merged as
`bf4eef8f0c59a9d06ae3a20d4a52c5f07943c2ee`, with identical
reviewed/committed/remote/preview/squash tree
`910e04a2129a692ec74ede4c34ff19c8f18e2397`.
Actual pre-production Red, current Green, thirteen-unit recertification,
all fifty fresh author commands and first-commit independent review passed.
Fresh committed checks preserve ordinary authority without journals, and
completed task refs are removed. This closes only the two mapping findings
and their evidence gap; MEM03/MEM04 cadence findings remain open.

## Authority and non-goals

Retain:

- the single-hart, SIE-clear, one-shot boot construction phase;
- 4096-byte frames, 512 entries per table and three Sv39 levels;
- the 1024 kernel-table-frame bound and all allocator/owner capacities;
- exact `KERNEL_PAGE_TABLE` allocation and full-page zeroing before linking;
- the public kernel-map error enumeration, two-scalar report and entry points;
- the public Sv39 codec, its permission/encoding rules and all existing callers;
- lowest-address allocation, the MEM01 cursor invariant and exact typed ownership;
- report publication only after complete validation and exact `satp` readback;
- the existing full local fence/write/fence sequence; and
- every current native, target, image, model, source and stack gate.

Do not add private-user large pages, dynamic kernel mapping, automatic
split/merge, a hash/tree fallback, ASIDs, global PTEs, a new memory type, a
metadata allocator, recovery or a higher-half layout.

MEM03's owner-validation cadence and MEM04's process lookup/activation remain
unchanged. VM selection, mapping, freeze, release and handoff policies remain
with their existing owners. No stack, image, table or frame budget increases.

## Exact mapping domains

The construction order remains:

1. text, read-only data and writable kernel ranges;
2. canonical managed intervals in ascending order;
3. UART; then
4. PLIC priority, supervisor enable and supervisor context.

The independent final expected-domain traversal may order those same
nonoverlapping intervals by virtual address. It must not infer ownership or
permissions from the constructed PTEs.

| Domain | Required permission |
| --- | --- |
| `[__kernel_text_start, __kernel_text_end)` | R/X |
| `[__kernel_rodata_start, __kernel_rodata_end)` | R |
| `[__kernel_writable_start, __kernel_writable_end)` | R/W, NX |
| Each allocator-managed interval | R/W, NX |
| UART page `0x10000000` | R/W, NX |
| PLIC priority page `0x0c000000` | R/W, NX |
| Supervisor-enable page `0x0c002000` | R/W, NX |
| Supervisor context page `0x0c201000` | R/W, NX |

Keep the four existing adjacent-PLIC absence checks. Every other virtual
address, including the private user root slot, remains absent unless it is
inside one of these domains.

Every leaf retains `U=0`, `G=0`, `A=1`, `D=W`, zero RSW and reserved high
bits, and the existing prohibition on W without R or W+X. Non-leaves retain
only the existing supported PPN and V encoding.

Domains are not per-frame ledger kinds. Free and allocated frames inside one
managed interval retain the same existing supervisor direct-map permission.
Conversely, an adjacent writable-kernel/managed-RAM boundary stays a domain
boundary even when both sides are R/W.

### Leaf selection

For each current domain position, choose the largest of 1 GiB, 2 MiB and
4 KiB whose virtual and physical base are naturally aligned and whose full
extent fits in the remaining domain.

Identity mapping makes the two bases equal, but both the address/encoding
constraint and complete extent remain checked. Require positive, page-aligned,
representable intervals with no canonical-address gap, overflow or unsupported
physical address. No 64-bit shift is performed before validating its level.

A partial domain head or tail uses smaller leaves. Do not map outside the
domain, round up its end, fill a hole, change permissions, or combine domains.
A valid existing leaf is a conflict, not an invitation to split or overwrite.
A lower table at a position selected for a large leaf is likewise a conflict.

The public codec remains level-agnostic. The mapper and validator, which know
the current level, additionally require zero lower PPN bits for that leaf size
and combine the physical base with the appropriate virtual offset.

## Direct table representation

Extend only the existing
`micros_kernel_address_space_private_state`. Its current physical table list,
root, counts and report retain their meanings; `leaf_count` counts actual
leaf PTEs at any level, not their expanded number of base pages.

Add these exact private arrays:

| Field | Type and extent | Meaning |
| --- | --- | --- |
| `table_range_indices` | `uint16_t[1024]` | Checked canonical managed-range index for each allocated table slot |
| `level1_table_ids` | `uint16_t[512]` | Root-VPN-indexed identifier of a middle table |
| `level0_table_ids` | `uint16_t[512][512]` | Root/middle-VPN-indexed identifier of a base-page table |

Zero means no child. A nonzero child identifier is table slot plus one.
The maximum encoded identifier is 1024, not a physical pointer or a process
handle. Bounds are checked before subtracting one or indexing a table record.

This is the Sv39 adaptation of MINIX's parent-indexed `pt_pt` relationship.
The full hardware radix shape remains explicit; no alternative searchable
registry, sparse container, collision policy or fallback is introduced.

The fixed increase is exactly
`2 * (1024 + 512 + 512 * 512) = 527360` bytes, or 515 KiB, before any unrelated
existing padding. The arrays live inside the sole existing BSS owner and are
reserved by `[0, __kernel_end)`. Their initial BSS clearing cost is recorded,
not treated as zero: the existing eight-byte boot loop performs 65,920
additional stores for these bytes. They add no automatic frame-sized object.
Fewer hardware tables do not imply a net resident-memory saving; at the
current 128/256 MiB sizes this software metadata can exceed the table-page
saving. Actual linker-end/page-rounding and managed-frame changes remain
visible in target evidence, without enlarging a resource budget.

### Allocation ordinal and owner identity

At constructor entry the real allocator is pristine and its cursor is zero.
Before publication, only this constructor allocates frames, every successful
allocation is `KERNEL_PAGE_TABLE`, and the valid construction path releases
none. Therefore the next table slot `i` must be compact managed-frame index
`i`.

Do not read the allocation cursor as owner/index authority. Resolve the actual
returned physical address against bounded canonical geometry once during
table allocation, require the resulting compact index to equal the next table
slot, and retain the matching range index.

Construction and the complete table-tree/ownership phase establish the
following selected checks before a table is dereferenced:

- non-null authoritative allocator and ledger, valid phase and bound counts;
- slot less than the current table count and the unchanged capacity;
- the selected range index within both geometry arrays;
- equality of that allocator descriptor and the ledger's immutable snapshot;
- the table slot within the descriptor's compact-index extent;
- checked recomposition of the exact recorded physical address;
- a set allocator bit at that compact index; and
- exact zero-identity `KERNEL_PAGE_TABLE` ownership.

This reads one selected range/owner/bitmap record rather than scanning ranges
or calling the geometry-wide public ownership lookup. The validation owner
implements the narrow read-only selected-frame helper; the existing private
header contains only its declaration. Construction depends downward on
validation, never the reverse. No supported public interface, header function
body or second mutable owner is added.

The later read-only expected-coverage phase may reuse that successful table
proof within the same quiesced validator invocation. It still checks
index bounds and PTE/child-address agreement, but does not repeat owner/range
queries on every lookup. The proof cannot escape that invocation, survive
mutation or authorize an ordinary process lookup; this is not a cache or a
MEM03/MEM04 cadence change.

The actual typed allocator still performs its existing geometry work. The
one-time range resolution for a newly allocated table is charged to table
allocation, not hidden in the direct-index claim.

### Parent/child agreement

The root is slot zero and its physical record equals the root address.
For a non-leaf root PTE, use only `level1_table_ids[vpn2]`. For a non-leaf
middle PTE, use only `level0_table_ids[vpn2][vpn1]`.

Require the selected identifier, physical table record, checked owner/index
and decoded PTE address to agree before following the child. No identifier
permits using a different physical frame. A missing, excessive or mismatched
identifier is an invariant failure, not a search request.

An absent entry or leaf has no child identifier at that active parent
position. Rows below an absent or large-leaf root are not authority and are
never consulted. When a new middle-table row becomes owned, initialize its
child identifiers before linking that table; do not repair an already-owned
row.

Preserve existing error precedence where the old condition still applies:
decode a PTE before interpreting its child identifier, and retain the
existing valid-leaf/destination conflict result rather than hiding it behind
an unrelated metadata error.

The final tree walk visits each table slot exactly once. Repeated slots,
cycles, aliases, orphan records and unexpected levels fail before activation.
The ordinal/range relation makes distinct table slots distinct physical
frames without another physical-frame scan or pairwise duplicate search.

## Construction and failure boundaries

Keep existing entry rejection order: already initialized, interrupts enabled,
linker layout and pristine allocator, followed by construction. Bound all
counts consumed by new selected-index checks before dependent access.

For a new table:

1. check capacity;
2. request the exact existing typed owner through the real runtime wrapper;
3. retain current allocation/error translation and physical PTE checks;
4. establish the checked ordinal/range relation;
5. clear the complete physical page;
6. prepare the table record and any newly owned child-index row; and
7. publish the prevalidated child identifier and hardware entry without a
   later fallible operation in that link commit.

For a new leaf, preflight address/level/permission, selected parent identity
and absent destination before its store and count update.

The original constructor is not a general failure-atomic mapping service.
Failure can leave an unpublished partial candidate and allocated boot
tables; no report is published and boot fails. Retain the existing release
of a newly allocated but unencodable physical frame. Do not add a reusable
rollback/retry framework or return success after partial initialization.

After all domains are built, retain complete candidate validation, the final
SIE check, ordered activation, exact readback and publication last. The
published root and its supervisor mappings remain immutable.

## Complete candidate validation

Keep the existing complete ownership-validator call and phase/allocator
binding. Do not change its active/inactive loops, invocation cadence or
other callers.

Preserve:

- exact allocated, owner and table counts;
- all original bitmap/owner validity;
- complete tree reachability;
- exact required physical mappings and permissions;
- actual table-frame direct-map access; and
- every existing device absence check.

Replace `candidate_table_index` and per-table address/range searches with
selected checked slots. Keep the existing 1024-element automatic reachability
witness; do not introduce a frame-capacity stack object.

Validate leaves at every level, including physical alignment and exact
non-user permissions. Require no child identifier for a leaf/absent entry.
Count all actual leaves during the independent tree walk and compare with
the constructed state.

Expected-domain validation advances by checked leaf extents, not one lookup
per covered base page. At each position require exact identity translation,
permission, domain containment and the largest legal level. Sum the required
leaf entries and require equality with the complete observed tree count, so
an otherwise valid extra leaf cannot hide outside the requested domains.

The selected mapping lookup used by validation remains fixed-depth and
supports the selected leaf's offset. It does not become a new runtime public
lookup or replace ordinary process validation. Existing pristine/allocated-bit
scans and complete ownership work remain separately accounted.

## Shared-root and hardware boundaries

Process root index one remains private and base-page-only. Every other root
entry continues to equal the immutable kernel entry exactly, whether that
entry is absent, a supervisor large leaf or a pointer to a shared subtree.
No process-root source or lifecycle policy needs to change.

### Existing MMU component

Retain the original store-to-text and execute-from-writable faults, their
exact PC/address/origin checks, two-trap condition, pass marker and shutdown.
Additional large-leaf probes use separate test state and cannot satisfy,
reset or weaken either original expected fault.

The test allocates only two exact `KERNEL_TEMPORARY` frames: a temporary root
and one middle table. It copies the real kernel root and adds read-only,
non-user aliases under previously absent root slots 4 and 5:

- a 2 MiB alias through the temporary middle table; and
- a 1 GiB root leaf.

Both point to aligned physical spans containing one known kernel sentinel.
Only that known valid physical location is read. These aliases are
test-only hardware probes, not production coverage of the remainder of
their spans or new mapping authority.

Use the existing checked activation assembly. Return to the real kernel
root before modifying either temporary PTE and before releasing any test
frame. Inactive test-root mutations separately misalign a lower PPN bit in
the 2 MiB and 1 GiB leaves. Each exact labeled load must produce cause 13,
the expected fault VA and PC, and one expected resume.

Add one zero-stack assembly leaf,
`micros_mmu_large_load_test_trigger(uint64_t virtual_address)`, with its
fault label on the single 64-bit load from the argument address and its
resume label on the return. Successful calls return the loaded value.
The same leaf serves both sizes; it changes no `sp`, `gp`, `tp`, saved
register, CSR or memory and makes no call. Fault handling uses separate
armed/handled state and separate counts, checks the same supervisor/hart/
interrupt conditions as the original test, and accepts each armed load
exactly once.

Restore the kernel root on every returning path. Unexpected hardware/trap/
readback failure is fatal, not successful cleanup. Release acquired test
frames in reverse order when cleanup is possible.

Before and after the probes, explicitly check that exactly the first
`table_count` owner/bitmap entries contain zero-identity `KERNEL_PAGE_TABLE`
owners. Combine that prefix proof with matching original scalar counts,
cursor and phase, complete owner validation, original root-entry equality,
released temporary owners and another independent mapping walk. This proves
restoration without a second multi-megabyte target snapshot.

### Target observation and acceptance

The immutable target fixture independently walks the actual installed root.
It checks complete base-page physical/permission coverage and leaf domain
containment without calling the production expected-mapping validator, and
records actual table and 4 KiB/2 MiB/1 GiB leaf counts.

The target derives its expected cover from linker domains and the validated
managed intervals, independently of production mapping/validation helpers.
The host oracle separately derives it from actual ELF linker bounds and
original FDT memory/reservation records: align memory inward, reservations
outward, subtract their union plus `[0,__kernel_end)`, and partition each
remaining domain by the Accepted leaf rule. Neither consumes reported
expected counts or candidate output as an expectation.

Every successful guard execution emits exactly these two newline-terminated
records, in this order:

```text
MICROS_KERNEL_MAPPING_OBSERVATION version=1 tables=0x0000000000000000 leaves-4k=0x0000000000000000 leaves-2m=0x0000000000000000 leaves-1g=0x0000000000000000 largest=0
MICROS_KERNEL_MAPPING_GUARDS aliases=2m,1g misaligned=rejected resources=preserved
```

The zero counts specify sixteen lowercase hexadecimal digits, not expected
values. `largest` is exactly `0` or `1`. Counts describe the installed real
kernel root, never the temporary aliases; `tables` must equal the existing
MMU-ready report. The observation follows ownership readiness. Only matching
topology (`largest=1`), both successful alias loads, both exact misalignment
faults and complete cleanup additionally permit:

```text
MICROS_KERNEL_MAPPING_TEST_PASS topology=largest aliases=2m,1g misaligned=rejected resources=preserved
```

These records precede the original MMU and TTY-controller pass records.
Unknown, missing, duplicate, malformed, unterminated or reordered records,
inconsistent counts and a false largest flag fail the current host oracle.
The ordinary `test-qemu-mmu` CMake recipe adds exact marker/ordered-pattern
requirements through the unchanged runner and runs the same ELF with
`--memory 128M --memory 256M`. Keep its eight-second per-guest timeout,
all original arguments and the TTY-controller scenario. No new runner
option, workflow or configure identity is added.

Baseline author execution uses the fixed #210 runtime plus the same bounded
immutable fixture extensions. It still runs both hardware probes, cleanup
and complete original MMU/TTY acceptance. A topology mismatch emits
`largest=0`, omits only the new pass marker and does not short-circuit those
guards or turn the original successful shutdown into a fixture failure.
The author driver records that mismatch as the named expected divergence.
It collects each memory run's raw `run_qemu` result through the existing
runner API; the ordinary multi-memory CLI's summary is not a substitute for
those transcripts. Missing guards, an unexpected trap, a build failure or
timeout is never a qualifying Red.

The original assembly/source/object proof stays active. Extend only
`tools/stack_bound_assembly.py`'s exact MMU test binding: independently compile
its fixed old assembly plus the declared load-probe extension and compare
the complete actual object. Bind the additional leaf as zero-stack/local,
require its exact load/return and fault/resume symbols, and retain every
old function/frame/call and source comparison. No arbitrary current source,
unknown assembly edge, stack-epoch release or budget waiver is admitted.

## Work and storage evidence

Let `g`, `gmax`, `p`, `P`, `t`, `T`, `b`, `L`, `R` and `E` have the meanings
in the research ledger. Count every invoked production primitive in the
claimed native operation; observations belong only to private generated
native translations.

| Work | Required account |
| --- | --- |
| Table allocation | Preserve real typed allocation and its `O(gmax+g)` work; add at most one bounded range resolution per new table |
| Mapping | At most three levels per emitted leaf; direct child/owner/index probes, no `t`-entry membership search |
| Physical table clearing | Exactly 512 entry clears per successfully created table |
| Child metadata preparation | Bounded direct-row initialization; count it rather than treating BSS or row clearing as free |
| Final table checks | Selected slot/owner/range/bitmap work plus `O(E*t)` tree inspection |
| Expected coverage | `O(R+L+t)` fixed-level domain/table lookup, not `O(b)` base-page expansion |
| Retained global validation | Existing `p` pristine/bit scans and complete `P` owner audit remain visible and are not claimed closed |
| Persistent metadata | Existing state plus exactly 515 KiB for the three specified arrays |
| Temporary storage | Fixed scalars, fixed three-level traversal and the existing 1024-byte reachability witness |

Use existing `KERNEL_MEMORY` observations and the unchanged 27-by-14 collector
ABI. PTE accesses, table/index probes, range/owner visits, bitmap loads,
clears and complete-validator calls remain distinct kinds. A host binding's
address translation is not counted as production `table_at` work, but every
actual production helper invoked above that binding is included.

Each valid case has five separate captures. The independent fixture prepares
any read-only walk witnesses outside the captured operation:

1. `pristine`: the actual initial allocator-pristine assertion;
2. `construct`: actual root/table allocation and mapping of all domains, with
   allocator/ledger preparation outside;
3. `validate`: the complete actual candidate validator, including its retained
   full owner audit and allocated-bit scan;
4. `tree`: the actual table-tree/reachability helper in isolation; and
5. `coverage`: the actual expected-domain and table-frame mapping helpers,
   immediately after a complete successful read-only validation.

The last two are separate read-only executions, not additional leaf costs to
sum into the inclusive `validate` result. The first three give the complete
measured portable construction/validation path; earlier ownership initialization
and boot BSS clearing remain separately identified costs. The public
privileged initialization sequence remains separately covered on target.

Let `m` be the independently observed number of middle tables and `Lreq`
the independent largest-leaf reference count. Required valid-case bounds are:

| Capture/kind | Bound |
| --- | --- |
| Pristine `BITMAP_WORD_LOAD` | Exactly `p`, retaining the original per-bit scan |
| Construction `INDEX_PROBE` | `32*(L+t+g+1)` |
| Construction non-clear `PTE_VISIT` | `4*L+2*t` |
| Construction `RANGE_VISIT` | `t*(2*gmax+4*g)+8*L+2*g+32` |
| Construction `RECORD_VISIT` | `t*(2*gmax+4*g)+16*L+16*t+128` |
| Construction `BITMAP_WORD_LOAD` | `8*(L+t+1)` |
| Construction complete-validator calls | Zero within this declared capture |
| Construction physical-page clearing | `512*t` words / `4096*t` bytes |
| Construction new middle-row clearing, current only | `512*m` halfwords / `1024*m` bytes |
| Full validation `RANGE_VISIT` | `4*gmax+4*t` |
| Full validation complete-validator calls | Exactly one |
| Tree-only `INDEX_PROBE` | `16*(t+1)` |
| Tree-only `PTE_VISIT` | `E*t` |
| Coverage-only `PTE_VISIT` | `3*(Lreq+t+4)` |

`L` and `t` come from an independent walk of the actual completed tree, not
unchecked production counters. `Lreq` comes only from the fixed input and
Accepted leaf-selection contract. The two clear kinds are recorded at their
actual stores; current total clear units/bytes therefore equal
`512*t+512*m` and `4096*t+1024*m`. Baseline has no fabricated metadata-row
clearing and retains `512*t` / `4096*t`.

The other full-validation visits and bitmap work remain measured and
reported as retained diagnostic cost, not hidden behind the selected-work
claim. Freeze the complete old/current primitive inventory, call closure
and exact capture placements before production edits. A counter cannot
label a linear scan as one direct operation or count the same read as two
leaf contributors of the same kind.

The producer label is `0x4d454d3032000000`, owner mask `4`, with capture
labels 1 through 85 in case order and the five-operation order above.
All 27-by-14 entries remain present. Unknown nonzero owners, duplicate or
missing labels, overflow, partial captures and execution failure invalidate
the complete result.

### Native source binding

Add exactly these evidence owners:

```text
tests/host/kernel_mapping_test.c
tests/host/kernel_mapping_test.h
tests/host/kernel_mapping_source.py
tests/host/kernel_mapping_evidence.py
tests/host/kernel_mapping_authority.py
tests/host/test_kernel_mapping.py
tools/generate_kernel_mapping_fixture.py
tools/check_kernel_mapping.py
```

The native fixture compiles actual selected constructor/validator functions,
the actual portable codec, allocator, all ownership sources and the required
existing object/scheduler source groups. Fixed mode uses #210 bodies and
headers; current mode uses actual current bodies and headers.

Only physical table access and numeric linker/runtime-environment bindings
are native substitutions. Bind table addresses to bounded test-owned backing
pages; compile the existing pure allocation/ledger wrapper closure against
real portable state. Do not compile a fake CSR, MMIO or `satp` implementation
and claim it as hardware evidence. Public privileged initialization remains
target-tested.

The baseline constructor closure is `linker_layout_is_valid`,
`table_frame_index`, `allocate_table`, `map_page`, `map_identity_range`,
`map_kernel_image`, `map_managed_memory` and the read-only report accessor.
The validator closure is every function in the fixed validation owner.
The fixture orchestrates root allocation and the exact domain-call order;
it does not reimplement a mapper. Do not compile or simulate the public
privileged initializer in that native fixture.

Extract the exact bootstrap getters, typed allocate/release wrappers and
`ownership_mutation_is_allowed`. Bind their existing readiness/state objects
to the native allocator/ledger and the IRQ-read operand to a declared Boolean
environment input, normally false. The two `table_at` bodies are the sole
physical-access substitutions. Numeric linker operands identify fixture
boundaries without dereferencing invented addresses. Compile the actual
codec and the fixed `MICROS_FRAME_OWNERSHIP_SOURCES`,
`MICROS_KERNEL_OBJECT_CORE_SOURCES` and `MICROS_SCHEDULER_CORE_SOURCES`
memberships. Include every invoked production helper rather than replacing
allocation, geometry, owner or bitmap checks with test equivalents.

Freeze every selected function, helper closure, binding and observation
location before production edits. A missing helper, copied candidate result,
unexpected physical access, changed source skeleton or incomplete capture
fails. Baseline has no fabricated new child-index arrays.

Static native storage owns pages, large state and snapshots. No new target
stack-sized object is introduced. Use ASan/UBSan and verify ordinary native
execution has no observation state.

Add only native executable `kernel_mapping_test` and CTest `kernel-mapping`
to the two existing native tiers. Its counted mode requires both
`MICROS_TEST_WORK_COUNTS=1` and `MICROS_TEST_KERNEL_MAPPING_COUNTS=1`;
other explicit values fail. Only generated native subjects receive those
observations. The ordinary counterpart has neither collector state nor
observation calls. Keep all source/header generation dependencies explicit,
as in MEM01, rather than adding a depfile namespace outside the existing
reusable profiles.

### Required input families

Use page size `4096`. The default numeric linker domains are
`[0x80200000,0x80201000)` RX, `[0x80201000,0x80202000)` R, and
`[0x80202000,0x80203000)` R/W. Device inputs are the four exact production
pages. Native allocator inputs below contain no page overlapping those
kernel domains.

The default managed base is `0xc0000000`, aligned to 1 GiB. The small
base-page shape needs nine tables: one root, two kernel-domain tables, two
managed-domain tables and four device tables. Counts one through eight
are therefore explicit allocation-failure cases, not successful boundary
fixtures.

The fixed valid matrix has seventeen cases:

| Cases | Exact input |
| --- | --- |
| 1-10 | One default-base managed interval with `N` in `{9,63,64,65,511,512,513,4096,262143,262144}` |
| 11 | One interval at `0xc01ff000`, 65 pages, crossing a 2 MiB boundary |
| 12 | One interval at `0xbffff000`, 513 pages, crossing a 1 GiB boundary |
| 13 | The same start, 514 pages, adding the final partial extent |
| 14 | Default-base 4096 pages; default text/rodata, writable end `0x80400000`, so one 2 MiB kernel span contains three permissions |
| 15 | Writable end `0x80401000`, managed base `0x80401000`, 4096 managed pages; the adjacent R/W domains must not coalesce |
| 16 | Sixteen pages at `0xc0000000` plus one page at `(1 << 38)-4096`; exercise the final lower-canonical root index 255 and middle/leaf indexes 511 |
| 17 | Memory interval `i` starts at `0xc0000000+i*(1 << 30)` and has eleven pages, for `0 <= i < 16`; reserve odd offsets 1/3/5/7/9, reverse both input arrays, and require 96 one-page managed intervals |

The last case uses the portable constructor's 80-reservation capacity,
not a fabricated FDT case or an extra 81st kernel reservation. All its
memory is already above the numeric kernel end.

All five captures execute for every valid case, giving 85 complete snapshots
per source stage with fixed increasing labels. Independent semantic and
malformed-state guards run outside those captures. Preserve the baseline
header/state shape instead of adding new child arrays to old code.

Current-only cases corrupt child IDs, range hints and their relation to
real PTE/owner state. Common cases cover wrong permissions, absent/extra
leaves, unaligned upper-level PPNs, foreign/free table frames, aliases,
cycles, orphan tables, bad counts, empty/unaligned/overflowing intervals,
allocation exhaustion and the unchanged capacity guard. Check rejection
before unsafe native backing access and preserve failed lookup outputs.
Partial boot allocation failures retain the documented unpublished state,
not an invented full rollback.

The independent reference derives per-level leaf sets from the input domains
and arithmetic alignment/containment, then derives the required table-coordinate
sets. It does not call production selection or validation helpers. Check every
covered base page's physical address/permission, exact holes and all observed
table/leaf counts outside the work captures. Real activation/readback and
publication ordering remain target and retained-source obligations, not
native fake-CSR claims.

## Source and evidence authority

Production changes are confined to:

```text
kernel/memory/address_space.c
kernel/memory/address_space_validation.c
kernel/memory/address_space_internal.h
```

Keep the public map/report/codec interfaces, shared/private user-root
implementations, allocator and ownership implementations unchanged.
Retain construction/publication and validation responsibility ownership.
Freeze the original definition/type/storage register before implementation;
new helpers must have one of the specified selection, identity, link,
reachability or coverage responsibilities.

The existing MMU C/assembly fixtures receive only the independently declared
test extension. An exact finite inverse must recover their complete
original source and assertions. Their old transport values stay active.

Use transition `MEM02-kernel-mapping`, task `MEM02`, primary B02 and findings
exactly `{B02-F003, B02-F004}`. Retire only:

```text
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/address_space.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/address_space_validation.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/address_space_internal.h/2
```

The current inventory gives each key exactly its physical owner and
`tests/host/kernel_layout_source.py` as payload. Together with the narrow
directory assertion adapter, the candidate set is exactly those three
production paths plus:

```text
tests/host/kernel_layout_source.py
tests/host/test_kernel_directory_layout.py
```

Every other merged-design member and all eight new evidence owners are
immutable within one actual attempt, including MMU fixtures, build recipes,
models, source inverses, Python owner inventory and documentation.
Finalize their integration before Red. Any immutable change renews Red and
Green; any post-Green source change blocks recertification.

Register exactly three oracles in
`tests.host.kernel_mapping_evidence.KernelMappingOracles`:

| Oracle | Test method | Property and coverage |
| --- | --- | --- |
| `mem02-large-leaf-topology` | `test_large_leaf_topology` | Exact largest-leaf native topology and semantic coverage; constructor and private-state identities |
| `mem02-direct-table-work` | `test_direct_table_work` | Direct membership/owner/reachability work with unchanged selected-state safety; validator and private-state identities |
| `mem02-target-mmu-baseline` | `test_target_mmu_baseline` | Actual old/current kernel topology plus original faults, large-leaf hardware and cleanup guards; all three identities |

All three must fail on actual fixed baseline execution for their named
audited divergence and pass on current execution. Hardware positive
controls are not misreported as discriminating failures.

Preserve all 3,946 existing complete records, fifteen fingerprints and
COPY01/PLAT01/MEM01 committed claims before adding one
`kernel-mapping-baseline` family:

| Owner | Kind/scope | Class |
| --- | --- | --- |
| `kernel_mapping_authority.py` | `design-anchor:MEM02` | `TRANSPORT_IDENTITY` |
| `kernel_mapping_source.py` | `source-membership:MEM02` | `PERMANENT_INVARIANT` |
| `kernel_mapping_source.py` | `artifact-contract:MEM02-recipes` | `ARTIFACT_CONTRACT` |
| `kernel_mapping_source.py` | `state-work-contract:MEM02` | `PERMANENT_INVARIANT` |
| `kernel_mapping_source.py` | `source-projection:MEM02` | `TRANSPORT_IDENTITY` |
| `kernel_mapping_authority.py` | `baseline-oracle:mem02-large-leaf-topology` | `BASELINE_ORACLE` |
| `kernel_mapping_authority.py` | `baseline-oracle:mem02-direct-table-work` | `BASELINE_ORACLE` |
| `kernel_mapping_authority.py` | `baseline-oracle:mem02-target-mmu-baseline` | `BASELINE_ORACLE` |

Both owners are under `tests/host/`; the resulting total is 3,954 facts in
sixteen families. Independently derive complete records from the merged
design/fixed reference before editing their owners or extractor.

Use the existing owner-prefixed identity spelling. Every new fact has primary
B02 and consumers B01-B12/B14, plus its own owner and the three MEM02 documents
in its payload. Bind the design anchor to the actual merged design pins;
source membership to the five candidate/eight addition paths; recipe policy
to CMake/presets, collector headers, exact source-group members, fixture,
generator and MMU/stack integration; state/work policy to the three production
owners, native fixtures, host oracle and observation headers; and projection
policy to candidate paths, shared C inspector, exact old source gates and
finite inverses. Each oracle additionally owns the complete eight-path
evidence addition set. Values hash these independently fixed declarations,
never current algorithm output.

The authority helper handles only these three keys and delegates all others
to the existing MEM01 chain. Compose exact source/recipe inverses before
older gates, not by replacing their hashes or weakening namespaces.

The finite immutable integration register contains CMake/native tiers and
MMU arguments; source/recipe/evidence-owner registration; the existing Python
runtime-user inventory; exact MMU C/assembly extension transport; the one
assembly-frame admission above; and directly related documentation. Existing
collector, planner, runner, parser, profile and historical expectation
algorithms stay unchanged. Any necessary namespace inverse must retain exact
old member equality and reject unknown additions. Verify complete old fact/
fingerprint conservation and actual reusable-profile eligibility before Red,
not after the final expensive gate.

Keep the original pristine and allocated-bit algorithms, public initializer
check/activation/publication order, report accessor, codec and unmodified
allocator/ownership code as retained source obligations. Mutable constructor
helpers own level selection, checked table allocation/identity and leaf links;
mutable validator helpers own direct table checks, reachability and span-based
coverage. New helpers stay within those responsibilities and ADR-0053's
acyclic declarations-only private boundary.

## Actual execution and completion

The implementation's closed author intents are:

```text
${MICROS_PYTHON} -E -S -B tools/check_kernel_mapping.py --red
${MICROS_PYTHON} -E -S -B tools/check_kernel_mapping.py --green
${MICROS_PYTHON} -E -S -B tools/check_kernel_mapping.py --recertify
```

The actual merged design commit/tree binds source, commands, declarations
and the complete immutable environment. No caller-supplied root, command,
expected result or snapshot authorizes a transaction.

Recertification replays actual Green, enters one exact root/plan/PID
provisional scope for only the three named assertions, and records every
actual result:

| Unit | Canonical recertification command |
| --- | --- |
| B01 | `cmake --workflow --preset test-qemu-frame-allocator` |
| B02 | `cmake --workflow --preset test-qemu-mmu` |
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
| B14 | The actual `--recertify` invocation above, including retained authority/source/recipe/Python-owner checks and complete native/target oracle replay |

Clear provisional state on every exit. Failure or interruption publishes no
usable terminal event. Only complete success derives `RETIREMENT_READY`.
Before the first commit, only that current terminal journal authorizes the
three retirements; afterward only the exact independently reviewed committed
declaration supports ordinary journal-free checks.

Then run the full fresh 50-command author gate, retaining 44 static
configurations, 113 ELF outputs, ten observed scopes and blocked B13.
Preserve real native/smoke/TTY profile eligibility without changing the
profile checker. Obtain independent implementation review before the first
permanent commit, verify a fresh committed journal-free checkout and exact
reviewed/committed/remote/preview/squash tree equality, then clean only the
completed task refs.

This design phase authors no executable artifact and claims none of those
future gates has run.

## Implementation map

The separate implementation retains the three specified production owners.
Construction selects a domain-contained leaf and follows checked child slots;
validation owns the shared selected-frame check, complete reachability and
span-based expected coverage. The private header contains only the three
arrays and the cross-owner helper declaration. Complete owner validation,
the original per-bit scans and publication/activation order remain unchanged.

The eight declared evidence owners now supply actual-source native bindings,
the independent seventeen-case model, strict observations and target parsing,
finite source/recipe transport and the three-phase authority transaction.
The existing MMU C/assembly fixture preserves both original faults while
using two temporary frames for its separate large-leaf controls. Its new
assembly leaf is bound to the exact RV64 load/return bytes and zero stack;
the original source/object proofs remain active.

The first genuine pre-production Red used staged tree
`2dd9e3dcba887d251f8e58e9cdf631b2dc645142`, with all three production files
still identical to #210. All three named oracles failed as expected, while
85 native snapshots and both successful original/large-leaf hardware-control
runs completed. Later immutable-binding changes renew that transaction;
the initial Red is retained as history rather than borrowed as final
acceptance. The complete current gates and independent implementation review
remain required before either finding closes.

# MINIX Kernel Mapping Topology Baseline

## Status and scope

This is the documentation-only MEM02 study. It owns B02-F003, B02-F004 and
their B02-E003 evidence boundary in the
[accepted correction DAG](minix-complete-baseline-findings-and-correction-dag.md).
The separately merged MEM01 implementation #210 makes this task ready.
No executable prototype, test, model, fixture, generator, build scaffold or
runtime change belongs to this research phase.

The fixed references are:

```text
micros commit: 3f7e33026f92c8c1765ce00c2b03b5c2d6855197
micros tree:   08f31a8b1dfc639c8b826ea65d092327fea159a1
MINIX commit:  4db99f4012570a577414fe2a43697b2f239b699e
```

The inspected MINIX files were compared to the fixed commit before this
study. They are behavioral and structural evidence, not source to copy.
The RISC-V reference is the existing
[20260120 Supervisor specification](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html#sv39).
Its Sv39 and translation-algorithm sections were read directly.

MEM03 still owns complete ownership-validation cadence. MEM04 owns ordinary
process lookup and root activation. VM mapping policy, private user large
pages, the VM bitmap/cache, release/handoff indexing, hosted CI and node Q
remain outside MEM02.

## Fixed MINIX source ledger

| Source | Operation or representation | Evidence |
| --- | --- | --- |
| `minix/kernel/arch/i386/pg_utils.c:18-19,123-134` | Bootstrap directory and small-table pool | One aligned 1024-entry directory and six aligned 1024-entry tables; indexed allocation from the fixed pool |
| `minix/kernel/arch/i386/pg_utils.c:161-202` | `pg_identity`, `pg_mapkernel` | Identity directory entries and kernel mappings use 4 MiB leaves rather than allocating a lower table for every base-page interval |
| `minix/kernel/arch/i386/pg_utils.c:267-310` | `pg_map` | Base-page mappings use VPN-derived directory/table slots and retain the current table across a sequential range |
| `minix/kernel/arch/earm/pg_utils.c:21-22,113-126` | Bootstrap directory and small-table pool | One aligned 4096-entry directory and 24 aligned 256-entry tables |
| `minix/kernel/arch/earm/pg_utils.c:157-203` | `pg_identity`, `pg_mapkernel` | Identity and kernel mappings use 1 MiB section entries |
| `minix/kernel/arch/earm/pg_utils.c:264-310` | `pg_map` | VPN-derived directory/table access and a retained current table for base-page mapping |
| `minix/servers/vm/pt.h:10-24` | `pt_t` | Separate directory virtual/physical addresses and one directory-indexed `pt_pt` pointer array |
| `minix/servers/vm/pagetable.c:494-539` | `pt_ptalloc` | Allocate and clear a missing table, install its direct directory-indexed software pointer, then install the hardware directory entry |
| `minix/servers/vm/pagetable.c:545-585` | `pt_ptalloc_in_range` | Visit only the directory positions covering a requested range and prepare missing tables before mapping writes |
| `minix/servers/vm/pagetable.c:784-925` | `pt_writemap` | Derive the directory and table indexes from each requested virtual address and write through `pt_pt[pde][pte]`, without searching a physical-frame list |
| `minix/servers/vm/pagetable.c:1370-1421` | `pt_bind` | Bind one exact root and its directory mapping through the privileged VM control boundary |
| `minix/servers/vm/pagetable.c:1442-1499` | `pt_mapkernel` | Install large kernel leaves in every process directory, retain shared directory mappings, then add separately specified kernel mappings |

### Large pages are a mechanism, not permission to copy broad mappings

The x86 bootstrap identity directory spans its directory capacity and includes
user/write flags. The ARM bootstrap similarly covers its directory and
distinguishes cached memory from device regions using target constants.
The kernel high mappings do not reproduce the exact `micros` linker W^X
partition.

Those address, permission and cache-policy choices are not requirements for
`micros`. Its existing supervisor-only identity map must continue to exclude
firmware reservations, pre-kernel memory, gaps and unrelated MMIO. The useful
baseline is large aligned leaves plus direct page-directory/table indexing,
not broader access or rounded-up permission domains.

The reference's ordinary VM `pt_writemap` is deliberately a base-page path.
It rejects a large directory leaf where it expects a lower table. MEM02 must
not infer that every process mapping should become large merely because the
kernel bootstrap uses large leaves.

### Direct software metadata accompanies hardware entries

`pt_t` does not recover a table pointer by linearly searching all allocated
physical table frames. The directory slot directly selects both the hardware
entry and `pt_pt[pde]`.

This distinction matters on Sv39: a software child identifier indexed by the
parent VPN coordinates can retain the same direct relation while using bounded
slots rather than raw pointer authority. It is not a cache whose absence may
silently fall back to a scan. Hardware PTEs and software child identities must
agree before a child is dereferenced.

MINIX's bootstrap fixed pools are not an excuse to bypass the `micros` typed
ledger. Every real kernel page-table frame must still be allocated as
`KERNEL_PAGE_TABLE`, cleared before linking, reserved in the availability
bitmap and reached exactly once.

### Reference failure boundaries

VM prepares missing tables before writing a requested mapping range.
If preparation fails, the reference can retain earlier consistent page tables;
it does not claim whole-operation rollback of every preparation allocation.
Its `pt_ptalloc` also handles a recursive VM-allocation side effect before
publishing another table.

The `micros` boot constructor is a different, serialized one-shot boundary.
It may retain an unpublished partial candidate when boot initialization
fails, and boot then fails rather than retrying or publishing that root.
MEM02 must describe that existing boundary honestly, preserve all checks
before activation, and not import recursive VM allocation or recovery.

## Sv39 constraints

Sv39 has three levels, 512 eight-byte PTEs per table and a 4096-byte table
frame. The natural leaf sizes at levels zero, one and two are 4 KiB, 2 MiB
and 1 GiB.

The specification requires virtual and physical alignment to the selected
leaf size. A nonzero lower PPN portion at an upper-level leaf is a misaligned
superpage and raises a page fault. Translation combines the high PPN portion
with the lower VPN bits and unchanged base-page offset.

The existing local `sfence.vma`, `satp` write, `sfence.vma` and readback
sequence remains sufficient for the one-hart, ASID-zero target. MEM02 does not
add active process-root mutation, selective fencing, global mappings, new
memory-type encodings or Svnapot.

The existing portable codec already distinguishes a leaf by its permission
bits and decodes its physical base without a level argument. Its public
4 KiB alignment contract need not be widened or replaced: the kernel mapper,
which knows the traversal level, must enforce the additional selected-leaf
alignment and offset rules. Ordinary user mappings remain level-zero leaves.

## Current micros source ledger

| Source | Current responsibility or constraint |
| --- | --- |
| `kernel/memory/address_space.c:35-58` | Physical table access and linear `table_frame_index` membership search |
| `kernel/memory/address_space.c:94-153` | Typed table allocation, physical/PTE validation, complete table clearing and append-only table registration |
| `kernel/memory/address_space.c:155-260` | Two intermediate walks followed by a level-zero leaf; existing children require linear frame-list lookup |
| `kernel/memory/address_space.c:263-295` | `map_identity_range` advances by 4096 bytes |
| `kernel/memory/address_space.c:297-368` | Three linker domains and every canonical managed interval |
| `kernel/memory/address_space.c:370-484` | One-shot construction, exact device pages, complete candidate validation, activation and report publication |
| `kernel/memory/address_space_internal.h` | One private state owner, 1024-frame table bound, append-only physical table list and two-scalar public report |
| `kernel/memory/address_space_validation.c` | Separate complete ownership, reachability and expected-mapping validation; repeated physical-frame searches and base-page expected walks |
| `kernel/memory/sv39.c` | Existing canonical-address, VPN, PTE encoding and decoded permission rules |
| `kernel/memory/bootstrap_memory.c:503-547` | Exact typed runtime allocation/release wrappers; these do not invoke a complete owner audit on each allocation |
| `kernel/memory/user_address_space.c:178-196,244-266` | Copy exact nonzero kernel-root entries outside private root index one |
| `kernel/memory/user_address_space_validation.c:525-553` | Compare every shared root entry exactly; only the separate private subtree is constrained to base-page user leaves |
| `kernel/tests/memory/mmu_test.c` | Original store-to-text and execute-from-writable faults, exact trap identities and two-trap success condition |
| `arch/riscv64/mmu_test.S` | Original labeled fault and resume instructions |

### Merged MEM01 changes the allocation baseline

The old B02 audit remains fixed historical evidence. Current #210 no longer
has quadratic successful-prefix bitmap search. The table constructor starts
from a pristine allocator and performs only successful kernel-table
allocations on its valid path. Under the retained lowest-address rule, table
allocation ordinal `i` therefore names compact managed-frame index `i`.

This relation is a consequence of validated pristine state and exclusive,
serialized construction, not authority inferred from the allocation cursor.
A future direct index must independently match the actual returned physical
address to its checked managed-range descriptor and exact typed owner.

Typed allocation still performs the existing bounded geometry comparisons
and physical-to-index lookup. Their cost remains visible. MEM02 must not
attribute the inherited `O(gmax + g)` allocation work to a new direct-index
mechanism or claim the later ownership-cadence correction complete.

### Existing mapping domains

The complete production mapping set is unchanged:

- text RX;
- read-only data R;
- writable kernel state RW/NX;
- each canonical managed interval RW/NX;
- the UART page at `0x10000000`;
- the PLIC priority page at `0x0c000000`;
- the supervisor-enable page at `0x0c002000`; and
- the supervisor context page at `0x0c201000`.

All leaves have `U=0`, `G=0`, the existing A/D policy and zero RSW/reserved
bits. The named adjacent PLIC pages remain absent.

Mapping-domain boundaries and per-frame ledger kinds are different.
Allocating a page table from managed RAM does not change that RAM's existing
supervisor RW/NX direct-map permission. Free frames, kernel tables and later
typed allocations inside one managed interval do not require new direct-map
splits merely because their semantic owners differ. A large leaf must,
however, remain inside one admitted linker, managed-RAM or device interval;
it cannot cover a reservation, gap or neighboring domain.

### Shared roots already preserve entries, not a particular leaf depth

The user-root constructor and shared-entry validator copy and compare the
kernel entries outside index one verbatim. They do not walk those entries as
private user mappings. A supervisor large leaf can therefore remain shared
without changing process, endpoint or thread identity, or permitting large
private user leaves.

ADR-0021's description of shared entries pointing to subtrees must be refined
explicitly to include an immutable supervisor leaf. Its private-subtree,
generation, owner, SUM, mutation, activation and teardown contracts remain.

## Logical work and storage

Use `g` managed intervals, `gmax` their capacity, `p` managed base pages,
`P` owner capacity, `t` allocated kernel table frames, `T` table capacity,
`b` required mapped base pages, `L` emitted leaf entries, and `R` mapping
domains. Hardware radix width is `E=512` and depth is three.

| Operation | Fixed reference | Current #210 micros |
| --- | --- | --- |
| Large kernel mapping | One direct directory store per large leaf | One mapper call per 4 KiB page |
| Existing child resolution | Direct directory/table-indexed pointer | Linear search through `table_frames` on each intermediate walk |
| New table | Fixed-pool or VM-owned table allocation, clear before link | Actual typed allocation, MEM01 word search, existing geometry work and 512 clears |
| Mapping after allocation | Fixed-level/direct indexing, linear in requested entries | `O(b*t)` table-membership work |
| Table reachability | Hierarchical table identity | `O(t^2)` physical-frame list search in the final tree walk |
| Final table ownership | Selected metadata | `O(t*gmax)` lookup and range checking after a complete owner audit |
| Expected kernel coverage | Large mappings plus separately requested ranges | One fixed-depth lookup per required base page, `O(b)` |
| Root publication | Selected root binding and invalidation | Existing fixed fence/write/fence/readback |

Current successful table allocation is `O(t*(gmax+g) + 512*t)`, after MEM01,
not the old audit's quadratic bitmap-prefix term. The current kernel
pristine and final allocated-bit checks also scan `p`; complete ownership
validation still covers `P`. These retained costs must be reported separately
from the corrected mapping and table-index terms.

The reference x86 bootstrap reserves 4 KiB for its directory and 24 KiB for
its six-table pool. ARM reserves 16 KiB plus 24 KiB for its small-table pool.
VM adds one pointer per directory entry in `pt_t` and allocates actual table
storage separately. The `micros` adaptation must account explicitly for
direct child-index metadata, retained table/owner records, constructor
temporaries and the existing 1024-byte automatic reachability witness.
Static capacity is not zero cost or a reason to omit its storage from the
ledger.

The Accepted parent-index metadata adds exactly 515 KiB of BSS and 65,920
stores in `arch/riscv64/boot.S:7-13`'s existing eight-byte clear loop. At the
current QEMU sizes, this fixed software representation can cost more memory
than the hardware table pages it saves. This is the explicit fixed-bound
Sv39 adaptation, not a net memory-saving claim. Native evidence separately
measures initial pristine scanning, construction and complete validation;
tree and coverage submeasurements are not added again to that total.

After correction, valid construction plus complete validation is bounded by
`O(p + P + T + R + E*t + t*(gmax+g) + L)`, with `E*m` child-row clearing
included in `E*t`. Initial BSS clearing adds the separate `O(E^2+E+T)`
metadata cost. Direct selected-child lookup is worst-case `O(1)`, not an
average hash or amortized claim. New persistent index storage is
`O(T+E+E^2)`; the existing physical tables use `O(E*t)` words and validation
retains its `O(T)` reachability witness. The retained `p`/`P` diagnostics can
still dominate total boot work; only mapping and table-index terms become
leaf/table-linear.

## Required design dispositions

The design must settle these matters before implementation:

1. Select the largest naturally aligned Sv39 leaf that fits wholly in the
   remaining admitted domain, without implicit runtime splitting or merging.
2. Retain the reference parent-indexed software/hardware table relation,
   using checked bounded identities rather than linear physical-frame search.
3. Keep the actual typed allocator and explicitly account for its retained
   geometry work; prove any allocation-ordinal relation from the pristine,
   exclusive construction phase.
4. Validate child identity, physical address, range bounds and exact ownership
   before dereference; reject cycles, aliases, orphan tables and extra or
   missing mappings before activation.
5. Distinguish an unpublished partial boot failure from a reversible runtime
   mutation. Add no retry, repair or recovery framework.
6. Preserve public report/error interfaces, private user leaves, full
   ownership-validation cadence, shared-root equality and every resource
   budget unless a specific required refinement is independently accepted.
7. Prove actual portable work from real fixed/current source and actual MMU
   behavior through QEMU. A host binding is not evidence that a CSR, TLB or
   hardware page walk executed.
8. Retain all original two-fault MMU evidence. Test larger leaf translation and
   misalignment without broadening the production mapping set or treating a
   test-only alias as production authority.
9. Keep old source assertions active until the independently specified,
   granular transition has real Red, Green and affected-unit recertification.

## Design difference classifications

| Mechanism | Classification | Reason |
| --- | --- | --- |
| Large aligned kernel leaves | Baseline parity | Reproduce the reference bootstrap mechanism instead of expanding every range into base pages |
| Sv39 sizes, three levels and exact existing identity layout | Required adaptation | RV64 target and retained kernel pointer/entry contract |
| Parent-indexed bounded child identities | Required adaptation | Preserve direct lookup while replacing VM pointers with kernel-owned checked slots |
| Exact W^X, reservation exclusion, typed ownership and non-user leaves | Compatible extension | Retain stronger safety without a per-leaf linear table-frame search |
| Base-page-only private user mappings | Baseline parity | Retain the reference ordinary base-page VM path; the separate fixed user window and broader VM policy remain outside this correction |
| Ordinary complete owner/root validation | Divergence requiring correction | Remains MEM03/MEM04 rather than an implicit part of MEM02 |
| Native primitive counts and isolated hardware observations | Compatible extension | Independent test-only evidence, not new runtime telemetry or authority |

These are design constraints and source evidence. They do not declare a
completed mapping implementation or authorize code before separate design
review, Acceptance and merge.

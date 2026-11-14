# MINIX VM Mapping-Plan Uniqueness Study

## Fixed inputs

```text
micros source: cb865a7623d8558dbc5f6558c2dacb4be4a6b7fe
micros tree:   036b98055822e03ce47ffdc63a109831f994412a
MINIX source:  4db99f4012570a577414fe2a43697b2f239b699e
```

Both repositories were checked clean at these revisions. Reference research
uses the local `/Users/alephling/Repo/minix` checkout read-only. VM01 #272
and MEM04 #228 satisfy VM02's actual predecessors.

VM02 owns B02-F008, B02-F009 and B07-F004. This study supports the
[Accepted contract](../architecture/vm-mapping-plan-uniqueness.md), not
implementation or finding closure. VM03's retained load plan, VM04's allocator,
VM05's movement primitives and VM06's snapshot lifetime remain separate.

## Fixed reference mechanisms

| Reference | Mechanism and bound |
| --- | --- |
| `minix/servers/vm/utility.c:84-94`, `vmproc.h:14-34` | Derive the process-table slot from an endpoint, bound it, compare the complete endpoint and require `VMF_INUSE`: constant selected identity work. The slot is not sufficient authority. |
| `region.h:21-68`, `phys_region.h:8-24` | A process owns an ordered region tree; a region owns a page-offset array of physical-region references. Physical blocks retain explicit reference lists/counts. This is ownership structure, not repeated cross-process equality search. |
| `region.c:60-93,616-640` | AVL predecessor lookup selects a region in logarithmic region-count work; page offset then directly selects its physical reference. Mutation updates the selected region/process accounting. |
| `pagetable.c:784-946` | `pt_writemap` prepares required tables before stores, then visits the requested pages through selected page-directory/table indexes. It does not certify every other process root. Work is linear in requested pages plus required table preparation. |
| `exit.c:33-94`, `region.c:589-605` | Resolve the exact announced exiting process, release that process's regions/tables and clear its slot. Region removal retains its tree work; teardown is not a constant-time whole command. |
| `alloc.c:32-51,496-545` | The allocator indexes a physical-page bitmap. Separate optional sanity checking clears a physical-page map and detects repeated pages by direct index, rather than pairwise comparisons. The fixed VM build disables this global sanity machinery. |
| `lib/libc/stdlib/radixsort.c:43-236` | The imported reference also supplies unstable in-place MSD radix distribution, an explicit partition stack and largest-partition-last processing. Its string translation, insertion cutoff, large histogram/stack and recursive overflow fallback are not required for fixed unsigned integer keys. |

The VM baseline is selected process/page authority and indexed ownership.
The libc radix routine is a separate reference for the representation
adaptation below; it is not misrepresented as MINIX's VM allocator.
Neither source is a donor for `micros` code.

## Actual remaining current work

VM01 removed ordinary complete VM/BOOT/PM audits, not the following
algorithms. Its final existing ELF-spawn guest still performs 199 complete
memory-root walks for 34 dynamic kernel commands.

| Current owner | Remaining mechanism |
| --- | --- |
| `user_address_space_vm_scratch.c` | Map and removal acquire full scratch, clear active-capacity bitmap prefixes and validate every process root before the already checked MEM04 leaf-slot walk. |
| `user_address_space_vm_freeze.c` | Freeze performs the same all-root preflight. Installation compares the constructed tables, attaches the root and invokes another generic complete root/owner certificate during the valid pre-FROZEN transient. |
| `user_address_space_vm_teardown.c` | Frozen abort and detached exit validate all roots before checking the selected record's data/table owners and exact four-table contents. |
| `user_address_space_validation.c::micros_user_address_space_vm_table_plan_matches_memory` | For each of 512 hardware slots, search every planned leaf. The exact table comparison can instead consume the already ordered plan once. |
| `frame_ownership_validation.c`, `frame_ownership_vm_release.c` | Every candidate frame performs a preceding-index duplicate search. Physical/index conversion also scans active ranges. Prepared plans already retain normalized frame indices and preserve original input order. |
| `user_address_space_vm_plan.c` | The portable plan checks table/table, data/table and preceding-data equality before publishing its candidate. Its per-item error order must survive replacement. |
| `lib/vm/vm_boot_validate.c` | Mapping import searches address-space prefixes and all preceding physical mappings. Active geometry is already sorted and checked. |
| `kernel/vm/vm_handoff_core.c` | Local record shape and complete physical/token uniqueness use nested comparisons. The portable state has physical addresses and summary counts, not a managed-range catalog. |
| `vm_handoff_scratch.c`, `vm_handoff_freeze.c` | Ordinary physical collision queries search every mapping record, although the actual frame ledger can identify the selected owner/process. |
| `servers/vm/vm_validation.c` | Complete validation compares each entry and identity with preceding records. Local record entries are already strictly physically ordered. |
| `servers/vm/vm_frames.c`, `vm_load.c` | Lowest-frame construction and binding repeatedly search all records for physical membership. The policy remains lowest eligible frames; replacing its membership query is distinct from VM04's bitmap/cache allocation policy. |
| `servers/vm/vm_exit.c`, `kernel/vm/vm_handoff_release.c` | Exit selection scans all slots despite carrying the exact load token and other generation/phase identities. |

The cold `vm_snapshot_validation.c` inventory already advances one mapping
cursor through the expected per-process prefix. It is not another quadratic
physical search to rewrite.

### Existing authority that can be reused

MEM04's actual `micros_user_address_space_select_path` checks each consumed
table owner before dereference and checks the selected exceptional leaf.
`locate_leaf_slot_locked` already uses that path; VM02 need not create
another walker.

MEM05 already maintains exact process-slot owner chains and the two
49,168-byte indexed selections in the memory owner. Its selected lease
clears only published words. An exact planned frame set and the selected
generation's validated owner chain can replace a managed-frame census.
Neither a count nor a bitmap bit alone authorizes physical access.

The existing completeness limitation remains important: a selected chain
cannot discover a separately corrupted, unindexed and unreachable foreign
owner. Complete diagnostics retain that independent domain check.

### Error and commit boundaries

Scratch/freeze portable rejection precedes the real ownership and memory
preflights. A healthy already-recorded scratch frame is `STATE`; a conflicting
freeze table is `ARGUMENT`. Other invalid physical ownership reaches the
existing fatal authority boundary. An owner-directed query must preserve
these distinctions, not turn every occupied frame into ordinary absence.

The kernel freeze order is table construction, owner publication, root
installation, the memory postcondition, then VM FROZEN publication. Teardown
detaches and clears mappings before freeing their owners. No proposed index
permits reversing those orders or reauthorizing PM/VM inside the freeze
transient.

## Storage evidence and representation choice

The complete #272 certificate covers 44 static configurations, 113 ELFs and
ten observed scopes. Reading every existing VM image and its compiler-derived
certificate, without rebuilding, gives:

| Boundary | Current maximum |
| --- | ---: |
| VM image, including its external stack | 154 of 160 pages |
| VM ordinary/metadata stack proof | 3,104 of 4,096 bytes |
| VM observed stack proof | 3,744 of 4,096 bytes |
| ELF-spawn kernel supervisor proof | 10,512 of 16,384 bytes |

The limiting observed VM image has six pages of total image headroom and
only 352 bytes of stack margin on its existing maximum path. These are
measured current inputs, not proof that a future change fits.

A new 32,768-byte dense frame bitmap exceeds that image headroom. The
existing indexed selection is still larger. An 8-KiB boot-order array or a
10-KiB runtime physical-key array cannot be placed on the user stack.
Repacking records, shrinking input buffers, borrowing live executable bytes
or reclaiming the snapshot merely to fit a bitmap would expand this task.

The proposed adaptation uses one bounded, temporary 16-KiB key workspace
per participating image, shared by nonoverlapping proof phases. Boot keys
are normalized 32-bit frame indices; runtime physical/scalar keys are
64-bit words. Fixed-width, two-way MSD radix distribution retains the
reference family without a histogram, recursion, allocation or small-input
quadratic fallback. Its partition descriptors also live in that workspace.
The private workspace is not an allocator, persistent membership index or
cached validation result.

For key width `w` and populated key count `n`, collection, radix partition,
adjacent duplicate checking and prefix cleanup have a uniform `O(w*n)`
upper bound, with explicit `w <= 64` and no average-case or amortization
assumption. Only populated keys are touched. This avoids quadratic equality work
and a frame-capacity clear on a small ordinary plan.

## Required adaptation ledger

| Difference | Classification | Reason |
| --- | --- | --- |
| Selected process/root/PTE and maintained owner-chain authority | Baseline parity | Reuse the reference's selected-owner/index organization through existing `micros` mechanisms. |
| Generation checks, direct grants, four-table Sv39 plans and protected commits | Required adaptation | Preserve the already Accepted RV64/isolation contract. |
| Compact managed ranges with binary physical/index resolution | Required adaptation | Retain the checked compact FDT-derived representation without a supported-physical-address-space-sized bitmap. |
| Fixed-width radix key proofs instead of another dense VM bitmap | Required adaptation | The measured image/stack budgets rule out the direct full bitmap; the reference radix family preserves linear fixed-word work and exact uniqueness. |
| Load-token routing bits plus complete token/generation/phase comparisons | Required adaptation | VM receives a transaction token rather than MINIX's child endpoint; a checked slot selector avoids a new hash/tree and preserves distinct authority identities. |
| Complete diagnostic duplicate/corruption coverage and deterministic first-error preservation | Compatible extension | Preserve existing safety evidence without placing a global audit on an ordinary selected path. |
| Original lowest-frame selection and repeated active load-plan work | Staged substitution | VM04 and VM03 retain their actual replacement points and disclosed costs. |

The [contract](../architecture/vm-mapping-plan-uniqueness.md) fixes the routing
format, workspace lifetime, owner-directed collision behavior, exact selected
root certificate and acceptance bounds. Implementation must not invent an
alternative index, relax a resource limit or treat these design measurements
as its own Green evidence.

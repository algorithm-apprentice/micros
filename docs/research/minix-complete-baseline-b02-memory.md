# B02 Memory Baseline Audit

## Status

```text
audit unit:       B02 memory
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; nine new blocking findings; not certified
implementation:   forbidden during complete-audit phase
```

B02 audits firmware-range canonicalization, physical-frame allocation, typed
ownership, kernel Sv39 construction, private process roots, read/mutation
paths, exceptional mappings, wired handoff, and hardware permission faults.
It reuses B01's accepted platform rows and does not start any correction.

## Exact primary path manifest

B02 has 37 primary paths in the pinned tree.

### Public contracts

| Path | Responsibility |
| --- | --- |
| `include/micros/bootstrap_memory.h` | Target bootstrap allocator/ownership runtime |
| `include/micros/frame_allocator.h` | Portable range, bitmap, allocate, and release contract |
| `include/micros/frame_ownership.h` | Dense typed owner, phase, count, and transition contract |
| `include/micros/frame_ownership_runtime.h` | Serialized target ownership authority |
| `include/micros/kernel_address_space.h` | Kernel-root initialization/report contract |
| `include/micros/sv39.h` | Sv39 PTE representation and codecs |
| `include/micros/user_address_space.h` | Process-root lifecycle, lookup, activation, and VM transaction API |
| `include/micros/user_address_space_core.h` | Portable owner/leaf classification core |

### Firmware ranges and allocator

| Path | Responsibility |
| --- | --- |
| `kernel/memory/bootstrap_memory.c` | FDT-to-allocator integration, successful validation, target wrapper |
| `kernel/memory/frame_allocator.c` | Canonical ranges, managed segments, bitmap allocation/release |

### Typed ownership

| Path | Responsibility |
| --- | --- |
| `kernel/memory/frame_ownership.c` | Owner encoding, geometry snapshots, physical-index translation |
| `kernel/memory/frame_ownership_bootstrap.c` | Bootstrap allocate/release and process release-set commit |
| `kernel/memory/frame_ownership_handoff.c` | Handoff planning and irreversible classification |
| `kernel/memory/frame_ownership_internal.h` | Private ownership primitives |
| `kernel/memory/frame_ownership_validation.c` | Complete active/inactive validation and read-only lookup |
| `kernel/memory/frame_ownership_vm_claim.c` | Post-handoff VM claim transition |
| `kernel/memory/frame_ownership_vm_freeze.c` | VM freeze owner transition |
| `kernel/memory/frame_ownership_vm_release.c` | Transferable/process release transitions |

### Kernel Sv39

| Path | Responsibility |
| --- | --- |
| `kernel/memory/address_space.c` | Kernel-root construction, mapping, activation, report |
| `kernel/memory/address_space_internal.h` | Kernel page-table/private-state representation |
| `kernel/memory/address_space_validation.c` | Reachability, owner, allocator, and mapping validation |
| `kernel/memory/sv39.c` | Canonical addresses, VPNs, PTE encode/decode |

### Private user address spaces

| Path | Responsibility |
| --- | --- |
| `kernel/memory/user_address_space.c` | Process-root create/destroy and shared-entry copy |
| `kernel/memory/user_address_space_activation.c` | Process/kernel root activation |
| `kernel/memory/user_address_space_core.c` | Portable owner/leaf classification |
| `kernel/memory/user_address_space_handoff.c` | Wired handoff preparation/completion |
| `kernel/memory/user_address_space_internal.h` | Runtime view, scratch lease, private declarations |
| `kernel/memory/user_address_space_mapping.c` | Lookup, translate, inventory, page-table walk |
| `kernel/memory/user_address_space_mutation.c` | Bootstrap map/unmap preparation and commit |
| `kernel/memory/user_address_space_runtime.c` | Authority binding, interrupt lock, shared scratch |
| `kernel/memory/user_address_space_table.c` | Page/index/bitmap primitives |
| `kernel/memory/user_address_space_tty.c` | One-shot TTY UART leaf transaction |
| `kernel/memory/user_address_space_validation.c` | Complete root/tree/owner validation |
| `kernel/memory/user_address_space_vm_freeze.c` | VM executable-freeze transaction |
| `kernel/memory/user_address_space_vm_plan.c` | VM table-plan shape |
| `kernel/memory/user_address_space_vm_scratch.c` | VM active-root scratch map/remove transaction |
| `kernel/memory/user_address_space_vm_teardown.c` | VM process teardown transaction |

## Cross-owner source dependencies

| Path family | B02 operation | Primary/later owner |
| --- | --- | --- |
| `arch/riscv64/mmu.S`, `arch/riscv64/mmu.h` | B02-O05 activation/fences | B01 primary, B02 semantic |
| `arch/riscv64/kernel.ld` | B02-O05 mapping boundaries | B01 primary |
| `arch/riscv64/platform.h` | B02-O05 fixed UART/PLIC MMIO leaves | B01 primary, B02 semantic |
| `arch/riscv64/mmu_test.S` | B02-O09 target protection faults | B14 evidence |
| `arch/riscv64/user_address_space_test.S` | B02-O06 private-root target behavior | B14 evidence |
| `arch/riscv64/address_space_handoff_test.S` | B02-O08 wired handoff | B14 evidence |
| `kernel/objects/kernel_object_validation.c`, `kernel/objects/kernel_objects.c`, `kernel/scheduler/scheduler_core_validate.c` | `Vobj(pcap,tcap,hcap)` invoked by B02-O04/O06-O08 | B03-O01 complete object/scheduler validation |
| remaining `kernel/objects/**` process/root fields | B02-O06 lifecycle attachment | B03 |
| remaining `kernel/scheduler/**` root selection | B02-O06 activation consumer | B03 |
| `kernel/execution/user_execution_validation.c` | B02-O08 prepared-context mapping validation | B05 |
| `kernel/ipc/**`, `kernel/grant/**` translation consumers | B02-O06 read path | B04/B05 |
| `kernel/vm/**`, `servers/vm/**` handoff/freeze/teardown callers | B02-O07/O08 | B07 |
| `kernel/pm/**`, `servers/pm/**` executable/lifecycle callers | B02-O06/O07 | B08 |
| `kernel/tty/**`, `servers/tty/**` device mapping | B02-O07 | B09 |

The later units must cite the exact B02 operation and may not treat cross-owner
use as another memory implementation.

## Fixed MINIX source evidence

The B02 comparison uses:

- `minix/kernel/arch/i386/pg_utils.c`
- `minix/kernel/arch/earm/pg_utils.c`
- `minix/kernel/arch/i386/memory.c`
- `minix/kernel/arch/earm/memory.c`
- `minix/kernel/system/do_vmctl.c`
- `minix/kernel/arch/i386/arch_do_vmctl.c`
- `minix/kernel/arch/earm/arch_do_vmctl.c`
- `minix/servers/vm/alloc.c`
- `minix/servers/vm/pagetable.c`
- `minix/servers/vm/region.c`
- `minix/servers/vm/regionavl.c`
- `minix/servers/vm/regionavl.h`
- `minix/servers/vm/regionavl_defs.h`
- `minix/servers/vm/cavl_if.h`
- `minix/servers/vm/cavl_impl.h`
- `minix/servers/vm/unavl.h`
- `minix/servers/vm/pagefaults.c`
- `minix/servers/vm/fork.c`
- `minix/servers/vm/exit.c`
- `minix/servers/vm/main.c`

MINIX bootstrap code owns a temporary range allocator and large-page bootstrap
map. VM later owns the free-page bitmap, region AVL tree, process page tables,
fault policy, and process-memory release. The kernel binds roots, walks page
tables for privileged copies, and invalidates translations.

## Logical variables

| Variable | Meaning |
| --- | --- |
| `m` | canonical available-memory ranges |
| `r` | canonical reservation ranges |
| `g` | managed frame ranges |
| `gmax` | configured managed-range capacity |
| `p` | managed physical frames |
| `pmax` | configured frame-ledger capacity |
| `a` | sequential bootstrap allocations |
| `t` | reachable page-table frames |
| `l` | mapped leaf pages |
| `u` | mappings owned by one process |
| `n` | live processes with private roots |
| `n0` | live processes without private roots |
| `pcap` | configured process-slot capacity |
| `tcap` | configured thread-slot capacity |
| `hcap` | configured hart-slot capacity |
| `v` | VM regions in one MINIX process |
| `Vobj(pcap,tcap,hcap)` | complete kernel-object/scheduler validation work, including at least `tcap^2`, `tcap*hcap`, and `hcap^2` stack/identity comparisons |
| `Sobj(pcap,tcap)` | peak automatic witness storage of complete object/scheduler validation, `O(pcap+tcap)` |

The current bounds (`g <= 96`, `pmax = 262144`, one hart) remain policy limits,
not excuses to remove them from complexity analysis.

## Operation ledger

### B02-O01 Firmware-range canonicalization and allocator initialization

B01-O03C records the complete cross-owner operation. `micros` aligns, insertion
sorts, merges, subtracts reservations, builds managed-range bitmap offsets, and
zeros the bitmap:

```text
O(m^2 + r^2 + m*r + p/64)
```

MINIX imports/cuts fixed range records rather than deriving one compact
segment-relative bitmap.

| Difference | Classification |
| --- | --- |
| FDT-derived ordered disjoint managed segments | Required adaptation |
| Failure-atomic candidate construction before allocator publication | Compatible extension |

### B02-O02 Bootstrap successful-state validation

B01-O03D establishes that `bootstrap_state_is_valid()` scans every managed
frame and repeatedly checks it against raw FDT unions/reservations:

```text
O(p * (m^2 + r))
```

MINIX completes bootstrap from transformed range records.

**Classification:** Divergence requiring correction. The stable finding is
B01-F006; B02 does not duplicate it.

### B02-O03 Physical-frame allocation and release

MINIX bootstrap allocation has two architecture forms with the same
range-record mechanism:

- i386 `pg_alloc_page()` scans descriptors from the highest index and removes
  one page from the selected range end;
- ARM `pg_alloc_page()` scans from index zero and advances the selected range
  base; and
- i386 `alloc_lowest()` separately scans range records and cuts one selected
  interval.

After VM starts, `alloc_pages()` uses:

- one physical-page bitmap;
- a single-page free cache;
- a remembered `lastscan`;
- word/chunk skipping over fully allocated bitmap chunks; and
- a high-to-low contiguous-run search.

`micros_frame_allocator_allocate()` restarts at the first managed range and
tests every frame bit individually until it finds the lowest free frame.
Release scans ordered managed ranges and clears one bit.

```text
MINIX bootstrap one-page allocation:  O(m) plus range cut
MINIX VM single-page best case:       O(1) valid cache top
MINIX VM stale-cache drain:           O(cache entries discarded)
MINIX VM bitmap fallback worst case:  O(p / word_bits) to O(p)
micros one allocation worst case:     O(p)
micros first a allocations:           O(a^2) when the lowest a frames fill
micros release:                       O(g)
```

| Difference | Classification |
| --- | --- |
| Temporary kernel allocation before VM policy | Baseline parity |
| Compact bitmap over sparse FDT ranges | Required adaptation |
| Lowest-address full restart with per-frame probes | Divergence requiring correction |

See B02-F001.

### B02-O04 Typed frame ownership

MINIX has no dense generation-bound owner table. It combines VM's free bitmap,
region/page metadata, page tables, process slots, and assertions.

`micros` adds:

- one eight-byte owner per frame;
- one handoff-target byte per frame;
- per-kind and total counts;
- immutable managed-range snapshots; and
- `BOOTSTRAP/HANDED_OFF` phase.

Address-to-index lookup scans `g` managed ranges. Ordinary ownership lookup
first validates active geometry and every inactive allocator/snapshot slot
through `gmax`. Complete validation visits every active
owner and every inactive owner/target through `pmax`, plus bitmap words and
counts. Process-owner counting scans `p`.

```text
lookup:                 O(gmax + g)
portable complete validation:
                        O(pmax + p + gmax)
target runtime validation:
                        O(pmax + p + gmax
                          + Vobj(pcap,tcap,hcap))
count one process:      O(p)
handoff classification: O(p)
storage:                O(pmax)
```

The ledger and generation checks are valid safety extensions only if their
valid-path cost does not replace MINIX's direct metadata operations with full
capacity scans.

| Difference | Classification |
| --- | --- |
| Dense exact owner/phase mirror | Compatible extension |
| Exact process generation in frame ownership | Compatible extension |
| Complete fixed-capacity validation on ordinary valid paths | Divergence requiring correction |

See B02-F002.

### B02-O05 Kernel Sv39 construction, validation, and activation

MINIX bootstrap maps x86 memory with 4 MiB pages and ARM memory with 1 MiB
sections. Its VM page-table layer directly indexes directory/table arrays,
allocates all missing tables before writes, maps `pages` in `O(pages)` after
preparation, and binds a process root in constant/fixed work.

`micros` uses only 4 KiB Sv39 leaves. It maps every managed frame separately.
For every existing non-leaf encountered by `map_page()`,
`table_frame_index()` linearly scans the table-frame array.

The fixed mapping set is:

- linker text as RX;
- linker read-only data as R;
- linker writable state as RW/NX;
- every managed physical frame as RW/NX;
- the UART page as RW/NX;
- the PLIC source-10 priority page as RW/NX;
- the hart-0 supervisor-enable page as RW/NX; and
- the hart-0 supervisor claim/complete context page as RW/NX.

Candidate validation also requires the adjacent PLIC pages named by the
contract to remain absent.

Construction therefore has worst-case:

```text
successful table allocation:
            O(t^2 + t*gmax)
optional final exhausted allocation:
            O(p)
mapping:    O(l * t)
table zero: O(512 * t)
```

Final candidate validation adds:

- complete ownership validation through `pmax`;
- allocated-bit counting over `p`;
- linear table-frame lookup for every reachable table, `O(t^2)`;
- ownership lookup for every table, `O(t * gmax)`;
- exact page-table entry scans, `O(512 * t)`; and
- one fixed three-level lookup for every expected mapped leaf, `O(l)`.

Activation itself is constant work: full fence, `satp` write, full fence,
readback.

| Difference | Classification |
| --- | --- |
| Sv39 PTE encoding and fence sequence | Required adaptation |
| Exact W^X linker boundaries and owner validation | Compatible extension |
| Fixed UART and three exact PLIC MMIO leaves | Required adaptation |
| Adjacent PLIC absence checks | Compatible extension |
| 4 KiB-only bootstrap/direct map instead of reference large leaves | Divergence requiring correction |
| Linear table-frame search during every mapped page and final tree check | Divergence requiring correction |

See B02-F003 and B02-F004.

### B02-O06 Private process roots and ordinary mappings

MINIX VM stores regions in an AVL tree. `map_lookup()` is `O(log v)`.
Architecture `vm_lookup()` walks a fixed two-level page table in `O(1)`.
`pt_writemap()` prepares missing tables, then writes or verifies each requested
page in `O(pages)`. `pt_bind()` performs fixed work and one kernel root-binding
call.

`micros` uses:

- one private Sv39 root per exact process generation;
- one fixed one-GiB user window;
- shared immutable supervisor root entries;
- 4 KiB private leaves;
- a 32 KiB reachability bitmap and a 32 KiB release bitmap; and
- no separate region tree.

The fixed window and page-table walk are staged replacements for dynamic
regions. A direct leaf lookup would be three PTE reads. The public lookup,
translate, inventory, and activation paths instead:

1. clear both scratch bitmaps;
2. run complete frame-ownership validation through `pmax`;
3. compare all shared root entries;
4. scan every reachable middle/leaf table entry;
5. mark every reachable frame;
6. scan all `p` owners for orphans;
7. perform the requested three-level lookup or root activation; and
8. clear both scratch bitmaps again.

```text
lookup/translate/activate:
  R = O(Vobj(pcap,tcap,hcap)
        + pmax + p + 512*t
        + (t + l)*gmax + p/64)
  peak automatic stack includes Sobj(pcap,tcap)
reference lookup/root bind:
  O(1) page-table levels, plus O(log v) only for VM region policy
```

Map, unmap, create, and destroy use the same complete validator around their
bounded prepare/commit paths.

| Difference | Classification |
| --- | --- |
| Exact generation-bound private root and shared kernel entries | Compatible extension |
| Fixed user window without dynamic AVL regions | Staged substitution; SOM-04 |
| Preflight/commit and reverse rollback | Compatible extension |
| Complete global validation before ordinary lookup/translation/root switch | Divergence requiring correction |

See B02-F005.

### B02-O07 Exceptional mapping transactions

TTY device mapping, VM active-root scratch mapping, executable freeze, and VM
teardown use explicit fixed plan structures. They preflight all frames,
owners, mappings, tokens, and active-root conditions before non-failing stores.

Every VM scratch map/removal, executable freeze, and process teardown also
calls `micros_user_address_space_validate_all_process_roots_locked()`. That
scans all `pcap` process slots, iterates all `n` rooted processes, and each
root validation repeats `R` above:

```text
O(pcap + sum(R_process for every rooted process))
```

A load that performs scratch map/removal for `u` pages can repeat that cost
`u` times.

Plan construction has another independent term. Address-space mapping plans,
frame-owner freeze plans, and release plans reject aliases by comparing each
frame with every previously accepted frame:

```text
O(u^2 + u*gmax)
```

MINIX `pt_writemap()`, fault handling, and exit operate on the selected page
range or selected process and do not perform all-process validation or a
pairwise distinctness pass.

| Difference | Classification |
| --- | --- |
| One fixed UART device leaf | Required adaptation |
| VM scratch map/remove while the VM root is temporarily inactive | Required adaptation |
| Freeze/teardown plan tokens and failure-atomic commits | Compatible extension |
| Absence of general post-handoff map/unmap/fault policy | Staged substitution; SOM-04 |
| All-process root validation on each selected-process transaction | Divergence requiring correction |
| Pairwise duplicate scans in valid frame plans | Divergence requiring correction |

B07 and B09 own the policy callers. See B02-F008 and B02-F009.

### B02-O08 Wired ownership handoff

MINIX starts VM with remaining memory chunks and process/page-table metadata,
then uses VM as the ordinary allocator/mapping owner. The import is linear in
the supplied chunks, processes, and represented mappings.

`micros_user_address_space_prepare_wired_handoff()` and
`complete_wired_handoff()` each:

- run complete ownership and object validation;
- scan all `pcap` process slots;
- scan all `p` owners for each of the `n0` live rootless processes;
- scan up to `tcap` threads to find one prepared thread for each rooted
  process;
- validate each rooted process's prepared execution context through two public
  address-space lookups;
- completely validate each live private root;
- scan the complete owner set for each process to reject orphans; and
- perform another full owner pass for handoff classification/commit.

```text
O(pcap + n0*p
  + sum(tcap + 3*R_process for every rooted process)
  + pmax + p + gmax
  + Vobj(pcap,tcap,hcap))
```

| Difference | Classification |
| --- | --- |
| Explicit one-way wired/transferable classification | Required adaptation |
| Complete preflight before irreversible commit | Compatible extension |
| Repeating the complete owner scan once per live process | Divergence requiring correction |

See B02-F007.

### B02-O09 Hardware translation and permission faults

MINIX binds architecture roots and relies on x86/ARM page-fault delivery.
`micros_riscv_activate_sv39()` uses ASID zero with full local fences. The MMU
component proves a store fault on RX text and an instruction fault on RW/NX
data.

```text
activation mechanism: O(1)
hardware lookup:      architecture-defined
```

**Classification:** Required adaptation.

## Time, stack, and auxiliary-space envelope

| Operation | MINIX time | `micros` time | MINIX stack/auxiliary | `micros` stack/auxiliary |
| --- | --- | --- | --- | --- |
| Bootstrap range import/allocation | Range scan/cut, `O(m)` to range-array mutation cost | Initialization `O(m^2+r^2+m*r+p/64)`; allocation best `O(1)`, worst `O(p)`, first `a` allocations `O(a^2)` | Fixed range arrays, constant call stack | At least 5,376 bytes of nested explicit automatic range arrays during initialization, plus constant call frames |
| VM physical allocation | Best `O(1)` valid cache top; stale-cache drain proportional to discarded entries; fallback `O(p/word_bits)` to `O(p)` | Post-handoff selection belongs to B07; bootstrap allocator remains worst `O(p)` | Persistent bitmap and 10,000-entry free-page cache | Persistent compact bitmap `O(p/64)` |
| Typed owner validation | No dense equivalent | Portable `O(pmax+p+gmax)`; target adds `Vobj(pcap,tcap,hcap)` | VM/process/page metadata | About 2 MiB owners, 256 KiB targets, range snapshot and counts; target peak adds `Sobj(pcap,tcap)` |
| Kernel page-table build | Large-page bootstrap entries; VM writes `O(leaves+tables)` | Successful `O(t^2+t*gmax+l*t+512*t)` construction, optional final `O(p)` exhaustion scan, and `O(pmax+l+t^2+t*gmax)` validation | Persistent page tables; constant/fixed iterators | Persistent page tables/table-frame list and a 1,024-element automatic reachability array |
| Region/mapping lookup | `O(log v)` AVL policy lookup; `O(1)` fixed hardware page-table levels | `R = O(Vobj+pmax+p+512*t+(t+l)*gmax+p/64)` | AVL iterator retains a depth-30 path; page walk constant | Two persistent 32 KiB scratch bitmaps plus peak `Sobj(pcap,tcap)` |
| Root activation | Fixed root bind plus architecture invalidation | Same complete validation as lookup, then `O(1)` fences/write/readback | Constant | Shared 64 KiB scratch plus peak `Sobj(pcap,tcap)` |
| Selected-process map/freeze/teardown | Requested range/process work | `O(pcap + sum(R_i) + u^2 + u*gmax)` | Process/region/page metadata | Fixed plan structures, shared 64 KiB scratch, and peak `Sobj(pcap,tcap)` |
| Wired handoff | Linear imported chunks/process metadata | `O(pcap+n0*p+sum(tcap+3*R_i)+pmax+p+gmax+Vobj)` | Imported VM metadata | Shared scratch, dense ledger, per-process context, and peak `Sobj(pcap,tcap)` |

No average or amortized allocator claim is inferred from the existence of the
MINIX cache. A valid top entry is only the best path; stale entries may all be
discarded before fallback. `micros` has no cursor/cache that yields an
amortized bound better than its restart scan under adversarial allocation and
release.

## Accepted structure and complexity summary

| Mechanism | Structure | Valid-path bound | Classification |
| --- | --- | --- | --- |
| Range canonicalization | fixed sorted/merged range arrays | `O(m^2 + r^2 + m*r + p/64)` | Required adaptation |
| Typed owner data | dense owner/target arrays and counts | `O(pmax)` storage | Compatible extension |
| Sv39 codec | fixed three-level encoding | `O(1)` per PTE/VPN operation | Required adaptation |
| Process root | exact generation-bound private root plus shared supervisor entries | `O(1)` identity | Compatible extension |
| Fixed user window | no region AVL | fixed range | Staged substitution; SOM-04 |
| Exceptional plans | bounded preflight/commit records | bounded by declared frame/mapping capacities | Compatible extension |
| SBI/CSR activation | full fences and ASID-zero root | `O(1)` mechanism | Required adaptation |

Per-frame FDT validation, allocator search, complete ownership scans, 4 KiB-only
kernel mapping, linear table-frame search, ordinary full-root validation,
full-capacity release sets, per-process handoff rescans, all-process VM
transaction validation, and quadratic frame-plan checks are excluded because
findings remain open.

## Executable evidence mapping

| Evidence | What it proves | What it does not prove |
| --- | --- | --- |
| `frame-allocator` | range normalization, bitmap semantics, deterministic allocation/release and model agreement | parity of scan direction/data structure; probe counts |
| `frame-ownership` | owner/phase/count invariants and model agreement | valid-path full-capacity cost |
| `sv39-encoding` | canonical addresses, VPNs, PTE encode/decode | table construction or hardware behavior |
| `test-qemu-frame-allocator` | real FDT/linker integration at 128/256 MiB | scaling to `pmax`; per-frame validation work |
| `test-qemu-frame-ownership` | generation owners and atomic handoff | complete-validator cadence/cost |
| `test-qemu-mmu` | kernel mapping permissions and real faults | large-leaf parity; mapping construction cost |
| `test-qemu-user-address-space` | private roots, mapping, activation, teardown | context-switch/lookup work bound |
| `test-qemu-address-space-handoff` | wired post-handoff reads and phase closure | per-process repeated `pmax`, tree, owner, and lookup work |
| `test-qemu-vm-scratch-preemption` and VM native cases | scratch map/remove state and exact rollback | all-process validation work per scratch operation |
| `test-qemu-elf-spawn` and ELF-spawn model | freeze/load/teardown behavior and resource restoration | pairwise frame-plan comparison count |
| downstream IPC/grant/user-execution gates | semantic reachability through translation | direct-path complexity |

## Evidence gaps

| ID | Gap | Later owner |
| --- | --- | --- |
| B02-E001 | No allocator probe counter distinguishes range selection, word skipping, and per-frame restart | B14 |
| B02-E002 | No validation visit counter distinguishes active `p`, inactive `pmax-p`, active `g`, inactive `gmax-g`, bitmap words, and `Vobj` work | B14 |
| B02-E003 | No mapping-construction counter records cumulative table-allocation probes/range validation, mapped leaves times table-frame searches, and table-owner lookups | B14 |
| B02-E004 | No lookup/root-switch counter separates fixed PTE levels from `Vobj`, `pmax`, page-tree, owner, and `gmax` work | B14 |
| B02-E005 | No release-set counter records full-capacity versus selected-frame work | B14 |
| B02-E006 | No handoff counter records `pcap`, `n0*p`, per-root `3*R`, `tcap`, and final full-validation work | B14 |
| B02-E007 | No VM scratch/freeze/teardown counter records `pcap + sum(R_i)` all-process validation per selected operation | B14 |
| B02-E008 | No counter separates pairwise duplicate checks from per-frame `gmax` ownership lookups in valid VM plans | B14 |

## Findings register

### B02-F001: Bootstrap allocation restarts a per-frame scan

- Severity: High
- `micros` evidence: `kernel/memory/frame_allocator.c:micros_frame_allocator_allocate`
- MINIX evidence: architecture `pg_alloc_page()`/`alloc_lowest()` and VM
  `alloc_pages()` in `minix/servers/vm/alloc.c`
- Difference: `micros` scans from the lowest frame on every allocation,
  testing one bit at a time. MINIX i386 and ARM choose opposite ends but both
  mutate range descriptors; VM uses a cache, remembered cursor, and
  bitmap-chunk skipping.
- Impact: the first `a` low-frame allocations require `O(a^2)` probes and can
  scale with total RAM rather than range descriptors or bitmap words.
- Smallest correction boundary: a reviewed allocator design reproducing the
  reference range/cursor/cache mechanisms for their matching phase before any
  later measured policy optimization.
- Dependencies: B01-F006 and B07 VM allocator audit.

### B02-F002: Complete ownership validation is on ordinary valid paths

- Severity: High
- `micros` evidence:
  - `micros_frame_ownership_validate`
  - `inactive_state_is_zero`
  - `micros_frame_ownership_runtime_validate`
- Difference: a compatible safety ledger becomes an
  `O(pmax + p + gmax + Vobj)` target valid path, including inactive
  owner/target and range slots plus complete object/scheduler validation,
  rather than an explicit audit boundary around direct metadata operations.
- Impact: lookup, mapping, PM/VM control, handoff, and process lifecycle can
  pay for the entire configured capacity even on small active states. At the
  recorded 128 MiB shape, 233,240 of 262,144 owner entries are inactive.
- Smallest correction boundary: separate incremental operation invariants and
  path-local owner checks from an explicit complete-audit validator; preserve
  complete corruption evidence without invoking the full scan on every
  ordinary operation.
- Dependencies: B03/B04/B05/B07/B08 consumers and ADR-0076 supersession.

### B02-F003: Kernel bootstrap omits the reference large-page map

- Severity: High
- `micros` evidence: 4 KiB-only mapping in `kernel/memory/address_space.c`
- MINIX evidence:
  - i386 4 MiB `I386_VM_BIGPAGE` bootstrap mappings
  - ARM 1 MiB section mappings
- Difference: Sv39 supports larger leaves, but `micros` maps every managed
  frame with a 4 KiB leaf solely to keep one simpler construction path.
- Impact: mapping work and page-table storage grow with `p`, and every process
  shares the resulting larger supervisor tree.
- Smallest correction boundary: a reviewed Sv39 baseline design using the
  largest legal leaf at homogeneous permission/ownership boundaries and
  splitting only where exact kernel permissions or later mapping policy
  require it.
- Dependencies: B02-F004 and B03/B05 shared-root consumers.

### B02-F004: Kernel mapping and validation linearly search table frames

- Severity: High
- `micros` evidence:
  - `address_space.c:table_frame_index`
  - `address_space_validation.c:candidate_table_index`
- MINIX evidence: direct directory/table arrays in `pagetable.c`
- Difference: every mapped page can scan `t` table frames, final reachability
  can scan `t` candidates for every table, and final table-owner checks pay
  `O(t*gmax)`.
- Impact: mapping is `O(l*t)` and validation includes `O(t^2+t*gmax)`, rather
  than page/table-linear work. The separate successful table-allocation term
  `O(t^2+t*gmax)` belongs to B02-F001/B02-F002.
- Smallest correction boundary: direct table identity/index metadata that
  preserves exact typed ownership and reachability while making map and
  validation work linear in leaves plus tables.
- Dependencies: B02-F003.

### B02-F005: Lookup, translation, and root activation run complete validation

- Severity: High
- `micros` evidence:
  - `user_address_space_validation.c:load_validation_context`
  - `validate_owned_frame_set`
  - public lookup/translate/activate paths
- MINIX evidence:
  - fixed-level kernel `vm_lookup()`
  - AVL `map_lookup()`
  - fixed-work `pt_bind()`
- Difference: an operation that should walk three PTE levels or install one
  root clears shared bitmaps, runs `Vobj`, validates all `pmax` ownership and
  `gmax` range slots, scans the complete reachable tree, and scans all `p`
  owners.
- Impact: every IPC/grant translation and scheduler root switch can become
  proportional to configured memory and object capacities.
- Smallest correction boundary: path-local PTE/owner validation for ordinary
  reads and root switches. Complete all-root/owner audits may remain only at
  explicit diagnostic or one-time certification boundaries, not ordinary
  mapping, mutation, scratch, freeze, teardown, handoff, or context-switch
  paths.
- Dependencies: B02-F002 and B03-B05 hot-path audits.

### B02-F006: Page release and process destruction scan full capacity

- Severity: High
- `micros` evidence:
  - `frame_ownership_bootstrap.c:collect_process_release_summary`
  - `commit_process_release`
  - user-address-space release/destroy bitmap walks
- MINIX evidence: region/page-table teardown over one process's regions and
  mapped pages
- Difference: one-page unmap and process teardown validate and scan
  fixed-capacity bitmaps/owner arrays rather than only selected or owned
  frames.
- Impact: small unmaps and exits can cost `O(pmax + p)` instead of
  `O(selected frames)` or `O(owned mappings)`.
- Smallest correction boundary: an exact per-process/release-set index or
  compact selected-frame plan that keeps failure-atomic commit while avoiding
  full-capacity scans.
- Dependencies: B02-F002, B08 process exit, B12 application lifecycle.

### B02-F007: Wired handoff repeats the full owner scan per process

- Severity: High
- `micros` evidence: `user_address_space_handoff.c` prepare and complete paths
- MINIX evidence: VM startup import of memory/process/page-table metadata
- Difference: handoff scans all `pcap` process slots, all owners for each
  rootless process, up to `tcap` threads for each rooted process, invokes two
  complete public lookup validations for each prepared context, validates the
  root again, then runs final full ownership/object validation.
- Impact: handoff repeats fixed-capacity ownership validation and complete
  root work for every process, rather than remaining linear in processes,
  mappings, and frames represented once.
- Smallest correction boundary: one global owner/reachability pass partitioned
  by process, reused by root validation and handoff classification before the
  same non-failing commit.
- Dependencies: B02-F002 and B07 handoff audit.

### B02-F008: VM mapping transactions validate every process root

- Severity: High
- `micros` evidence:
  - `user_address_space_vm_scratch.c`
  - `user_address_space_vm_freeze.c`
  - `user_address_space_vm_teardown.c`
  - `micros_user_address_space_validate_all_process_roots_locked`
- MINIX evidence: selected-range `pt_writemap()`, selected-process
  `pagefaults.c`, and selected-process `exit.c`
- Difference: one scratch map/removal, executable freeze, or teardown scans
  all `pcap` slots and validates all rooted processes; each root repeats
  `Vobj`, complete ownership/range validation, and orphan scans.
- Impact: one selected-process transition costs
  `O(pcap + sum(R_i))`; a load repeating scratch operations for `u` pages can
  multiply the term by `u`.
- Smallest correction boundary: path-local selected-process validation plus
  incremental global reachability/alias authority. Complete all-root audits
  remain explicit diagnostic or one-time certification operations only.
- Dependencies: B02-F002, B02-F005, B07 VM transactions, B12 executable load.

### B02-F009: VM frame plans use pairwise duplicate searches

- Severity: Medium
- `micros` evidence:
  - `user_address_space_vm_plan.c` prior-mapping scans
  - `micros_frame_ownership_frame_index_is_duplicate`
  - frame freeze/release preflights
- MINIX evidence: linear requested-range preparation in `pt_writemap()`
- Difference: each accepted data/table frame is compared with every preceding
  frame and performs a range-capacity owner lookup, making valid plan
  preparation `O(u^2+u*gmax)`.
- Impact: load, abort, freeze, and release preparation can become quadratic in
  application pages despite fixed current capacity.
- Smallest correction boundary: one bounded linear-time distinctness
  structure over managed frame indexes that preserves exact anti-alias
  rejection and failure-atomic plan publication.
- Dependencies: B07 VM plan audit, B12 executable load.

## Unit conclusion

B02 path and operation coverage is complete for the pinned tree. The author
audit confirms B01-F006 and adds B02-F001 through B02-F009 as blocking
findings.

B02 is **audited but not certified**. Independent review corrected
all-process VM transaction coverage, quadratic frame-plan work, multiplicative
range/object bounds, stack and auxiliary storage, fixed MMIO mappings,
architecture-specific MINIX allocation direction, managed-range capacity,
object/scheduler validation terms, and successful table-allocation bounds.
Final re-review reported no significant issue. No B02 correction may start
during the read-only audit.

## References

- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [B01 platform audit](minix-complete-baseline-b01-platform.md)
- [Earlier parity audit](minix-baseline-parity-audit.md)
- [Kernel address-space source baseline](minix-kernel-address-space-source-organization.md)
- [Frame-ownership source baseline](minix-frame-ownership-source-organization.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)

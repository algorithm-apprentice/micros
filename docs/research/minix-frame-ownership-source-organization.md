# MINIX Frame-Ownership Source-Organization Baseline

This note records the fixed MINIX baseline used to decompose the `micros`
bootstrap allocator and typed frame-ownership evidence. Runtime semantics
remain governed by the Accepted `micros` ADRs, especially ADR-0017, ADR-0020,
ADR-0045, and ADR-0050.

## Reference

The reference tree is the local MINIX checkout at:

```text
/Users/alephling/Repo/minix
```

The inspected revision is the fixed ADR-0025 baseline:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

Representative sources are:

```text
minix/kernel/arch/i386/pg_utils.c
minix/kernel/arch/earm/pg_utils.c
minix/servers/vm/main.c
minix/servers/vm/alloc.c
minix/servers/vm/pagetable.c
minix/servers/vm/exit.c
```

The inspection used the local tree only. Representative commands were:

```sh
git -C /Users/alephling/Repo/minix rev-parse HEAD
sed -n '1,280p' /Users/alephling/Repo/minix/minix/kernel/arch/i386/pg_utils.c
sed -n '220,500p' /Users/alephling/Repo/minix/minix/servers/vm/alloc.c
sed -n '420,520p' /Users/alephling/Repo/minix/minix/servers/vm/main.c
sed -n '980,1510p' /Users/alephling/Repo/minix/minix/servers/vm/pagetable.c
sed -n '1,120p' /Users/alephling/Repo/minix/minix/servers/vm/exit.c
```

No GitHub or web copy was used.

## Bootstrap allocation authority

MINIX architecture bootstrap code consumes the boot memory map while building
the initial kernel and VM mappings. That allocator is temporary kernel
authority. Once VM has imported the remaining memory chunks, ordinary
physical-page policy no longer belongs to the kernel bootstrap path.

The exact i386 and ARM page-table mechanisms are not portable to RISC-V.
The reusable contract is:

- bootstrap allocation is bounded and precedes ordinary VM policy;
- the memory map handed to VM excludes memory already retained by the kernel;
- the transition is one way; and
- later process-memory allocation does not silently fall back to the
  bootstrap allocator.

## VM physical-page allocator

`minix/servers/vm/alloc.c` owns the post-bootstrap physical-page bitmap and its
allocation policy:

- `mem_init()` imports usable chunks into VM-owned allocator state;
- `alloc_mem()` selects ordinary pages;
- `free_mem()` returns pages to that state; and
- `memstats()` reports summaries from the same authority.

Process regions, page-table construction, startup loading, and exit
orchestration remain separate owners. `alloc.c` supplies physical availability
mechanism; it does not own every process lifecycle that consumes pages.

The exact scan direction, cache policy, contiguous-range behavior, and bitmap
layout are implementation details. The observable baseline is that VM chooses
ordinary frames after bootstrap while the kernel retains privileged mapping
mechanism.

## No MINIX equivalent of the typed ledger

The fixed MINIX tree does not maintain one dense kernel ledger that records,
for every managed page, a generation-bound semantic owner equivalent to:

```text
KERNEL_RETAINED
KERNEL_PAGE_TABLE
KERNEL_TEMPORARY
PROCESS_PAGE_TABLE
PROCESS_USER
VM_WIRED
VM_TRANSFERABLE
```

MINIX instead relies on VM allocator state, process and region metadata,
page-table structures, assertions, and boot-time memory-map ownership.

The `micros` typed ledger is therefore a compatible safety extension. It
cross-checks availability, exact process generations, page-table ownership,
and the one-way handoff without changing the baseline authority result: VM
owns ordinary post-bootstrap memory policy.

## Handoff and post-handoff transitions

MINIX transfers ordinary memory policy by starting VM with imported memory
and process metadata, then making VM the allocator and mapping-policy owner.
It does not expose a `micros`-style staged owner-classification commit.

`micros` requires an explicit transition because:

- the kernel allocator bitmap remains a privileged mechanism mirror;
- initial services are already mapped before VM runs;
- exact `VM_WIRED` process-generation authority must survive handoff; and
- VM later supplies exact frame lists while the kernel performs privileged
  owner and page-table commits.

The staged `BOOTSTRAP -> HANDED_OFF` commit and prevalidated VM claim, freeze,
rollback release, and process release operations are compatible safety and
target adaptations. They preserve the MINIX authority split while making
failure atomicity and stale-generation rejection explicit.

## Source-ownership lesson

The useful MINIX source shape is responsibility direction, not an exact file
list:

1. architecture bootstrap allocation is temporary and separate from VM's
   steady-state allocator;
2. VM physical availability is separate from process regions and page-table
   construction;
3. page-table and process lifecycle orchestration consume allocator
   operations rather than becoming the allocator itself; and
4. exit orchestration releases process memory through lower-level VM
   mechanisms.

This supports separate `micros` owners for:

- canonical ledger identity and allocator geometry;
- complete allocator-ledger validation and read-only queries;
- bootstrap allocation and release;
- irreversible handoff planning and commit;
- post-handoff VM claim;
- VM freeze; and
- transferable and process-owned VM release.

It does not justify copying MINIX's global VM state, page cache, region tree,
contiguous allocation policy, recovery breadth, or architecture-specific page
table implementation.

## Difference classification

The classification follows
[ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md).

| ID | `micros` choice relative to the fixed MINIX baseline | Classification | Reason |
| --- | --- | --- | --- |
| FO-01 | Kernel bootstrap memory authority ends before ordinary VM policy | Baseline parity | MINIX kernel boot allocation is temporary |
| FO-02 | FDT-derived bounded bitmap and RISC-V frame geometry implement bootstrap allocation | Required adaptation | FDT, OpenSBI, and Sv39 replace MINIX boot and architecture mechanisms |
| FO-03 | VM selects ordinary frames and releases them after handoff | Baseline parity | MINIX VM owns steady-state physical allocation policy |
| FO-04 | A dense kernel ledger mirrors exact semantic ownership and allocator state | Compatible extension | MINIX has no equivalent generation-bound ledger |
| FO-05 | Every process-bound owner carries an exact slot and generation | Compatible extension | Stale-generation rejection strengthens process identity |
| FO-06 | Handoff uses one staged, fully preflighted, irreversible owner-class commit | Compatible extension | Atomic classification strengthens VM authority publication |
| FO-07 | VM submits bounded transition inputs instead of mutating kernel state directly | Required adaptation | The unprivileged `micros` VM and pointer-free ABI replace MINIX VM internals |
| FO-08 | Kernel retains privileged page-table commits | Baseline parity | MINIX also retains privileged kernel mapping mechanism |
| FO-09 | Kernel allocator-bit and typed-owner-record state remains a privileged mirror | Compatible extension | MINIX VM owns its bitmap and has no equivalent typed kernel ledger |
| FO-10 | VM chooses a free physical address before kernel `VM_TRANSFERABLE` claim | Baseline parity | Policy remains in VM |
| FO-11 | Claim preflight rejects stale or corrupted allocator/ledger state before commit | Compatible extension | Complete mirror validation strengthens the baseline |
| FO-12 | Freeze converts exact transferable data and free table frames to generation-bound owners | Required adaptation | Kernel-built Sv39 tables and typed owners replace MINIX's VM-internal construction |
| FO-13 | VM release is validated as one exact distinct frame set before mutation | Compatible extension | Failure-atomic release strengthens rollback and exit |
| FO-14 | Ledger primitives and validation remain below handoff and VM transitions | Baseline parity | Allocation mechanism remains below process lifecycle |
| FO-15 | Native deterministic models and isolated QEMU evidence remain mandatory | Compatible extension | Replayable evidence strengthens the source move without changing behavior |

No unclassified divergence is introduced by the source decomposition.

## Derived constraints

1. Preserve the public allocator and frame-ownership APIs and every existing
   error, output, and phase contract.
2. Keep canonical owner, geometry, and frame-index primitives below complete
   validation and every mutation owner.
3. Keep complete validation read-only. It may resolve exact frame sets for a
   later commit but must not perform an operation commit.
4. Keep bootstrap allocation/release separate from handoff planning and from
   post-handoff VM transitions.
5. Keep VM claim, freeze, and release as distinct transition owners. They
   change for different lifecycle reasons.
6. Preserve preflight-before-commit ordering and non-failing prevalidated
   commit functions.
7. Pass test context explicitly and retain one mutable context owner per test
   family.
8. Preserve native and QEMU evidence identities. Source decomposition does
   not add another test execution.

## Non-goals

This baseline does not authorize:

- a new owner kind, allocator policy, VM call, frame state, or lifecycle;
- changes to public structure layout or operation signatures;
- general paging, reclaim, overcommit, sharing, copy-on-write, or recovery;
- a direct user-space mutation of the kernel allocator or ledger;
- copying MINIX source, identifiers, structures, or comments; or
- changing validation cadence merely because source files are split.

# MINIX Bootstrap Allocation Baseline

## Status and scope

This is the MEM01 design-phase source study for
[Accepted ADR-0100](../adr/0100-minix-bootstrap-allocation.md) and the
[bootstrap allocation contract](../architecture/bootstrap-allocation.md).
It owns only
B01-F006/B01-E006 and B02-F001/B02-E001 in the
[accepted correction DAG](minix-complete-baseline-findings-and-correction-dag.md).
The implementation task remains pending a separately reviewed, Accepted
design merge. No executable prototype, fixture, model, observation or
runtime correction is authored by this research phase.

The restarted source base includes the independently reviewed binding
prerequisite #208:

```text
micros commit: bcace37522e5beb101ef5278889a2f87742f5721
micros tree:   36110108d7839460b2f384761bb9aaa540e82444
MINIX commit:  4db99f4012570a577414fe2a43697b2f239b699e
```

EV04 is the actual runtime prerequisite. PLAT03 has completed the preceding
position in the serialized queue: all 50 fresh author commands, 44 static
configurations/113 ELF outputs, ten observed scopes and independent
implementation review passed. Its reviewed, committed, remote and squash
trees are identical, and a fresh committed checkout passed the ordinary
authority checks without author journals.

The original MEM01 design review found one blocking source-payload mismatch.
Separate ADR-0098 design #205 and implementation #208 repaired the exact
bounded-validator binding. Separate ADR-0099 #206/#207 repaired the later
ordinary trace workflow's historical-scope restriction. Both implementations
passed complete fresh gates, independent review, identical-tree publication
and journal-free committed controls.

Before this restart, the four inspected MINIX files were reconfirmed equal
to the fixed pin, and all `micros` runtime C/header/assembly remained byte
equal to #204. The source ledger and algorithm trace below therefore remain
valid on #208; only the evidence/acceptance foundation changed.

MEM02 owns mapping topology and table-frame indexing; MEM03 owns complete
ownership-validation cadence. B07-F003 belongs to the later VM allocator
correction. None is pulled into MEM01, and this work does not claim that the
separately blocked hosted-CI attempt is accepted.

## Fixed source ledger

All inspected MINIX files were checked against the fixed commit, including
their working-tree content. They are evidence, not code to copy.

| Source | Relevant operation | Evidence |
| --- | --- | --- |
| `minix/kernel/arch/i386/pre_init.c:173-214` | Firmware import and exclusions | Import available multiboot records into `kinfo.memmap`; cut each kernel/module interval from that map |
| `minix/kernel/arch/earm/pg_utils.c:35-111` | `cut_memmap`, `add_memmap` | Mutate fixed range descriptors; splitting reuses empty descriptor slots |
| `minix/kernel/arch/earm/pg_utils.c:130-155` | `pg_alloc_page` | Scan from descriptor zero; consume one page by advancing the selected base and reducing its length |
| `minix/kernel/arch/i386/pg_utils.c:32-118` | Range import/cut and `alloc_lowest` | Fixed descriptor operations and a separate lowest-address selection |
| `minix/kernel/arch/i386/pg_utils.c:138-159` | `pg_alloc_page` | Scan descriptors in reverse; consume one page from the selected range's high end |
| `minix/servers/vm/alloc.c:32-44` | VM allocator storage | Address-indexed free bitmap, 10,000-entry single-page cache and private allocator entry |
| `minix/servers/vm/alloc.c:315-335` | VM memory initialization | Clear the free bitmap, then import the supplied available chunks |
| `minix/servers/vm/alloc.c:369-400` | `findbit` | Descending contiguous-run search with empty-chunk skipping |
| `minix/servers/vm/alloc.c:404-460` | `alloc_pages` | Check eligible cached pages, use remembered `lastscan`, retry from the upper bound if necessary, then clear the selected bits |
| `minix/servers/vm/alloc.c:465-481` | `free_pages` | Set the released bits and append page indexes while cache capacity remains |

### Bootstrap and VM are different phases

The two kernel `pg_alloc_page` implementations share a mechanism, not a
universal address-order policy: they consume a remaining extent rather than
re-probe every previously allocated page. The ARM path consumes its low end;
the i386 path consumes its high end. Kernel allocation is guarded by
`kernel_may_alloc` and records dynamic kernel bytes.

The VM bitmap/cache allocator is a later policy owner. Its valid cache-top
case is constant work, but stale cached entries can all be discarded before
fallback. Each cache entry is removed at most once after insertion; that
does not make the bitmap search constant-time. `findbit` can inspect
individual bits in nonempty chunks, and allocation may perform two searches.
Worst-case fallback remains linear in the searched page population.

The bootstrap correction must not silently import VM's cache or high-to-low
selection policy. The [B07 allocator finding](minix-complete-baseline-b07-vm.md#b07-f003-frame-allocation-does-not-reproduce-the-minix-bitmapcache-allocator)
separately requires that policy owner and its current-free bitmap.

### Reference safety differences are not requirements

The fixed range importer has a 32-bit physical-address truncation policy.
`micros` instead retains checked unsigned 64-bit half-open arithmetic and its
accepted bound on total managed frames, independently from physical-address
sparsity.

The importer also rounds the base and length separately; this is not the
accepted `micros` rule that each available input is aligned inward from its
checked original end. Reservation inputs remain outward-aligned.

The i386 `alloc_lowest` call at line 81 supplies `len` as the third
`cut_memmap` argument, whereas the callee interprets that argument as an
absolute end. That argument relation is not a correct general interval cut
for a nonzero selected base. MEM01 must not inherit that spelling or use it
to justify weakening checked interval arithmetic.

## Current micros source ledger

| Source | Current responsibility |
| --- | --- |
| `include/micros/frame_allocator.h` | Exact range/bitmap capacities, allocator representation, error values and three portable operations |
| `kernel/memory/frame_allocator.c:109-281` | Normalize, insertion-sort, merge and subtract range records |
| `kernel/memory/frame_allocator.c:283-347` | Clear all inactive metadata and publish an initialized allocator |
| `kernel/memory/frame_allocator.c:349-434` | Failure-atomic initialization using bounded automatic candidate arrays |
| `kernel/memory/frame_allocator.c:436-478` | Restart at range zero and probe every preceding allocated frame bit |
| `kernel/memory/frame_allocator.c:480-527` | Find a physical address's managed range, reject invalid release, then clear one allocation bit |
| `kernel/memory/bootstrap_memory.c:41-151` | Original-FDT union coverage, reservation intersections and selected-frame checks |
| `kernel/memory/bootstrap_memory.c:153-249` | Count allocation bits individually, check each managed interval, then repeat original-FDT checks for every frame |
| `kernel/memory/frame_ownership_bootstrap.c:20-93` | Typed allocation, post-allocation checks and low-level rollback |
| `kernel/memory/frame_ownership_bootstrap.c:244-279` | Atomic process-set release directly clears allocator bits |
| `kernel/memory/frame_ownership.c:117-209` | Geometry and pristine-state validation |
| `kernel/memory/frame_ownership.c:479-499` | Shared prevalidated bitmap mutation used by VM transitions |
| `kernel/memory/frame_ownership_validation.c` | Existing complete active/inactive ledger validation and aggregate checks |

### Canonicalization already exists

The allocator already:

1. aligns each available range inward and each reservation outward;
2. sorts and merges both sets;
3. subtracts reservations to derive sorted managed intervals;
4. assigns contiguous bitmap offsets across physically sparse intervals;
5. clears all unused range records and the complete fixed bitmap; and
6. publishes `initialized` last.

The bootstrap adapter appends `[0, __kernel_end)` before calling that
constructor. It does not permanently reserve the consumed FDT blob.

These are retained mechanisms, not new MEM01 functionality. There is no
reason to replace the accepted sparse geometry with an address-span-sized
bitmap, introduce a dynamic free-range list, or change the VM handoff ABI.

### The ordinary validation defect is repeated work

`bootstrap_state_is_valid` already checks each complete managed interval
against the original decoded FDT memory union and both reservation arrays.
It then repeats the same protection for every frame inside those validated
intervals. Each individual frame also causes another bitmap-word load in
the allocation-count loop.

For one valid raw memory interval, no FDT reservation and one managed
interval containing `p` frames, the coverage helper is invoked `p + 1`
times. Removing this repeat must not remove the whole-interval original-FDT
check, count agreement, checked geometry, kernel exclusion or any required
inactive-tail validation.

### The allocation defect is lost progress

On an initially free contiguous managed interval, the first `a` allocations
execute exactly `a(a + 1)/2` candidate-bit probes. A free-count fast exit
avoids scanning after complete exhaustion, but it does not help the
successful prefix. Releasing a low page and allocating it again is
semantically correct; the existing tests therefore do not discriminate the
work defect.

[ADR-0017](../adr/0017-bootstrap-physical-frame-allocator.md) still requires
lowest-address allocation and deterministic reuse.
[ADR-0020](../adr/0020-typed-bootstrap-frame-ownership.md) still makes immutable
managed geometry and typed ownership authoritative. A correction cannot
mutate those geometry descriptors as if they were MINIX's remaining-free
extents, nor silently replace the accepted address-order behavior.

## Logical work and storage

Use `m` raw memory intervals, `r` raw FDT reservations, `g` managed
intervals, `p` managed frames, `P` frame capacity, `w = 64` bitmap bits per
word and `a` allocation operations. Let `c` be cached VM entries only when
describing the separate reference VM allocator.

| Operation | Fixed reference | Current micros |
| --- | --- | --- |
| Range import/exclusion | Fixed descriptor scans and cuts; no per-page proof of firmware coverage | Existing range normalization/subtraction plus the full fixed bitmap clear |
| Kernel one-page allocation | `O(m)` descriptor scan, constant selected-extent mutation | `O(g + p)` worst candidate scan; the first `a` low-frame allocations are quadratic |
| VM one-page allocation | `O(1)` valid cache top; `O(c)` stale drain; fallback `O(p/w)` through `O(p)` depending on bitmap shape | Not the MEM01 policy owner |
| Portable release | No corresponding bootstrap release path in the inspected kernel routines | `O(g)` physical-range selection plus one bitmap mutation |
| Bootstrap validity | Work over transformed range records | Per-frame allocation-bit loads plus original-FDT rechecks |
| Persistent availability storage | Bootstrap extent descriptors; later VM bitmap/cache | Canonical geometry, counts and `P/w` allocation words |
| Initialization auxiliary storage | Fixed descriptor arrays and constant-depth calls | At least 5,376 bytes of nested explicit automatic range arrays, plus compiler frames |

The audit's `O(p * (m^2 + r))` bound remains conservative evidence of the
current divergence. A tighter interval argument is useful to the correction:
for ordered, disjoint managed intervals, a successful union-coverage step
that stops before its interval end advances to a raw memory endpoint inside
that interval. Such an endpoint cannot be an interior step in another
disjoint interval. Final overshoots contribute at most one step per interval.
The total coverage-loop iterations across whole managed intervals are
therefore at most `m + g`, each scanning at most `m` raw records.

Retaining only the complete-interval checks can thus bound raw-range work by
`O(m(m + g) + gr)`, independent from `p`. With the accepted
`g <= m + r + 1` component bound including the kernel reservation, this stays
inside the existing range-normalization complexity envelope. A separate
word-based count and inactive-tail pass must be accounted over `P/w`; a
per-frame bit-count helper cannot be hidden behind a word-load counter.

## Accepted mechanism mapping

These classifications describe the Accepted design, not an already implemented
correction:

| Difference or mechanism | Classification | Bound and reason |
| --- | --- | --- |
| Temporary kernel allocation before VM policy | Baseline parity | Same bootstrap owner; ordinary VM selection remains a later authority |
| FDT-derived, sorted, compact unsigned-64-bit geometry | Required adaptation | Accepted sparse target representation and immutable ownership/handoff indexes replace multiboot descriptors |
| Retained bootstrap range progress represented by one lower-bound bitmap cursor | Required adaptation | Geometry cannot be consumed in place; the scalar preserves progress while the bitmap alone authorizes availability |
| Ascending/lowest bootstrap result | Required adaptation | Preserve ADR-0017 while applying low-end range consumption to the accepted sorted geometry; do not claim parity with VM's descending/cache policy |
| Word skipping within the accepted bitmap | Required adaptation | Do not revisit an allocated prefix one frame at a time; monotone allocation matches the bootstrap range envelope, while fragmented release remains separately accounted |
| Whole-interval firmware coverage instead of per-frame repetition | Baseline parity | Restore range-based boot work without deleting independent range safety |
| Overflow, failure atomicity, typed generations and directly introduced cursor validation | Compatible extension | Retain accepted safety; cursor mutation is constant work, and the complete diagnostic check does not worsen its existing full-scan envelope |
| Native primitive observations | Compatible extension | Test-only, passive in ordinary builds, no new runtime authority |
| Existing complete ownership-validation cadence | Divergence requiring correction | Remains MEM03 and later affected-unit work; not hidden by the allocator's smaller search bound |
| Missing production VM bitmap/cache policy | Divergence requiring correction | Remains B07-F003; no VM cache or policy correction is included in MEM01 |

The range allocator reference has no corresponding arbitrary bootstrap
release operation. Consequently a fragmented release/reallocation trace does
not receive a nonexistent reference constant-time guarantee. The proposed
worst case states its actual word scan, while the monotone bootstrap path
removes the original quadratic prefix.

## Design constraints exposed by the trace

The following are requirements for the pending design, not permission to
start implementation:

- Retain the constructor's capacities, checked alignment/overflow rules,
  failure-atomic publication, sparse bitmap index and exact canonical
  memory/reservation storage.
- Preserve the current lowest-address contract unless the design explicitly
  supersedes it with a justified phase-specific baseline.
- Retain allocation progress without treating a mutable search coordinate as
  frame ownership or a second availability authority.
- Specify every bitmap mutation path. Ordinary release, typed allocation
  rollback, process-set release and the shared VM release primitive must not
  leave search state inconsistent. Setting another bit and freeing a bit
  have different effects on any lower-bound search invariant.
- Preserve byte-exact rollback, including any new search fields, after the
  typed allocator's defensive post-allocation failure.
- Preserve the existing partial target snapshots: the grant-syscall fixture
  and address-space-handoff fixture copy and compare the allocator prefix
  through `offsetof(..., allocated_bitmap)` before its active words.
  New search metadata must be inside that prefix, not silently appended
  beyond their observation boundary.
- Keep complete ownership-validation cadence and its active/inactive checks;
  MEM01 may maintain a directly introduced invariant but does not close
  MEM03 or B07 validation findings.
- Retain complete original-FDT interval safety while removing the duplicate
  per-frame proof. Specify header bounds before derived indexing and explicit
  partial/inactive bitmap-word treatment.
- Use actual `KERNEL_MEMORY` primitive observations from the existing EV02
  vocabulary, including range visits, index probes and bitmap-word loads.
  Keep measurement test-only and counters/snapshots outside automatic stacks.
- Supply discriminating native evidence and actual target integration at the
  correct layer, not a compilation error, timing threshold or a newly
  generated expected result.
- Preserve every unrelated identity, source owner, public error, handoff
  phase, kernel/service authority and current stack/image/deadline budget.

## Historical authority boundary

Current inventory inspection identifies whole-file transport assertions for
the five existing kernel memory owners above. These retain their original
values under `tests/host/kernel_layout_source.py:KERNEL_FILE_FACTS`.
The shared C inspector's digest is a different authority and is not a
candidate-source assertion to retire.

The complete ownership validator also passes through
`restore_frame_validation_hot_path`, which accepts only the fixed original
or fixed bounded-validator source hashes before the later whole-file
assertion runs. Its actual active/inactive token identities are separately
declared by `test_frame_validation_hot_path.py`. A design that introduces
search-state validation must account for that earlier assertion boundary;
retiring only the final file check would not authorize a changed input to
the earlier adapter.

The exact candidate set, retained function/type/data register, planned
retirement keys, replacement oracles and affected-unit recertification must
be closed in the separately reviewed design before any old assertion can be
suppressed. No expectation has been regenerated and no identity is retired
by this study.

On corrected #208, all six prospective retirement facts have precise
two-path payloads: their actual memory C source and the shared evidence
owner. Their union fits the declared eight candidate paths once the header
and directory adapter are included by unit inputs. CMake and the planner
remain in the disjoint immutable envelope. The whole current 3,938-record,
fourteen-family inventory includes the Accepted corrected authority-core
fingerprint; MEM01 does not restore the superseded 797-path binding.

# ADR-0101: MINIX Kernel Mapping Topology and Direct Table Identity

- Status: Accepted
- Date: 2026-11-05
- Refines: ADR-0018, ADR-0020, ADR-0021, ADR-0053, ADR-0073, ADR-0080,
  ADR-0082, ADR-0083, ADR-0084, and ADR-0100
- Supersedes in part, after separate design merge:
  ADR-0018's base-page-only kernel map; ADR-0021's description of every
  shared kernel root entry as a non-leaf; and the three exact historical
  kernel-map source identities only after the successful MEM02 transition.

## Context

MEM01 implementation #210 is merged as
`3f7e33026f92c8c1765ce00c2b03b5c2d6855197`, with reviewed, committed,
remote and squash tree `08f31a8b1dfc639c8b826ea65d092327fea159a1`.
It removes repeated bootstrap allocation/range work while preserving typed
ownership, exact lowest-address allocation and all existing limits.

MEM02 is now the next preferred dependency-ready correction. It owns only
B02-F003/B02-F004 and B02-E003. The
[fixed-source study](../research/minix-kernel-mapping-topology.md) establishes:

- MINIX uses large bootstrap/kernel leaves and directory-indexed software
  table pointers;
- the current kernel emits only 4 KiB leaves and linearly searches table
  frames on construction and final validation paths;
- Sv39 already supplies 2 MiB and 1 GiB leaves;
- the existing portable PTE codec can retain its public contract; and
- shared process-root entries are copied and compared exactly, so the kernel
  leaf depth need not become private user mapping policy.

The original complete owner audit and ordinary process/root validation remain
separate MEM03/MEM04 findings.

## Decision

After independent review, Acceptance and separate merge, implement the
[kernel mapping contract](../architecture/kernel-mapping-topology.md).

### Preserve exact mapping authority

Retain the current identity layout, text RX, read-only data R, writable
kernel and managed RAM RW/NX, four exact UART/PLIC pages, adjacent PLIC
absence, user-window exclusion, `U=0`, `G=0`, A/D policy and zero
RSW/reserved bits.

Within each existing admitted domain, select the largest aligned leaf that
fits completely: 1 GiB, then 2 MiB, then 4 KiB. Do not combine adjacent
domains merely because their permissions match. Do not split managed RAM
according to transient per-frame owner kinds: the existing supervisor
direct-map authority already covers both free and allocated managed frames.

No dynamic kernel-map mutation, implicit split/merge, user large page,
global-PTE policy, higher-half relocation, ASID or TLB policy is introduced.

### Direct parent-indexed table identities

Retain the existing 1024-frame table bound and append-only physical table
list. Add checked `uint16_t` child-slot identifiers indexed directly by
root VPN and by root/middle VPN pair. Zero denotes no child; a nonzero
identifier denotes table slot plus one.

Use the existing one-shot pristine construction invariant: allocation
ordinal `i` is compact managed-frame index `i`. Establish that relation
against the actual allocated physical address and checked range geometry,
not the allocator's search cursor. Retain one range index per table so later
selected owner/bitmap/address checks do not scan ranges or call a
geometry-wide owner lookup.

Hardware PTEs, software child slots, physical table records, allocator bits
and exact `KERNEL_PAGE_TABLE` records must agree before dereference.
Unknown or inconsistent identities fail; no scan fallback or index repair
exists.

The exact fixed metadata increase is 515 KiB in the existing kernel-private
state. It is included in BSS and `[0, __kernel_end)`, not allocated recursively
from the pool being constructed. This explicit Sv39 three-level/fixed-bound
adaptation retains MINIX's direct parent-indexed relationship. It is not
claimed to be storage-free, a net resident-memory saving or an optimized
sparse index. The constructor depends on a read-only selected-frame helper
in the validation owner; the private header remains declarations-only.

### Preserve complete validation and publication

Keep complete ownership validation at the existing boundary. Retain its
active/inactive coverage and invocation cadence, and account separately for
the current pristine/bitmap scans and typed allocation geometry work.

Replace only mapping/table-membership searches and base-page expansion of
expected kernel coverage. Validate the real tree, direct identities,
distinct reachability, exact owner/bitmap/table agreement, largest legal
leaves, complete domain coverage and absence of extra leaves.

Keep error values, SIE-clear construction, the existing fence/write/fence/
readback sequence and report publication last. An initialization failure
does not publish an active root or report and may retain the existing
unpublished partial boot candidate. It is fatal to boot, not a new
recoverable transaction.

### Independent native and hardware evidence

Measure actual fixed #210 and current portable mapping/validation source
under declared native physical-page and numeric-linker bindings. Use the
real allocator and typed ownership implementations; do not replace their
algorithms or infer their costs from call counts. Privileged initialization
and hardware translation are not claimed by those host bindings.

Use the existing MMU component for actual target Red/Green. Preserve its
original two permission faults and marker, then add isolated, read-only
2 MiB/1 GiB translation and misalignment probes in an unpublished temporary
test root. Return to the real kernel root before every test-root mutation
and before releasing test frames. The production mapping set never gains
those aliases.

The same MMU ELF runs at 128 MiB and 256 MiB with the original per-guest
timeout. A new component marker requires actual expected topology and
hardware/resource guards. Baseline execution retains the original complete
MMU acceptance and records the audited topology/work divergences separately.
One exact zero-stack assembly-probe binding extends, rather than replaces,
the existing source/object stack proof.

Three independently declared oracles cover topology, direct table work and
actual target behavior. Preserve all original 3,946 complete facts and
fifteen fingerprints, prior committed claims and all unrelated gates.
Only the three named kernel-map source identities may retire.

## Scope and non-goals

Production changes stay in the two kernel-map owners and their private
header. Public kernel-map/Sv39 interfaces and the portable PTE codec remain
unchanged. Existing MMU C/assembly evidence may receive exact reversible
test-only extensions; its original assertions remain active.

No MEM03/MEM04 cadence change, VM mapping/allocation policy, user large pages,
release/handoff redesign, larger frame/table capacity, stack/image budget
increase, new runtime workflow, hosted-CI acceptance or node-Q implementation.

## Acceptance and phase barrier

The design contains documents only. No executable prototype, test/model,
harness/generator, runtime/header, assembly, build scaffold or dependency
change precedes separate design Acceptance and merge.

The dependent implementation requires independently frozen source, recipe,
primitive and model declarations; actual discriminating native and QEMU Red;
complete current semantic/work/hardware Green; exact affected-unit
recertification; all 50 fresh author commands; 44 static configurations,
113 ELF outputs and ten observed stack scopes with B13 still blocked;
preserved original reusable-profile eligibility; and independent
implementation review before the first permanent commit.

Publish only the identical reviewed/committed/remote/preview/squash tree,
verify ordinary committed authority without author journals and remove only
the completed task refs.

## Design review disposition

Independent read-only review inspected the complete nine-document proposal,
the fixed/current source and relevant Accepted contracts, and the exact
documentation-only author evidence for staged tree
`3a9b906853baabea8d9350f957a1681eb2b59749`. All four required author commands
were FRESH/exit0; native/build/QEMU execution was correctly N/A.

The reviewer reported: "No significant issues found in the reviewed changes.
No substantive design issue remains." No targeted reproduction or validation
rerun was needed. Acceptance records this design result, not an implementation
or a passing future runtime gate. Implementation remains pending the separate
design merge.

## Alternatives

- Keep all 4 KiB leaves: retains B02-F003 and unnecessary hardware tables.
- Search the physical table list: retains B02-F004.
- Replace the search with hashing or a balanced lookup tree: changes the
  reference direct-index family and does not provide the required direct
  worst-case relation.
- Trust an arbitrary decoded physical pointer: loses selected ownership and
  bounds checks before dereference.
- Use PTE reserved/RSW bits as a private table identifier: changes the retained
  encoding contract and is unnecessary.
- Allocate a second metadata-frame hierarchy: adds ownership, allocation and
  failure machinery before the fixed direct-index baseline works.
- Merge writable kernel and managed domains: obscures the exact original
  ownership/exclusion boundary.
- Change full ownership-validation cadence here: expands into MEM03.

## References

- [Fixed MINIX and current-source study](../research/minix-kernel-mapping-topology.md)
- [Exact kernel mapping contract](../architecture/kernel-mapping-topology.md)
- [B02 memory audit](../research/minix-complete-baseline-b02-memory.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)
- [Sv39 specification](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html#sv39)

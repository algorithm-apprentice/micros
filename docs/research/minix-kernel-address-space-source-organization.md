# MINIX Kernel Address-Space Source-Organization Baseline

This note records the MINIX 3 source-ownership baseline used to design the
first responsibility-based decomposition of the `micros` kernel
address-space and user-execution sources. Runtime behavior remains governed
by the Accepted `micros` ADRs, especially ADR-0021, ADR-0022, ADR-0040,
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

The representative sources are:

```text
minix/kernel/system/do_vmctl.c
minix/kernel/arch/i386/arch_do_vmctl.c
minix/servers/vm/main.c
minix/servers/vm/pagetable.c
minix/servers/vm/region.c
minix/servers/vm/pagefaults.c
minix/servers/vm/fork.c
minix/servers/vm/exit.c
minix/servers/vm/proto.h
```

The inspection used the local tree only. Representative commands were:

```sh
git -C /Users/alephling/Repo/minix rev-parse HEAD
sed -n '1,185p' /Users/alephling/Repo/minix/minix/kernel/system/do_vmctl.c
sed -n '1,90p' /Users/alephling/Repo/minix/minix/kernel/arch/i386/arch_do_vmctl.c
sed -n '760,1068p' /Users/alephling/Repo/minix/minix/servers/vm/pagetable.c
sed -n '1350,1485p' /Users/alephling/Repo/minix/minix/servers/vm/pagetable.c
sed -n '424,620p' /Users/alephling/Repo/minix/minix/servers/vm/region.c
sed -n '70,390p' /Users/alephling/Repo/minix/minix/servers/vm/pagefaults.c
sed -n '1,130p' /Users/alephling/Repo/minix/minix/servers/vm/fork.c
sed -n '1,105p' /Users/alephling/Repo/minix/minix/servers/vm/exit.c
sed -n '280,390p' /Users/alephling/Repo/minix/minix/servers/vm/main.c
sed -n '1,260p' /Users/alephling/Repo/minix/minix/servers/vm/proto.h
```

No GitHub or web copy was used.

## Privileged kernel ownership

MINIX does not place all virtual-memory work in one kernel file.
`do_vmctl.c` owns the generic privileged control dispatch that stops or
resumes processes, clears page-fault state, and forwards architecture-specific
commands. `arch_do_vmctl.c` owns architecture binding and invalidation:

- reading the current process page-table root;
- binding a new root to one process;
- loading the active hardware root when required; and
- flushing the complete TLB or one page.

The reusable ownership boundary is that the kernel retains privileged root
binding, activation, inhibition, and invalidation while VM supplies memory
policy and page-table content.

`micros` preserves that boundary with a different mechanism. Sv39 PTE mutation
remains in the kernel because the current VM server is unprivileged and sends
validated operation-12 plans rather than directly writing page tables. The
kernel still does not select ordinary post-handoff frames or application
layout policy.

## VM page-table mechanism

`pagetable.c` owns the architecture-facing VM page-table representation and
mutation:

- allocating and clearing page-table roots and subordinate tables;
- installing kernel mappings in each process root;
- preparing all required subordinate tables before writing a mapping;
- writing, clearing, checking, and copying PTE ranges;
- binding a completed root to the kernel; and
- releasing page-table storage.

The `pt_writemap()` ordering is particularly relevant. It allocates every
required page table before the first mapping write because undoing a partially
published mapping is difficult. This is behavioral evidence for keeping
preflight, resource preparation, commit, and rollback visible as separate
tasks.

The source shape is not adopted literally. `pagetable.c` combines bootstrap
self-mapping, spare-page management, PTE mutation, binding, and kernel
mapping breadth that `micros` does not need. The useful baseline is one
page-table mechanism owner below higher-level mapping policy.

## VM region and mapping policy

`region.c` owns virtual-region policy rather than hardware-root activation:

- choosing a free virtual range;
- constructing and linking region metadata;
- preallocating region contents when requested;
- resolving a virtual address to a region and physical block;
- handling region faults and writes;
- splitting, extending, unmapping, copying, and freeing regions; and
- releasing every region owned by one process.

Region teardown and page-table teardown remain distinct. `map_free_proc()`
releases the process's region tree, while `pt_free()` releases page-table
storage. `exit.c` orchestrates both through `free_proc()` and then clears the
VM process slot.

This supports separate `micros` owners for ordinary root lifecycle, mapping
lookup and mutation, complete structural validation, and VM-directed
teardown. It does not justify importing MINIX's dynamic region tree, slab
allocator, copy-on-write, file-backed mappings, or broad `mmap` surface.

## Fault, process-copy, startup, and exit owners

MINIX keeps several higher-level workflows outside the page-table mechanism:

- `pagefaults.c` validates the faulting endpoint and region, applies access
  policy, asks the region layer to materialize the mapping, clears stale
  mapping caches, and only then unblocks the process;
- `fork.c` owns process-copy orchestration, new root creation, region copy,
  kernel process creation, root binding, and message-buffer preparation;
- `main.c` owns boot-process ELF loading and initial stack construction,
  calling the page-table and region mechanisms through narrow callbacks; and
- `exit.c` owns process memory release and VM-slot cleanup.

These workflows change for different reasons and are not folded into
`pagetable.c`. The useful baseline is an acyclic dependency direction:
higher-level lifecycle or fault orchestration depends on lower-level mapping
and page-table mechanisms.

## Private contract observation

MINIX exposes a broad server-private `proto.h` that declares functions across
allocation, regions, page tables, faults, process lifecycle, files, and
service control. That header reflects the larger monolithic VM server and is
not a source-shape authority for `micros`.

`micros` uses narrower private contracts:

- one private kernel-root contract;
- one private user-address-space contract;
- one private user-execution contract; and
- test-private contracts that production sources cannot include.

Private headers contain declarations and shared types, not function bodies.
State is passed explicitly rather than recovered through a new broad mutable
global namespace.

## Difference classification

The classification follows
[ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md).

| Topic | MINIX baseline | `micros` adaptation | Classification |
| --- | --- | --- | --- |
| Privileged boundary | Kernel binds roots, inhibits processes, and flushes translations; VM owns mapping policy | The same authority boundary is retained, but kernel-validated Sv39 mutation replaces direct VM page-table writes | Required adaptation |
| Page-table mechanism | `pagetable.c` owns table allocation, mapping writes, kernel entries, binding, and release | Separate kernel-root and private-user-table mechanism modules retain the accepted Sv39 representation | Baseline parity |
| Mapping policy | `region.c` owns virtual-region selection, mapping, unmapping, copy, and process-region release | A fixed user window and bounded VM transaction plans replace dynamic regions while staying separate from low-level table walking | Required adaptation |
| Fault handling | `pagefaults.c` owns fault validation, region policy, retry, and process resumption | Non-VM fault delivery remains deferred; the decomposition creates no fault path | Staged substitution |
| Process lifecycle | Fork, boot exec, and exit orchestration live outside page-table primitives | Wired handoff, VM freeze, VM teardown, and execution preparation remain higher-level owners | Baseline parity |
| Ownership validation | MINIX relies on VM metadata, assertions, and architecture structures | Generation-safe typed frame owners, complete root validation, exact shared-root equality, and corruption rejection remain mandatory | Compatible extension |
| Transaction atomicity | `pt_writemap()` prepares tables before writing; broader failure handling is not uniformly atomic | Every `micros` prepare/commit boundary, output-preservation rule, reverse rollback, and fatal post-commit invariant remains exact | Compatible extension |
| Special mappings | MINIX has VM self-mapping and architecture-specific mapping caches | `micros` retains only the Accepted TTY UART leaf and token-bound VM scratch exception | Required adaptation |
| Execution context | MINIX process and architecture code owns context and root installation separately from VM region policy | `micros` keeps thread-owned contexts and stacks distinct from address-space ownership | Compatible extension |
| Source ownership hierarchy | Responsibilities are separated across kernel control, page tables, regions, faults, fork, startup, and exit | `micros` retains the same higher-to-lower responsibility direction | Baseline parity |
| Semantic function phases | MINIX exposes some preparation-before-mutation boundaries but also retains long mixed functions | `micros` requires named preflight, preparation, commit, rollback, publication, and evidence phases where they are distinct tasks | Compatible extension |

## Derived constraints

The baseline and Accepted `micros` decisions imply:

1. Keep kernel-root construction and validation separate from per-process
   user-root operations.
2. Keep low-level page-table traversal below lifecycle, handoff, device,
   scratch, freeze, teardown, activation, and execution orchestration.
3. Keep complete validation read-only and independent of operation handlers.
4. Keep one higher-level wired-handoff owner above both address-space and
   execution validation, rather than introducing a low-level dependency
   cycle.
5. Keep ordinary mapping mutation separate from exact TTY, scratch, freeze,
   and teardown exceptions.
6. Preserve one shared bounded validation/release scratch owner and pass its
   lease explicitly across private modules.
7. Keep context preparation and stack ownership separate from trap capture
   and return validation.
8. Preserve the existing public APIs and evidence identities. Source
   decomposition does not imply another syscall, CTest, or QEMU workflow.
9. Do not copy MINIX's VM-global `proto.h`, dynamic region framework, spare
   page machinery, copy-on-write, file-backed mapping, fault retry, or SMP
   behavior.

## Non-goals

This baseline does not authorize:

- any address-space, frame-owner, execution, syscall, or wire behavior
  change;
- post-handoff general map, unmap, allocation, or fault delivery;
- `fork`, copy-on-write, `mmap`, file-backed paging, ASIDs, SMP shootdown, or
  recovery;
- changes to public `micros` headers or structure layouts;
- additional test executions or broader validation cadence; or
- copying MINIX source, identifiers, comments, or data structures.

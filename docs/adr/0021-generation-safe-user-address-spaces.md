# ADR-0021: Generation-Safe User Address Spaces

- Status: Accepted
- Date: 2026-10-06

## Context

The kernel now has:

- an active Sv39 kernel root with exact supervisor-only permissions;
- generation-safe process objects with a reserved address-space field;
- a typed frame ledger that distinguishes kernel tables, process tables, and
  process user frames;
- exact process-release blocking while process-bound frames remain.

The next development-DAG step needs private process roots before U-mode entry,
saved contexts, switching, or scheduling can be built. The address-space
mechanism must not accidentally make the one-thread or one-hart MVP limits
structural, and it must not move future VM mapping policy permanently into the
kernel.

The kernel identity map occupies Sv39 root index 2 for RAM beginning at
`0x80000000`. UART occupies part of root index 0. Root index 1, covering the
lower canonical interval `[0x40000000, 0x80000000)`, is currently unused.
Using that complete one-GiB slot for bootstrap user mappings permits each
process to own one private subtree while sharing immutable supervisor-only
kernel subtrees.

Earlier review identified five requirements that must be explicit before
implementation:

- an active address space cannot be mutated without immediate invalidation;
- trap entry must not execute C with a stale live `sstatus.SUM`;
- destruction must be ordered after thread lifetime;
- tests must prove complete-page zeroing through dirty-frame reuse;
- validation must reject leaves redirected to foreign user or page-table
  frames through the typed ledger.

## Decision

### Scope and authority

This slice implements the kernel mechanism for:

- creating and destroying one private Sv39 root for a live process generation;
- allocating, zeroing, mapping, looking up, unmapping, and releasing bootstrap
  anonymous user pages;
- validating exact root composition, PTE structure, reachability, and typed
  ownership;
- activating a process root or the kernel root with ASID zero and full local
  invalidation.

The bootstrap kernel chooses anonymous page placement only until the one-way
VM handoff. The process create, destroy, map, unmap, lookup, validate, and
activation contract defined here is explicitly a `BOOTSTRAP`-phase contract
and rejects process-root operations once typed frame ownership has entered
`HANDED_OFF`. Kernel-root activation remains available.

A later VM protocol ADR must define handed-off mapping authority and
validation before production performs that handoff. In particular,
`VM_TRANSFERABLE` intentionally loses process identity, so the later protocol
must add authoritative mapping metadata or whole-system reachability
validation rather than pretending the bootstrap `PROCESS_USER` check remains
valid. That ADR will also define mapping generations, prepared-address-space
sealing, and post-handoff allocation without replacing the root composition
or Sv39 walk representation introduced here.

This slice does not implement U-mode entry, saved execution contexts, kernel
thread stacks, scheduling, IPC, ELF loading, page-fault delivery, ASID
allocation, mapping-generation tokens, or the VM server.

### Process lifecycle attachment

The portable process-object module adds exact attach and detach operations for
its existing `address_space_root` field:

```c
micros_process_attach_address_space(objects, process, root);
micros_process_detach_address_space(objects, process, expected_root);
```

Attach requires:

- an exact live process handle;
- a nonzero root;
- no root already attached.

Detach requires:

- the exact live process and root;
- zero live threads.

Process release rejects a nonzero address-space root. Free and quarantined
process slots always contain a zero root. The object layer treats the root as
an opaque mechanism handle; Sv39 alignment, ownership, and reachability remain
the address-space module's responsibility.

The target address-space wrapper resolves only the production object registry.
No caller-supplied registry can substitute matching slot/generation values.
All multi-object operations execute with SIE clear.

### Virtual layout and root composition

The bootstrap user window is:

```text
base = 0x0000000040000000
end  = 0x0000000080000000
```

It is page aligned, lower-canonical, and occupies exactly Sv39 VPN2 index 1.
Every process root is a private `PROCESS_PAGE_TABLE` frame owned by the exact
process generation.

Creation:

1. resolves the exact live process and requires no attached root;
2. allocates one `PROCESS_PAGE_TABLE` frame;
3. clears all 4096 bytes;
4. copies each nonzero kernel-root entry outside VPN2 index 1;
5. leaves VPN2 index 1 absent;
6. verifies every other root entry exactly equals the kernel root, including
   zero entries;
7. attaches the root to the process as the final state change.

The shared entries continue to point to immutable `KERNEL_PAGE_TABLE`
subtrees. No process operation mutates those tables. Every private intermediate
or leaf table below root index 1 is owned as `PROCESS_PAGE_TABLE` by the same
exact process generation.

### Bootstrap anonymous mappings

The target API is:

```c
micros_user_address_space_create(process);
micros_user_address_space_destroy(process);
micros_user_address_space_allocate_page(
    process,
    virtual_address,
    permissions,
    physical_address
);
micros_user_address_space_release_page(
    process,
    virtual_address,
    physical_address
);
micros_user_address_space_lookup(
    process,
    virtual_address,
    physical_address,
    permissions
);
micros_user_address_space_activate(process);
micros_user_address_space_activate_kernel(void);
micros_user_address_space_validate(process);
```

All output arguments remain unchanged on failure.

Mapping accepts one page-aligned address inside the user window and only the
portable `R`, `R|W`, `X`, `R|X`, or other Sv39-valid nonempty combinations
that do not contain `U`, reserved bits, `W` without `R`, or `W|X`. The wrapper
adds `U`; all user leaves are 4 KiB, non-global, and use the existing `A`/`D`
rules. Mapping an existing leaf is a conflict.

Before linking any new PTE, mapping:

1. validates the complete existing address space;
2. computes every missing private table;
3. allocates all required `PROCESS_PAGE_TABLE` frames and one
   `PROCESS_USER` frame for the exact process generation;
4. clears every allocated frame byte for byte;
5. constructs every PTE in local values;
6. links the prepared tables and leaf in one non-failing pass;
7. publishes the physical-address output.

Any allocation or preparation failure releases all unpublished frames in
reverse order and leaves the root, ledger, allocator, process object, and
output unchanged.

Lookup is read-only and returns the exact mapped physical page and permissions.

Teardown uses one additional portable ownership operation:

```c
micros_frame_ownership_release_process_set(
    ownership,
    process,
    release_bitmap,
    release_word_count
);
```

The caller supplies a fixed-size bitmap over the ledger's managed indices.
The operation first requires the ownership phase to be `BOOTSTRAP`; any
`HANDED_OFF` call returns `PHASE` before bitmap inspection and preserves every
byte. It then validates the complete ledger and every selected bit, requires
each selected frame to be `PROCESS_PAGE_TABLE` or `PROCESS_USER` for the exact
process generation, computes resulting counts, and verifies that every
selected allocator bit is set. Only after the complete preflight does one
non-failing pass clear the allocator bits, owner records, handoff-plan bytes,
and install the precomputed resulting allocator free count, total owned count,
and per-kind counts. A failed preflight changes nothing; no callback or
fallible check occurs after the first mutation.

The bitmap represents a mathematical set; selecting an already selected index
is idempotent and carries no multiplicity. The target scratch builder clears
the complete bitmap before each transaction and rejects an out-of-range index
before setting any bit.

Unmapping:

1. validates the complete address space and exact user leaf;
2. requires the mapped frame to be `PROCESS_USER` for the same generation;
3. prepares one release bitmap containing that frame and any private tables
   that become empty;
4. atomically commits the ownership/allocator release set;
5. clears the leaf and pruned parent entries through prevalidated non-failing
   stores;
6. publishes the released physical address.

All structural, owner, count, and active-root checks complete before the first
mutation. With SIE clear and the target root inactive, no observer can access
the brief commit interval between the ownership release-set commit and the
final PTE stores. If an impossible post-preflight condition is detected, the
kernel panics; no operation returns a recoverable error after partial mutation.

Every newly allocated table or user page is cleared across the complete
4096-byte frame, not only its currently used words.

### Inactive-root mutation and activation

All process roots use ASID zero in v0.1. Activation performs a full
`sfence.vma x0, x0`, writes the selected root to `satp`, performs a second full
fence, and verifies exact readback.

Create may run while the kernel root or another process root is active.
Map, unmap, and destroy reject the target process when its root is active.
They do not silently mutate an active table and do not defer an invalidation.
Because every mutation target is inactive and every later activation performs
full invalidation, no per-page flush is required in this slice.

Process-root activation first validates the complete root. Kernel-root
activation uses the already validated boot root. Both transitions preserve the
shared supervisor mapping needed by the executing kernel, trap stacks, UART,
ledger, and allocator.

ASID allocation, generation, selective invalidation, and remote shootdown are
deferred. A later multi-hart ADR may replace full local fences without changing
process identity, root ownership, or the mapping API's active-root rule.

ADR-0006's future VM scratch self-mapping cannot call this bootstrap mutation
API while the VM root remains active. The later VM mapping wrapper must switch
temporarily to the kernel root, mutate and validate the inactive VM root, then
reactivate it with the required fence sequence, or explicitly supersede this
rule in a new ADR. Silent active-root mutation is not an allowed
implementation shortcut.

### Validation and destruction

Validation walks only the private root-index-1 subtree and independently checks
the shared entries:

- typed frame ownership remains in `BOOTSTRAP`;
- the process handle resolves exactly and has one nonzero attached root;
- the root owner is `PROCESS_PAGE_TABLE` for that process generation;
- every root entry outside index 1 exactly equals the kernel root;
- the index-1 entry is absent or a valid non-leaf table;
- every private non-leaf points to a distinct exact
  `PROCESS_PAGE_TABLE` owner;
- every leaf occurs only at level zero, has `U`, has valid non-WX
  permissions, and points to a distinct exact `PROCESS_USER` owner;
- no private table or user frame is reachable twice;
- every `PROCESS_PAGE_TABLE` and `PROCESS_USER` ledger owner for that process
  is reachable with the matching role;
- no reachable private frame is free, transferable, kernel-owned, owned by
  another process, or owned by a stale generation.

The implementation owns exact statically allocated BSS scratch storage:

- one reachability bitmap;
- one release-set bitmap.

Each contains
`MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES / 64` 64-bit words, or 32 KiB.
Compile-time assertions bind their capacity to the allocator maximum. They are
never automatic stack objects. The one-hart implementation is deliberately
non-reentrant: SIE remains clear from scratch acquisition through validation
or commit, and a checked busy flag rejects accidental nesting. Native tests
exercise bit zero, the maximum index 262143, and out-of-range rejection.
No validation metadata is allocated from the frame allocator.

Destruction requires:

- an exact valid address space;
- zero live threads;
- an inactive target root.

It completes the full walk and owner preflight, marks every private frame in
the release-set bitmap, and verifies that the set equals all
`PROCESS_PAGE_TABLE` and `PROCESS_USER` owners for that generation. Commit
then:

1. atomically applies the ownership/allocator release set;
2. clears private PTEs through non-failing stores;
3. clears the prevalidated exact process root as the final non-failing object
   store.

The ordinary portable detach API remains the lifecycle contract and is tested
independently. The target commit holds the already resolved mutable production
process slot; any impossible disagreement after preflight is fatal rather than
returning a partially detached address space. Process release can then proceed
through the existing ownership-aware runtime wrapper.

Corruption is never repaired. A foreign leaf, aliased frame, malformed PTE,
or orphan owner rejects validation, unmap, destruction, and activation before
any release.

### Trap-entry SUM discipline

The first user mappings make stale supervisor access to user pages a concrete
hazard. This ADR supersedes only ADR-0019's regular-prologue ordering from its
step 6 through step 8. The replacement exact sequence is:

1. complete ADR-0019 steps 1-5: route through the hart anchor, allocate the
   primary frame, and copy saved `t0`-`t2`, interrupted `sp`, and interrupted
   `tp`;
2. write the hart pointer to `hart_context` and install the same pointer in
   kernel `tp`;
3. read interrupted `sstatus` into `t0` and store that unchanged value in the
   frame;
4. clear live `sstatus.SUM` with a CSR operation;
5. write zero to `sscratch`, arming the nested-trap sentinel;
6. clear the hart-anchor scratch words and save the remaining GPRs, `sepc`,
   `scause`, and `stval` without recapturing `sstatus`;
7. dispatch to C.

The primary stack, early frame words, hart anchor, `sstatus` frame store, and
CSR instructions through sentinel arming are wired, aligned, and non-faulting.
The saved frame value remains unchanged. Exit restores only the validated
selected frame value on `sret`, with SIE still forcibly clear as defined by
ADR-0015.

A trap taken while interrupted code has SUM set therefore records SUM for
correct return but executes the kernel dispatcher with SUM clear. Nested panic
entry after sentinel arming also observes SUM clear.

The trap-frame layout, complete register preservation, hart routing, nested
stack selection, and return ABI remain unchanged. ADR-0015's statement that
the dispatcher does not alter the captured status remains true; the new action
changes only the live CSR before dispatch.

### Native tests

Portable kernel-object tests add:

- attach success and duplicate-attach rejection;
- exact-root and stale-generation detach rejection;
- detach rejection while a live thread exists;
- process-release rejection while a root is attached;
- failure-atomic state and output checks;
- validator corruption cases for free, live, and quarantined root state;
- seeded-model attach/detach operations.

Portable ownership tests add release-set coverage for empty, single-frame,
multi-kind, maximum-index, out-of-range bitmap shape, wrong-generation,
free-bit, count-corruption, and byte-exact failed-preflight cases. A handed-off
case selects a retained `PROCESS_PAGE_TABLE` frame and requires `PHASE` with
exact allocator/ledger preservation. A seeded model compares release-set
commits with an independent bitmap/owner reference state.

The existing Sv39 encoding tests remain the portable PTE contract. Target page
tables are exercised in QEMU because their correctness depends on real
physical direct-map access, `satp`, translation caches, and page faults.

### QEMU component tests

The existing trap-recovery image sets SUM before its exception. Dispatch must
observe the saved SUM bit set while a direct CSR read shows live SUM clear. Its
selected return frame clears SUM, and the post-`sret` snapshot confirms that
value. The nested-trap image also sets SUM before the outer exception; the
saved outer frame must retain SUM while the nested handler's direct CSR read
must observe SUM clear at the first injected instruction after sentinel
arming.

A separate `MICROS_BUILD_USER_ADDRESS_SPACE_TEST` image runs after normal
object, FDT, allocator, Sv39, and ownership readiness. It:

1. creates two process generations and private roots;
2. proves both roots share every kernel entry but have distinct private
   root-index-1 subtrees;
3. maps the same user virtual address in both processes and proves distinct
   exact `PROCESS_USER` frames and values;
4. activates each ASID-zero root with full fences and proves same-VA isolation
   while kernel, trap, UART, ledger, and allocator access remain live;
5. proves SUM-clear supervisor access faults while a bounded SUM-enabled
   access succeeds;
6. rejects caller-supplied `U`, empty, `W`-only, `W|X`, reserved permissions,
   duplicate mapping, and map/unmap/destruction of the active root without
   changing outputs or state;
7. uses a test-only deterministic allocation-failure hook after one and after
   two unpublished allocations, proving exact rollback of tables, user frames,
   PTEs, allocator, ledger, process attachment, and outputs;
8. dirties a complete user frame, unmaps it, reuses it at another address in
   the same private leaf table, and verifies all 4096 bytes are zero;
9. redirects a leaf first to another process's user frame and then to a
   page-table frame, proving validation and destruction reject both without
   release;
10. separately aliases one correctly owned user frame through two valid leaves
    and clears another valid leaf while retaining its exact owner, proving
    duplicate reachability and orphan detection reject validation, activation,
    unmap, and destruction without mutation;
11. creates a live thread and proves destruction remains blocked until that
   thread is released;
12. destroys one address space, dirties its freed root frame, reuses it for the
    next generation, and proves every non-shared root entry was cleared;
13. destroys all roots, restores the kernel root, and verifies the
    allocator/ledger return to the kernel-page-table baseline;
14. retains one live process with no root, commits the otherwise empty one-way
    ownership handoff, and invokes create, destroy, allocate, release, lookup,
    validate, and process-root activation, requiring `PHASE` plus byte-exact
    state/output preservation from each while kernel-root activation still
    succeeds;
15. releases the final rootless process and shuts down.

Only that complete sequence emits:

```text
MICROS_USER_ADDRESS_SPACE_TEST_PASS roots=isolated reuse=zeroed active=guarded ownership=validated sum=cleared
```

The host gate requires one exact pass record after ownership readiness, clean
SBI shutdown, no panic or explicit failure, and no timeout. Parser regressions
reject missing, duplicate, malformed, unterminated, or out-of-order records.

## Consequences

- Process identity now owns a concrete generation-safe Sv39 root.
- Supervisor kernel mappings remain shared without sharing writable user page
  tables.
- Typed frame ownership becomes an enforceable map and teardown authority
  rather than only an allocation record.
- Full local invalidation keeps ASID-zero switching correct for the one-hart
  MVP.
- Complete-page zeroing is observable before user data or stale PTEs can leak
  across reuse.
- U-mode entry and scheduling can consume this mechanism without defining VM
  policy prematurely.
- The kernel retains bootstrap user-frame policy only until the already
  defined one-way VM handoff.

## Alternatives considered

### Put user mappings below root index 0

UART already occupies that root slot. Sharing its complete subtree would mix
immutable supervisor entries with private user mutation and require copying or
splitting it. The unused index-1 slot provides a simpler ownership boundary.

### Give each process a complete copy of kernel page tables

This avoids shared subtrees but duplicates dozens of immutable table frames,
increases bootstrap memory use, and creates multiple copies whose permissions
must remain synchronized.

### Mutate an active root and issue a local page flush

That can be correct, but it adds per-address invalidation ordering before
mapping concurrency exists. Rejecting active-root mutation plus full flush on
activation is smaller and directly testable.

### Add ASIDs now

ASID allocation, reuse generations, selective invalidation, and future remote
shootdown are separate concurrency decisions. ASID zero with complete local
flush is sufficient for one hart.

### Store a page-table frame list in each process

The typed ledger and page-table walk already provide independent ownership and
reachability records. A second mutable list would need transactional updates
and could disagree with both.

### Combine U-mode entry and scheduling with this slice

That would mix root composition, ownership, privilege return, saved contexts,
kernel stacks, current-thread selection, and timer preemption. This slice first
makes isolation and teardown independently observable.

## Specification basis

- [RISC-V Privileged Architecture, Supervisor-Level ISA](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html)
- [ADR-0001: Target Platform](0001-target-platform.md)
- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0006: VM Bootstrap and Handoff](0006-vm-bootstrap-and-handoff.md)
- [ADR-0010: Testing and Observability](0010-testing-and-observability.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0015: Supervisor Trap Entry](0015-supervisor-trap-entry.md)
- [ADR-0018: Sv39 Kernel Address Space](0018-sv39-kernel-address-space.md)
- [ADR-0019: Kernel Object Identity and Ownership](0019-kernel-object-identity-and-ownership.md)
- [ADR-0020: Typed Bootstrap Frame Ownership](0020-typed-bootstrap-frame-ownership.md)
- [Development dependency DAG](../architecture/development-dag.md)

# ADR-0020: Typed Bootstrap Frame Ownership

- Status: Accepted
- Date: 2026-10-06

## Context

ADR-0017 intentionally implements only physical-frame availability. Its bitmap
can prove that a managed frame is free or allocated, but it cannot distinguish
a kernel page table from another kernel allocation, a process page table, user
data, or memory prepared for the VM handoff.

That distinction is now a dependency. The next address-space slice must be able
to reject a corrupted user leaf that points to another process's frame or to a
page-table frame. Checking only that the physical frame is allocated would
allow teardown to release foreign memory.

ADR-0017 therefore requires an authoritative typed ledger and a phase gate
before the first service, VM-wired, transferable, or user frame is allocated.
ADR-0006 additionally requires the later VM handoff to distinguish
kernel-retained, VM-wired, and VM-transferable memory and to close bootstrap
allocation in one direction.

The current target has no user frames yet. Every allocated managed frame is a
reachable Sv39 kernel page table recorded separately by
`kernel/address_space.c`. This slice can introduce the ledger, move those real
allocations behind it, and prove exact ownership without pulling user address
spaces or the VM server into the task.

## Decision

### Availability and ownership remain separate

`struct micros_frame_allocator` remains the canonical availability mechanism.
Its ranges, bitmap, and free count are unchanged. A separate
`struct micros_frame_ownership` binds to one initialized allocator and records
the semantic owner of each bitmap index.

The ownership table is dense over the allocator's existing managed-frame
indices. The supported maximum remains 262144 frames. Each entry is exactly
eight bytes:

```c
struct micros_frame_owner {
    uint32_t generation;
    uint16_t slot;
    uint8_t kind;
    uint8_t reserved;
};
```

The complete maximum owner table occupies two MiB of statically allocated BSS.
One additional byte per managed frame records a staged handoff target, for a
maximum of 256 KiB. A small fixed snapshot records all 96 possible managed
range descriptors. The complete ledger is therefore about 2.25 MiB, lies
inside `[0, __kernel_end)`, and is reserved before any managed segment is
derived. No ledger memory is allocated from the allocator it describes.

`reserved` is always zero. The all-zero entry is the free owner and preserves
one-shot initialization on zeroed caller storage.

### Owner kinds

The initial owner kinds are:

| Kind | Identity fields | Meaning |
| --- | --- | --- |
| `FREE` | zero | Allocator bit must be clear |
| `KERNEL_RETAINED` | zero | General non-transferable kernel allocation |
| `KERNEL_PAGE_TABLE` | zero | Kernel-root or shared kernel page-table frame |
| `KERNEL_TEMPORARY` | zero | Releasable bootstrap scratch/test allocation |
| `PROCESS_PAGE_TABLE` | exact process slot and generation | Private page-table frame owned by one process |
| `PROCESS_USER` | exact process slot and generation | Pre-handoff user data/code/stack frame |
| `VM_WIRED` | exact process slot and generation | User/service frame retained and wired across handoff |
| `VM_TRANSFERABLE` | zero | Frame whose policy ownership transfers to VM |

Process-bound owners require:

- slot less than `MICROS_PROCESS_CAPACITY`;
- a nonzero generation no greater than
  `MICROS_PROCESS_GENERATION_MAX`;
- a zero reserved byte.

The ledger stores the exact generation, so a reused process slot cannot release
or adopt a stale frame. The ledger does not resolve process liveness itself;
the owning subsystem must first resolve the process handle. This keeps the
portable memory mechanism independent from one particular object registry
instance while preserving exact durable identity.

Kernel and transferable kinds require zero slot and generation. Invalid kind,
reserved, slot, or generation combinations are rejected before mutation.

### Storage and initialization

Initialization requires:

- non-null zeroed ownership storage;
- an initialized allocator with at least one managed frame;
- every managed frame free;
- every allocator bitmap bit clear;
- free count equal to managed count.

It binds the exact allocator pointer and stores an immutable snapshot of:

- `managed_range_count`;
- `managed_frame_count`;
- every active `{base, frame_count, bitmap_offset}` tuple.

It enters the `BOOTSTRAP` phase and leaves every owner and handoff-plan entry
free. Reinitialization returns `ALREADY_INITIALIZED`. Nonzero storage or a
non-pristine allocator is rejected without changing either object.

The snapshot, rather than mutable allocator geometry, is authoritative for
physical-address/index translation. Before every low-level allocate or release,
the ledger performs a bounded comparison of the current range count, managed
count, and every active tuple against the snapshot. It validates count bounds
before indexing either the allocator ranges or bitmap. A mismatch returns an
invariant error before the low-level allocator is called.

After initialization, target code must not call
`micros_frame_allocator_allocate` or `micros_frame_allocator_release`
directly. All target mutation goes through the ownership layer. The low-level
API remains public for its native unit tests and as the mechanism used
internally by the ledger.

The target initializes the ledger immediately after
`MICROS_FRAME_ALLOCATOR_READY` and before the first kernel page-table frame is
allocated.

Portable operations require external serialization. The one-hart target
wrapper requires SIE clear for every ownership mutation, geometry check, and
handoff commit. No interrupt or other target path may mutate the allocator,
object registry, or ownership state inside that critical region.

### Portable operations

The portable API provides:

```c
micros_frame_owner_make_kernel(kind, owner);
micros_frame_owner_make_process(kind, process, owner);
micros_frame_ownership_initialize(ownership, allocator);
micros_frame_ownership_allocate(ownership, owner, physical_address);
micros_frame_ownership_release(ownership, expected_owner, physical_address);
micros_frame_ownership_prepare_handoff(
    ownership,
    physical_address,
    expected_owner,
    target
);
micros_frame_ownership_lookup(ownership, physical_address, owner);
micros_frame_ownership_complete_handoff(ownership);
micros_frame_ownership_validate(ownership);
```

All output arguments remain unchanged on failure.

Allocation is permitted only in the `BOOTSTRAP` phase and only for
`KERNEL_RETAINED`, `KERNEL_PAGE_TABLE`, `KERNEL_TEMPORARY`,
`PROCESS_PAGE_TABLE`, or `PROCESS_USER`. It:

1. validates the complete requested owner;
2. asks the bound allocator for its deterministic lowest free frame;
3. resolves that physical address back to exactly one managed-frame index;
4. verifies the corresponding ledger entry is free;
5. installs the owner and updates exact total and per-kind counts;
6. publishes the physical-address output.

If any check after low-level allocation fails, the allocator bit is released
before returning and the ledger and output remain unchanged. Such a mismatch
is reported as an invariant error.

Release is permitted only in `BOOTSTRAP`. It first resolves and validates the
physical frame, then requires the stored owner to equal the caller's expected
owner byte for byte. A wrong process generation, owner class, unmanaged frame,
free frame, or unaligned address is rejected before the allocator is touched.
After the low-level release succeeds, the entry becomes free and counts are
updated. A surprising low-level failure is an invariant error and does not
pretend that ownership was released.

Lookup is read-only in either phase. It returns the exact owner of one managed
frame, including `FREE`, and rejects unaligned or unmanaged addresses without
changing the output.

### Staged atomic handoff

Handoff preparation is permitted only in `BOOTSTRAP`. It requires the exact
stored owner but does not change that owner or any count. It records one of
three one-byte targets for the frame:

- no transition;
- `VM_WIRED`;
- `VM_TRANSFERABLE`.

The allowed plans are:

- `PROCESS_USER` to `VM_WIRED`, retaining the same process identity;
- `PROCESS_USER` to `VM_TRANSFERABLE`, clearing process identity at commit;
- `KERNEL_RETAINED` to `VM_TRANSFERABLE`.

Replacing or clearing a plan also requires the exact current owner. Allocation
and release always clear that frame's plan entry. Page-table and temporary
frames cannot receive a handoff target.

The later VM bootstrap protocol will construct this plan, validate the complete
`VM_READY` summary, quiesce process/address-space mutation, clear SIE, and call
the target handoff wrapper while holding the bootstrap-memory critical
section. The wrapper first verifies that every process-bound owner resolves to
the exact live process generation in the authoritative object registry.

`micros_frame_ownership_complete_handoff` then performs a full non-mutating
preflight:

- geometry, owner records, allocator bits, and all counts validate;
- no `KERNEL_TEMPORARY` frame exists;
- every `PROCESS_USER` frame has a wired or transferable plan;
- every other plan is absent or is an allowed kernel-retained transfer;
- resulting total and per-kind counts are computed without overflow.

Only after the complete preflight succeeds does one bounded pass apply every
planned owner change, clear every plan byte, install the precomputed counts,
and change the phase to `HANDED_OFF`. No callback, allocation, release, owner
resolution, or fallible check occurs after the first owner mutation. With the
target critical section held, the classification and phase transition are one
atomic system operation rather than externally visible per-frame
reclassifications.

Bootstrap allocate, release, and handoff-plan operations reject all calls after
that transition. Read-only lookup, reporting, and validation remain available.
The VM implementation will add its post-handoff allocation and release
protocol in the later ADR that defines the retained kernel pool and VM summary;
this slice establishes the authoritative plan and irreversible commit
mechanism without inventing that future policy.

Normal boot remains in `BOOTSTRAP` until VM exists. The isolated ownership test
may prepare and commit a representative plan immediately before shutdown to
prove the atomic gate.

### Validation invariants

Full validation checks:

- initialization magic, phase, allocator pointer, and managed count;
- current managed-range count and every active range tuple exactly match the
  immutable snapshot;
- every unused allocator managed-range entry is zero;
- every allocator bitmap bit beyond the managed-frame count is zero;
- every owner record has a valid kind/identity/reserved combination;
- an allocator bit is set exactly when the owner is not `FREE`;
- allocator `managed - free`, set bitmap bits, total owned count, and the sum
  of per-kind counts are equal;
- each stored per-kind count equals an independent scan;
- `HANDED_OFF` contains no `KERNEL_TEMPORARY` or `PROCESS_USER` frame;
- every handoff-plan entry is valid for its current owner and phase;
- unused owner and handoff-plan entries beyond the managed count remain zero.

Validation never repairs state. Any mismatch is an explicit invariant error.

### Target integration

The bootstrap wrapper owns one static ledger and exposes only typed operations.
Kernel Sv39 construction requests `KERNEL_PAGE_TABLE` for every root and
intermediate table. Its existing reachable-table list remains an independent
structural record; final validation requires every recorded table frame to have
the exact ledger class and requires the ledger's kernel-page-table count to
equal the reachable table count.

The allocator QEMU self-test requests `KERNEL_TEMPORARY` for each transient
frame and releases it with the exact same owner. It must restore both allocator
and ownership counts to the live post-MMU baseline.

The target runtime also becomes the production authority for process release.
Its serialized release wrapper queries the ledger for every process-bound
frame naming the exact handle and rejects release while that count is nonzero.
It calls the portable object-table release only after the ledger count reaches
zero. Generic object-table tests may still call the portable release directly,
but production target code after ledger initialization may not bypass the
runtime wrapper.

Target-level validation scans all process-bound `PROCESS_PAGE_TABLE`,
`PROCESS_USER`, and `VM_WIRED` owners, resolves each exact handle through the
object registry, and rejects stale or missing owners. Handoff preflight performs
the same cross-subsystem check while the critical section is held.

No current target allocation remains outside the ledger after initialization.

After Sv39 activation and `MICROS_MMU_READY`, every image emits:

```text
MICROS_FRAME_OWNERSHIP_READY owned=0x0000000000000000 kernel-tables=0x0000000000000000 phase=bootstrap
```

The zero fields specify width, not required values. Both are nonzero and equal
in the current system. Every target workflow requires exactly one valid record
before its final pass or panic outcome.

### Native tests

The native suite compiles the production ledger with the production frame
allocator under ASan and UBSan. Tests cover:

- zero-storage one-shot initialization and allocator binding;
- rejection of non-pristine, mismatched, or corrupted allocators, including
  range-count, tuple, unused-range, and trailing-bitmap corruption;
- every valid and invalid owner encoding;
- allocation, deterministic reuse, exact lookup, release, and output
  preservation;
- wrong kind, slot, generation, alignment, range, and phase rejection;
- byte-exact state preservation on every failed operation;
- allocator/ledger rollback when an injected post-allocation invariant check
  fails;
- every allowed and forbidden handoff plan;
- handoff rejection with temporary or unclassified process-user frames;
- one-way handoff completion and rejection of later mutation;
- corruption of bits, owner records, total counts, per-kind counts, phase,
  unused entries, and bound allocator metadata;
- maximum managed-frame capacity;
- a replayable seeded reference model containing allocation, exact-owner
  release, lookup, handoff planning, invalid operations, and full invariant
  checks after every step.

The reference model independently tracks physical availability, exact owners,
counts, and phase; it does not call production owner-validation helpers.

### QEMU component test

A separate `MICROS_BUILD_FRAME_OWNERSHIP_TEST` image runs after normal object,
FDT, allocator, Sv39, and ownership readiness. It:

1. records the live allocator and ledger baseline containing only reachable
   kernel page tables;
2. creates and releases one process, recreates the same slot at the next
   generation, and creates a second process;
3. allocates process-page-table and process-user frames for the replacement
   and second process;
4. proves the stale first-generation handle and the other process cannot
   release those frames;
5. proves production process release is blocked while an exact process-bound
   frame remains;
6. verifies exact owner lookup for every allocation;
7. stages one process-user frame as `VM_WIRED` with the same identity and
   another as `VM_TRANSFERABLE`;
8. proves forbidden cross-process and page-table plans do not mutate state;
9. validates the object registry, ledger, and every process-bound owner before
   handoff;
10. commits the complete plan while SIE is clear, retains representative
    `PROCESS_PAGE_TABLE`, `VM_WIRED`, and `VM_TRANSFERABLE` frames, and verifies
    their exact final owners;
11. proves later allocation, release, planning, and process release are
    rejected as required without changing state or outputs;
12. revalidates the sealed ledger and cross-subsystem owner relationships.

Only that complete sequence emits:

```text
MICROS_FRAME_OWNERSHIP_TEST_PASS stale=rejected release=blocked handoff=atomic invariants=preserved
```

The host gate requires exact readiness and pass records, no panic or explicit
failure, clean SBI shutdown, and no timeout. Parser regressions reject missing,
duplicate, malformed, unterminated, or out-of-order records.

## Consequences

- The allocator answers availability; the ledger answers authority.
- A process generation can later own page-table and user frames without stale
  slot reuse acquiring them.
- Kernel page-table ownership is independently cross-checked by the page-table
  tree, allocator bitmap, and typed ledger.
- User-address-space teardown can reject a leaf redirected to another process
  or to a page-table frame before releasing anything.
- The two-MiB dense table increases the reserved kernel image but keeps lookup
  bounded, allocation-free, and independent from physical-address sparsity.
- VM handoff classes, a staged plan, and an irreversible atomic commit exist
  before user frames, while post-handoff allocation policy remains deferred.
- The next address-space ADR may consume `PROCESS_PAGE_TABLE` and
  `PROCESS_USER` without expanding this prerequisite PR.

## Alternatives considered

### Infer ownership from page-table reachability

Reachability can prove that a frame is a table in one known tree, but it cannot
prove user-data ownership, distinguish two processes, or classify VM handoff
memory. A corrupted leaf may point to an allocated foreign frame that is not
reachable as a table.

### Store only one owner class per frame

A class without the exact process generation cannot distinguish a reused slot
from its predecessor. Address-space destruction would remain vulnerable to a
stale owner.

### Use a sparse fixed ownership array

A sparse array saves BSS while few frames are allocated but must eventually
represent every managed frame at handoff. Its capacity would either duplicate
the full bound or introduce metadata exhaustion before physical memory is
exhausted. Dense indexing reuses the allocator's canonical frame index.

### Add owner bits to the allocator bitmap

The availability allocator has a small, stable, independently tested contract.
Packing semantic ownership into it would couple low-level range allocation to
process and VM lifecycle and make the pre-ledger state harder to validate.

### Wait until the VM server

The first process user frame would then exist without an authoritative owner,
violating ADR-0017 and making safe address-space rollback and destruction
impossible to prove. The ledger is a direct prerequisite, not speculative VM
policy.

## Specification basis

- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0006: VM Bootstrap and Handoff](0006-vm-bootstrap-and-handoff.md)
- [ADR-0010: Testing and Observability](0010-testing-and-observability.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0017: Bootstrap Physical Frame Allocator](0017-bootstrap-physical-frame-allocator.md)
- [ADR-0018: Sv39 Kernel Address Space](0018-sv39-kernel-address-space.md)
- [ADR-0019: Kernel Object Identity and Ownership](0019-kernel-object-identity-and-ownership.md)
- [Development dependency DAG](../architecture/development-dag.md)

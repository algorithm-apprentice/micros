# ADR-0017: Bootstrap Physical Frame Allocator

- Status: Accepted
- Date: 2026-10-05

## Context

Development DAG Step 4 ends with the physical-memory mechanism required by
page tables, process objects, and user-mode entry. ADR-0006 assigns early frame
ownership to the kernel until the one-way VM handoff, but the current boot path
only reports the firmware-provided memory and reservation ranges.

The allocator must not release firmware, pre-kernel, kernel, stack, or
allocator metadata frames. The FDT blob must remain reserved only until
parsing completes, as required by ADR-0001. The allocator must also remain
directly testable on the host: range normalization, ownership, exhaustion, and
release validation are portable algorithms and should not require QEMU to
debug.

The current launch default has one hart and 128 MiB of RAM, but ADR-0001 makes
that size configurable without rebuilding the kernel. The bootstrap allocator
is not the future VM policy engine. It needs a bounded availability model that
can initialize page tables and provide an exact record of early kernel
allocations without prematurely designing the later ownership handoff.

## Decision

### Frame and range model

One physical frame is 4096 bytes. All internal ranges are unsigned 64-bit,
half-open physical intervals `[base, end)`.

Initialization consumes:

- every validated FDT memory range;
- every FDT reservation-map range;
- every static `/reserved-memory` range;
- the range `[0, __kernel_end)`, which reserves all physical memory below the
  fixed kernel load address plus the complete linker-defined image, code,
  data, BSS, allocator metadata, and boot and trap stacks.

Only frames derived from an FDT memory range can become managed. MMIO and other
physical addresses outside those ranges are never allocator candidates and do
not need synthetic reservations.

Initialization occurs only after `micros_fdt_parse_memory_map` has copied every
required memory and reservation range. No later boot step reads the raw blob,
so the FDT range is deliberately absent from the permanent reservation set and
its complete non-overlapping frames may be reclaimed.

Any input overflow, impossible alignment, or exceeded static capacity fails
initialization. Boot treats that failure as a structured panic rather than
silently ignoring memory or a reservation.

### Canonicalization

The portable allocator initializes in three bounded stages:

1. Memory ranges are aligned inward to complete frames, sorted by base, and
   merged when they overlap or touch.
2. Reservation ranges are aligned outward to every touched frame, sorted by
   base, and merged when they overlap or touch.
3. The canonical reservation union is subtracted from the canonical memory
   union, producing sorted, non-overlapping managed segments.

Partial frames at a memory boundary are unavailable. Any frame touched by a
reservation is unavailable. Overlapping or duplicate firmware descriptions
therefore cannot create duplicate ownership or accidentally recover a reserved
frame.

The supported bounds are:

- 16 input memory ranges;
- 80 input reservation ranges, covering the current maximum of 32 FDT
  reservation-map ranges, 32 static `/reserved-memory` ranges, the pre-kernel
  plus kernel range, and 15 future statically described bootstrap ranges;
- 96 managed segments, because subtracting 80 canonical reservations from 16
  canonical memory ranges can create at most 96 components;
- 262144 managed frames, supporting up to 1 GiB of total managed RAM
  independently from the 128 MiB default launch size.

These are checked capacities, not truncation points. A future target above
1 GiB must deliberately enlarge or replace the bootstrap metadata, but any RAM
size through that supported maximum remains a runtime FDT input rather than a
kernel ABI.

### Ownership representation

`struct micros_frame_allocator` owns:

- the canonical memory ranges;
- the canonical reserved ranges;
- the derived managed segments;
- a fixed allocation bitmap;
- total managed and free frame counts.

Each managed segment maps a physical base and frame count to a contiguous
bitmap offset. A clear bit is free and a set bit is allocated to the bootstrap
kernel. Frames outside the managed segments are never releasable through this
allocator.

Preserving the canonical memory and reservation unions plus every allocation
bit makes the allocator an exact availability ledger for this phase. Until a
later Accepted ADR adds typed ownership, the bootstrap wrapper exposes only
kernel-retained allocation; page-table frames allocated in the next slice are
therefore unambiguously kernel owned.

Before the first launcher, service, VM-wired, or transferable user frame is
allocated, the bootstrap memory subsystem must gain a separate authoritative
ownership ledger and phase gate satisfying ADR-0006. That later ledger will
classify kernel-retained, VM-transferable, and VM-wired allocations and define
the atomic handoff. ADR-0017 neither treats a free bit as a VM ownership class
nor claims that the availability bitmap alone completes the VM handoff.

### Allocation contract

The portable API is:

```c
enum micros_frame_allocator_error micros_frame_allocator_initialize(
    struct micros_frame_allocator *allocator,
    const struct micros_physical_range *memory_ranges,
    size_t memory_range_count,
    const struct micros_physical_range *reserved_ranges,
    size_t reserved_range_count
);

enum micros_frame_allocator_error micros_frame_allocator_allocate(
    struct micros_frame_allocator *allocator,
    uint64_t *physical_address
);

enum micros_frame_allocator_error micros_frame_allocator_release(
    struct micros_frame_allocator *allocator,
    uint64_t physical_address
);
```

Allocation returns the lowest-address free frame. The deterministic rule makes
boot behavior and host models reproducible; it is not a claim about future VM
allocation policy.

Release succeeds only when the address:

- is frame-aligned;
- belongs to a managed segment;
- currently has its allocation bit set.

An unaligned address, reserved or unmanaged frame, and any frame that is not
currently allocated return explicit errors. A never-allocated frame and a
previously released frame intentionally share `NOT_ALLOCATED`, because the
one-bit state does not retain allocation history. Allocation exhaustion is
also an ordinary explicit error in the portable API. Bootstrap callers decide
which errors are fatal for their phase.

The allocator contains no UART output, panic call, dynamic allocation, hidden
interrupt manipulation, or architecture-specific code.

### Bootstrap integration

The kernel owns one static bootstrap allocator instance. After FDT parsing has
finished and before any target-specific panic, trap, or timer self-test, boot:

1. appends `[0, __kernel_end)` to the FDT reservations;
2. initializes the allocator;
3. verifies that every managed segment is fully covered by the union of the
   validated FDT memory ranges and intersects neither an FDT reservation nor
   `[0, __kernel_end)`;
4. verifies that no managed frame lies below `__kernel_start`, that at least
   one frame is managed, and that all managed frames are initially free;
5. emits exactly one record:

```text
MICROS_FRAME_ALLOCATOR_READY managed=0x0000000000000000 free=0x0000000000000000
```

The zeros above specify field width, not required values. Both fields are
exactly 16 lowercase hexadecimal digits. The host gate full-line matches the
record, requires newline termination, nonzero equal counts, and ordering after
`MICROS_FDT_READY`. Every target image passes through this production
initialization path.

The bootstrap wrapper exposes explicit allocate and release operations for the
next page-table slice. It does not enable interrupts or provide concurrent
access. Callers remain responsible for entering a documented critical section
if allocator use later overlaps interrupt or scheduler activity.

### Native and target tests

Native tests compile the production allocator implementation with ASan and
UBSan. Deterministic cases cover:

- unsorted, touching, overlapping, and duplicate memory ranges;
- partial-page memory boundaries;
- overlapping reservations and reservations outside memory;
- reservations that split or completely remove memory;
- lowest-address allocation and exhaustion;
- release, reuse, invalid release, and double release;
- every overflow and capacity boundary.

A seeded model test compares randomized allocate/release sequences against a
small reference bitmap. Every operation checks:

- managed ranges remain sorted, page-aligned, and disjoint;
- no managed frame intersects a canonical reservation;
- allocated plus free equals managed;
- no frame is returned twice without a release;
- failed operations leave state unchanged.

An isolated `MICROS_BUILD_FRAME_ALLOCATOR_TEST` image uses the production
allocator created from the real QEMU FDT. Before allocation it independently
checks every managed segment against the union of the original FDT memory
ranges, both FDT reservation sources, `__kernel_start`, and
`[0, __kernel_end)`. It then
allocates four frames, verifies strictly increasing aligned addresses and that
none intersects a forbidden range, releases them in a non-LIFO order, checks
that the free count returns to its initial value, reallocates the lowest frame,
releases it, and verifies the count again.

Only then does it emit:

```text
MICROS_FRAME_ALLOCATOR_TEST_PASS allocations=0x0000000000000004 reuse=lowest invariants=preserved
```

The host gate requires exactly one allocator-ready record and one exact,
newline-terminated pass record in the order
`MICROS_FDT_READY < MICROS_FRAME_ALLOCATOR_READY < test pass`, with no panic,
explicit failure, timeout, or unclean exit. The allocator workflow boots the
same ELF once with 128 MiB and once with 256 MiB of RAM, proving that the
default launch size is not compiled into the kernel.

## Consequences

- Page-table work receives deterministic, validated physical frames.
- Firmware, pre-kernel, kernel, stack, and metadata frames cannot be released
  through the normal allocator API.
- The parsed FDT blob is reclaimable rather than becoming an accidental
  permanent reservation.
- Bitmap metadata is bounded and simple to verify; the supported ceiling is
  1 GiB rather than the 128 MiB launch default.
- Native randomized tests cover allocator state transitions quickly; QEMU
  proves integration with the real linker and FDT ranges.
- All current allocations are explicitly kernel retained. Typed service and VM
  ownership remains blocked on a later authoritative handoff ledger rather
  than being inferred from this availability bitmap.

## Alternatives considered

### Monotonic bump allocation

A bump pointer is smaller, but it cannot validate release, represent ownership,
or provide the handoff ledger required by ADR-0006.

### Mutable free-range list only

A free-range list represents large sparse memory compactly, but bounded static
storage can be exhausted by release fragmentation. It also cannot distinguish
a reserved frame from an allocated frame without a second ownership structure.

### One bitmap over the complete physical address span

This makes lookup simple but wastes metadata for sparse high-address ranges.
Segment-relative bitmap offsets bound metadata by managed frame count instead
of the largest physical address.

### Allocate metadata dynamically

Dynamic allocator metadata requires the allocator it is meant to bootstrap.
The fixed target-bound bitmap breaks that cycle explicitly.

### Manage only the first FDT memory range

Ignoring later ranges makes ownership depend on firmware ordering and would
silently discard valid memory. All validated memory ranges participate.

## Specification basis

- [ADR-0001: Target Platform](0001-target-platform.md)
- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0006: VM Bootstrap and Handoff](0006-vm-bootstrap-and-handoff.md)
- [ADR-0010: Testing and Observability](0010-testing-and-observability.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [Development dependency DAG](../architecture/development-dag.md)

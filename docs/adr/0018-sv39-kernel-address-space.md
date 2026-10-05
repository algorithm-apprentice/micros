# ADR-0018: Sv39 Kernel Address Space

- Status: Accepted
- Date: 2026-10-06

## Context

Development DAG Step 5 begins with the address-translation mechanism required
before process address spaces and user-mode isolation can exist. ADR-0001
selects Sv39, ADR-0015 provides a supervisor trap path, and ADR-0017 provides
validated kernel-owned physical frames. The current kernel still runs with
`satp.MODE=Bare`, so linker permissions are not enforced by hardware and every
supervisor address is a physical address.

This slice must introduce translation without changing the kernel's linked
address, stack, trap entry, UART diagnostics, timer path, or allocator
contract. It must also establish an API that can later construct per-process
roots without prematurely implementing process objects, ASIDs, U-mode, page
fault policy, or the VM handoff.

The initial QEMU `virt` machine exposes RAM beginning at `0x80000000`, loads the
kernel at `0x80200000`, and places UART0 at `0x10000000`. OpenSBI executes in
M-mode, where supervisor `satp` translation does not apply. The kernel
therefore needs supervisor mappings only for memory it directly accesses.

## Decision

### Translation model

The boot hart activates one Sv39 root with ASID zero. All initial mappings are
identity mappings: virtual address equals physical address. This preserves the
current linked PC, boot and trap stacks, `stvec`, `sscratch`, global pointers,
allocator frame addresses, and MMIO pointers across the `satp` transition.

The initial root maps exactly:

- the linker-defined kernel text range as supervisor read-execute;
- the linker-defined kernel read-only data range as supervisor read-only;
- the linker-defined writable kernel range, including data, BSS, boot stack,
  primary trap stack, and emergency trap stack, as supervisor read-write and
  non-executable;
- every allocator-managed physical frame as supervisor read-write and
  non-executable, including free frames and the page-table frames allocated
  from that pool;
- the single 4 KiB page containing QEMU `virt` UART0 as supervisor
  read-write and non-executable.

No mapping sets the user bit. Firmware, FDT reservations, pre-kernel RAM,
unmanaged RAM, PLIC, timer MMIO, virtio, and other device ranges remain
unmapped unless a later Accepted ADR gives the kernel a direct-access reason.
The parsed FDT blob is no longer needed when translation is activated.

Identity mapping is a bootstrap layout, not a permanent userspace ABI. Future
process roots will install equivalent supervisor-only kernel leaves alongside
their U-mode leaves. Because the UART and future low user addresses can share
an Sv39 root index, this decision does not require sharing a complete root
entry or page-table subtree. A later address-space ADR will define root
composition, user virtual ranges, ASID allocation, and switch-time
invalidation.

### Linker permission boundaries

The linker aligns all permission boundaries to 4096 bytes and exports:

```text
__kernel_text_start
__kernel_text_end
__kernel_rodata_start
__kernel_rodata_end
__kernel_writable_start
__kernel_writable_end
```

`__kernel_start` equals `__kernel_text_start`, and page-aligned
`__kernel_end` equals `__kernel_writable_end`. Padding between output sections
belongs to the preceding mapping only when it lies before that section's
aligned end. No physical page is mapped with permissions from two linker
ranges.

Every target image also passes a mandatory post-link section audit. Each
allocatable ELF section must lie wholly within exactly one of the three
exported ranges, and its ELF write and execute flags must be permitted by that
range. An unexpected allocatable orphan, a section that crosses a boundary, or
a writable-executable section fails the build. This closes the contract for
test-specific and future input sections rather than relying on LLD's default
orphan placement.

The page-table API rejects a writable-executable leaf. Writable leaves must
also be readable, as required by the RISC-V PTE encoding. Kernel text is RX,
read-only data is R, and writable state is RW/NX. Execute-only mappings remain
architecturally valid but are not used by this slice.

### Sv39 representation

One base page and one page table are both 4096 bytes. A table contains 512
64-bit entries. The mapper uses three levels and creates only level-zero
4 KiB leaves. Larger leaves are deferred because the current priority is exact
permission boundaries and a single implementation path rather than reducing
the early table count.

The portable Sv39 layer defines:

- canonical-address validation for the 39-bit virtual-address form;
- extraction of the three 9-bit VPN indices;
- aligned, 56-bit physical-address validation;
- non-leaf PTE construction;
- leaf PTE construction and permission validation;
- PTE decoding needed by the mapper and tests.

A non-leaf entry contains only a supported physical page number and `V`.
`R`, `W`, `X`, `U`, `G`, `A`, and `D` are clear. A leaf contains `V`, the
requested `R`, `W`, `X`, and optional `U` permission bits, and an encoded
physical page number. The implementation sets `A` on every leaf and `D` on
every writable leaf so normal access does not depend on hardware A/D updates.
`G` is clear until the later multi-root contract defines which mappings are
global. Reserved high PTE bits and both RSW bits are zero.

The kernel-address-space wrapper permits only canonical, page-aligned identity
ranges and clears `U` unconditionally. Mapping an already valid leaf, walking
through a leaf, overflowing a range, exhausting frames, or observing an
invalid nonzero encoding is an explicit initialization error. A zero `V=0`
entry is the expected absent state: at an intermediate level it causes a
cleared child table to be allocated and linked, and at level zero it receives
the new leaf. Malformed invalid encodings, reserved high bits, forbidden
non-leaf flags, a leaf above level zero, and a duplicate valid leaf are
rejected. Boot converts any such error into a structured panic.

### Allocation and construction

The root and every intermediate table are allocated through
`micros_bootstrap_frame_allocate`. Each allocated frame is zeroed before it is
linked into its parent. Page-table frames remain allocated to the bootstrap
kernel and are never released by this slice, consistent with ADR-0017.

Construction occurs after allocator readiness and while SIE remains clear:

1. allocate and clear the root;
2. map the three non-overlapping linker ranges;
3. map every allocator-managed segment;
4. map the UART page;
5. walk every mapped page and verify its expected physical address and exact
   leaf permissions;
6. verify that the root and every intermediate table frame are allocator
   managed and currently allocated;
7. verify that the number of allocated frames equals the table count and that
   no non-table frame is allocated;
8. activate the root.

The allocator's pristine initialization invariant and its live post-MMU
invariant are separate. Immediately after allocator initialization, every
managed frame is free and every bitmap bit is clear. After construction,
`managed_frame_count - free_frame_count == table_count`; every set bit names
exactly one reachable table frame, and every reachable table frame has its bit
set. The allocator self-test uses the live free count as its baseline,
allocates and releases four non-table frames, restores that baseline, and
leaves all table bits set.

The same mapper is intended to support later roots, but this slice exposes
only creation and inspection of the boot kernel address space. It does not
expose arbitrary runtime map or unmap operations before their invalidation,
ownership, and concurrency contracts are designed.

### Activation

The architecture layer encodes:

```text
satp.MODE = 8
satp.ASID = 0
satp.PPN = root_physical_address >> 12
```

After all PTE stores, the architecture layer executes
`sfence.vma x0, x0` before changing `satp`. This orders the completed
page-table writes before any implicit page-table access can use the new root.
It then writes `satp` and immediately executes a second
`sfence.vma x0, x0` in the same audited assembly sequence to invalidate any
cached translations associated with ASID zero. The instruction sequence
itself, the active stack, `stvec`, `sscratch`, and all reachable kernel data
are identity mapped before the write. SIE remains clear throughout
construction and activation. This one-hart slice has no remote translation
cache to invalidate.

After activation, boot reads `satp` back, verifies Sv39 mode and the exact root
PPN, and emits exactly one newline-terminated record:

```text
MICROS_MMU_READY mode=sv39 root=0x0000000000000000 tables=0x0000000000000000
```

The zero values specify field width, not required values. `root` is a nonzero
aligned physical address and `tables` is a nonzero count including the root.
Every target image must emit this record after
`MICROS_FRAME_ALLOCATOR_READY` and before any target-specific pass record or
panic.

### Native tests

The native unit suite compiles the production portable Sv39 implementation
with ASan and UBSan. Tests cover:

- both boundaries of the lower and upper canonical virtual-address regions
  and the noncanonical gap;
- VPN index extraction, including values that distinguish all three levels;
- physical-address alignment and the maximum encodable physical page number;
- exact non-leaf and leaf PTE encodings;
- rejection of `W` without `R`, `W+X`, empty leaf permissions, reserved
  permission bits, invalid levels, unaligned addresses, and overflow;
- round-trip decoding of valid PTEs;
- exact `A`, `D`, `U`, and reserved-bit invariants.

These tests validate encoding and input contracts. They do not claim to
exercise hardware translation or permission faults.

The post-link audit is also covered by host regressions with representative
section tables. They reject an allocatable orphan, a boundary-crossing
section, incorrect write or execute flags, W+X, a missing permission range,
and malformed inspection output.

### QEMU component test

A separate `MICROS_BUILD_MMU_TEST` image first passes through the production
FDT, allocator, page-table construction, activation, and readiness path. Its
software inspection verifies exact mappings for text, read-only data,
writable kernel state, every managed frame, every table frame, and UART.

It then triggers and recovers from exactly two supervisor exceptions:

1. a store to a known instruction address must raise store/AMO page fault
   cause `15`, with `sepc` at the store instruction and `stval` equal to the
   text target;
2. an indirect call to an aligned instruction word stored in writable kernel
   data must raise instruction page fault cause `12`, with both `sepc` and
   `stval` equal to that writable address.

The test-only trap state accepts each expected fault once, advances `sepc` to
an assembly resume label, and rejects wrong order, cause, origin, address, or
duplicate delivery through trap-aware panic. The ordinary trap, timer, and
panic behavior remains unchanged outside this isolated image.

Only after both recoveries does the image emit:

```text
MICROS_MMU_TEST_PASS store-fault=text execute-fault=writable traps=0x0000000000000002
```

The host gate requires exactly one valid MMU-ready record and one exact pass
record in order, with normal boot and FDT evidence, no panic or explicit
failure, clean SBI shutdown, and no timeout. Host regressions reject missing,
duplicated, malformed, unterminated, or early records.

All pre-existing target workflows require MMU readiness. The allocator test
operates from the live post-MMU allocation baseline rather than assuming every
frame remains free. Their continued success after activation proves that UART
output, panic reporting, complete trap entry and return, timer interrupts,
allocator access with retained table ownership, stacks, and SBI shutdown
remain reachable under Sv39.

## Consequences

- Hardware enforces supervisor W^X boundaries before any U-mode code exists.
- The current physical-address-based kernel continues to run unchanged through
  identity mappings.
- Mapping all managed RAM creates a simple supervisor direct map and makes
  future kernel-owned frame access independent of temporary aliases.
- Using 4 KiB leaves consumes more table frames than large leaves but keeps
  permission and test behavior uniform.
- Free managed frames are supervisor mapped but remain inaccessible to U-mode;
  allocator ownership, not mapping presence, controls their use.
- ASIDs, per-process roots, U-mode layout, dynamic mapping, unmapping,
  shootdown, page-fault policy, and VM ownership remain explicit later work.

## Alternatives considered

### Link the kernel into the upper Sv39 half immediately

A high-half kernel can reserve clean root entries for shared supervisor
mappings, but it requires a relocation transition or dual mapping before the
first process exists. Identity mapping is smaller, preserves current pointers,
and does not prevent later roots from installing the same supervisor leaves.

### Map every FDT RAM range

This would include firmware reservations, pre-kernel RAM, and other ranges the
allocator intentionally excludes. Mapping only the kernel image and managed
segments keeps direct access aligned with current ownership.

### Use 1 GiB or 2 MiB leaves where possible

Large leaves reduce table memory but complicate exact text, read-only, and
writable boundaries and require splitting logic for later fine-grained
mappings. The supported 1 GiB allocator ceiling makes the 4 KiB-table cost
acceptable for the bootstrap slice.

### Add process address spaces and U-mode in the same pull request

That would combine translation encoding, linker permissions, root switching,
process ownership, user layout, and privilege transition failures. This slice
first makes the supervisor substrate independently observable and reviewable.

## Specification basis

- [RISC-V Privileged Architecture, Supervisor-Level ISA](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html)
- [QEMU `virt` generic virtual platform](https://www.qemu.org/docs/master/system/riscv/virt.html)
- [ADR-0001: Target Platform](0001-target-platform.md)
- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0006: VM Bootstrap and Handoff](0006-vm-bootstrap-and-handoff.md)
- [ADR-0010: Testing and Observability](0010-testing-and-observability.md)
- [ADR-0015: Supervisor Trap Entry](0015-supervisor-trap-entry.md)
- [ADR-0017: Bootstrap Physical Frame Allocator](0017-bootstrap-physical-frame-allocator.md)
- [Development dependency DAG](../architecture/development-dag.md)

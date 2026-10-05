# ADR-0001: Target Platform

- Status: Accepted
- Date: 2026-10-05

## Context

The first target must expose real privilege levels, page tables, traps, timer
interrupts, and user-mode isolation without requiring physical hardware. A
single stable virtual platform keeps early failures attributable to kernel
code rather than board variation.

## Decision

The initial `micros` target is:

- 64-bit little-endian RISC-V;
- QEMU `virt`;
- OpenSBI firmware;
- one hart;
- supervisor-mode kernel and user-mode services;
- Sv39 virtual memory;
- the `rv64imac_zicsr_zifencei` ISA baseline;
- the QEMU PLIC interrupt controller with AIA disabled;
- fixed QEMU `virt` UART and PLIC addresses for the MVP;
- a minimal FDT parser for physical memory and reserved ranges;
- direct UART MMIO for early serial output;
- SBI services for the initial timer and system reset paths.

The default development machine has 128 MiB of guest RAM. The exact value is a
launch default, not a kernel ABI.

OpenSBI passes the boot hart ID in `a0` and the FDT address in `a1`. The early parser validates all offsets and lengths and reads only:

- every available root child whose node basename is `memory` or whose
  `device_type` property is `"memory"`, including all valid `reg` tuples;
- memory nodes whose `status` is absent, `"ok"`, or `"okay"` are available;
  other status values are excluded from usable physical memory;
- the FDT reservation map;
- `/reserved-memory` child ranges;
- root address and size cell widths required to decode those ranges.

The kernel additionally reserves physical memory below its fixed load address,
its linker-defined image, embedded images, stacks, page tables, and the FDT
blob until parsing is complete. Unsupported cell layouts or malformed ranges
stop boot with an explicit diagnostic.

General device discovery, SMP, physical boards, floating-point/vector context,
and a reusable full FDT library are deferred.

## Consequences

- The architecture has a small, documented supervisor interface.
- QEMU provides deterministic automation and fault reproduction.
- The first platform layer may use QEMU-specific addresses.
- Guest RAM size can change without recompiling the kernel.
- The bounded FDT parser becomes security-sensitive input code and requires
  native malformed-input tests.
- QEMU launch commands must explicitly select the PLIC configuration rather
  than relying on a version-dependent AIA default.
- Code must not assume a host and target share an architecture or object
  format.
- Adding hardware later requires a platform abstraction and a new ADR.

## Alternatives considered

### x86_64

x86_64 has mature emulation and documentation, but its boot and interrupt
legacy adds material unrelated to the initial learning goals.

### AArch64

AArch64 is relevant to the development host and modern hardware, but RISC-V
offers a smaller architectural surface and a straightforward OpenSBI boot path.

### Physical RISC-V hardware first

Physical hardware would slow diagnosis and automation before the kernel has
basic observability.

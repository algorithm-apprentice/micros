# Building and Testing the Boot Foundation

## Scope

The current implementation provides:

- a freestanding RV64 ELF linked at `0x80200000`;
- an OpenSBI supervisor-mode entry with a 16 KiB boot stack and cleared BSS;
- polled output through the QEMU `virt` UART at `0x10000000`;
- bounded parsing of the OpenSBI-provided FDT memory map;
- native FDT parser tests under ASan and UBSan;
- shutdown through the SBI System Reset extension;
- a deterministic host harness that reports TAP output.

Panic diagnostics, traps, timer interrupts, physical-memory allocation, page
tables, and user mode remain dependency-ordered later tasks.

## Prerequisites

The build requires:

- CMake 3.25 or newer;
- Ninja;
- Python 3.8 or newer;
- Clang with the `riscv64-unknown-elf` target;
- LLD;
- QEMU 7.0 or newer with `qemu-system-riscv64`, the `virt,aia=none`
  machine option, and default OpenSBI firmware providing SBI System Reset.

On macOS, the supported Homebrew packages are `cmake`, `ninja`, `qemu`, `llvm`,
and `lld`. Linux installations may provide equivalent packages through their
distribution.

## Tool discovery

The RISC-V toolchain file discovers Clang in this order:

1. `MICROS_CLANG`;
2. `${MICROS_LLVM_ROOT}/bin/clang`;
3. `brew --prefix llvm` on macOS;
4. `clang` on `PATH`.

It discovers LLD in this order:

1. `MICROS_LD_LLD`;
2. `${MICROS_LLD_ROOT}/bin/ld.lld`;
3. `brew --prefix lld` on macOS;
4. `ld.lld` on `PATH`.

CMake discovers `python3` and `qemu-system-riscv64` on `PATH`. They may be
overridden in an explicit configure step:

```bash
cmake --preset riscv64-debug \
  -DMICROS_PYTHON=/absolute/path/to/python3 \
  -DMICROS_QEMU=/absolute/path/to/qemu-system-riscv64
```

Missing tools stop configuration with an explicit error. The build never
falls back to a hosted C library or a host linker.

## Build

From a clean checkout, configure and build with one command:

```bash
cmake --workflow --preset build-riscv64-debug
```

The primary artifacts are:

```text
build/riscv64-debug/kernel/micros.elf
build/riscv64-debug/kernel/micros.map
```

The ELF uses the `rv64imac_zicsr_zifencei` and `lp64` baseline, contains no
host startup objects or libc, and preserves the OpenSBI boot arguments in `a0`
and `a1` until `kernel_main`.

## Native unit tests

Configure, build, and run the native suite with:

```bash
cmake --workflow --preset test-unit
```

The host graph is separate from the freestanding target graph. It compiles the
same FDT parser implementation with warnings as errors, ASan, and UBSan, then
runs its malformed-input corpus and the Python harness tests.

The parser has fixed resource bounds:

- FDT blob size: 1 MiB;
- node depth: 32;
- physical-memory ranges: 16;
- reservation-map ranges: 32;
- static `/reserved-memory` ranges: 32.

It accepts one- or two-cell addresses and sizes, all `reg` tuples from matching
root memory nodes, the reservation map, and static `/reserved-memory` children.
Dynamic reserved-memory allocation requests are rejected explicitly until a
physical allocator exists.

## QEMU smoke test

Build and run the acceptance test with:

```bash
cmake --workflow --preset test-qemu-smoke
```

The generated Ninja target is also available after configuration:

```bash
cmake --build --preset riscv64-debug --target test-qemu-smoke
```

The harness launches one RV64 hart with 128 MiB of RAM on
`virt,aia=none`, disables the monitor and network, uses serial standard I/O,
and boots through QEMU's default OpenSBI firmware.

A pass requires all of:

1. a serial line exactly equal to `MICROS_BOOT 0.1.0`;
2. a serial line exactly equal to
   `MICROS_FDT_MEMORY base=0x0000000080000000 size=0x0000000008000000`;
3. a serial line exactly equal to `MICROS_FDT_READY`;
4. QEMU exit status zero after the SBI shutdown request.

The kernel emits every decoded range using stable, fixed-width hexadecimal
events:

```text
MICROS_FDT_MEMORY base=0x0000000080000000 size=0x0000000008000000
MICROS_FDT_RESERVATION base=0x... size=0x...
MICROS_FDT_RESERVED_MEMORY base=0x... size=0x...
MICROS_FDT_COUNTS memory=0x... reservation=0x... reserved-memory=0x...
MICROS_FDT_READY
```

The reservation event kinds are emitted only when the corresponding FDT source
contains ranges. The smoke harness verifies that each count equals the number
of emitted events of that kind, so omitted memory or reservation records cannot
produce a pass. A parse error emits
`MICROS_TEST_FAILURE fdt-<error-category>`, flushes the UART, and requests SBI
shutdown with the system-failure reason.

The harness emits TAP plus a stable outcome field:

```text
TAP version 13
ok 1 - versioned boot marker followed by clean QEMU shutdown
# outcome: pass
```

Non-success outcomes are `failure`, `panic`, `unexpected-exit`, and `timeout`.
The timeout is eight seconds. A missing or malformed marker, an explicit target
failure, a panic marker, a non-clean exit, and a guest that does not terminate
cannot be reported as success.

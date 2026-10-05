# Building and Testing the Boot Foundation

## Scope

The current implementation provides:

- a freestanding RV64 ELF linked at `0x80200000`;
- an OpenSBI supervisor-mode entry with a 16 KiB boot stack and cleared BSS;
- polled output through the QEMU `virt` UART at `0x10000000`;
- bounded parsing of the OpenSBI-provided FDT memory map;
- structured panic diagnostics with RISC-V machine-state snapshots;
- direct-mode supervisor trap entry with a complete integer return context;
- native FDT parser tests under ASan and UBSan;
- shutdown through the SBI System Reset extension;
- a deterministic host harness that reports TAP output.

Timer interrupts, physical-memory allocation, page tables, and user mode remain
dependency-ordered later tasks.

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

It accepts one- or two-cell addresses and sizes, all `reg` tuples from available
matching root memory nodes, the reservation map, and static `/reserved-memory`
children. A memory node is available when `status` is absent, `"ok"`, or
`"okay"`. Dynamic reserved-memory allocation requests are rejected explicitly
until a physical allocator exists.

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
2. exactly one serial line equal to `MICROS_TRAP_READY`;
3. a serial line exactly equal to
   `MICROS_FDT_MEMORY base=0x0000000080000000 size=0x0000000008000000`;
4. a serial line exactly equal to `MICROS_FDT_READY`;
5. QEMU exit status zero after the SBI shutdown request.

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
of emitted events of that kind and independently requires at least one firmware
reservation from either FDT reservation source. An empty reservation result or
an omitted emitted record therefore cannot produce a pass. A parse error emits
`MICROS_TEST_FAILURE fdt-<error-category>`, flushes the UART, and requests SBI
shutdown with the system-failure reason.

The harness emits TAP plus a stable outcome field:

```text
TAP version 13
ok 1 - QEMU smoke test observed expected pass
# outcome: pass
# expected outcome: pass
```

Non-success outcomes are `failure`, `panic`, `unexpected-exit`, and `timeout`.
The timeout is eight seconds. A missing or malformed marker, an explicit target
failure, a panic marker, a non-clean exit, and a guest that does not terminate
cannot be reported as success.

## Intentional panic test

Build and run the isolated fatal-path acceptance test with:

```bash
cmake --workflow --preset test-qemu-panic
```

This uses `build/riscv64-panic-test`, leaving the normal debug image unchanged.
The test-only image completes FDT discovery and then invokes
`MICROS_PANIC(hart_id, "intentional-test")`.

Panic atomically disables supervisor interrupts, destructively puts the QEMU
16550 into a known polled transmit state, and emits exactly five ordered core
records:

```text
MICROS_PANIC reason=intentional-test
MICROS_PANIC_BUILD version=0.1.0
MICROS_PANIC_SOURCE file=kernel/main.c line=0x...
MICROS_PANIC_HART mode=S id=0x0000000000000000
MICROS_PANIC_MACHINE sstatus=0x... scause=0x... stval=0x... sepc=0x... ra=0x... sp=0x...
```

Every hexadecimal value is fixed-width lowercase. The source path is
repository-relative. The host gate requires the five records exactly once and
in order, verifies the source and machine lines with full-line regular
expressions, independently rechecks the FDT event invariants, rejects any
`MICROS_TEST_FAILURE`, and requires QEMU status zero without a timeout after
the SBI system-failure shutdown request.

The direct panic image also requires exactly one `MICROS_TRAP_READY` record and
rejects any appended `MICROS_TRAP_CONTEXT`, because no hardware trap frame
exists for a direct panic.

## Trap recovery test

Build and run the supervisor exception-return test with:

```bash
cmake --workflow --preset test-qemu-trap
```

The isolated image loads one register pattern into every x1-x31 integer
register, selects a test stack, and executes an exact 32-bit illegal
instruction. The production entry moves to its dedicated trap stack, captures
the complete 288-byte frame, and calls the C dispatcher.

The dispatcher verifies the fault label, S-mode origin, cause code, and every
entry register. It then writes a second pattern into every return register,
selects a different stack, toggles a safe `sstatus` bit, and selects a
continuation that is not `fault + 4`. The assembly exit masks SIE, restores the
mutable frame, and executes `sret`. The continuation snapshots every restored
register before calling C.

Only a complete round trip emits:

```text
MICROS_TRAP_TEST_PASS origin=S cause=illegal-instruction registers=preserved
```

The host gate requires exactly one trap-ready record, then FDT readiness, then
exactly one pass record. Missing, duplicated, early, or malformed records fail.

## Unexpected trap panic test

Build and run the trap-aware fatal-path test with:

```bash
cmake --workflow --preset test-qemu-trap-panic
```

This image executes a separate unarmed illegal instruction. The dispatcher
must emit the unchanged five panic core records followed immediately by:

```text
MICROS_TRAP_CONTEXT origin=S sstatus=0x... scause=0x0000000000000002 stval=0x... sepc=0x... ra=0x... sp=0x...
```

The gate resolves `micros_trap_panic_test_fault` from the built ELF with
`llvm-nm` and requires the context `sepc` to equal that exact address. It also
requires one newline-terminated context record, rejects any recovery pass
record, and requires clean SBI system-failure shutdown.

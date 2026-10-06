# Building and Testing the Boot Foundation

## Scope

The current implementation provides:

- a freestanding RV64 ELF linked at `0x80200000`;
- an OpenSBI supervisor-mode entry with a 16 KiB boot stack and cleared BSS;
- polled output through the QEMU `virt` UART at `0x10000000`;
- bounded parsing of the OpenSBI-provided FDT memory map;
- structured panic diagnostics with RISC-V machine-state snapshots;
- direct-mode supervisor trap entry with a complete integer return context;
- OpenSBI TIME programming and one-hart supervisor timer interrupt handling;
- a canonicalized bootstrap physical-frame allocator with a 1 GiB supported
  metadata bound;
- an allocator-backed Sv39 kernel address space with page-aligned RX, R, and
  RW/NX permission ranges;
- supervisor-only identity mappings for managed RAM and the QEMU UART;
- fixed-capacity process, thread, and hart identity tables with generation-safe
  handles and checked one-thread/one-hart production policies;
- a per-hart trap anchor carried through every trap frame, plus hart-owned
  timer mechanism state;
- mandatory post-link closure checks for every allocatable ELF section;
- native FDT parser tests under ASan and UBSan;
- native frame allocator invariant and seeded model tests under ASan and UBSan;
- native Sv39 encoding and ELF permission-layout tests;
- native kernel-object lifecycle, exhaustion, corruption, and seeded
  reference-model tests;
- shutdown through the SBI System Reset extension;
- a deterministic host harness that reports TAP output.

Per-process page-table roots, saved user contexts, user mode, and scheduling
remain dependency-ordered later tasks.

## Prerequisites

The build requires:

- CMake 3.25 or newer;
- Ninja;
- Python 3.8 or newer;
- Clang with the `riscv64-unknown-elf` target plus `llvm-nm` and
  `llvm-readelf`;
- LLD;
- QEMU 7.0 or newer with `qemu-system-riscv64`, the `virt,aia=none`
  machine option, and default OpenSBI firmware providing SBI TIME and System
  Reset.

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
and `a1` until `kernel_main`. Every target link runs
`tools/check_elf_sections.py`; the build fails if an allocatable output section
is unexpected, crosses a permission boundary, lies outside the kernel ranges,
or has write/execute flags inconsistent with its linker-defined range.
It also rejects the superseded standalone `micros_trap_hart_id` and
`timer_state` symbols.

## Native unit tests

Configure, build, and run the native suite with:

```bash
cmake --workflow --preset test-unit
```

The host graph is separate from the freestanding target graph. It compiles the
same FDT parser, frame allocator, Sv39 encoding, and kernel-object
implementations with warnings as errors, ASan, and UBSan. It then runs their
native tests plus the Python QEMU-harness and ELF-layout tests.

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
2. exactly one valid `MICROS_OBJECTS_READY` line;
3. exactly one serial line equal to `MICROS_TRAP_READY`;
4. a serial line exactly equal to
   `MICROS_FDT_MEMORY base=0x0000000080000000 size=0x0000000008000000`;
5. a serial line exactly equal to `MICROS_FDT_READY`;
6. exactly one valid `MICROS_FRAME_ALLOCATOR_READY` line after FDT readiness;
7. exactly one valid `MICROS_MMU_READY` line after allocator readiness;
8. QEMU exit status zero after the SBI shutdown request.

The object record occurs after the boot marker and before trap readiness:

```text
MICROS_OBJECTS_READY processes=0x0000000000000000 threads=0x0000000000000000 harts=0x0000000000000001 max-threads=0x0000000000000001 max-harts=0x0000000000000001 boot-hart=0x0000000000000000
```

It proves that the zeroed fixed-capacity registry was initialized once, the
OpenSBI boot hart was registered, and the production one-thread/one-hart
policies are active before traps can be delivered. Every QEMU workflow requires
the record exactly once and validates all six values.

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

After parsing, the raw FDT blob is no longer used and its frames are
reclaimable. The kernel canonicalizes the complete memory union, both FDT
reservation sources, and `[0, __kernel_end)`. It then emits exactly one
newline-terminated record after FDT readiness:

```text
MICROS_FRAME_ALLOCATOR_READY managed=0x... free=0x...
```

Both values are 16-digit lowercase hexadecimal. Every QEMU gate requires one
record with equal, nonzero counts. Initialization independently verifies that
every managed segment is covered by the FDT memory union and intersects
neither a firmware reservation nor any physical address through the
linker-defined kernel end.

The kernel then allocates the root and intermediate tables from that allocator,
maps text RX, read-only data R, writable kernel state and managed RAM RW/NX,
and maps the UART page RW/NX. Every leaf is supervisor-only. Page-table frames
remain allocated to the kernel, so the live allocator invariant becomes
`managed - free == tables`. Activation executes one `sfence.vma` before the
`satp` write and one immediately after it, then verifies the exact readback.
Every QEMU image emits:

```text
MICROS_MMU_READY mode=sv39 root=0x... tables=0x...
```

The root is a nonzero aligned physical address and the table count is nonzero.
Every target gate requires this newline-terminated record after allocator
readiness and before its pass or panic outcome.

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
The test-only image completes FDT discovery, allocator initialization, and
Sv39 activation, then invokes `MICROS_PANIC(hart_id, "intentional-test")`.

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

Only a complete round trip through the registered hart's primary trap stack
emits:

```text
MICROS_TRAP_TEST_PASS origin=S cause=illegal-instruction registers=preserved hart-context=routed primary-stack=selected sscratch=anchor
```

The host gate requires exactly one trap-ready record, then FDT, allocator, and
MMU readiness, then exactly one pass record. Missing, duplicated, early, or
malformed records fail.

## Supervisor timer interrupt test

Build and run the isolated timer test with:

```bash
cmake --workflow --preset test-qemu-timer
```

The image initializes the one-hart timer with global SIE clear, programs an
absolute deadline through SBI TIME, and enables only `sie.STIE`. Its wait loop
keeps SIE clear while inspecting timer state, executes `wfi`, and briefly opens
an adjacent set-SIE/clear-SIE delivery window. The trap dispatcher routes
supervisor timer cause code `5` to the timer module.

Each accepted expiration first verifies that the unsigned `time` counter has
reached the recorded deadline. A stale pending indication therefore returns
without incrementing or rearming. Accepted expirations rearm from the current
counter rather than the previous deadline. The third expiration disables STIE
and programs `UINT64_MAX`, after which the image verifies clear SIE, clear STIE,
and exactly three accepted expirations.

Only that complete sequence, including final hart-owned timer state, emits:

```text
MICROS_TIMER_TEST_PASS ticks=0x0000000000000003 interval=0x00000000000186a0 active=0x0000000000000000 deadline=0xffffffffffffffff owner=hart
```

The host gate requires normal boot, complete FDT evidence, allocator and MMU
readiness, exactly one newline-terminated pass record after MMU activation,
clean SBI shutdown, and no panic, explicit failure, or timeout. Missing,
duplicated, early, or malformed tick and interval fields fail. The interval is
expressed only in platform counter ticks; it is not a wall-clock ABI.

## Kernel object model test

Build and run the target lifecycle test with:

```bash
cmake --workflow --preset test-qemu-object-model
```

The portable implementation uses 64 process slots, 128 thread slots, and
eight hart slots. Process and thread handles contain a nonzero generation;
release followed by reuse advances it, and stale handles no longer resolve.
Process generations also reject endpoint-reserved encodings, and a slot is
quarantined rather than wrapping into an invalid identity. Allocation is
deterministic and chooses the lowest available slot.

The isolated target image exercises the production registry rather than a test
copy. It proves successful generation advance and stale rejection, enforces
the production one-thread-per-process policy, binds and clears a running thread
through the boot hart's current-thread field, directly checks the relevant
intermediate states, and validates the complete final table. Only then does it
emit:

```text
MICROS_OBJECT_MODEL_TEST_PASS process-generation=advanced stale=rejected thread-limit=enforced hart-local=preserved
```

Native ASan/UBSan coverage additionally exercises complete capacities,
generation exhaustion and quarantine, failure atomicity, larger multi-thread
and multi-hart policies, deliberately corrupted invariants, and a replayable
4,096-operation independent reference model.

## Per-hart nested trap test

Build and run the isolated nested-fault test with:

```bash
cmake --workflow --preset test-qemu-nested-trap
```

Outside trap dispatch, `sscratch` contains the current hart's trap-anchor
address. Entry saves interrupted `t0`-`t2` through that anchor, switches to the
hart's primary stack, stores the hart context in the 288-byte frame, installs
kernel `tp`, and clears `sscratch`. A second trap observes the zero sentinel
and uses kernel `tp` to select the same hart's emergency stack.

The test registers a distinct emergency stack, poisons interrupted `tp`, takes
an initial exception, and injects another illegal instruction immediately
after sentinel arming. It verifies hart identity, primary- and emergency-stack
bounds, outer-frame context, and preservation of the poisoned interrupted
`tp`. Only that route emits:

```text
MICROS_NESTED_TRAP_TEST_PASS hart=routed emergency-stack=selected
```

## Bootstrap frame allocator test

Build and run the physical-frame allocator test with:

```bash
cmake --workflow --preset test-qemu-frame-allocator
```

The portable allocator aligns memory inward, aligns reservations outward,
sorts and merges both unions, subtracts reserved frames, and maps the remaining
segments into a fixed allocation bitmap. It returns the lowest physical frame,
rejects unmanaged and non-allocated releases, and preserves state on every
failed operation. The supported metadata ceiling is 262144 frames, or 1 GiB,
independent from the default launch size.

The native suite covers range normalization, overflow and capacity boundaries,
exhaustion, invalid release, and a replayable 2,000-step reference-model trace.
The target image independently rechecks every real managed segment against the
FDT and linker inputs. After Sv39 activation it uses the live free count as its
baseline, leaves every page-table bit allocated, allocates four increasing
non-table frames, releases them in non-LIFO order, verifies exact baseline
restoration, and proves lowest-frame reuse. Only then does it emit:

```text
MICROS_FRAME_ALLOCATOR_TEST_PASS allocations=0x0000000000000004 reuse=lowest invariants=preserved
```

The QEMU workflow boots the same ELF with 128 MiB and 256 MiB. Both runs must
pass, and the larger guest must expose exactly `0x8000` additional managed
frames. This rejects a kernel that silently compiles in the default RAM size.

## Sv39 MMU test

Build and run the isolated page-table permission test with:

```bash
cmake --workflow --preset test-qemu-mmu
```

The production boot path allocates and validates the complete root, activates
Sv39, and emits MMU readiness. The test then performs exactly two expected
supervisor faults. A store to a known text instruction must raise store/AMO
page-fault cause `15`, and an indirect call into writable kernel data must
raise instruction page-fault cause `12`. The test trap path verifies the exact
cause, `sepc`, `stval`, S-mode origin, and order before selecting explicit
assembly resume labels.

Only both hardware-enforced recoveries emit:

```text
MICROS_MMU_TEST_PASS store-fault=text execute-fault=writable traps=0x0000000000000002
```

The host gate rejects a missing, duplicated, malformed, unterminated, or early
MMU-ready or test-pass record, plus any panic, explicit failure, timeout, or
unclean exit.

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

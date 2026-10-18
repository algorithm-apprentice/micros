# B01 Platform Baseline Audit

## Status

```text
audit unit:       B01 platform
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; six blocking findings; not certified
implementation:   forbidden during complete-audit phase
```

This is the first unit required by
[the complete baseline audit contract](minix-complete-baseline-audit.md). It
reviews platform entry, firmware data, early serial output, panic, PLIC, and
timer programming. Scheduler accounting, trap dispatch, address-space
mechanisms, and TTY service policy remain in their later units.

## Exact primary path manifest

B01 has 38 primary paths in the pinned tree.

### Paths audited in this unit

| Path | B01 responsibility |
| --- | --- |
| `arch/riscv64/boot.S` | OpenSBI supervisor entry, boot stack, BSS clear, `kernel_main` transfer |
| `arch/riscv64/interrupt.h` | Supervisor interrupt-save/restore, timer/external enables, MMIO fence, WFI window |
| `arch/riscv64/kernel.ld` | Entry placement, section boundaries, BSS alignment, boot stack placement |
| `arch/riscv64/panic.S` | Interrupt-disable and fixed panic-context capture |
| `arch/riscv64/panic_context.h` | Assembly/C panic-context layout |
| `arch/riscv64/platform.h` | QEMU `virt` UART/PLIC constants and SBI interface |
| `arch/riscv64/sbi.c` | SBI TIME, System Reset, and `rdtime` calls |
| `arch/riscv64/uart.c` | Early/panic polled UART and console-ownership transition |
| `kernel/diagnostic/panic.c` | Panic seizure, structured report, reset, fallback halt |
| `kernel/platform/fdt.c` | FDT validation and memory/reservation extraction |
| `kernel/platform/plic.c` | Fixed QEMU PLIC register binding and fences |
| `kernel/platform/plic.h` | Target PLIC interface |
| `kernel/platform/plic_core.c` | PLIC state machine, validation, claim, completion |
| `kernel/platform/plic_core.h` | Portable PLIC controller and plan structures |
| `kernel/platform/timer.c` | Per-hart SBI timer state and programming |
| `include/micros/fdt.h` | Bounded FDT output structure and errors |
| `include/micros/panic.h` | Panic entry/report API |
| `include/micros/timer.h` | Timer operation/result API |

`include/micros/trap.h` is a B01 primary header under the closed path
inventory. Its exact semantic owner is reserved as B05-O01, supervisor trap
entry and frame publication. B01 records no trap-dispatch conclusion for it.

### Primary paths delegated to later operation units

The remaining primary `arch/**` paths reserve these exact future rows:

| Path | Exact semantic owner | Secondary evidence role |
| --- | --- | --- |
| `arch/riscv64/address_space_handoff_test.S` | B02-O08 address-space handoff | B14-E-B02-O08 |
| `arch/riscv64/grant_syscall_test.S` | B05-O08 grant syscall entry/return | B14-E-B05-O08 |
| `arch/riscv64/ipc_ecall_test.S` | B04-O10 IPC ecall semantics; B05-O05 syscall entry/return | B14-E-B04-O10; B14-E-B05-O05 |
| `arch/riscv64/ipc_syscall_panic_test.S` | B04-O11 IPC retained-buffer failure; B05-O02 panic trap entry | B14-E-B04-O11; B14-E-B05-O02 |
| `arch/riscv64/ipc_syscall_test.S` | B04-O10 IPC syscall semantics; B05-O05 syscall entry/return | B14-E-B04-O10; B14-E-B05-O05 |
| `arch/riscv64/mmu.S` | B02-O05 kernel Sv39 activation and translation fences | B14 source consumer only |
| `arch/riscv64/mmu.h` | B02-O05 kernel Sv39 activation and translation fences | B14 source consumer only |
| `arch/riscv64/mmu_test.S` | B02-O09 hardware MMU fault behavior | B14-E-B02-O09 |
| `arch/riscv64/nested_trap_test.S` | B05-O02 nested supervisor trap entry | B14-E-B05-O02 |
| `arch/riscv64/scheduler.S` | B03-O06 context restore and selected return | B14 source consumer only |
| `arch/riscv64/scheduler_context.h` | B03-O06 context restore and selected return | B14 source consumer only |
| `arch/riscv64/scheduler_test.S` | B03-O06 context restore and selected return | B14-E-B03-O06 |
| `arch/riscv64/trap.S` | B05-O01 supervisor trap entry and frame publication | B14 source consumer only |
| `arch/riscv64/trap_context.h` | B05-O01 supervisor trap entry and frame publication | B14 source consumer only |
| `arch/riscv64/trap_test.S` | B05-O01 supervisor trap entry and return | B14-E-B05-O01 |
| `arch/riscv64/user_address_space_test.S` | B02-O06 user mapping and activation | B14-E-B02-O06 |
| `arch/riscv64/user_entry.S` | B05-O03 initial U-mode entry | B14 source consumer only |
| `arch/riscv64/user_execution_test.S` | B05-O03 U-mode entry/trap/resume | B14-E-B05-O03 |
| `arch/riscv64/user_runtime_test.S` | B05-O06 user runtime raw syscall ABI | B14-E-B05-O06 |

Shared paths reserve these additional consumers:

| Path | Current B01 rows | Exact later rows |
| --- | --- | --- |
| `arch/riscv64/interrupt.h` | B01-O07, B01-O08, B01-O09 | B03-O08 timer/preemption masking; B05-O01 trap guards; B09-O06 UART IRQ guards |
| `arch/riscv64/kernel.ld` | B01-O01 | B02-O05 kernel mapping sections; B03-O02 thread stacks; B05-O01 trap stacks; B06-O03 embedded bootstrap images |
| `arch/riscv64/platform.h` | B01-O08 through B01-O10 | B09-O02 UART MMIO mechanism |
| `arch/riscv64/uart.c` | B01-O04 through B01-O06 | B09-O02 UART MMIO mechanism and ownership |

### Cross-owner sources required by B01 operations

| Path | B01 operation | Primary/future owner |
| --- | --- | --- |
| `kernel/memory/frame_allocator.c` | B01-O03C range canonicalization | B02-O01 |
| `kernel/memory/bootstrap_memory.c` | B01-O03C initialization and B01-O03D successful validation | B02-O02 |
| `kernel/tty/uart_console_core.c` | B01-O05 ownership and B01-O06 panic seizure | B09-O01 |
| `kernel/boot/boot_foundation.c` | B01-O02/O03 consumption and B01-O04 early records | B06-O01 |

These paths retain their primary units. B01 audits only the named cross-owner
operation and reserves the exact later row.

The B02 identifiers reserved by B01 are non-overlapping:

| ID | Reserved B02 operation family |
| --- | --- |
| B02-O01 | Firmware-range canonicalization and frame-allocator initialization |
| B02-O02 | Bootstrap allocator valid-path validation |
| B02-O03 | Physical-frame allocation/release and bitmap mutation |
| B02-O04 | Typed frame-ownership transitions |
| B02-O05 | Kernel Sv39 map/walk/validate/activate |
| B02-O06 | User address-space create/map/freeze/teardown |
| B02-O08 | Address-space handoff |
| B02-O09 | Hardware MMU protection-fault behavior |

## Fixed MINIX source evidence

The B01 comparison uses:

- `minix/kernel/arch/i386/head.S`
- `minix/kernel/arch/earm/head.S`
- `minix/kernel/arch/i386/pre_init.c`
- `minix/kernel/arch/earm/pre_init.c`
- `minix/kernel/arch/i386/pg_utils.c`
- `minix/kernel/arch/earm/pg_utils.c`
- `minix/kernel/main.c`
- `minix/kernel/utility.c`
- `minix/kernel/interrupt.c`
- `minix/kernel/clock.c`
- `minix/kernel/arch/i386/arch_clock.c`
- `minix/kernel/arch/earm/arch_clock.c`
- `minix/kernel/arch/i386/apic.c`
- `minix/kernel/arch/i386/arch_reset.c`
- `minix/kernel/arch/earm/arch_reset.c`
- `minix/kernel/arch/i386/direct_tty_utils.c`
- `minix/kernel/arch/earm/direct_tty_utils.c`
- `minix/kernel/arch/earm/bsp/ti/omap_serial.c`
- `minix/include/minix/type.h`
- `minix/include/minix/sys_config.h`
- `minix/lib/libtimers/tmrs_exp.c`
- `minix/lib/libsys/stacktrace.c`
- `minix/kernel/arch/i386/Makefile.inc`
- `minix/kernel/arch/earm/Makefile.inc`
- `share/mk/bsd.own.mk`

The fixed MINIX tree has no RISC-V, OpenSBI, SBI TIME, QEMU `virt` PLIC, or
FDT boot path. Those mechanisms require explicit target adaptation rather than
invented parity claims.

## Logical variables

| Variable | Meaning |
| --- | --- |
| `z` | BSS bytes cleared before C entry |
| `f` | validated FDT blob bytes |
| `m` | canonical available-memory ranges |
| `r` | canonical reservation ranges |
| `x` | kernel and boot-module exclusion ranges |
| `p` | managed physical frames |
| `c` | diagnostic characters |
| `k` | retained printable diagnostic-buffer bytes |
| `w` | UART status polls for one character or flush |
| `h` | handlers sharing one MINIX IRQ line |
| `s` | panic stack frames reported by the reference |
| `q` | runnable processes inspected by MINIX load accounting |
| `e` | expired MINIX clock timers plus callback work |

Fixed QEMU addresses, one hart, one PLIC source, and bounded FDT capacities are
policy limits. They do not erase these logical variables from the complexity
account.

## Operation ledger

### B01-O01 Firmware entry, stack, and BSS

MINIX ARM and x86 entry code installs a temporary load stack, runs
architecture pre-initialization, switches to the kernel stack, and enters
`kmain`. ARM `pre_init()` clears kernel and unpaged BSS before returning the
boot-information object. `kmain()` verifies that BSS is zero.

`micros` enters supervisor mode from OpenSBI with hart ID and FDT address in
the firmware-defined argument registers. `_start` installs one 16 KiB boot
stack, clears `[__bss_start, __bss_end)` in eight-byte stores, and calls
`kernel_main` without rewriting the two arguments.

| Property | MINIX | `micros` |
| --- | --- | --- |
| Initial stack | temporary load stack, then kernel stack | one aligned boot stack |
| Boot-data pointer | multiboot-derived `kinfo_t` | OpenSBI FDT address |
| BSS clear | linear byte/word clear in pre-init | linear eight-byte clear |
| Return from main | unreachable/halt | WFI loop if `kernel_main` returns |
| Time | `O(z)` | `O(z)` |
| Auxiliary storage | fixed stacks | fixed 16 KiB boot stack |

**Classification:** Required adaptation. The target entry mechanism differs,
while stack-before-C, complete BSS initialization, nonreturning kernel entry,
and linear work are retained.

### B01-O02 Firmware memory description

MINIX consumes a multiboot information object, copies available memory ranges
into fixed `kinfo.memmap` storage, and later cuts kernel and boot-module
ranges. The initial import is linear in multiboot records; later cuts scan the
fixed memory-map array.

`micros` receives an FDT blob from OpenSBI. The FDT parser publishes three
fixed arrays:

- available memory ranges, capacity 16;
- reservation-map ranges, capacity 32; and
- `/reserved-memory` ranges, capacity 32.

Kernel/frame exclusion and canonicalization occur in cross-owner B02 sources
but remain part of the B01 firmware-memory operation. They are audited in
B01-O03C and B01-O03D; B02 must later reuse those exact rows.

**Classification:** Required adaptation. FDT replaces multiboot on the
selected platform; fixed bounded range storage and later ownership filtering
retain the reference separation between firmware discovery and memory-policy
consumption.

### B01-O03 FDT validation and extraction

`micros_fdt_parse_memory_map()` uses:

- one immutable `fdt_view`;
- one depth-32 array of node summaries;
- one cursor over the reservation map;
- one cursor over the structure block; and
- fixed output arrays owned by the caller.

Header offsets and sizes are checked with overflow-safe addition. Structure
tokens are consumed once. Node names are scanned once in place. Relevant
property names are bounded to 31 bytes and compared with a constant set.
`reg` tuples are consumed once.

```text
time:       O(f + r)
stack:      O(MICROS_FDT_MAX_DEPTH)
output:     O(MICROS_FDT_MAX_MEMORY_RANGES
              + MICROS_FDT_MAX_RESERVATION_RANGES
              + MICROS_FDT_MAX_RESERVED_MEMORY_RANGES)
heap:       none
```

Atomic difference classifications:

| Difference | Classification | Reason |
| --- | --- | --- |
| FDT instead of multiboot | Required adaptation | OpenSBI supplies an FDT on RISC-V QEMU `virt` |
| Overflow, layout, cell, duplicate-property, depth, and capacity checks | Compatible extension | Bounded safety preserves the linear parse |

### B01-O03C Range canonicalization and exclusion

MINIX imports available multiboot ranges into a fixed memory map. Each
`add_memmap()` scans for a free slot. `cut_memmap()` scans existing ranges and
adds up to two residual pieces for every kernel/module exclusion. The logical
work is range-based; it does not inspect every physical page.

The `micros` bootstrap path copies FDT ranges, appends `[0, __kernel_end)` to
the reservations, and invokes `micros_frame_allocator_initialize()`:

1. inward-align available ranges and outward-align reservations;
2. insertion-sort each range array;
3. merge overlapping or adjacent ranges;
4. subtract every canonical reservation from every canonical memory range;
5. build ordered managed ranges with bitmap offsets; and
6. zero the complete allocation bitmap before publishing initialized state.

```text
MINIX range import/cuts:
  time:       O(m^2 + x * m^2) with fixed-array free-slot scans
  storage:    O(m)

micros canonicalization:
  sorting:    O(m^2 + r^2)
  merging:    O(m + r)
  subtraction O(m * r)
  bitmap zero O(p / 64)
  storage:    O(m + r + managed_ranges + p / 64)
```

**Classification:** Required adaptation. Ordered disjoint managed ranges and
bitmap offsets are required by the Accepted frame allocator/ownership
representation. The quadratic insertion sorts do not worsen the fixed
reference's range-array family. B02-O01 is reserved for the deeper allocator
structure audit.

### B01-O03D Invoked valid-path bootstrap validation

After canonicalization, `micros_bootstrap_memory_initialize()` invokes
`bootstrap_state_is_valid()` before returning success. It:

- scans every managed bitmap bit;
- validates each managed range;
- visits every managed frame;
- rescans the raw FDT memory union for each frame; and
- rescans both reservation arrays for each frame.

`fdt_memory_union_covers()` may rescan all `m` memory ranges for each overlap
step, producing a worst-case `O(m^2)` union check. The complete success path is
therefore:

```text
O(p * (m^2 + r) + p + managed_ranges * (m^2 + r))
```

MINIX validates and cuts range records; it does not re-prove every physical
page against the original firmware map before completing boot.

**Classification:** Divergence requiring correction. See B01-F006. B02-O02 is
reserved for the complete bootstrap-state validator audit.

### B01-O04 Kernel diagnostic buffering and early output

MINIX `struct kmessages` retains two diagnostic structures:

- `km_buf[10000]`, a circular history with next index and current size; and
- `kmess_buf[80 * 25]`, a printable rolling tail with `blpos`.

`kputc()` appends every character to both. The circular history is `O(1)` per
character. Before the printable tail saturates, its update is also `O(1)`;
after saturation, every new character shifts almost the complete
`kmess_buf` with `memmove()`, so its worst-case update is `O(k)`.
`END_OF_KMESS` may notify the output driver. The x86 panic path prints the
retained printable tail before reset.

Optional direct serial debugging waits at most 100,000 status polls before
writing a character. ARM direct serial also performs a second bounded
empty-transmitter poll.

`micros` has no `kmess`-equivalent ring. Every early boot record calls
`uart_write()`, which polls `LSR.THRE` without a bound for every character.
`uart_flush()` separately polls `LSR.TEMT` without a bound. Normal output is
forbidden after TTY handoff; panic destructively seizes the UART.

```text
MINIX circular history:   O(c)
MINIX printable tail:     O(c * k) worst case after saturation
MINIX direct serial:      O(c * min(w, 100000))
micros early/panic path:  O(c * w), with unbounded w
```

The owner state (`EARLY`, `TTY`, `PANIC`) is a valid isolation extension, but
it does not replace the missing baseline buffer or justify unbounded polling.

Each missing or changed mechanism is classified separately:

| Difference | Classification |
| --- | --- |
| Unbounded direct UART polling | Divergence requiring correction |
| Missing circular diagnostic history | Divergence requiring correction |
| Missing printable rolling panic tail | Divergence requiring correction |

See B01-F001 and B01-F002.

### B01-O05 Console ownership transition

MINIX may emit direct early/debug output while ordinary terminal drivers own
normal device service. `micros` makes this split explicit with one
`micros_uart_console_state` and the transitions:

```text
EARLY -> quiesced EARLY -> TTY
EARLY|TTY -> PANIC
```

Quiesce drains the physical transmitter, disables UART interrupt generation,
then publishes the state transition. Panic disables supervisor/external
interrupts, disables the PLIC route, moves TTY handoff state to panic, and
reinitializes the UART.

**Classification:** Compatible extension. Explicit ownership prevents mixed
kernel/TTY output. The unbounded drain itself remains B01-F001. B09 owns the
normal TTY-side transition and device rings.

### B01-O06 Panic capture, report, and termination

MINIX `panic()`:

1. checks `kinfo.minix_panicing`;
2. immediately resets on recursive entry;
3. publishes the panicking sentinel;
4. formats the reason;
5. identifies the CPU;
6. emits a stack trace; and
7. shuts down.

`micros_panic_entry` atomically clears supervisor interrupt enable and captures
six fixed machine fields. `micros_panic_report()` seizes console/PLIC/TTY
ownership, emits deterministic source, build, hart, machine, and optional trap
records, flushes, requests SBI system-failure shutdown, and uses WFI if SBI
returns.

MINIX links `util_stacktrace()` into both x86 and ARM kernels when
`USE_SYSDEBUG` is enabled, which is the default. It follows frame pointers,
prints each saved PC, and stops at null or a non-increasing next frame. The
walk is `O(s)` time, `O(1)` auxiliary storage, and has no explicit maximum
depth.

Three terminal-path properties are absent or unsafe in `micros`:

- there is no recursive-panic sentinel before seizure/reporting; and
- there is no stack trace or accepted staged replacement point; and
- if the UART console initialization magic is corrupt,
  `uart_panic_seize()` hard-spins before raw UART setup or SBI reset.

```text
MINIX report time:   O(reason + s)
micros report time:  O(reason + file + fixed context), excluding UART polls
```

Atomic difference classifications:

| Difference | Classification |
| --- | --- |
| RISC-V CSR capture and SBI termination | Required adaptation |
| Fixed machine-readable records and explicit console seizure | Compatible extension |
| Missing recursive-entry sentinel | Divergence requiring correction |
| Missing frame-pointer stack trace | Divergence requiring correction |
| Pre-output hard-spin on corrupt console magic | Divergence requiring correction |

See B01-F003 through B01-F005.

### B01-O07 Supervisor interrupt primitives

MINIX architecture code saves/restores interrupt state and separately controls
local timer and hardware-interrupt sources. `micros` uses fixed CSR operations
for global SIE, STIE, and SEIE plus `fence iorw, iorw`.

Every operation has constant state and `O(1)` work.

**Classification:** Required adaptation.

### B01-O08 External interrupt controller

MINIX keeps one linked handler list per IRQ vector. Registration walks the
selected line to detect duplicate hooks and allocate an ID bit. Dispatch masks
the line, walks all `h` handlers, tracks active IDs, acknowledges the
controller, and unmasks only when every handler completes.

`micros` v0.1 has one PLIC source, one manifest-bound TTY owner, and one
controller object holding four register pointers plus
`DISABLED/PREPARED/ENABLED`.

The complete mechanism has five atomic operations:

1. **Initialize:** validate distinct aligned register pointers, clear priority,
   enable and threshold, then publish `DISABLED`.
2. **Prepare/commit enable:** validate the complete disabled state into one
   magic plan; commit priority/threshold and `PREPARED`; commit source-10
   enable and `ENABLED`; clear the plan. Target wrappers fence before and after
   MMIO publication.
3. **Direct disable:** clear enable, priority and threshold, then publish
   `DISABLED`.
4. **Claim:** validate `ENABLED`, fence, and read one claim register.
5. **Complete:** require source 10, validate `ENABLED`, write that source, and
   fence.

```text
MINIX register/dispatch: O(h)
micros fixed source:     O(1)
controller storage:      O(1)
enable-plan storage:     O(1)
```

Atomic difference classifications:

| Difference | Classification | Replacement/effect |
| --- | --- | --- |
| PLIC registers and claim/complete instead of legacy mask/ack | Required adaptation | QEMU `virt` hardware |
| Prevalidated two-commit enable plan | Compatible extension | Failure-atomic console handoff |
| One source and one owner instead of per-vector hook lists | Staged substitution | Dynamic/shared routing returns with reviewed driver discovery |

`tty-core` supplies portable phase/plan/failure-preservation evidence.
`test-qemu-mmu` authenticates disabled priority/enable state and independent
SEIE behavior on target. B09 audits notification and device-drain ownership.

### B01-O09 Timer program and delivery

MINIX hardware programming/rearming is constant work, but complete clock
delivery is not:

- current/billed user, system and virtual-timer updates are constant work;
- `load_update()` walks all `q` runnable processes across priority queues; and
- `tmrs_exptimers()` removes all `e` expired timers and invokes every callback.

Complete reference clock delivery is `O(q + e + callback work)`.

`micros` stores initialized, active, interval, absolute deadline, and tick
count in the hart object. SBI TIME programs an absolute deadline. Start, stop,
interrupt handling, and return preparation each perform a fixed number of CSR,
counter, SBI, and state operations. A stale early delivery does not consume a
tick. Return preparation reprograms only an already expired deadline.

```text
time per operation: O(1)
state per hart:      O(1)
```

Atomic difference classifications:

| Difference | Classification | Exact owner/replacement |
| --- | --- | --- |
| SBI absolute timer instead of local APIC/board registers | Required adaptation | B01-O09 |
| Hart-local initialized/active/deadline/tick state | Compatible extension | B01-O09 |
| Context-stop accounting and runnable-load computation | Staged substitution | B03-O08 must audit the current accounting replacement and absent load-history behavior |
| General kernel timer queue and expired callbacks | Staged substitution | Must return before the first feature needing multiple independent kernel timers, no later than Step 17 POSIX timers, through a reviewed DAG/ADR |
| Process virtual/profile timers and signal delivery | Staged substitution | Step 17 POSIX/signals |

### B01-O10 SBI reset

MINIX delegates final reset/poweroff to architecture shutdown code. `micros`
uses the SBI System Reset extension and enters a nonreturning WFI loop if
firmware violates that contract.

Atomic difference classifications:

| Difference | Classification |
| --- | --- |
| SBI System Reset instead of architecture reset/poweroff | Required adaptation |
| Machine-readable SBI-return failure record and WFI fallback | Compatible extension |

## Accepted structure and complexity summary

| Mechanism | Structure | Valid-path time | Storage | Classification |
| --- | --- | --- | --- | --- |
| Entry/BSS | linker ranges plus fixed boot stack | `O(z)` | fixed stack | Required adaptation |
| FDT transport | immutable view, depth stack, three range arrays | `O(f + r)` | bounded fixed arrays | Required adaptation |
| FDT validation | checked offsets/cells/properties/ranges | `O(f + r)` | same parser state | Compatible extension |
| Range canonicalization | sorted/merged memory and reservation arrays plus managed bitmap geometry | `O(m^2 + r^2 + m*r + p/64)` | `O(m + r + p/64)` | Required adaptation |
| Console ownership | one explicit owner/quiesced state | `O(1)` transition excluding drain | `O(1)` | Compatible extension |
| PLIC hardware binding | one fixed controller object | `O(1)` | `O(1)` | Required adaptation |
| PLIC enable transaction | one prevalidated plan | `O(1)` | `O(1)` | Compatible extension |
| PLIC one-owner scope | no hook lists | `O(1)` | `O(1)` | Staged substitution |
| SBI timer hardware | one state object per hart | `O(1)` | `O(1)` per hart | Required adaptation |
| Hart-local timer validation | checked initialized/active/deadline state | `O(1)` | same state | Compatible extension |
| General/virtual timer omission | no timer queue or signal timers | N/A | N/A | Staged substitution |
| SBI reset | register ABI call | `O(1)` from kernel perspective | `O(1)` | Required adaptation |
| SBI-return failure record | fixed fallback record and WFI | `O(1)` excluding UART | `O(1)` | Compatible extension |

Per-frame bootstrap validation, early/panic output, panic recursion, panic
seizure corruption, and panic tracing are excluded from this accepted table
because their findings remain open.

## Executable evidence mapping

| Evidence | What it proves | What it does not prove |
| --- | --- | --- |
| `fdt-parser` | header/layout/token/property/range/capacity/depth behavior under ASan/UBSan | exact byte-visit complexity; output preservation on every failure |
| `test-qemu-smoke` | OpenSBI entry, boot arguments, early UART, FDT event/count records, clean SBI shutdown | stuck-UART liveness; kernel message buffering |
| `test-qemu-panic` | direct panic capture, five core records, UART seizure, SBI failure shutdown | recursive panic; stack trace; bounded UART polling |
| `test-qemu-trap-panic` | separately captured trap context in panic output | recursive fault while already reporting panic |
| `test-qemu-timer` | real SBI TIME delivery, stale rejection, rearm, stop | MINIX load scan, expired timer queue, virtual timers, long-run performance |
| `tty-uart` and `test-qemu-uart-console` | console state transitions and physical UART configuration | baseline kernel message ring; stuck-device bounds |
| `tty-core` PLIC cases | initialize, prepare/commit enable, disable, claim/complete, plan consumption and failure preservation | target MMIO/CSR ordering; dynamic/shared IRQ hooks |
| `test-qemu-mmu` controller record | target disabled priority/enable state and independent SEIE control | complete TTY claim/drain/complete lifecycle |

The B14 secondary manifest must include the production conditional bridges in
`kernel/platform/timer.c`, `kernel/diagnostic/panic.c`,
`arch/riscv64/uart.c`, `kernel/boot/boot_component_tests.c`, and their target
assembly/report consumers.

## Evidence gaps

| ID | Gap | Later owner |
| --- | --- | --- |
| B01-E001 | No deterministic token/byte-visit counter proves the FDT parser's linear bound independently of source inspection | B14 |
| B01-E002 | No stuck-THRE/TEMT case proves a bounded serial failure policy | B14 after B01-F001 design |
| B01-E003 | No recursive-panic target proves second-entry behavior | B14 after B01-F003 design |
| B01-E004 | No evidence exists for a panic stack trace because the mechanism is absent | B14 after B01-F004 design |
| B01-E005 | No corrupt-console-magic panic case proves terminal reset/halt independent of ownership metadata | B14 after B01-F005 design |
| B01-E006 | No operation-count evidence exposes per-frame raw-FDT rescans on the successful bootstrap path | B14 after B01-F006 design |

These gaps are findings inputs, not permission to add tests during the
read-only audit.

## Findings register

### B01-F001: UART polling is unbounded

- Severity: High
- `micros` evidence:
  - `arch/riscv64/uart.c:uart_write_character`
  - `arch/riscv64/uart.c:uart_flush`
- MINIX evidence:
  - `minix/kernel/arch/i386/arch_reset.c:ser_putc`
  - `minix/kernel/arch/earm/bsp/ti/omap_serial.c:bsp_ser_putc`
- Difference: MINIX caps each direct serial wait at 100,000 polls; `micros`
  waits forever for THRE/TEMT.
- Impact: a missing or wedged UART can prevent boot failure, panic diagnostics,
  or shutdown from reaching a terminal state.
- Smallest correction boundary: one separately reviewed early/panic UART
  design defining bounded polls, timeout result/diagnostic precedence, and
  deterministic host/target stuck-device evidence.
- Dependencies: B01-F002 must define which paths use direct polling.

### B01-F002: The MINIX diagnostic buffers are absent

- Severity: High
- `micros` evidence:
  - `arch/riscv64/uart.c:uart_write`
  - direct boot records in `kernel/boot/boot_foundation.c`
- MINIX evidence:
  - `minix/kernel/utility.c:kputc`
  - `minix/include/minix/type.h:struct kmessages`
  - `_KMESS_BUF_SIZE` in `minix/include/minix/sys_config.h`
  - x86 panic consumption of `kmess.kmess_buf`
- Difference: MINIX retains a 10,000-byte circular history and a separate
  2,000-byte printable rolling tail. The saturated printable tail shifts with
  `memmove()` on every character. It may notify an output driver at record
  end. `micros` retains neither structure, synchronously polls MMIO for every
  early character, and discards the general kernel diagnostic stream after
  TTY handoff.
- Impact: the baseline data structure, normal-output cost, retained diagnostic
  history, and driver handoff behavior are not reproduced.
- Smallest correction boundary: a design/implementation pair that first
  reproduces both reference history roles, capacities, circular/rolling
  overflow algorithms, record-end notification, panic consumption, and
  early/panic direct fallback. A later measured optimization may replace the
  saturated `memmove()` algorithm.
- Dependencies: B09 console/TTY audit and B14 evidence audit.

### B01-F003: Panic has no recursive-entry guard

- Severity: High
- `micros` evidence:
  - `arch/riscv64/panic.S`
  - `kernel/diagnostic/panic.c:micros_panic_report`
- MINIX evidence:
  - `minix/kernel/utility.c:panic`
  - `ARE_PANICING` sentinel and immediate reset
- Difference: a synchronous fault or invariant failure during panic seizure or
  reporting can re-enter the full panic path and consume another panic frame.
- Impact: recursive panic may loop, corrupt the failure stack, or prevent the
  terminal reset/halt path.
- Smallest correction boundary: a pre-report one-way panicking state with an
  exact second-entry reset/halt rule and target evidence that injects a
  recursive failure.
- Dependencies: B01-F001 direct-output failure policy.

### B01-F004: Panic omits the reference stack trace

- Severity: Medium
- `micros` evidence:
  - fixed five-record contract in ADR-0014
  - `kernel/diagnostic/panic.c:micros_panic_report`
- MINIX evidence:
  - `minix/kernel/utility.c:panic`
  - `minix/lib/libsys/stacktrace.c:util_stacktrace`
  - x86/ARM `Makefile.inc` linkage of `stacktrace.o`
  - default `USE_SYSDEBUG=yes` in `share/mk/bsd.own.mk`
- Difference: fixed source/CSR/trap records do not reproduce the reference
  frame-pointer walk, and no staged replacement point is recorded. The
  reference walk has `O(s)` time, `O(1)` auxiliary storage, no explicit depth
  cap, and terminates at null or a non-increasing next frame.
- Impact: failures below a shared call site can be materially harder to locate;
  the baseline panic algorithm is incomplete.
- Smallest correction boundary: a reviewed bounded RISC-V stack-trace design
  with explicit frame validity, maximum depth, fault containment, output
  format, and deterministic target evidence, or a superseding decision that
  proves a required target constraint and an exact replacement point.
- Dependencies: B03 stack ownership and B05 trap-context audits.

### B01-F005: Corrupt console metadata hard-spins before panic output

- Severity: High
- `micros` evidence:
  - `arch/riscv64/uart.c:uart_write`
  - `arch/riscv64/uart.c:uart_panic_seize`
  - `kernel/tty/uart_console_core.c:micros_uart_console_panic`
- Difference: invalid console state can trigger panic, but corrupt
  initialization magic makes panic seizure enter a permanent empty loop before
  raw UART setup, diagnostics, SBI reset, or fallback WFI.
- Impact: the exact corruption that requires terminal diagnostics can suppress
  both diagnostics and termination. Bounded THRE/TEMT polling and a recursive
  sentinel do not repair this pre-output spin.
- Smallest correction boundary: a panic-seizure design whose raw hardware
  reinitialization and reset/halt path do not depend on valid ordinary console
  ownership metadata, while still reporting metadata corruption when output
  is possible.
- Dependencies: B01-F001 and B01-F003.

### B01-F006: Successful bootstrap validation rescans every frame

- Severity: High
- `micros` evidence:
  - `kernel/memory/bootstrap_memory.c:bootstrap_state_is_valid`
  - `kernel/memory/bootstrap_memory.c:managed_frame_is_allowed`
  - `kernel/memory/bootstrap_memory.c:fdt_memory_union_covers`
- MINIX evidence:
  - range import/cut algorithms in x86/ARM `pre_init.c` and `pg_utils.c`
- Difference: after range canonicalization, `micros` revalidates every managed
  frame against the raw FDT memory and reservation arrays. Worst-case work is
  `O(p * (m^2 + r))`; MINIX completes boot from transformed range records.
- Impact: boot validation grows with every managed frame and repeats
  range-union work already represented by canonical managed ranges. It can
  create large target-time differences without changing small-memory output.
- Smallest correction boundary: a B02 design/implementation pair that proves
  canonical managed-range equivalence and reservations at range/bitmap-word
  granularity, retains complete safety and inactive-tail checks, and removes
  per-frame raw-FDT rescans from the successful boot path.
- Dependencies: B02 range/allocator audit and B14 operation-count evidence.

## Unit conclusion

B01 path and operation coverage is complete for the pinned tree. The FDT,
entry, PLIC, timer, interrupt-control, and SBI mechanisms have explicit
structural/algorithmic classifications and no unclassified hot path remains.

B01 is **audited but not certified**. B01-F001 through B01-F006 are blocking
inputs to the final correction DAG. Independent review corrected operation
closure, timer and diagnostic complexity, PLIC transaction coverage, atomic
classifications, stack-trace evidence, delegated row ownership, panic-seizure
failure, and non-overlapping B02 identifiers. Final re-review reported no
substantive issue. B02 may now start; no B01 correction may start during the
read-only audit.

## References

- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [Earlier parity audit](minix-baseline-parity-audit.md)
- [Scheduler/timer study](minix-scheduler-and-context-switch.md)
- [IRQ notification study](minix-irq-notification.md)
- [TTY/console/IRQ study](minix-tty-console-and-irq.md)

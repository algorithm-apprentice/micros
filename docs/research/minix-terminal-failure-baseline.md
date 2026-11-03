# MINIX UART and Terminal Failure Baseline

## Status and scope

This is the fixed-reference research for PLAT02, following independently
reviewed PLAT01 implementation #199. The independently reviewed Accepted
design separately merged as #200. Its runtime and evidence implementation
follows that contract; this fixed-source study alone does not close the
remaining platform findings.

```text
micros source commit: 8375ca2f5285bc42063af8ed92ba6f3811d517e8
micros source tree:   cb472a9262b9d6f22c42c4c9e1182bf1da069d78
MINIX source commit:  4db99f4012570a577414fe2a43697b2f239b699e
owning findings:      B01-F001, B01-F003, B01-F005
```

PLAT02 owns bounded early/panic UART polling, recursive panic ownership,
corrupt-console fallback and terminal failure precedence. The diagnostic
histories and TTY consumer from PLAT01 remain intact. Panic stack traces
belong to PLAT03. Normal TTY buffering, IRQ service policy, inherited
memory/object/IPC validation costs and hosted CI retain their existing
owners.

The local MINIX checkout was verified at the fixed revision before tracing
these sources. Its implementation is evidence, not source to copy.

## Exact reference owners

| Fixed MINIX source | Evidence |
| --- | --- |
| `minix/kernel/utility.c:22-49` | Panic sentinel is checked and published before reason formatting, CPU identification, trace and shutdown |
| `minix/kernel/utility.c:55-83` | Direct serial emission precedes history insertion; physical CR translation is not retained |
| `minix/kernel/arch/i386/arch_reset.c:31-79` | Recursive reset bypasses ordinary diagnostics and attempts architecture-specific reset mechanisms |
| `minix/kernel/arch/i386/arch_reset.c:110-150` | Panic shutdown distinguishes direct serial from retained-tail display and an interactive reset |
| `minix/kernel/arch/i386/arch_reset.c:154-173` | Default x86 serial output limits readiness polling to 100,000 reads, then writes the character |
| `minix/kernel/arch/i386/oxpcie.c:49-72` | Optional PCIe serial output has its own in-use guard and the same finite readiness limit |
| `minix/kernel/arch/earm/arch_reset.c:35-41` | ARM reset calls the board reset primitive, with a fallback if it returns |
| `minix/kernel/arch/earm/bsp/ti/omap_serial.c:62-84` | ARM serial has two separately bounded polls around one character write |

The fixed tree has no RISC-V/OpenSBI reset path. The current platform,
console ownership, panic capture and shutdown ADRs remain authoritative.

## Polling is finite, not an elapsed-time deadline

The default x86 path reads transmitter readiness at most 100,000 times for
one character. It exits the loop early when `THRE` is observed. It then
performs the character write even if the poll count was exhausted.
The helper returns no timeout result.

ARM uses the same fixed attempt limit before the write and a second limit
afterward. The second predicate tests whether either `THRE` or `TEMT` is
set; it does not require both bits. This is not equivalent to the stronger
`micros` transmitter-empty requirement at console handoff.

These are count-bounded loops. Their bounds do not depend on timer
initialization, interrupt delivery, scheduler progress, a monotonic-clock
conversion or a wall-clock sleep. There is no cross-call amortization.
Optional PCIe discovery and board-specific register mapping are not part of
the fixed QEMU `virt` adaptation.

The baseline therefore establishes the finite polling algorithm and exact
attempt limit, but does **not** establish an explicit failure result or
guaranteed drain after exhaustion. A `micros` design must classify its
exhaustion policy separately. It cannot describe an unconditional
best-effort store as independently proved successful output.

## Recursive panic bypasses ordinary reporting

MINIX keeps one kernel-owned panicking sentinel. The panic routine checks it
before formatting the reason, then publishes it before any ordinary report
work. An already claimed panic calls the architecture reset routine rather
than formatting another report, replaying history or waiting for the
terminal driver.

The sentinel is distinct from serial-driver ownership and from the
diagnostic-history buffers. It is not a process, thread, endpoint or
per-message resource. The selected `micros` one-hart policy permits a small
kernel-owned terminal state without importing SMP coordination.

The architecture reset implementations must not be conflated. The x86
routine tries fixed reset mechanisms and eventually spins if reset fails.
The ARM routine invokes a board reset operation and may attempt a direct
message if it returns. Neither behavior establishes the required
RISC-V/SBI return boundary, and neither licenses an interactive key wait.

For `micros`, the existing system-failure SBI request and explicit
nonreturning fallback remain the starting contract. The design must state
which paths may still attempt UART output and which immediately terminate
without consulting broken diagnostic state.

## Current micros mechanism and coupling

At the pinned `micros` revision:

- `arch/riscv64/uart.c:35` waits for `LSR.THRE` without a bound before
  writing `THR`.
- `arch/riscv64/uart.c:197` waits for `LSR.TEMT` without a bound.
- `uart_console_begin_quiesce_prevalidated` calls that drain before
  disabling UART interrupts, publishing quiescence and capturing the
  history handoff cursor. Exhaustion cannot be treated as successful
  handoff.
- `uart_panic_seize` disables supervisor/external interrupt delivery and
  asks the console core to enter panic ownership. An invalid initialization
  magic causes an empty infinite loop before raw UART setup or SBI reset.
- `micros_uart_console_panic` rejects bad initialization magic. With valid
  magic it overwrites the owner and quiesced fields, even if those old
  fields are invalid. The corruption cases are therefore not interchangeable.
- `micros_panic_report` has no recursive-entry latch. It seizes the console,
  writes the five core records and optional trap record, replays the
  diagnostic tail, drains, requests failure shutdown and retains the
  required reset-returned failure line, second drain and terminal WFI loop.

The existing `panic_seizure_failure_emitted` flag suppresses a duplicate
PLIC/TTY seizure-failure record. It does not own first panic entry and must
not silently acquire that unrelated role.

### Console seizure is not a panic-entry sentinel

`micros_panic_seize` is also called before the actual panic report by
`kernel/tty/tty_fault.c:68`. The UART component calls it directly at
`kernel/tests/tty/uart_console_test.c:243` and then checks the terminal
device state. Consequently a valid console owner of `PANIC` does not prove
that a panic report has already entered.

Reusing that owner as a recursion guard would suppress legitimate first
reports after prior seizure. The first-entry state and physical-console
state require separate responsibilities.

### Failure reset is not confined to the reporter

`kernel/boot/boot_foundation.c:103-107` directly requests
shutdown/system-failure after an FDT error. If it returns,
`stop_after_reset_failure` at lines 44-50 writes the failure line, drains
and waits. A new UART-exhaustion terminator that only knows reporter-side
reset claims could issue another failure request from that fallback.

Independent design review identified this omission. The corrected
proposal puts the one request admission in the shared SRST wrapper for
the exact `(0, 1)` tuple. It keeps the protected boot source unchanged and
adds an actual FDT-error reset observation. The source-authority inspection
confirmed that `boot_foundation.c` has an active whole-file transport key,
while `arch/riscv64/sbi.c` has no such inventory key; its existing firmware
and register-effect obligations still remain.

### Capture and live frames

The two assembly entry points preserve the existing 48-byte machine
context. Direct entry supplies no trap frame; trap panic carries its
separately captured frame. The reporter receives both through the current
six-argument ABI.

A C-entry guard does not remove its caller or the assembly capture frame.
Recursive-entry evidence must retain every simultaneously live frame and
the applicable primary/emergency stack ownership. `_Noreturn` is not proof
that a failing reset or a recursive call is unreachable.

### Existing finite stack qualification

ADR-0086 and PLAT01's ADR-0093 refinement currently derive only a
UART-state qualification and prove three exact UART call sites infeasible.
They do not certify panic liveness or a new entry-ownership fact.

Bounded polls, new terminal calls, changed seizure behavior and fault
fixtures must be reflected in the actual source/object/linked graph.
Unknown calls, remaining cycles, modified firmware effects or a test hook
that breaks the preservation predicate cannot be silently ignored.

## Logical work and storage

Let `p` be physical characters, including an inserted CR, `w` the number of
status reads needed for readiness, `P = 100000` the reference attempt limit,
and `d` the number of explicit drains.

| Mechanism | Work | Auxiliary state |
| --- | --- | --- |
| x86 readiness poll | `O(min(w, P))` per character; at most `P` reads | One loop counter |
| ARM pre/post polls | At most `2 * P` status reads per physical character | One reused loop counter |
| Current unbounded micros write/drain | No finite bound when the device stays unready | Scalar loop state |
| Panic ownership test/publication | `O(1)` | One fixed kernel sentinel |
| Reentrant terminal selection | `O(1)` before architecture termination | No history or output buffer |
| A count-bounded micros write/drain adaptation | Upper bound `O((p + d) * P)` | Constant counters/state |

The last row is a prospective bound, not a measured implementation claim.
Firmware termination and the final nonreturning wait are separate from
polling work. PLAT01's retained-history movement remains independently
accounted; a polling correction must not alter or hide its saturated
1,999-byte shifts.

## Accepted design disposition

The separate [Accepted ADR-0095](../adr/0095-bounded-uart-and-terminal-panic.md)
and [canonical contract](../architecture/terminal-failure.md) now specify:

1. Exact poll exhaustion results, first/last permitted successful reads,
   write/drain ordering and the no-successful-handoff failure boundary.
2. First-entry ownership, recursive entry, reset-request ownership,
   reset-return behavior and the priority between simultaneous failures.
3. The exact corrupt-console fallback, including which metadata may be
   overwritten and which reporting dependencies must be bypassed.
4. Native state/work evidence and actual target observations for stuck
   transmitter, stuck drain, recursive panic and corrupt metadata.
   Missing serial output or a host timeout alone cannot prove correct
   failure handling.
5. Exact source owners, retained original assertions, current committed
   COPY01/PLAT01 authority, any additional identity transition, finite
   source/machine proof, current scope and existing budgets.

Independent design and correction review are clean, and separate design
#200 is merged. The later implementation supplies the declared runtime and
evidence owners. No executable prototype, test, harness or production edit
belonged to the research/design phase; implementation acceptance still
requires the current full author evidence and independent review.

## References

- [B01 platform audit](minix-complete-baseline-b01-platform.md)
- [Correction DAG](minix-complete-baseline-findings-and-correction-dag.md)
- [Diagnostic histories](../architecture/diagnostic-history.md)
- [Console ownership](../adr/0012-console-and-irq-handoff.md)
- [Panic capture and termination](../adr/0014-panic-diagnostics.md)
- [Finite fatal context](../adr/0086-finite-fatal-stack-context.md)
- [Structural baseline rule](../adr/0080-minix-structural-and-algorithmic-baseline.md)

# MINIX Diagnostic History Baseline

## Scope and fixed references

This study supports PLAT01 / B01-F002 after merged COPY01 #191. It is
design evidence, not implementation or a claim that the platform baseline
is certified. PLAT02 owns bounded UART polling, recursive panic entry and
corrupt-console fallback; PLAT03 owns panic stack traces.

The fixed MINIX reference is
`4db99f4012570a577414fe2a43697b2f239b699e`.
The original `micros` runtime research is pinned at
`b99848f52d01100a6db2833f610cff7007e1cc6f`.
The separate evidence prerequisites #192 through #195 advance the authoring base to
`32f19782075ab34ff2bd6992f80540b6bfd47a4f` without changing the runtime
sources traced here. Neither source pin is replaced by a future candidate.
MINIX/NetBSD source remains reference evidence, not a source donor.

| Fixed MINIX source | Responsibility |
| --- | --- |
| `minix/include/minix/sys_config.h:22` | Circular-history capacity of 10,000 bytes |
| `minix/include/minix/type.h:170-176` | Separate circular history, printable tail and their scalar positions |
| `minix/kernel/const.h:30` | Record-end sentinel is integer zero |
| `minix/kernel/utility.c:55-83` | Character insertion, rolling overflow and record-end signaling |
| `minix/kernel/system.c:469-482` | Diagnostics signaling to registered system-process privilege records |
| `minix/kernel/system/do_diagctl.c` | Bounded diagnostic producer input and privileged registration/unregistration |
| `minix/drivers/tty/tty/tty.c:389-457` | Signal-driven snapshot, wrap reconstruction and terminal output |
| `minix/kernel/arch/i386/arch_reset.c:118-130` | Panic consumption of the retained printable tail |
| `minix/kernel/arch/earm/arch_reset.c:53-73` | ARM shutdown has no corresponding printable-tail replay |

## Two distinct retained roles

The circular history has a 10,000-byte array, a next-write index and a
current retained size. For each normal nonzero input byte, `kputc` writes
the current slot, increments size only below capacity, and advances the
index modulo capacity. Saturation overwrites the oldest byte; it does not
grow, clear the whole buffer or move the other retained bytes.

The printable tail is a different 2,000-byte array with its own position.
It is not a second circular index and is not an alias of the first role.
Its exact reference overflow order is important:

1. Write the new byte at the current printable-tail position.
2. If that position is 1,998, shift exactly 1,999 bytes one position toward
   the start, using an overlap-safe forward transfer.
3. Otherwise increment the position.

The final array byte is never a producer destination. With the reference
zero-initialized storage, two zero bytes remain after the saturated
1,998-byte payload. The shift includes a trailing zero. The effective
maximum printable payload is therefore **1,998**, not 1,999 or 2,000.
Before saturation, the still-zero next slot terminates the current text.

There is no character-class filter in this routine. "Printable" describes
the tail's use as a terminated diagnostic string; nonzero control and
high-bit bytes are not silently discarded or translated. Carriage-return
insertion for a serial newline belongs to physical output, not either
retained history.

The record-end sentinel takes a separate branch: it is not appended and
does not advance either history. A `micros` adaptation must distinguish
record completion from an ordinary byte rather than treating a zero byte
as another retained character.

## Record-end and consumer authority

At record end, the reference signals diagnostics only when neither panic
nor direct serial-debug mode suppresses that route. `send_diag_sig` walks
the fixed privilege table and signals each populated record that registered
for diagnostics. Registration is restricted to system processes. If the
log is already nonempty and direct serial debugging is inactive,
registration immediately sends the first signal.

This is notification of available state, not one IPC payload per character
and not proof that the driver has consumed a byte. The history remains
kernel-owned regardless of the physical terminal driver.

The diagnostic-producer kernel call copies a bounded caller message into
kernel-owned storage before emitting its bytes and record end. This
general user-service producer interface is distinct from the kernel's own
history and from output-driver subscription. A minimal PLAT01 design must
state which producer and consumer authority it actually introduces rather
than importing the complete MINIX diagnostics-control namespace.

## TTY snapshot and output

The reference TTY obtains the kernel-message structure through its mapped
kernel-information interface. On a diagnostics signal it samples the next
index once and computes the modulo-10,000 distance from its previous index.
It copies that span into a 10,000-byte automatic buffer using at most two
source spans: the physical suffix, then the wrapped prefix.

The source explicitly notes the cursor limitation: a complete buffer lap
can be missed because next-index equality does not distinguish no new bytes
from one or more complete laps. The circular history's retained size does
not repair that consumer ambiguity in this implementation. A replacement
must either preserve and document that bounded-loss behavior or explicitly
classify an independently justified safety extension; it must not claim
that a modulo cursor detects every overwrite.

TTY performs a nonblocking kernel-origin terminal write. If an ordinary
writer is active, it temporarily saves that terminal record, admits the
kernel write, and restores the original record. It then advances its
previous index to the sampled next index. It does not wait for physical
UART drain before advancing that cursor.

The mapped kernel pointer, automatic 10,000-byte snapshot, saved terminal
record and kernel-origin non-grant write are not transferable `micros`
interfaces. The current isolated service contract has a 4 KiB user stack,
no cross-process pointer authority and an existing asynchronous VFS output
owner. Those boundaries require an explicit adaptation of snapshot storage,
data transfer, output arbitration and cursor ownership.

## Direct fallback and panic consumption

When direct serial debugging is compiled and active, `kputc` emits the
physical byte before retaining it. A logical newline emits an additional
physical carriage return; that extra byte is not inserted into the
histories. The x86 serial primitive limits its readiness polling before
the write. The ARM wrapper delegates to its board serial implementation.
PLAT02, not PLAT01, owns the independently audited polling correction.

The x86 panic shutdown path distinguishes already-direct serial output
from buffered diagnostics. Without direct serial debugging, it clears the
display, prints a fixed introduction and the retained printable tail
through `direct_print`, then waits for a key before reset. That direct
printer does not feed the replay back through `kputc`. The ARM shutdown
shown in the fixed revision does not perform that tail replay. The ledger
must not claim identical architecture behavior.

`micros` must preserve the Accepted panic capture, five contiguous core
records, optional separately captured trap context, exceptional UART
seizure and clean SBI failure shutdown. It cannot inherit x86's interactive
key wait. Any retained-tail report also needs an unambiguous representation
that does not turn old diagnostic marker text into duplicate live panic or
boot records. Replay must bypass history insertion, or it would mutate the
very data being reported.

## Complexity, storage and hot paths

Let `c` be emitted bytes, `k` the printable-tail capacity, `n` the sampled
new history span and `p` the privilege-table length.

| Operation | Reference work | Storage |
| --- | --- | --- |
| Circular append, including overwrite | `O(1)` per byte | 10,000 retained bytes and two scalars |
| Unsaturated printable append | `O(1)` per byte | 2,000 retained bytes and one scalar |
| Saturated printable append | `O(k)` per byte; exact 1,999-byte overlapping shift | No temporary array |
| Combined history production | `O(c)` before tail saturation; `O(c*k)` worst case afterward | Two distinct fixed retained structures |
| Record-end notification | `O(p)` privilege inspection | Scalar traversal state |
| TTY sampled-span reconstruction | `O(n)`, at most two source spans | 10,000 automatic bytes plus terminal-record save |
| Printable panic replay | `O(k)` plus physical-output cost | Scalar traversal |

There is no cross-call amortization that makes saturated tail shifting
constant work. The histories must not be collapsed into one ring or
silently replaced by a supposedly better overflow algorithm before this
baseline is integrated.

For a forward one-byte-overlapping shift, the selected generic word-copy
family cannot align source and destination together. A bounded forward
byte transfer preserves that reference fallback without adding a general
`memmove` API or an out-of-range word access.

Kernel diagnostic production, record completion, driver wakeup/readback,
ordinary-output arbitration and fatal replay all belong in the valid-path
cost account. A complete-state address-space or ownership validator called
by a new readback path is not free: its inherited B02 costs must be named
and cannot be hidden behind a small transfer size.

## Current micros constraints

`arch/riscv64/uart.c` currently couples kernel text output to physical UART
ownership. `uart_write` panics when ordinary output is disallowed, and its
newline translation is physical. `uart_write_hex64` sends its prefix through
`uart_write` but sends the sixteen digits directly to the character writer.
A history integration must account for both paths without losing digits,
inventing carriage returns or recording replay output again.

`kernel/diagnostic/panic.c` currently formats the core and optional trap
records after seizure and before flush/reset. It has no history replay.
The finite fatal-stack proof has a closed UART-state preservation register;
new state writes and helper calls must have explicit source/machine effects
and preserve all existing stack bounds rather than being ignored.

The current TTY accepts one exact kernel IRQ event, not arbitrary event-bit
unions. Operation 14 admits only console commit and IRQ completion.
Diagnostics notification, registration/readback authority and output
arbitration therefore require explicit new contracts, not an assumed
existing "get kernel log" API.

These are design constraints, not permission to change code in this phase.
PLAT01's final contract must cover the complete B01-F002 boundary:
both histories, record-end notification, consumer transfer, panic
consumption and early/panic direct fallback, while leaving PLAT02 and
PLAT03's separate findings open.

## References

- [B01 platform audit](minix-complete-baseline-b01-platform.md)
- [Correction DAG](minix-complete-baseline-findings-and-correction-dag.md)
- [TTY/console baseline](minix-tty-console-and-irq.md)
- [Structural baseline rule](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [Console ownership](../adr/0012-console-and-irq-handoff.md)
- [Panic contract](../adr/0014-panic-diagnostics.md)
- [Finite fatal-stack context](../adr/0086-finite-fatal-stack-context.md)

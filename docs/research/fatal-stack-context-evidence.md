# Fatal-Stack Context Evidence

## Status and exact sources

This is the documentation-only corrective prerequisite for EV03 under
[Accepted ADR-0086](../adr/0086-finite-fatal-stack-context.md).
The initial and artifact designs are merged as #180 and #181.

The current design base is:

```text
micros HEAD: 0e2a2814f4a77fdc7cf2fdc8fa0c1552631a17f5
MINIX HEAD:  4db99f4012570a577414fe2a43697b2f239b699e
```

The local MINIX checkout was inspected read-only and its working tree was
clean. No reference test runner or privilege-changing command was executed.
MINIX remains evidence, not copied implementation.

The abandoned implementation's exact preserved identity is:

```text
staged tree: 1a1c115960c94cd006e8f0d685e45e86134e55ff
source SHA:  e056101515ddf8fecc33ba78214dbc90b5a16b85e4436803d6f9dbec4532dabd
patch SHA:   75a1f2f48ee9b7e75819734e4890607cbd13df3b72ed13b9c879345ff050afe4
change:      27 files, 3917 insertions, five deletions
```

That patch and its build results are non-authoritative reference. The branch
is abandoned and implementation remains blocked until this design merges.

## Actual machine-cycle evidence

The full-spawn metadata kernel was inspected through exact object-section,
linked-symbol and call-register ownership. Its ELF SHA-256 is:

```text
659269b4e0f3ab814b28a0e22f9a69085fa1878b4c71680e913c058f5ae5f1ac
```

The minimal cycle is:

| Caller | Object-relative site | Verified target |
| --- | ---: | --- |
| `kernel/diagnostic/panic.c:micros_panic_report` | `0x24` | `arch/riscv64/uart.c:uart_write` |
| `arch/riscv64/uart.c:uart_write` | `0x34` | `arch/riscv64/panic.S:micros_panic_entry` |
| `arch/riscv64/panic.S:micros_panic_entry` | `0x6` | Shared body inside `micros_trap_panic_entry` |
| `arch/riscv64/panic.S:micros_trap_panic_entry` | `0x24` | `kernel/diagnostic/panic.c:micros_panic_report` |

The direct call pairs and the first entry's cross-symbol branch were checked
against actual linked instructions. Clang records 80 bytes for the reporter
and 32 for `uart_write`. The direct panic prefix allocates zero; the shared
body allocates the Accepted 48-byte panic context exactly once.

The current iterative graph rejects this verified four-edge subset as
`recursive same-epoch call graph`. This is a concrete graph rejection, not a
complete stack certificate and not a reproduced runtime recursive fault.
Other unresolved assembly paths were not silently accepted by that probe.

## Existing state makes this specific return edge conditional

| Current source | Relevant behavior |
| --- | --- |
| `kernel/diagnostic/panic.c:44-58` | Reporter calls `micros_panic_seize` before its first formatting output |
| `kernel/diagnostic/panic.c:17-42` | Seizure clears interrupt delivery, handles PLIC/TTY mirrors, then calls `uart_panic_seize` before any optional seizure diagnostic |
| `arch/riscv64/uart.c:38-43` | `uart_write` calls panic only when the exact console output predicate is false |
| `arch/riscv64/uart.c:93-111` | UART seizure stops on console-transition failure; its normal return follows interrupt disable and exact UART reinitialization |
| `kernel/tty/uart_console_core.c:107-122` | Valid magic permits transition to owner `PANIC` and `quiesced=true` |
| `kernel/tty/uart_console_core.c:15-37,125-133` | Those fields make complete validation and output permission succeed |
| `kernel/tty/uart_console_core.h:9-33` | Fixed magic, owner/error values and the three-field state |

Only normal return from the checked seizure chain establishes the fact.
Invalid initialization magic takes the existing pre-output nonreturning spin;
it does not establish a successful return or prove liveness.

The formatting path reads console metadata and writes only UART MMIO.
SIE and SEIE stay clear. PLIC/TTY mirror failure may emit an existing failure
diagnostic after UART seizure, but does not undo the UART predicate.
No general assumption about arbitrary panic callees follows.

## System Reset return preservation

Independent review identified one substantive omission: the closed
preservation register did not cover `uart_flush` and `sbi_system_reset`.
The required fallback at `kernel/diagnostic/panic.c:109-119` follows both
calls, and the seizure wrapper also flushes before returning. Dominance
alone cannot qualify that continuation.

`arch/riscv64/uart.c:114-118` flushes through UART status reads and changes
neither console metadata nor interrupt-enable CSRs. Its return effect
does not prove that a stuck transmitter ever terminates.

The [fixed SBI v2.0 calling convention](https://github.com/riscv-non-isa/riscv-sbi-doc/blob/v2.0/src/binary-encoding.adoc)
defines result-register changes; that GPR rule alone does not prove console
memory or CSR preservation.
The [System Reset specification](https://github.com/riscv-non-isa/riscv-sbi-doc/blob/v2.0/src/ext-sys-reset.adoc)
requires nonreturning success and permits returning errors. Accepted
ADR-0014 consequently retains the failure diagnostic even when the ordinary
shutdown succeeds.

The exact current default firmware was independently compared with QEMU's
upstream prebuilt artifact, not installed or executed for this research:

```text
upstream QEMU release: v11.1.3
firmware update:       663db5ccad62ab4d2357ed527494e1126fc98da6
OpenSBI source:        74434f255873d74e56cc50aa762d1caf24c099f8
OpenSBI release:       1.8.1
artifact:              pc-bios/opensbi-riscv64-generic-fw_dynamic.bin
bytes:                 275928
upstream SHA-256:      894e2aef99590fc07ec6c60ab00282b8bc5d5d5bb2a1d0c6ada0c52df24274c0
installed comparison:  byte-for-byte equal
```

The [upstream firmware update](https://github.com/qemu/qemu/commit/663db5ccad62ab4d2357ed527494e1126fc98da6)
updates both the OpenSBI source and prebuilt images. Inspection used that
fixed source, not an unrelated current branch:

| Fixed OpenSBI source | Scoped effect |
| --- | --- |
| [firmware/fw_base.S](https://github.com/riscv-software-src/opensbi/blob/74434f255873d74e56cc50aa762d1caf24c099f8/firmware/fw_base.S) | Machine trap save/restore preserves supervisor `sp`/registers and `mstatus`, including SIE, on the exact normal return |
| [lib/sbi/sbi_ecall_srst.c](https://github.com/riscv-software-src/opensbi/blob/74434f255873d74e56cc50aa762d1caf24c099f8/lib/sbi/sbi_ecall_srst.c) | Scalar type/reason validation and unsupported errors; supported reset does not return through this path |
| [lib/sbi/sbi_ecall.c](https://github.com/riscv-software-src/opensbi/blob/74434f255873d74e56cc50aa762d1caf24c099f8/lib/sbi/sbi_ecall.c) | Normal completion advances the saved caller PC and updates result registers; it does not write the supervisor console object |
| [lib/sbi/sbi_trap.c](https://github.com/riscv-software-src/opensbi/blob/74434f255873d74e56cc50aa762d1caf24c099f8/lib/sbi/sbi_trap.c) | Distinguishes normal ecall completion from fault redirection and post-call software-event processing |
| [lib/sbi/sbi_sse.c](https://github.com/riscv-software-src/opensbi/blob/74434f255873d74e56cc50aa762d1caf24c099f8/lib/sbi/sbi_sse.c) | Only enabled events can inject a handler; the scoped kernel SBI inventory does not register/enable SSE |

For this native S-mode SRST boundary, the returning path preserves SIE,
SEIE and console metadata, with no supervisor buffer pointer supplied.
The scoped contract excludes another SEE/hypervisor, modified firmware,
enabled SSE, changed arguments or a redirected return. Source/byte/boot
identity and the exact `ecall + 4` continuation are therefore part of
qualification, not an assumption from an observed successful shutdown.

All returning result values still lead to the existing failure message,
final flush and fatal wait. No result-register value, nonreturning success
claim or private firmware stack can erase the supervisor caller frames
or turn the fallback into success.

## Fixed MINIX behavior, structures and algorithms

At `minix/kernel/utility.c:17-49`, MINIX owns one kernel panicking sentinel.
`panic` checks it before formatting, resets immediately on repeated entry,
publishes it before output, reports the CPU/trace, then shuts down.
The state check and publication are constant work and scalar storage;
reason formatting depends on emitted character count.

At `minix/kernel/utility.c:55-82`, `kputc` owns both diagnostic buffer roles,
optionally performs direct serial output and suppresses driver notification
while panicking. Ring insertion is constant work; saturated printable-buffer
movement has linear cost in its retained buffer size. These mechanisms are
already assigned to `PLAT01`/`PLAT02`, not implemented by this design.

The reference's recursive guard is not the same as the `micros` UART ownership
predicate. `micros` currently lacks the guard. Its polled transmitter and
corrupt-magic spin also lack the later required liveness policy.
No passing state-qualified stack proof closes those findings.

## ADR-0080 classification

| Difference | Classification | Bound or remaining owner |
| --- | --- | --- |
| RV64 48-byte panic context and explicit console seizure | Required adaptation | Retain Accepted ABI, CSR ordering and stack owner |
| Two finite host analysis contexts for one proved UART predicate | Compatible extension | At most `2V` nodes and constant-multiple edges; `O(V + E)` work/storage, no runtime cost |
| Source/linked dominance and preservation evidence | Compatible extension | Closed current sites; unknown effects fail |
| Missing MINIX recursive-entry guard and corrupt-console fallback | Divergence requiring correction | `PLAT02`, B01-F003/B01-F005 |
| Unbounded polling and missing panic trace | Divergence requiring correction | `PLAT02`/`PLAT03`, B01-F001/B01-F004 |

The analysis retains every simultaneously live frame. An ordinary
`uart_write` ownership failure may retain its original 32-byte frame, enter
the 48-byte panic context and 80-byte reporter, then invoke another
32-byte `uart_write` in the proved ready context. Context qualification is
not a stack pivot and cannot discard that original frame.

## Evidence and failure boundary

Before abandonment, all 37 native CTests passed. The complete Python discovery
passed 1123 cases after narrowly correcting new source-family and Python-owner
registration. The CTest and Python runs were bound to different staged trees;
their difference contained documentation and host registration only, with no
C/assembly/recipe change to the CTests.

The fresh full-spawn pair passed ten mapped ELF byte comparisons and complete
seed comparisons on its recorded earlier tree. Other current variants, the
complete linked/assembly proof, observer, authenticated target reports and
full final gate were not claimed complete.

Design acceptance must now precede a test-first restart. Native negative
controls remove or bypass seizure, mutate the console object, enable
interrupts, alter the guard, borrow a qualifier, or add an unbound callee.
Remaining real cycles, unknowns, excess and incomplete evidence still fail.
The unchanged target/full acceptance remains required afterward.

The corrected acceptance also requires independent flush/System Reset
return-effect evidence and negative controls for unknown firmware, byte/
source mismatch, CSR or console mutation, SSE/injected return and omitted
fallback. No ordinary runtime code or required failure output changes.

Independent corrected design review reported no substantive issue.
This records design Acceptance only; restarted implementation, target
observations and stack certificates remain later evidence.

## References

- [Accepted ADR-0086](../adr/0086-finite-fatal-stack-context.md)
- [Stack contract](../architecture/stack-bound-evidence.md)
- [B01-O06 and findings](minix-complete-baseline-b01-platform.md#b01-o06-panic-capture-report-and-termination)
- [Correction DAG](minix-complete-baseline-findings-and-correction-dag.md)

# B09 TTY Baseline Audit

## Status

```text
audit unit:       B09 TTY
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; seven blocking findings; not certified
implementation:   forbidden during complete-audit phase
```

B09 audits early-console handoff, exact UART mapping and control authority,
PLIC claim/complete ordering, low-level receive/transmit mechanisms, canonical
input and echo, grant-backed read/write request lifecycle, failure handling,
and the current one-console staged boundary.

## Exact primary path manifest

B09 has 35 primary paths in the pinned tree.

### Public contract

| Path | Responsibility |
| --- | --- |
| `include/micros/tty.h` | TTY capacities, UART tuple, messages, events, results, and operation-14 commands |

### Kernel console, handoff, interrupt, and control owners

| Path | Responsibility |
| --- | --- |
| `kernel/tty/tty_control_core.c` | Operation-14 decode and exact tuple checks |
| `kernel/tty/tty_control_core.h` | Decoded control request contract |
| `kernel/tty/tty_control_syscall_core.c` | Caller, binding, endpoint, and profile authority |
| `kernel/tty/tty_control_syscall_core.h` | Control-authority result and resolver interface |
| `kernel/tty/tty_control_syscall.c` | Captured commit/IRQ-complete dispatch and runtime agreement |
| `kernel/tty/tty_control_syscall.h` | Captured operation-14 adapter |
| `kernel/tty/tty_fault.c` | One-shot TTY-owner fault report and panic seizure |
| `kernel/tty/tty_fault.h` | TTY-owner fault interface |
| `kernel/tty/tty_handoff_core.c` | Console/route phases, deadlines, claims, completion, device-leaf classification |
| `kernel/tty/tty_handoff_core.h` | Portable handoff and device-authority contracts |
| `kernel/tty/tty_handoff_runtime_internal.h` | Authoritative context and private runtime helpers |
| `kernel/tty/tty_handoff_runtime_state.c` | Runtime storage and byte copy/clear helpers |
| `kernel/tty/tty_handoff_runtime_transitions.c` | Candidate-copy prepare and prevalidated commit transitions |
| `kernel/tty/tty_handoff_runtime_validate.c` | Bootstrap, object, endpoint, profile, and live-binding agreement |
| `kernel/tty/tty_handoff_runtime.c` | Runtime preparation, binding, reset, begin, deadline, and panic |
| `kernel/tty/tty_handoff_runtime.h` | Target handoff runtime interface |
| `kernel/tty/tty_interrupt.c` | PLIC claim, exact-source routing, notification, and retained completion |
| `kernel/tty/tty_interrupt.h` | External-interrupt dispatch result/interface |
| `kernel/tty/uart_console_core.c` | Early/TTY/panic UART ownership state |
| `kernel/tty/uart_console_core.h` | Console ownership interface |

### User TTY service owners

| Path | Responsibility |
| --- | --- |
| `servers/tty/tty_control.c` | TTY commit and IRQ-complete syscall wrappers |
| `servers/tty/tty_control.h` | Private control-wrapper interface |
| `servers/tty/tty_core.c` | Wire codec, canonical queue, echo, read staging, and write/output state |
| `servers/tty/tty_core.h` | Terminal state, effects, errors, and core operations |
| `servers/tty/tty_service_core.c` | Service initialization, ownership, and request dispatch |
| `servers/tty/tty_service_core.h` | Pending/completion/service state and I/O callback contracts |
| `servers/tty/tty_service_internal.h` | Private request/read/write helper interface |
| `servers/tty/tty_service_read.c` | Pending-read acceptance, grant transfer, commit, and completion |
| `servers/tty/tty_service_request.c` | Reply construction, grant copy, request validation, cancel, collect, and reply commit |
| `servers/tty/tty_service_validate.c` | Complete service-state validation |
| `servers/tty/tty_service_write.c` | Write probe/copy/acceptance and completion publication |
| `servers/tty/tty_service.c` | Bootstrap configuration, UART MMIO bus, receive loop, IRQ/call orchestration |
| `servers/tty/tty_uart.c` | NS16550A setup, IIR dispatch, receive/transmit work, and interrupt-enable shadow |
| `servers/tty/tty_uart.h` | UART registers, FIFO capacity, state, bus, and drain interface |

## Cross-owner source dependencies

| Path family | B09 operation | Primary/later owner |
| --- | --- | --- |
| trap, PLIC, early UART, panic, timer deadline | B09-O01/O02/O08 handoff, claim, completion, failure | B01 |
| exact device mapping and address-space validation | B09-O01/O02 UART leaf and ordinary mapping agreement | B02/B07 |
| process/thread objects and scheduling | B09-O01/O02 exact live owner and notification wakeup | B03 |
| endpoint, notification, reply token, privilege profile | B09-O02/O03/O06 kernel/user transport | B04 |
| grants, checked copy, runtime wrappers | B09-O06/O07 terminal byte transfer | B05 |
| static manifest, held release, ready deadline, fatal service loss | B09-O01/O03/O08 service lifecycle | B06 |
| VFS console requester and descriptor policy | B09-O06/O07 request producer/consumer | B11 |
| PM/init/shell terminal ownership and lifecycle | B09 staged session/job-control behavior | B08/B12/B13 |
| B09 native/model/QEMU evidence | All B09 operations | B14 |

## Fixed MINIX source evidence

The B09 comparison uses only local source:

- `minix/drivers/tty/tty/tty.c`
- `minix/drivers/tty/tty/tty.h`
- `minix/drivers/tty/tty/arch/earm/rs232.c`
- `minix/lib/libchardriver/chardriver.c`
- `minix/kernel/interrupt.c`
- `minix/kernel/system/do_irqctl.c`
- `minix/kernel/utility.c`
- `minix/include/minix/type.h`
- `minix/kernel/arch/earm/bsp/ti/omap_serial.c`
- `common/lib/libc/arch/arm/string/memcpy.S`
- `common/lib/libc/arch/arm/string/memset.S`

## Logical variables

| Variable | Meaning |
| --- | --- |
| `q` | active device-independent canonical input entries, at most 256 |
| `s` | resident read-staging bytes, at most 256 in the current canonical system |
| `e` | queued echo bytes, at most 256 |
| `w` | one accepted source write, at most 4,096 bytes |
| `br` | receive bytes processed during one `micros_tty_uart_drain()` |
| `bt` | transmit bytes removed during one `micros_tty_uart_drain()` |
| `i` | IIR causes inspected by one drain, bounded by 256 |
| `n` | configured bootstrap services/bindings |
| `Vtty` | complete terminal-state validation |
| `Vsvc` | complete TTY-service validation |
| `Vth` | complete kernel TTY-handoff runtime validation |
| `Vboot` | B06 complete bootstrap runtime validation |
| `Vep` | B04 complete endpoint/object/IPC validation |
| `Vreg` | B04 complete privilege-profile validation |
| `Vgrant` | B05 complete grant validation/copy work |
| `Rmem` | B02/B07 complete address-space translation/TTY-leaf validation |

## Complete validation cost definitions

The terminal validator:

1. checks all scalar bounds;
2. scans all `q` active input entries and recomputes line counts;
3. compares all `s` staged bytes against the input ring; and
4. validates resident write and transmit-enable relationships.

```text
Vtty = O(q + s)
Vsvc = O(Vtty)
```

The target handoff validator:

1. validates handoff identity and phase;
2. linearly finds the TTY bootstrap binding and runtime transition;
3. resolves the live process, thread, and endpoint through object/endpoint
   authority; and
4. validates the exact TTY privilege profile.

```text
Vth = O(n + Vep + Vreg)
```

The fixed capacities remain logical input variables under ADR-0080. Their
small current values do not make a capacity-wide scan an O(1) algorithm.

## Operation ledger

### B09-O01 Early console, exact mapping, and normal-owner handoff

MINIX may use a direct polled early/debug serial path, maps a driver-authorized
UART range, and normally routes later output through TTY. It does not have the
same irreversible boot-memory handoff or RISC-V PLIC state machine.

`micros` uses an explicit:

```text
EARLY
-> MAP_REQUESTED
-> MAPPED
-> STARTING
-> OWNED
```

console transition, one exact UART leaf, one fixed TTY generation, a held
service release, and terminal panic seizure. These are required platform and
failure-atomicity adaptations. Exact mapping authority remains owned by B02/
B07; the TTY runtime repeatedly revalidates it as described in B09-F002.

The absence of normal post-handoff kernel diagnostic forwarding is already
the blocking B01-F002 diagnostic-buffer finding. B09 adds no duplicate
finding for that same missing source stream.

### B09-O02 PLIC claim, kernel notification, and explicit completion

MINIX kernel IRQ state marks selected hooks active, notifies the driver, and
reenables the exact hook only when the driver invokes `sys_irqenable()`.
Shared hooks and dynamic policy are broader than the current fixed target.

`micros` claims exact PLIC source 10, records route phase `IN_SERVICE`, sends
one coalesced kernel notification, and accepts operation-14 completion only
from the exact TTY process/thread/profile while the same source is retained.
The fixed source and PLIC completion primitive are required adaptations;
dynamic/shared routing remains SOM-03.

One complete hardware-notification round trip executes at least:

```text
kernel dispatch:
    Vboot + 2*Vth + Rmem + Vep + PLIC checks

TTY IRQ-complete syscall:
    Vboot + 2*Vth + Rmem + PLIC checks

combined lower bound:
    2*Vboot + 4*Vth + 2*Rmem + Vep
```

This whole-system cadence is B09-F002.

### B09-O03 Low-level receive buffering and IRQ service boundary

MINIX ARM serial state has a 40,960-byte input ring with `ihead`, `itail`, and
`icount`, plus high/low watermarks. One IRQ callback reads one IIR snapshot,
drains available UART receive bytes into that raw ring, marks a TTY event, and
reenables the IRQ. The next TTY event pass transfers raw-ring spans through
`in_process()` into the separate 256-entry line queue.

`micros` has no raw receive ring. `micros_tty_uart_drain()` repeatedly reads
IIR until no cause remains or 256 causes have been inspected. Every receive
byte without an `LSR` error is immediately canonicalized, edited, and echoed
into `struct micros_tty_state`. A byte observed with the configured line-
status error mask is read, counted, and discarded. Pinned MINIX records an RX
overrun but still appends each byte read from `RHR` to its raw input ring. If
a newline completes a pending read, the same claimed-source interval also
stages the line, enters checked grant-copy authority, publishes the
completion, and notifies VFS before operation-14 completes the PLIC claim.

| Difference | Classification |
| --- | --- |
| Fixed NS16550A register tuple and 16-byte hardware FIFO | Required platform adaptation |
| No software raw receive ring or watermarks | Divergence requiring correction |
| Repeated-IIR full drain instead of one MINIX IIR snapshot per callback | Faster different algorithm; post-baseline optimization |
| PLIC claim retained across line discipline, grant copy, and VFS notification | Divergence requiring correction |
| Discard of an errored byte that pinned MINIX still admits | Divergence requiring correction |

See B09-F003.

### B09-O04 Canonical input, erase, echo generation, and read transfer

Both systems keep a 256-entry device-independent flagged input ring, CR-to-LF
canonical conversion, end-of-line counts, constant-time tail erase, bounded
no-overwrite admission, and line-bounded grant delivery.

MINIX accepts an ordinary character whenever `tty_incount < 256`. Reaching
256 triggers an attempted transfer; if no completed line exists, later
characters, including newline, are discarded until editing or another state
change frees space.

`micros` instead drops an ordinary byte once 255 entries exist, reserving the
last slot for newline. This prevents a full unterminated line but changes the
accepted 256-byte sequence and is explicitly locked by ADR-0047.

```text
MINIX line processing: O(input bytes)
MINIX line transfer:   O(selected line bytes)
micros receive path:   O(input bytes * Vtty)
micros line transfer:  O(Vtty + selected line bytes)
```

The reserve-slot behavior is B09-F005. EOF, kill, configurable erase/EOL,
raw/noncanonical mode, `VMIN`/`VTIME`, and flow control remain owned by
SOM-13; terminal-generated signal behavior is owned by SOM-07/SOM-09.
SOM-10 supplies the timer substrate required by SOM-13's `VMIN`/`VTIME`
semantics rather than owning those terminal semantics.

### B09-O05 Output acceptance, echo order, and physical transmission

MINIX ARM serial state has one 40,960-byte circular output ring with `ohead`,
`otail`, and `ocount`. A retained write is copied from its grant into
available ring spans; the driver replies after every source byte has entered
resident storage. If space is exhausted, the pending caller/ID/grant/remnant
stays active. Refill is retried through later TTY events: the pinned source
always raises an event when the output ring becomes empty, while its nominal
low-water branch compares `icount` rather than `ocount` and therefore does
not implement the comment's intended output-count trigger. Echo appends at
the same ring head, so already queued output remains before later echo.

`micros` instead has:

- one 4,096-byte linear resident write array and cursor;
- one separate 256-byte echo ring;
- no pending-write record;
- a completed-write slot that must be collected;
- `BUSY` plus one writable notification while physical bytes remain; and
- echo selection before older source bytes that have not yet been selected,
  except that a pending LF from an already selected newline is emitted first.

An accepted but not yet transmitted write therefore blocks later writes, and
new echo overtakes older accepted source bytes without splitting a CR/LF pair.
`micros` admits each erase or newline echo sequence atomically or drops it as
a unit. MINIX newline expansion also requires two output slots and is atomic;
its three-character erase echo is appended through three independent shared-
ring operations and may be partial at capacity.

MINIX `write_chars()` emits one byte per invocation. One `micros` transmit-
empty cause instead fills up to all 16 NS16550A FIFO slots. The hardware FIFO
is a required platform adaptation; filling all slots per cause is a faster
different algorithm and remains a post-baseline optimization.

All queue operations remain bounded and constant-time at their heads/tails,
but the data structure, output order, backpressure, cancellation boundary,
refill trigger, transmit batching, and attainable overlap differ. The pinned
`icount` low-water condition is reference behavior; correcting the apparent
defect to `ocount` requires an explicit accepted baseline exception rather
than silent repair. See B09-F004.

### B09-O06 Request identity, submit, cancel, collect, and reply acceptance

MINIX stores independent pending read and write caller/ID/grant/count state.
The character-driver ID is an opaque exact-match identity. Cancellation
matches caller plus ID against the pending read, write, or ioctl; no global
ordering relationship between IDs is required.

`micros` has one fixed VFS caller, one pending read, separate read/write
completion slots, and reply-token-bound collection. The asynchronous
submit/collect split and reply-acceptance release are required adaptations to
the one-thread service protocol.

The service additionally requires every accepted request ID to be nonzero and
strictly greater than one global `last_accepted_request_id`, coupling reads
and writes and permanently rejecting lower/reused opaque identities.
Cancellation only selects the pending read because no pending-write state
exists. The write-cancellation consequence belongs to B09-F004; the global
ID-order algorithm is B09-F006.

### B09-O07 Grant-backed read/write failure atomicity

MINIX copies line bytes through a 64-byte temporary buffer and checked grants
while consuming its input queue. Serial output copies grant spans directly
into the hardware output ring. Ordinary copy failures use the legacy driver
error behavior.

`micros` first copies one selected line into resident staging, attempts one
checked grant copy, and removes input only after success. A rejected grant
therefore publishes an explicit completion without losing line data. Writes
probe the grant range, copy the complete request into resident storage, and
publish completion only after acceptance.

The stronger read failure atomicity, exact generation grants, and dense
request validation are compatible safety extensions. Complete grant
validation cost remains owned by B05. The redundant write self-copy and
general byte-loop primitives are B09-F007.

### B09-O08 UART initialization, interrupt-enable shadow, and failure

MINIX serial initialization obtains device/IRQ authority, configures the
platform UART, initializes raw rings and flow-control state, and explicitly
reenables the IRQ. Driver failure participates in the broader service
framework.

`micros` configures the fixed NS16550A as 8N1 with divisor 2, enables and
clears FIFOs, tracks an IER shadow, and treats malformed device state,
unexpected source/cause, failed notification/completion, or service loss as
fatal. Panic seizure reinitializes raw UART ownership terminally.

The target register sequence, exact MMIO page, fixed source, and fatal
pre-recovery policy are required/staged adaptations. Dynamic driver lifecycle
and shared routing remain SOM-01/SOM-03. The ordinary-path repeated validation
is not required by those adaptations.

### B09-O09 Complete terminal/service validation cadence

Every public terminal mutation first invokes `Vtty`. UART validation invokes
`Vtty`; `micros_tty_uart_apply_effects()` invokes it directly and again
through its return-time UART validation. Service validation invokes it again.

For one ordinary receive or transmit byte:

```text
core byte operation:      1*Vtty
UART effect application:  2*Vtty
total per byte:           3*Vtty
```

For one IRQ with no completed read, the UART/service orchestration adds at
least five more complete validations. A successful read completion raises
that fixed count to at least eight:

```text
no completed read:  (5 + 3*(br + bt))*Vtty
completed read:     (8 + 3*(br + bt))*Vtty
```

These counts exclude grant, IPC, kernel handoff, and mapping validation.
MINIX raw-ring and line-discipline paths use local counts, flags, and selected
request fields; they do not rescan the complete canonical queue before every
byte. See B09-F001.

### B09-O10 TTY copy and clear algorithms

MINIX uses architecture/generic `memcpy`/`memset` block algorithms for driver
records, raw-ring spans, messages, and temporary storage.

`micros` kernel and service TTY helpers clear and copy byte by byte. In the
write path, the checked grant copy already writes directly into
`state->terminal.write`; `micros_tty_write_accept()` then iterates over the
same source and destination array and performs a full no-op self-copy before
publishing the write.

Wire-format byte codecs and per-character line processing are semantic byte
operations. General state/message movement and the duplicate write pass are
not. See B09-F007.

### B09-O11 Deferred MINIX/POSIX terminal breadth

Current TTY lacks:

- service restart/reconstruction and dynamic device publication;
- dynamic/shared IRQ routes;
- process groups, sessions, controlling terminals, and foreground policy;
- signals and terminal-generated signal delivery;
- raw/noncanonical modes, VMIN/VTIME, configurable termios, and flow control;
- multiple consoles/serial lines, pseudo terminals, modem state, and login
  behavior;
- select/poll and complete descriptor readiness;
- credentials/permission policy and public POSIX terminal/libc interfaces.

These map to SOM-01, SOM-03, SOM-07, SOM-09, SOM-11, SOM-12, SOM-13, and
SOM-17. SOM-10 is a timer-substrate predecessor for the SOM-13
`VMIN`/`VTIME` behavior, not a second owner. No new staged-omission row is
required by B09.

## Difference summary

| Area | Result |
| --- | --- |
| Early console/MMIO/PLIC ownership | Required RISC-V adaptation with exact authority |
| Complete terminal/service validation cadence | Blocking divergence |
| Kernel IRQ/control validation cadence | Blocking divergence |
| Low-level raw receive ring and IRQ service boundary | Blocking divergence |
| Output ring, FIFO order, pending write, and backpressure | Blocking divergence |
| Canonical full-queue admission | Blocking divergence |
| Request-ID ordering | Blocking divergence |
| Read grant failure atomicity | Compatible safety extension |
| Copy/clear and duplicate write movement | Blocking divergence |
| Full terminal/POSIX breadth | Staged substitution |

## Time, stack, and auxiliary-space envelope

Persistent B09-owned target state includes:

```text
TTY terminal state:              9,016 bytes
complete TTY service state:      9,136 bytes
UART state:                         16 bytes
kernel handoff runtime state:       72 bytes
early-console ownership state:      16 bytes
```

The terminal state contains:

```text
canonical input entries:           512 bytes
echo ring:                          256 bytes
resident read staging:           4,096 bytes
resident write storage:          4,096 bytes
```

Only 256 read-staging bytes are reachable while the canonical input capacity
is 256; the larger array anticipates staged terminal breadth but is not a
MINIX raw receive ring.

Representative automatic storage is:

```text
service reply action:               112 bytes
IPC message:                         64 bytes
decoded TTY request:                 48 bytes
kernel control request:              40 bytes
handoff transition candidate:        32 bytes
PLIC enable plan:                     8 bytes
```

The TTY service uses one 4 KiB user stack. Its ordinary call/IRQ paths nest
small request/effect structures around blocking IPC/grant wrappers. Kernel
interrupt/control paths nest their small TTY candidates with B02-B07 complete
validators on the 16 KiB supervisor stack. Neither complete envelope has
direct target stack-usage evidence.

The fixed MINIX ARM serial baseline additionally owns one 40,960-byte input
ring and one 40,960-byte output ring per serial line. B09-F003/F004 require
the later correction design to preserve the ring algorithms and to justify
any target-specific capacity adaptation explicitly.

## Staged omissions

| ID | B09 relation |
| --- | --- |
| SOM-01 | TTY/driver restart, reconstruction, publication, and dependency policy |
| SOM-03 | Dynamic/shared IRQ routes and driver registration |
| SOM-07 | Sessions, process groups, controlling terminal, and foreground/background policy |
| SOM-09 | Terminal-generated and asynchronous signals |
| SOM-10 | Timer substrate required by SOM-13 noncanonical `VMIN`/`VTIME`; not the terminal-semantics owner |
| SOM-11 | Credentials and terminal permission transitions |
| SOM-12 | Complete descriptor readiness, select/poll, and related file behavior |
| SOM-13 | Complete termios including noncanonical `VMIN`/`VTIME`, PTY, multi-terminal, modem, login, and terminal behavior |
| SOM-17 | Public POSIX terminal headers, libc calls, errno, and ioctl surface |

## Evidence gaps

| ID | Missing evidence | Owner |
| --- | --- | --- |
| B09-E001 | No counter records `Vtty` calls or scanned input/staging entries per received/transmitted byte and request | B14 |
| B09-E002 | No IRQ round-trip trace counts `Vboot`, `Vth`, `Rmem`, `Vep`, PLIC checks, and operation-14 repetition | B14 |
| B09-E003 | No structural probe distinguishes a raw UART receive ring from direct canonical admission or measures retained-claim work | B14 |
| B09-E004 | No ordering probe queues VFS output before later echo and compares shared-FIFO MINIX order with echo priority | B14 |
| B09-E005 | No backpressure/cancel probe distinguishes retained refillable pending writes from `BUSY` plus physical-drain retry | B14 |
| B09-E006 | No 255/256-byte unterminated-line probe compares MINIX full admission with the reserved-newline slot | B14 |
| B09-E007 | No request-ID probe demonstrates opaque lower/reused IDs versus global monotonic rejection | B14 |
| B09-E008 | No transfer-work counter exposes duplicate write self-copy and byte-at-a-time general clear/copy paths | B14 |
| B09-E009 | No direct 4 KiB TTY user-stack or 16 KiB supervisor-stack evidence covers worst IRQ/grant/control nesting | B14 |
| B09-E010 | No injected UART line-error probe compares `micros` discard with pinned MINIX byte admission and error accounting | B14 |

## Findings register

### B09-F001: Complete terminal validation is multiplied per byte and request

- Severity: High
- `micros` evidence:
  - `micros_tty_state_validate`
  - every terminal read/write/receive/output operation
  - `micros_tty_uart_validate`
  - `micros_tty_uart_apply_effects`
  - `micros_tty_service_validate`
  - `handle_irq`
- MINIX evidence: raw-ring counts/head/tail, selected TTY fields, and
  path-local request checks in `rs232.c` and `tty.c`
- Difference: ordinary mutations rescan the complete canonical queue and
  staged prefix. Nested UART helpers execute three complete validations per
  receive/transmit byte, plus five to eight fixed complete validations per
  IRQ.
- Impact: device work becomes
  `O((br + bt) * (q + s))` instead of linear raw/line/output processing.
  A full canonical queue affects transmit-only work even though output does
  not mutate that queue.
- Smallest correction boundary: retain complete terminal/service validation
  as an explicit diagnostic/startup boundary; use one boundary preflight and
  path-local head/tail/count/request/IER checks with prevalidated internal
  byte/batch operations.
- Dependencies: explicit ADR-0047 supersession and corresponding TTY
  architecture-document updates, plus B14 validator and operation-count
  evidence.

### B09-F002: Every IRQ dispatch and completion validates whole-system TTY state

- Severity: High
- `micros` evidence:
  - `micros_tty_interrupt_dispatch`
  - `common_invariants_hold`
  - `micros_tty_control_handle_captured_user_ecall`
  - `micros_tty_handoff_runtime_validate`
- MINIX evidence: selected IRQ hook/list state in `irq_handle`, one hardware
  notification, exact driver hook reenable in `do_irqctl`
- Difference: dispatch and operation-14 completion each validate complete
  bootstrap, live object/endpoint/profile binding, exact address-space UART
  mapping, PLIC state, and then repeat handoff agreement after mutation.
- Impact: one hardware interrupt plus its completion pays at least
  `2*Vboot + 4*Vth + 2*Rmem + Vep`, before user UART work and nested grant/
  notification validation.
- Smallest correction boundary: retain full cross-owner agreement as an
  explicit diagnostic gate; dispatch/complete consume one exact prevalidated
  route/owner token and check only current source, route phase, endpoint
  generation, and PLIC transition invariants.
- Dependencies: explicit ADR-0047 supersession and corresponding TTY
  architecture-document updates, B02-F002, B03-F001, B04-F001/F002,
  B06-F002, B07-F001, and B14 IRQ round-trip counts.

### B09-F003: The raw receive ring is absent and the PLIC claim spans higher-level work

- Severity: High
- `micros` evidence:
  - `struct micros_tty_uart_state`
  - `receive_available`
  - `handle_line_status`
  - `micros_tty_uart_drain`
  - `handle_irq`
- MINIX evidence:
  - `rs232_t.ibuf`, `ihead`, `itail`, `icount`, and watermarks
  - `rs232_handler`, `read_chars`, `rs_interrupt`, `rs_read`
- Difference: MINIX IRQ work drains hardware into a raw circular ring,
  reenables the exact IRQ, and performs line discipline/grant transfer in a
  later event pass. `micros` has no raw ring, repeatedly drains all IIR causes,
  canonicalizes each byte immediately, may complete a checked grant copy and
  VFS notification, and only then completes the retained PLIC claim. It also
  discards a byte when the `LSR` error mask is set; pinned MINIX increments
  overrun accounting but still appends the byte read from `RHR`.
- Impact: burst capacity and flow-control structure are lost, while IRQ
  retention includes queue scans, echo work, grant validation/copy, IPC
  notification, and service validation. The repeated-IIR loop also changes
  MINIX scheduling/fairness before baseline performance is measured, and
  line-error input/drop behavior is observably different.
- Smallest correction boundary: reproduce a bounded raw receive ring with
  head/tail/count and watermark policy; one device callback transfers UART
  bytes and releases the claim before a separate event pass runs line
  discipline and client completion. Preserve pinned MINIX admission of the
  byte read under line-error/overrun reporting unless an explicit baseline
  exception is accepted. Any all-cause batching follows baseline only as a
  measured optimization.
- Dependencies: explicit ADR-0012/ADR-0047 supersession and corresponding
  architecture-document updates, B09-F001/F002, and B14 raw-ring/claim-
  duration plus line-error-injection evidence.

### B09-F004: Output storage changes FIFO order, backpressure, and cancellation

- Severity: High
- `micros` evidence:
  - separate `echo[]` and `write[]`
  - echo-first `micros_tty_output_take`
  - no pending-write member in `struct micros_tty_service_state`
  - `write_completion`, `write_retry_armed`, and `BUSY` handling
- MINIX evidence:
  - `rs232_t.obuf`, `ohead`, `otail`, and `ocount`
  - retained `tty_outcaller/id/grant/outleft/outcum`
  - `rs_write`, `rs_echo`, `write_chars`, and exact write cancellation
- Difference: MINIX appends caller output and later echo to one physical FIFO
  and retains/refills a pending write as ring space changes. `micros` uses one
  linear request buffer as the complete physical source, gives a separate
  echo ring priority over older source bytes except a pending LF, blocks later
  acceptance until completion collection and physical drain, fills up to 16
  hardware FIFO slots per transmit cause, and has no cancellable pending
  write. The pinned MINIX source emits one byte per `write_chars()` call and
  tests `icount`, not `ocount`, in its nominal output-low-water branch.
- Impact: observable byte order differs; accepted output cannot overlap later
  acceptance despite available hardware-queue capacity; backpressure and
  cancellation semantics no longer reproduce the baseline; and transmit/refill
  scheduling differs before either optimization is measured.
- Smallest correction boundary: restore one bounded physical output ring,
  append all physical write/echo bytes in FIFO order, retain independent
  pending-write caller/ID/grant/remnant state, and preserve completion-after-
  resident-acceptance. Reproduce the pinned empty-ring event and one-byte
  transmit callback first; classify 16-byte FIFO batching afterward. The
  corrective design must either reproduce the pinned `icount` nominal-low-
  water condition or explicitly authorize the `ocount` correction as a
  baseline exception.
- Dependencies: explicit ADR-0047 supersession and corresponding architecture-
  document updates, plus B14 output-order, refill/backpressure, batching, and
  cancellation evidence.

### B09-F005: Canonical admission reserves a newline slot that MINIX does not

- Severity: Medium
- `micros` evidence: ordinary-byte capacity check in
  `micros_tty_receive_byte`
- MINIX evidence: full-queue check and post-insert `in_transfer()` in
  `in_process`
- Difference: `micros` drops the 256th ordinary byte to preserve one newline
  slot; MINIX accepts that byte when count is 255 and only rejects later input
  once the queue is full.
- Impact: exact input/drop/echo behavior differs at the capacity boundary.
  The current policy is a safety-oriented alternative, not baseline parity.
- Smallest correction boundary: a corrective design explicitly supersedes
  ADR-0047's reserve-slot clause and restores MINIX full-queue admission;
  preventing unterminated-line saturation may be reconsidered only after
  baseline evidence.
- Dependencies: ADR-0047 supersession and B14 255/256-byte queue evidence.

### B09-F006: Accepted request IDs are globally ordered instead of opaque

- Severity: Medium
- `micros` evidence:
  - `last_accepted_request_id`
  - `tty_service_validate_submit`
  - service validation relationships
- MINIX evidence: independent `tty_inid`, `tty_outid`, and exact
  caller-plus-ID cancellation/reply matching
- Difference: one global high-water mark requires strict increase across
  reads and writes and forbids lower or reused IDs after prior completion.
  MINIX uses the ID only as opaque identity for the currently retained
  operation.
- Impact: otherwise valid request sequences are rejected, read and write
  identity domains are coupled, and protocol clients inherit ordering policy
  absent from the baseline.
- Smallest correction boundary: keep independent exact active/completion
  identities for read and write, reject only conflicts/stale active slots,
  and treat the nonzero wire ID as opaque rather than globally ordered.
- Dependencies: explicit ADR-0047 supersession and corresponding system-
  overview/protocol updates, B09-F004, and B14 opaque-ID/reuse evidence.

### B09-F007: TTY state movement is byte-serial and accepted writes self-copy

- Severity: Medium
- `micros` evidence:
  - terminal, service, control, and handoff byte clear/copy helpers
  - grant destination in `tty_service_handle_submit_write`
  - copy loop in `micros_tty_write_accept`
- MINIX evidence: architecture/generic `memcpy`/`memset` and direct grant-to-
  output-ring span copies
- Difference: messages, actions, terminal/service state, control requests, and
  handoff candidates use source-level byte loops. Every accepted write is
  copied from the grant into `terminal.write`, then iterated again as a
  same-array source/destination self-copy.
- Impact: a `w`-byte write pays one avoidable additional `O(w)` byte pass, and
  repeated service/kernel initialization and request movement retain the
  project-wide block-copy constant-factor regression.
- Smallest correction boundary: use independently written standard-defined
  RV64 word/block copy and fill primitives with alignment handling and byte
  tails; let the corrected output-ring acceptance path copy each source span
  once. Keep wire codecs and genuine character processing byte-oriented.
- Dependencies: B05-F006, B06-F004, B07-F005, B08-F006, B09-F004, and B14
  transfer-work evidence.

## Unit conclusion

B09 primary-path and operation coverage is complete for the pinned tree. The
audit adds B09-F001 through B09-F007 as blocking findings, confirms B01-F002,
B02-F002, B03-F001, B04-F001/F002, B05-F006, B06-F002/F004, B07-F001/F005,
and B08-F006, maps deferred TTY breadth to existing SOM-01, SOM-03, SOM-07,
SOM-09, SOM-11, SOM-12, SOM-13, and SOM-17 rows, and records SOM-10 as
SOM-13's timer-substrate predecessor rather than a second owner.

B09 is **audited but not certified**. The earlier TTY study's
"compatible bounded substitution" for low-level rings and its broad
"drain every condition" abstraction are insufficient under ADR-0080: the
exact MINIX raw-ring, IRQ-release, shared-output-FIFO, request, and capacity
algorithms expose blocking differences. No B09 correction may start during
the read-only audit.

## References

- [ADR-0012](../adr/0012-console-and-irq-handoff.md)
- [ADR-0047](../adr/0047-tty-console-handoff-and-serial-protocol.md)
- [ADR-0070](../adr/0070-tty-function-and-evidence-decomposition.md)
- [ADR-0071](../adr/0071-tty-model-validation-reference-dependency.md)
- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)
- [B01 platform audit](minix-complete-baseline-b01-platform.md)
- [B05 grant/user-execution audit](minix-complete-baseline-b05-grants-user-execution.md)
- [B06 static-bootstrap audit](minix-complete-baseline-b06-static-bootstrap.md)
- [B07 VM audit](minix-complete-baseline-b07-vm.md)
- [MINIX TTY/console/IRQ study](minix-tty-console-and-irq.md)
- [MINIX TTY source-organization study](minix-tty-source-organization.md)
- [TTY source organization](../architecture/tty-source-organization.md)

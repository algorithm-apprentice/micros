# MINIX TTY, Console, and IRQ Baseline

## Purpose

This study fixes the MINIX behavioral baseline for development-DAG Step 11:
one user-space serial TTY, its character-device request state, canonical input,
buffered output, MMIO authority, interrupt notification, and explicit
controller acknowledgment.

It is the ADR-0025 classification ledger for the initial `micros` TTY
outcome. The Step 11 implementation remains deliberately smaller than the
complete MINIX terminal subsystem: one fixed QEMU `virt` NS16550A console,
one fixed VFS peer, one canonical line discipline, and no termios
reconfiguration, job control, signals, multiple terminals, modem policy, or
service recovery.

This study records behavior, authority, ordering, and failure evidence. It
does not authorize copying MINIX source, structures, identifiers, or wire
formats.

## Reference

- Repository: local MINIX 3 source tree
- Commit: `4db99f4012570a577414fe2a43697b2f239b699e`
- Commit date: 2018-11-14
- Relevant areas:
  - `minix/drivers/tty/tty/tty.c`
  - `minix/drivers/tty/tty/tty.h`
  - `minix/drivers/tty/tty/arch/earm/rs232.c`
  - `minix/lib/libchardriver/chardriver.c`
  - `minix/kernel/utility.c`
  - `minix/kernel/arch/earm/bsp/ti/omap_serial.c`

The local checkout was verified at the exact fixed reference commit before
this study was written.

## Fixed baseline

### Device-independent policy is separated from UART mechanism

MINIX keeps terminal policy in the device-independent TTY layer and hardware
mechanism in the serial driver. The TTY layer:

- receives character-driver requests and hardware notifications;
- retains pending reads, writes, and ioctls;
- performs canonical input processing and local echo;
- transfers completed input through grants; and
- asks device hooks to drain input or continue output
  (`minix/drivers/tty/tty/tty.c:1-19,138-222`,
  `minix/drivers/tty/tty/tty.h:25-105`).

The ARM serial driver separately owns MMIO registers, low-level input and
output rings, UART configuration, interrupt draining, and IRQ reenable
(`minix/drivers/tty/tty/arch/earm/rs232.c:23-38,66-114,203-311,503-603`).

The reusable boundary is that the kernel routes interrupts and validates
authority, the user TTY owns terminal policy, and hardware-specific register
access is isolated from the protocol and line-discipline state machines.

### Character-driver requests are grant-backed and request-identified

MINIX's character-driver protocol carries:

- a request type;
- a device minor;
- a grant;
- a byte count;
- flags;
- one request identifier; and
- a position where applicable
  (`minix/lib/libchardriver/chardriver.c:1-40`).

The common driver layer dispatches `READ`, `WRITE`, `IOCTL`, `CANCEL`, and
other requests and permits a read or write to remain suspended without an
immediate reply. A later task reply carries the original request identifier
(`minix/lib/libchardriver/chardriver.c:126-150,499-527`).

MINIX wire layouts and minor-device breadth are not `micros` ABI. The baseline
behavior is:

- terminal bytes cross the service boundary through checked grants;
- a retained operation has one caller and one nonzero request identity;
- acceptance, deferred completion, cancellation, and reply are explicit; and
- payload pointers never substitute for kernel-written endpoint identity.

### Read and write state are independent and bounded

The MINIX TTY record has separate pending input and output caller, request ID,
grant, remaining-count, and completed-count fields
(`minix/drivers/tty/tty/tty.h:55-76`).

`do_read()` rejects a second read, records the exact caller, grant, count, and
ID, consumes already completed input if possible, and otherwise retains the
request. `do_write()` does the corresponding work for output
(`minix/drivers/tty/tty/tty.c:469-579`).

This permits one read and one write to make progress independently. It does
not require an unbounded request table or one worker thread per client.

### Cancellation matches the exact caller and request ID

MINIX cancellation searches the pending read, write, or ioctl state using
both the caller endpoint and request ID. Only a matching operation is
finished and cleared; a stale or duplicate cancellation does not select
another request (`minix/drivers/tty/tty/tty.c:778-807`).

The reusable baseline is exact cancellation identity and complete release of
driver-owned request state. `micros` VFS remains the only TTY client and owns
the application-side cancellation decision.

### Canonical input is queued independently from hardware input

The device-independent MINIX input queue contains 256 flagged character
entries. It tracks the queue head, tail, byte count, and completed-line count
separately from the low-level UART ring
(`minix/drivers/tty/tty/tty.h:15-16,25-43,101-105`).

`in_process()` applies input conversion, canonical erase and kill behavior,
EOF and line-delimiter marking, flow control, signals, echo, and bounded queue
admission. In canonical mode a full queue discards additional characters
rather than overwriting retained input
(`minix/drivers/tty/tty/tty.c:1010-1165`).

`in_transfer()` does not read past a canonical line boundary. It copies
through a temporary resident buffer and a checked grant, removes input only
as it is delivered, and replies when the requested count or line condition
is satisfied (`minix/drivers/tty/tty/tty.c:949-1006`).

The Step 11 boundary preserves:

- a separate bounded canonical queue;
- CR-to-LF input conversion;
- newline-delimited completion;
- backspace/delete editing;
- local echo;
- no overwrite of unread bytes; and
- no read past the first completed line.

Full termios configuration, raw mode, timers, signals, flow control, kill,
reprint, escaped characters, and configurable EOF/EOL characters are later
terminal-policy work.

### Output completes after bounded driver acceptance

The MINIX serial driver copies output from the caller's grant into its own
resident ring, applies output processing, starts hardware transmission, and
replies once all request bytes have been accepted into driver-owned storage
(`minix/drivers/tty/tty/arch/earm/rs232.c:203-281`).

Physical transmission may continue after that reply. The low-level ring and
transmit interrupt state remain owned by the driver. Echo uses the same
hardware queue and drops echo when no queue space exists rather than
overwriting pending output
(`minix/drivers/tty/tty/arch/earm/rs232.c:292-311,795-823`).

The reusable baseline is that write completion means bounded resident
acceptance, not necessarily an empty UART shift register. A separate drain
operation would need an explicit later protocol.

### Hardware notifications are invitations to inspect device state

The MINIX TTY handles a hardware notification by inspecting the serial lines,
then repeatedly running input, output, and deferred-request processing until
no TTY event remains. It does not treat notification count or ordering as a
complete device-state description
(`minix/drivers/tty/tty/tty.c:155-190,901-945`).

The serial handler reads UART interrupt and line-status registers, drains all
available receive bytes, advances transmit work, and records device events
for the higher TTY layer
(`minix/drivers/tty/tty/arch/earm/rs232.c:730-836`).

This matches the existing `micros` kernel-notification substrate: one
coalesced bit wakes the exact owner, and the owner must inspect the UART until
the device reports no pending condition.

### Explicit IRQ acknowledgment follows device draining

The ARM serial driver installs its IRQ policy without automatic reenable.
When a hardware notification arrives, it runs the UART handler first and
calls `sys_irqenable()` only afterward
(`minix/drivers/tty/tty/arch/earm/rs232.c:543-565,588-603`).

The reusable ordering is:

1. the kernel/controller retains outstanding interrupt state;
2. the exact driver receives or coalesces a notification;
3. the driver drains every currently reported device condition; and
4. the exact driver explicitly acknowledges completion.

MINIX's legacy hook and line-mask implementation is not the RISC-V ABI.
ADR-0012 already selects the QEMU `virt` PLIC adaptation: the kernel claims
source 10, records it in service, and does not complete it until exact TTY
authority invokes `irq_complete`.

### MMIO access follows privilege authorization and VM mapping

The MINIX ARM serial driver first requests a bounded memory privilege for the
UART range, then asks VM to map the physical range, and treats either failure
as fatal driver initialization failure
(`minix/drivers/tty/tty/arch/earm/rs232.c:503-532`).

The baseline authority boundary is:

- a manifest or service manager identifies the bounded device resource;
- VM owns user mapping policy;
- the kernel remains the page-table mechanism and privilege validator; and
- the driver receives only its exact device mapping.

`micros` requires a narrower RISC-V adaptation because ADR-0045 has already
completed an irreversible wired-memory handoff and exposes no generic mapping
protocol. Step 11 therefore needs one exact manifest-bound UART page mapping,
not a general physical-map service.

### Early kernel serial and user TTY ownership do not overlap normally

MINIX can use a direct polled kernel serial path during early initialization.
Its ARM implementation maps a fixed page and writes the UART directly
(`minix/kernel/arch/earm/bsp/ti/omap_serial.c:29-87`).

Ordinary kernel diagnostics are accumulated in a kernel message buffer and
notified to the TTY path. Direct serial output remains a debug or panic mode
rather than a concurrent normal writer
(`minix/kernel/utility.c:55-84`,
`minix/drivers/tty/tty/tty.c:367-450`).

MINIX does not provide the runtime two-phase transfer required by ADR-0012.
The `micros` launcher/kernel/VM/TTY transition is therefore a required
platform adaptation:

- early kernel output is flushed and disabled before user mapping;
- TTY initializes while the PLIC source remains disabled;
- commit establishes one normal owner; and
- panic seizure is terminal and never resumes normal execution.

## Classification ledger

| Topic | MINIX behavior | `micros` classification |
| --- | --- | --- |
| User-space terminal policy | Device-independent TTY owns line discipline and request state | Baseline parity |
| Hardware split | Serial driver owns MMIO, rings, and UART interrupts | Baseline parity with a QEMU NS16550A backend |
| Request transport | Character-driver messages carry grants, counts, flags, and request IDs | Baseline parity through a new fixed `micros` TTY protocol |
| Pending operations | Separate retained read and write state | Baseline parity, bounded to one read and one write for the sole VFS peer |
| Cancellation | Exact endpoint and request-ID match | Baseline parity |
| Read delivery | Grant copy stops at canonical line boundary | Baseline parity |
| Write completion | Reply after bytes enter resident driver storage | Baseline parity |
| Input queue | 256-entry device-independent queue | Baseline parity for the initial capacity |
| Low-level rings | Large platform-specific UART input/output rings | Compatible bounded substitution: NS16550 FIFO draining plus fixed resident TTY buffers |
| Canonical policy | Configurable termios canonical/raw behavior | Staged substitution: fixed canonical CR/LF, erase, echo, and newline policy only |
| EOF, signals, and job control | Full termios and process-group behavior | Staged later work; not claimed by Step 11 |
| Multiple terminals | Console, serial lines, pseudo terminals, and video | Deliberate v0.1 divergence: one serial console only |
| MMIO authority | Privilege check followed by VM physical mapping | Required post-handoff adaptation: one exact manifest UART page map through VM authority |
| IRQ registration | Dynamic IRQ hook and privilege table | Required manifest adaptation: one fixed source 10 route to the exact TTY generation |
| Notification | Coalesced hardware bit invites complete device inspection | Baseline parity through source-`NONE` kernel notification |
| IRQ acknowledgment | Explicit reenable after UART handling | Required PLIC adaptation: explicit claim completion after draining |
| Shared lines | Legacy lines may have several hooks | Deliberate v0.1 divergence: one owner for one PLIC source |
| Early serial | Kernel may poll before user drivers and for debug/panic | Required ADR-0012 adaptation: explicit two-phase normal ownership transfer plus terminal panic seizure |
| Kernel logs after startup | Kernel buffer can notify TTY | Staged later work; v0.1 has no nonfatal post-handoff kernel log transport |
| Driver restart | Character-driver framework tolerates service restart machinery | Staged later RS work; unexpected TTY loss is fatal in Step 11 |

## Derived Step 11 boundary

The smallest complete `micros` outcome is:

1. preserve the fixed launcher, VM, PM, and TTY identities;
2. begin one failure-atomic console transition while TTY remains held;
3. let exact VM authority install one fixed UART page into the exact TTY root
   without frame allocation or page-table growth;
4. notify the launcher only after that PTE is committed and locally fenced;
5. release TTY, initialize one NS16550A console, and commit ownership;
6. route QEMU PLIC source 10 to the exact TTY endpoint;
7. retain each claimed source in service until TTY drains the UART and invokes
   exact completion authority;
8. provide one bounded canonical input queue, local echo, CR/LF processing,
   and interrupt-driven transmit path;
9. expose grant-backed `SUBMIT_READ`, `SUBMIT_WRITE`, `CANCEL`, and `COLLECT`
   calls only to the exact VFS endpoint;
10. signal completed requests with one coalesced endpoint-origin notification;
11. reject malformed, stale, foreign, out-of-phase, or invalid-grant requests
    without partial state mutation; and
12. prove the real handoff, mapping, input, output, notification, and deferred
    PLIC completion under QEMU.

This boundary does not include a general VM mapping API, direct application
TTY access, raw mode, configurable termios, signals, process groups, multiple
devices, select/poll, modem control, kernel log forwarding, RS recovery, or
the later VFS descriptor implementation.

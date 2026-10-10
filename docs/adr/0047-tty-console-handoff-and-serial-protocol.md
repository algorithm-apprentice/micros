# ADR-0047: TTY Console Handoff and Serial Protocol

- Status: Accepted
- Date: 2026-10-09
- Refines: ADR-0003, ADR-0004, ADR-0005, ADR-0006, ADR-0012,
  ADR-0015, ADR-0016, ADR-0018, ADR-0021, ADR-0022, ADR-0025,
  ADR-0029, ADR-0030, ADR-0037, ADR-0038, ADR-0039, ADR-0040,
  ADR-0042, ADR-0043, ADR-0044, ADR-0045, and ADR-0046
- Supersedes in part:
  - ADR-0045's prohibition on every post-handoff mapping mutation. Step 11
    adds one nonrepeatable, manifest-bound UART device PTE for the exact held
    TTY process. It allocates no frame or page table and does not expose the
    later general VM mapping protocol.
  - ADR-0043's initial immutable TTY and VFS profile definitions. TTY gains
    exact reply, VFS-notification, and TTY-control authority; VFS gains only
    the exact call target required to use TTY.
  - ADR-0043's rule that every service readiness deadline starts at release.
    TTY's one manifest-bounded absolute deadline starts atomically at console
    begin and covers mapping, release, initialization, commit, and readiness
    without being extended.

## Context

Development-DAG Steps 9 and 10 are complete:

- VM owns ordinary post-handoff memory policy;
- all existing static user pages are exact `VM_WIRED` managed frames;
- generic allocation, mapping, unmapping, root growth, and page-fault
  delivery remain unavailable;
- PM is a real static service with its own lifecycle authority; and
- the kernel can inject source-`NONE` coalesced events into an exact endpoint.

TTY is the next dependency-ready service. ADR-0012 already requires:

- a two-phase transfer from the kernel's polled early console;
- an exact VM-installed UART mapping while TTY remains held;
- TTY initialization before ownership commit;
- kernel PLIC claim without immediate completion;
- explicit TTY completion after the UART is drained; and
- fatal owner loss before RS recovery exists.

The fixed MINIX baseline is documented in
[the TTY, console, and IRQ study](../research/minix-tty-console-and-irq.md).
MINIX keeps line discipline and retained character requests in a user TTY,
copies terminal bytes through grants, uses bounded device rings, treats a
hardware notification as an invitation to inspect all device state, and
explicitly reenables the serial IRQ after draining it.

The literal MINIX mechanisms do not fit the accepted `micros` platform:

- QEMU `virt` uses one NS16550A at `0x10000000` and PLIC source 10;
- the PLIC claim/complete register, not a legacy line hook, retains the
  interrupt in service;
- ADR-0045 completed VM handoff before TTY starts;
- the TTY root currently contains only managed-RAM user leaves;
- the kernel UART mapping is supervisor-only and cannot be shared with one
  user root by changing its permission; and
- the one-thread-per-process limit makes a long-lived blocking VFS-to-TTY call
  an unsafe foundation for later concurrent descriptor service.

The TTY device page therefore needs one reviewed exception that is narrower
than the later dynamic VM mapping protocol.

## Decision

### Scope

This outcome defines:

- one real statically embedded TTY service ELF;
- one exact post-handoff UART mapping into the held TTY root;
- complete begin, map, release, commit, and ready ordering;
- exact TTY and VFS privilege profiles;
- syscall operation 14 for TTY ownership commit and PLIC completion;
- the QEMU `virt` PLIC S-mode context mapping and source-10 route;
- supervisor-external interrupt dispatch for both supervisor and user
  interruption;
- source-`NONE` UART notification with deferred PLIC completion;
- one fixed NS16550A initialization and interrupt-drain policy;
- one bounded canonical line discipline and interrupt-driven output path;
- one grant-backed VFS-to-TTY request protocol with explicit cancellation and
  collection;
- native state-machine, parser, authority, mapping, and controller evidence;
- one deterministic QEMU scenario with host-injected serial input; and
- fail-closed validation ownership.

It does not implement:

- a general post-handoff VM allocation, map, unmap, root-growth, alias,
  scratch-window, prepared-address-space, or page-fault protocol;
- arbitrary physical mapping requests or a user-visible PLIC mapping;
- more than one UART, terminal, IRQ route, hart, or TTY client;
- direct application access to TTY, UART, PLIC, or IRQ completion;
- configurable termios, raw mode, EOF, signals, job control, process groups,
  sessions, pseudo terminals, modem control, flow control, select/poll, or
  window sizing;
- nonfatal kernel log forwarding after handoff;
- a drain/flush request that waits for the hardware shift register;
- RAMFS, production VFS descriptors, init, shell, or application I/O;
- TTY restart, route reassignment, owner replacement, or RS recovery; or
- dynamic discovery of platform devices from user space.

### Fixed platform and service tuple

The production TTY identity remains:

```text
service ID       = 4
process slot     = 3
profile ID       = 4
profile name     = TTY
direct prerequisite = PM
role             = CONSOLE_OWNER
UART physical base = 0x0000000010000000
UART mapped length = 0x0000000000001000
PLIC source      = 10
```

The QEMU-generated FDT identifies:

```text
compatible       = ns16550a
register span    = [0x10000000, 0x10000100)
clock frequency  = 3686400 Hz
interrupt source = 10
PLIC base        = 0x0c000000
PLIC span        = 0x00600000
```

The manifest continues to authorize one page because Sv39 maps 4 KiB leaves.
TTY accesses only the NS16550A byte registers inside the first `0x100` bytes.

The fixed TTY user mapping is:

```text
MICROS_TTY_UART_VIRTUAL_BASE = 0x000000007fffe000
[0x7fffe000, 0x7ffff000) = user R/W, non-executable UART page
[0x7ffff000, 0x80000000) = existing external TTY stack
```

Every `CONSOLE_OWNER` manifest entry requires:

- exactly one stack page;
- image end at or below `MICROS_TTY_UART_VIRTUAL_BASE`;
- the exact UART physical base, mapped length, and IRQ source above; and
- the exact TTY production tuple in an ordinary production prefix.

The adjacent stack mapping proves that the root-index-1 middle and leaf table
path for `0x7fffe000` already exists before VM handoff. The UART leaf itself
must be absent. Mapping it therefore requires one PTE write and no frame
allocation, page-table allocation, root change, or user-page-limit charge.

The launcher uses the same virtual address for its manifest view in a
different private root. That mapping creates no cross-process alias and grants
TTY no access to launcher memory.

### Immutable privilege profiles

The kernel-operation bit is:

```text
MICROS_KERNEL_OPERATION_TTY_CONTROL = 0x0000000000000008
```

The production TTY profile becomes:

```text
operations =
    RECEIVE | CALL | REPLY | NOTIFY
call targets =
    BOOTSTRAP_LAUNCHER
send targets = 0
notify targets =
    VFS
kernel operations =
    TTY_CONTROL
```

The production VFS profile becomes:

```text
operations =
    RECEIVE | CALL
call targets =
    BOOTSTRAP_LAUNCHER | TTY
send targets = 0
notify targets = 0
kernel operations = 0
```

No application profile gains a TTY target. Applications later call VFS, and
VFS uses its resident bounce buffer for the non-transitive second grant hop.

The otherwise inactive VFS profile is installed in the stable profile table
now so TTY's exact notification target is valid. A dependency-closed test
manifest may run a probe ELF under an exact test VFS profile, but may not
grant TTY, the probe, or another service broader targets or kernel operations.

### Console handoff state and binding

The kernel keeps one fixed TTY binding prepared from the validated manifest:

```text
launcher process/thread/endpoint generation
VM process/thread/endpoint generation
TTY service/process/thread/endpoint/profile generation
TTY private root physical address
UART virtual base, physical base, and mapped length
PLIC source
console phase
route phase
TTY handoff/readiness deadline and armed state
```

The console phases are:

```text
EARLY
MAP_REQUESTED
MAPPED
TTY_STARTING
TTY_OWNED
PANIC
```

The route phases are:

```text
DISABLED
IDLE
IN_SERVICE
PANIC
```

Complete validation cross-checks the binding against the manifest, live
kernel objects, endpoint generations, profiles, scheduler state, TTY root,
console phase, route phase, UART PTE, PLIC registers, and bootstrap service
state. A mismatch is corruption, not an alternative owner.

The transition is one-way. There is no normal operation from `MAPPED`,
`TTY_STARTING`, or `TTY_OWNED` back to `EARLY`, and no operation leaves
`PANIC`.

### Kernel-origin synchronization events

The kernel-event mask adds:

```text
MICROS_KERNEL_EVENT_CONSOLE_MAP_REQUEST = 0x0000000000000002
MICROS_KERNEL_EVENT_CONSOLE_MAPPED      = 0x0000000000000004
MICROS_KERNEL_EVENT_TTY_IRQ             = 0x0000000000000008
```

They use the ADR-0037 canonical envelope:

```text
source      = NONE
type        = MICROS_IPC_TYPE_KERNEL_NOTIFICATION
reply token = 0
payload[0..7] = little-endian event mask
payload[8..47] = 0
```

The map-request event is delivered only to exact VM, the mapped event only to
the launcher, and the UART event only to exact TTY. Each recipient accepts
only its event bits in the applicable phase; a duplicate or unknown bit is an
invariant failure.

### Bootstrap console begin

Operation 11 adds:

```text
MICROS_BOOTSTRAP_COMMAND_CONSOLE_BEGIN = 5
```

Registers are:

```text
a0  CONSOLE_BEGIN
a1  TTY service ID
a2  0
a3  0
a4  0
a5  0
a6  0
a7  MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL
```

The ordinary launcher invokes it immediately before it would release TTY.
Preflight requires:

- exact launcher authority and bootstrap phase `RUNNING`;
- ownership and VM-handoff phases `HANDED_OFF`;
- exact VM and PM services `READY`;
- TTY is the exact next dependency-ready service and remains `PREPARED`;
- console phase `EARLY` and route phase `DISABLED`;
- exact TTY root, one-page stack, absent UART leaf, and pre-existing table
  path;
- a canonical current counter value and overflow-safe addition of TTY's exact
  manifest readiness interval;
- no other readiness or console deadline armed;
- valid early UART state that can be flushed before ownership mutation;
- PLIC source 10 priority and enable disabled, with no source in service; and
- one complete kernel-event injection plan for exact VM.

After full preflight, one non-failing commit:

1. waits for the early UART transmitter to become empty;
2. writes UART `IER = 0`;
3. arms one absolute TTY handoff/readiness deadline using the manifest
   interval;
4. disables ordinary early-console output;
5. records `MAP_REQUESTED`; and
6. injects `MICROS_KERNEL_EVENT_CONSOLE_MAP_REQUEST` to VM.

No fallible action follows the first ownership mutation.

An ordinary kernel console write after step 4 is an ownership violation. It
does not silently disappear or write concurrently; the kernel enters the
terminal panic-seizure path.

Failure after begin and before TTY commit is a fatal boot error. The panic path
is still able to seize UART as defined below.

The deadline is guest-owned and remains active through `MAP_REQUESTED`,
`MAPPED`, `TTY_STARTING`, and `TTY_OWNED` until readiness is accepted. Expiry
identifies exact TTY service and endpoint, records the current console phase,
and enters the existing readiness-timeout bootstrap failure and panic path. A
live VM that ignores the map request therefore cannot wedge bootstrap until
the host timeout.

### One-shot post-handoff UART mapping

Operation 12 retains `MICROS_VM_HANDOFF_READY = 1` and adds:

```text
MICROS_VM_HANDOFF_MAP_TTY_UART = 2
MICROS_TTY_MAPPING_VERSION     = 1
```

The exact scalar summary is:

```text
a0  MAP_TTY_UART
a1  mapping version = 1
a2  TTY service ID = 4
a3  exact TTY endpoint
a4  UART virtual base = 0x000000007fffe000
a5  UART physical base = 0x0000000010000000
a6  PLIC source in bits 63..32, mapped length in bits 31..0
a7  MICROS_SYSCALL_ABI_VM_HANDOFF
```

`a1` through `a3` and both packed fields require canonical widths.

VM accepts the map-request event only after its ordinary handoff and bootstrap
readiness. It validates the exact TTY tuple from its immutable bootstrap
configuration, invokes this command once, and records one separate runtime
device-mapping descriptor after success.

The large ADR-0045 VM boot snapshot remains unchanged:

- UART MMIO is outside managed RAM;
- no frame-state byte represents it;
- no free or wired frame count changes; and
- the initial static mapping digest remains the handoff-time digest.

VM's device descriptor records only the exact tuple and
`UNMAPPED`/`MAPPED` phase. It is policy state, not PTE authority.

The kernel preflight order is:

1. unknown command, width violation, or malformed packed shape:
   `MICROS_SYSCALL_ABI_ARGUMENT`;
2. impossible current-thread resolution: invariant failure;
3. exact VM binding and `VM_HANDOFF` profile authority:
   `MICROS_SYSCALL_ABI_UNAUTHORIZED`;
4. handed-off VM, running launcher, `MAP_REQUESTED` console, disabled route,
   and held TTY phase:
   `MICROS_SYSCALL_ABI_STATE`;
5. complete VM, bootstrap, TTY binding, root, endpoint, scheduler,
   address-space, allocator, ownership, UART, and PLIC validation:
   corruption is fatal;
6. scalar summary mismatch, missing table path, occupied UART leaf, active
   TTY root, or any required managed-mapping inconsistency: nonreturning
   `console-map-gate` bootstrap failure;
7. exact user R/W, non-executable leaf-PTE construction and launcher-event
   injection preflight; an impossible failure is the same gate failure; and
8. one non-failing commit.

The commit:

1. writes the exact UART leaf into the inactive TTY root;
2. performs the required page-table publication ordering and a complete local
   `sfence.vma`;
3. records `MAPPED`; and
4. injects `MICROS_KERNEL_EVENT_CONSOLE_MAPPED` to the launcher.

The source is one hart with ASID zero. TTY activation later performs its
existing complete local invalidation as an independent safety boundary.

The command cannot select a process, root, virtual address, physical address,
length, permissions, or IRQ source. Repetition returns `STATE`. No unmap
operation exists.

### Device-leaf validation without managed-frame authority

The UART PTE is a distinct exact device leaf, not a `VM_WIRED` managed frame.
Complete user-root validation accepts it only when all of these match:

- exact current TTY process generation and private root;
- virtual base `0x7fffe000`;
- physical base `0x10000000`;
- user R/W, non-executable permissions;
- console phase `MAPPED`, `TTY_STARTING`, or `TTY_OWNED`; and
- the sole TTY binding remains valid.

Every other user leaf retains the ADR-0045 requirement for one exact managed
`VM_WIRED` owner. A foreign, duplicate, aliased, executable, differently
permissioned, or differently addressed device leaf is corruption.

Managed-memory APIs do not turn the UART page into transferable data:

- ordinary kernel lookup and byte translation reject the device leaf with
  ownership failure;
- IPC buffers cannot occupy it;
- direct grants cannot cover or copy it;
- PM-control output cannot use it; and
- managed mapping inventory excludes it and continues to enumerate only exact
  managed-RAM leaves.

TTY accesses the mapping through ordinary U-mode hardware translation. A
separate internal exact-device validator and mapper own the exception.

### Mapping completion and TTY release

After `CONSOLE_BEGIN`, the launcher blocks in `receive(ANY)` and requires one
canonical source-`NONE` notification containing exactly
`MICROS_KERNEL_EVENT_CONSOLE_MAPPED`.

Only then does it invoke ordinary `RELEASE(TTY)`. The release gate requires:

- console phase `MAPPED`;
- exact UART PTE and complete TTY root validation;
- route phase `DISABLED`;
- TTY still `PREPARED`;
- no pending or staged inconsistent console event; and
- every existing ADR-0043 release invariant.

The release commit changes console phase to `TTY_STARTING` in the same
serialized transition that installs TTY's exact profile, publishes its
endpoint, admits its prepared thread, and records its readiness deadline.

For TTY, "records its readiness deadline" means retaining the absolute
deadline armed at `CONSOLE_BEGIN`; release neither recomputes nor extends it.
Successful `ACCEPT_READY(TTY)` clears that deadline. Every other service keeps
ADR-0043's release-time deadline rule unchanged.

An early release is `release-transition` failure. A malformed or unexpected
mapping notification is a new launcher failure reason:

```text
MICROS_BOOTSTRAP_FAILURE_CONSOLE_PROTOCOL = 10
```

The kernel diagnostic name is `console-protocol`.

### TTY-control syscall namespace

Operation 14 is:

```text
MICROS_SYSCALL_ABI_TTY_CONTROL = 14
```

The commands are:

```text
MICROS_TTY_CONTROL_COMMIT       = 1
MICROS_TTY_CONTROL_IRQ_COMPLETE = 2
MICROS_TTY_CONTROL_VERSION      = 1
```

The generic ADR-0042 runtime is not expanded. TTY uses one private raw-syscall
wrapper.

#### Console commit

Registers are:

```text
a0  COMMIT
a1  control version = 1
a2  TTY service ID
a3  exact self endpoint
a4  UART virtual base
a5  UART physical base
a6  PLIC source in bits 63..32, mapped length in bits 31..0
a7  MICROS_SYSCALL_ABI_TTY_CONTROL
```

TTY invokes commit only after it has:

- validated its bootstrap configuration and exact VFS peer if present;
- disabled UART interrupts;
- programmed the fixed NS16550A state;
- cleared the FIFOs;
- drained stale receive, line-status, modem-status, and interrupt state until
  `IIR` reports no pending condition; and
- enabled UART receive-data and line-status interrupts while leaving
  transmit-empty interrupts disabled.

Kernel preflight requires exact TTY authority, `TTY_STARTING`, the exact UART
PTE, a disabled and idle PLIC route, and a UART source configuration matching
the manifest. After full validation, one non-failing commit:

1. records console phase `TTY_OWNED` and route phase `IDLE`;
2. programs source-10 priority 1 and supervisor threshold 0;
3. enables source 10 in hart-0 supervisor context;
4. enables `sie.SEIE` without changing `sie.STIE` or global
   `sstatus.SIE`; and
5. returns success.

State is published before external delivery is enabled. No fallible action
follows the first route mutation.

The operation is one-shot. It does not reply to the launcher. TTY sends the
ordinary ADR-0043 ready call afterward, and `ACCEPT_READY(TTY)` requires
`TTY_OWNED` plus the complete route and mapping invariants. It accepts either
an idle route or one valid retained source-10 claim. In the latter case,
readiness clears only the deadline; it preserves `IN_SERVICE`, the claimed
source, and the queued notification so TTY drains and completes that interrupt
immediately after `READY_ACK`.

#### IRQ completion

Registers are:

```text
a0  IRQ_COMPLETE
a1  control version = 1
a2  PLIC source = 10
a3  0
a4  0
a5  0
a6  0
a7  MICROS_SYSCALL_ABI_TTY_CONTROL
```

TTY invokes this command only after draining UART `IIR` until no interrupt is
pending.

The kernel requires:

- exact TTY process, thread, endpoint, profile, and `TTY_CONTROL` authority;
- console phase `TTY_OWNED`;
- route phase `IN_SERVICE`;
- recorded claimed source exactly 10; and
- complete TTY, endpoint, route, PLIC, and scheduler invariants.

The commit writes source 10 to the supervisor claim/complete register and
records route phase `IDLE`. A duplicate, early, wrong-source, or non-owner
completion cannot acknowledge the interrupt.

#### Common failure ordering

Both commands use:

1. unknown command, width violation, or malformed scalar shape:
   `MICROS_SYSCALL_ABI_ARGUMENT`;
2. impossible current-thread resolution: invariant failure;
3. exact TTY profile and binding authority:
   `MICROS_SYSCALL_ABI_UNAUTHORIZED`;
4. complete binding and object validation: corruption is fatal;
5. command-specific phase or in-service mismatch:
   `MICROS_SYSCALL_ABI_STATE`;
6. exact scalar tuple or source validation:
   `MICROS_SYSCALL_ABI_ARGUMENT`; and
7. one non-failing commit.

Every returning failure preserves console, route, PLIC, UART, endpoint,
scheduler, PTE, notification, and output state except `a0` and advanced
`sepc`. TTY treats every returned negative result as fatal and deliberately
traps.

### Kernel PLIC mapping and initialization

The kernel maps only the supervisor-owned pages needed for QEMU `virt`:

```text
priority page          0x000000000c000000
source-10 priority     0x000000000c000028
S-mode enable page     0x000000000c002000
hart-0 S enable word   0x000000000c002080
S-mode context page    0x000000000c201000
threshold              0x000000000c201000
claim/complete         0x000000000c201004
```

They are supervisor-only, R/W, non-executable identity mappings. No user root
receives a PLIC leaf.

Early initialization sets source-10 priority to zero, clears its supervisor
enable bit, sets threshold zero, and records the route disabled. All other
source priorities and enables remain zero in v0.1.

PLIC access is volatile 32-bit MMIO. Register offsets and context selection
are fixed platform constants verified against the QEMU-generated FDT's
hart-0 machine-external then supervisor-external context order.

### Supervisor-external interrupt dispatch

RISC-V supervisor-external cause 9 becomes an asynchronous interrupt path
parallel to the existing timer path.

For a supervisor-origin external interrupt, the kernel dispatches PLIC work
and returns to the interrupted supervisor context.

For a user-origin external interrupt, the kernel:

1. captures the interrupted current thread through the existing scheduler
   trap-entry path;
2. validates the hart and current-thread relationship;
3. dispatches the PLIC source;
4. may wake exact TTY through kernel-event injection;
5. runs the ordinary scheduler selection path; and
6. returns to the selected user thread.

It does not classify an asynchronous external interrupt as a user service
fault. Timer and external enable bits remain independent.

PLIC dispatch:

1. reads the hart-0 supervisor claim register;
2. treats claim zero as a handled spurious/raced observation with no route
   mutation;
3. requires a nonzero claim to be source 10 with console `TTY_OWNED` and route
   `IDLE`;
4. records source 10 and route `IN_SERVICE`;
5. injects or coalesces `MICROS_KERNEL_EVENT_TTY_IRQ` to exact TTY; and
6. returns without writing the completion register.

After claim, an impossible route, endpoint, scheduler, or notification failure
is fatal. The source remains uncompleted and panic seizure disables external
delivery. The kernel never completes source 10 merely because the event was
delivered or coalesced.

If a new UART condition appears between TTY's final `IIR` read and completion,
the level source reasserts after the completion write and produces a later
claim. No condition is inferred from notification count.

### Early console and panic seizure

The UART layer is split into:

- an ownership-aware ordinary early-console path valid only in `EARLY`; and
- one raw polled panic path valid only after seizure.

After `CONSOLE_BEGIN`, no routine kernel path writes UART. Any attempted
ordinary write is a `console-owner-violation` panic, not a silent drop.

Every fatal path first performs idempotent panic seizure:

1. clear global interrupt enable and `sie.SEIE`;
2. disable PLIC source 10 when PLIC initialization is available;
3. set route and console phases `PANIC`;
4. write `LCR = 8N1` so `DLAB` is definitely clear;
5. write `IER = 0`;
6. program the fixed divisor through `LCR.DLAB`, `DLL = 2`, and `DLM = 0`;
7. restore `LCR = 8N1`; and
8. emit diagnostics through the raw UART path.

Panic may occur before begin, during mapping, while TTY starts, while a source
is in service, or after ownership. No normal execution, route completion, or
TTY return follows seizure.

An unexpected TTY trap or endpoint loss after begin emits the exact existing
bootstrap service-fault record while bootstrap is running. After launcher
sealing it additionally emits:

```text
MICROS_TTY_OWNER_FAULT service=0x... process-slot=0x... process-generation=0x... endpoint=0x... console=<phase> route=<phase> source=0x...
```

The kernel then enters ordinary structured panic shutdown. There is no
fallback to normal early-console ownership.

### Fixed NS16550A policy

TTY uses byte-wide registers at offsets:

```text
0  RBR / THR / DLL
1  IER / DLM
2  IIR / FCR
3  LCR
4  MCR
5  LSR
6  MSR
7  SCR
```

Initialization is fixed:

```text
IER = 0
LCR = DLAB
DLL = 2
DLM = 0
LCR = 8 data bits, no parity, one stop bit
FCR = FIFO enable | clear receive FIFO | clear transmit FIFO
MCR = DTR | RTS
IER = received-data available | receiver-line status
```

With the QEMU clock of 3686400 Hz and 16x oversampling, divisor 2 selects
115200 baud.

TTY drains `IIR` until the no-interrupt-pending bit is set. It handles:

- receiver-line status by reading `LSR`, counting errors, and discarding an
  errored byte after draining it;
- received-data and receive-timeout conditions by reading `RBR` while
  `LSR.DR` remains set;
- transmit-empty by filling `THR` from the output state and disabling the
  transmit-empty interrupt when no byte remains; and
- modem status by reading `MSR` to clear the condition, without adding modem
  policy.

The cached UART interrupt-enable state always retains receive-data and
line-status bits after ownership commit. Whenever aggregate output changes
from empty to nonempty, TTY first publishes the complete echo or VFS output
state and then sets the transmit-holding-register-empty bit in `IER`. An empty
THR therefore asserts the first transmit interrupt without polling. While
draining a receive interrupt, the same `IIR` loop may observe and service that
new transmit condition before PLIC completion.

TTY clears the transmit-empty bit only after the echo ring, VFS write source,
and any pending CR insertion are all empty. Queueing more output while it is
already enabled requires no register transition.

The normal path does not busy-poll for receive or transmit work. Polling is
limited to initialization, early-console flush, and terminal panic output.

### Portable TTY state and capacities

The TTY core has fixed capacities:

```text
MICROS_TTY_INPUT_CAPACITY   = 256 flagged entries
MICROS_TTY_ECHO_CAPACITY    = 256 physical bytes
MICROS_TTY_TRANSFER_MAX     = 4096 bytes
MICROS_TTY_WRITE_CAPACITY   = 4096 source bytes
```

It keeps:

- one canonical input ring with end-of-line flags;
- one current-line length;
- one echo ring;
- one resident read staging buffer;
- one resident write staging buffer and physical transmit cursor;
- one pending read;
- one read completion;
- one write completion;
- one last accepted nonzero request ID;
- one write-retry-armed flag;
- saturating receive-error, input-drop, and echo-drop counters; and
- the exact VFS endpoint or `NONE` when VFS is absent from the manifest.

There is no heap or dynamic allocation.

### Canonical input policy

Version 1 input behavior is:

- carriage return becomes line feed;
- line feed is stored and marks one completed line;
- backspace `0x08` and delete `0x7f` remove the last byte of the current
  incomplete line;
- backspace/delete at the start of a line has no effect;
- tab and bytes `0x20` through `0xff`, except delete, are ordinary data;
- other control bytes are ignored;
- accepted ordinary bytes echo unchanged;
- line feed echoes as CR followed by LF;
- erase echoes as backspace, space, backspace; and
- echo failure never discards accepted input.

When the ring has 255 entries, additional ordinary bytes are dropped so one
queue slot remains available for a terminating line feed. A line feed is
accepted whenever that final slot is available. When unread completed data
fills the ring, new input is drained from hardware and dropped rather than
overwriting retained bytes. Drops increment the saturating counter.

EOF, kill, signal, flow-control, raw-mode, timer, and configurable-character
semantics are not implied.

### Input and output completion

A canonical read can complete only when at least one line boundary exists.
It copies at most the requested count and never crosses the first line
boundary. A short request may consume a prefix and leave the rest of that line
for a later read.

TTY first copies the selected bytes into its resident read staging buffer. It
then performs one checked grant copy to VFS. Input-ring entries are removed
only after that copy succeeds. A revoked, stale, wrongly directed, or
unmapped grant therefore produces an explicit completion error without
losing terminal input.

For output, TTY copies the complete accepted request from VFS's grant into the
resident write buffer before acknowledging completion. It later transforms
each line feed into CR followed by LF while feeding the UART. The original
source count, not the expanded physical count, is returned to VFS.

Only one physical VFS write buffer exists. A new write is `BUSY` until:

- the previous completion has been collected; and
- every physical byte from the previous write has left TTY's resident state.

If the completion has already been collected but physical bytes remain, a
well-formed `SUBMIT_WRITE` returns `BUSY` and atomically arms one write-retry
notification before replying. The unaccepted request ID is not consumed, so
VFS may reuse it unchanged for the retry. When the final physical byte leaves
resident state, TTY clears the armed flag and emits one writable event. If the
buffer was already reusable when the request was checked, TTY accepts the
request instead, so no wakeup can be lost.

If an uncollected completion is the blocking condition, VFS already owns its
completion notification and must collect it before retrying. No separate
writable event is required until a later retry observes only physical-drain
backpressure.

Echo bytes use their separate ring and are selected before VFS write bytes at
each transmit-empty opportunity. An echo sequence is admitted atomically or
dropped as a whole.

Write completion means the complete source request is in TTY-owned resident
storage. It does not mean `LSR.TEMT` is set. Accepted writes cannot be
cancelled.

### VFS-to-TTY protocol

TTY uses:

```text
MICROS_TTY_PROTOCOL_VERSION      = 1
MICROS_TTY_MESSAGE_SUBMIT_READ   = 0x00020001
MICROS_TTY_MESSAGE_SUBMIT_WRITE  = 0x00020002
MICROS_TTY_MESSAGE_CANCEL        = 0x00020003
MICROS_TTY_MESSAGE_COLLECT       = 0x00020004
MICROS_TTY_MESSAGE_RESULT        = 0x00020005
MICROS_TTY_EVENT_COMPLETION      = 0x0000000000000001
MICROS_TTY_EVENT_WRITABLE        = 0x0000000000000002
```

Every request is a `call` from the exact configured VFS endpoint and carries
a nonzero reply token. Every multibyte field is little-endian and decoded
bytewise.

#### Submit request

Both submit messages use:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..15   nonzero request ID
bytes 16..19  grant token
bytes 20..23  zero
bytes 24..31  grant offset
bytes 32..39  byte count
bytes 40..47  zero
```

The byte count is in `[1, MICROS_TTY_TRANSFER_MAX]`. Accepted request IDs are
strictly increasing for the sole VFS endpoint and never wrap or become
accepted twice in v0.1. A `BUSY` submission is not accepted, does not advance
the last accepted ID, and may be retried with the same ID.

For `SUBMIT_READ`, the grant must authorize TTY to copy to VFS. TTY validates
the exact token, participant, generation, direction, offset, and bound without
copying data, then retains the request if no line is complete.

For `SUBMIT_WRITE`, the grant must authorize TTY to copy from VFS. TTY copies
the complete source bytes before returning `OK`. A copy failure leaves no
write request, completion, or UART output state.

Submission `OK` means accepted. Completion is retrieved separately even if it
became ready during submission.

#### Cancel and collect request

Both use:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..15   request ID
bytes 16..47  zero
```

`CANCEL` succeeds only for the exact pending read. It releases the retained
grant identity and request state without consuming input. A completed read
must be collected, and an accepted write cannot be cancelled.

`COLLECT` succeeds only for an exact completed request. It returns `PENDING`
for the exact pending read and `REQUEST` for an unknown, stale, future, or
already collected ID.

#### Result message

Every call receives:

```text
type = MICROS_TTY_MESSAGE_RESULT

bytes 0..3    protocol version = 1
bytes 4..7    original request type
bytes 8..11   signed TTY result
bytes 12..15  flags = 0
bytes 16..23  request ID, or zero when no canonical ID was decoded
bytes 24..31  transferred source-byte count, or zero
bytes 32..47  zero
```

Stable TTY results are:

```text
 0  OK
-1  BAD_TYPE
-2  BAD_VERSION
-3  MALFORMED
-4  CALLER
-5  STATE
-6  BUSY
-7  REQUEST
-8  GRANT
-9  PENDING
```

For a non-`OK` result the transferred count is zero. Internal corruption does
not become an ordinary result.

Request validation order is:

1. known message type;
2. protocol version and canonical fixed payload;
3. exact kernel-written VFS source endpoint and nonzero reply token;
4. console `TTY_OWNED` phase;
5. accepted-submit request-ID monotonicity or exact active/completed identity;
6. operation-specific slot, count, and grant conditions;
7. complete TTY state invariants; and
8. one failure-atomic transition.

### Completion notification and collection ownership

When a read or write completion first becomes available, TTY notifies exact
VFS with `MICROS_TTY_EVENT_COMPLETION`. Endpoint-origin notification
coalescing is sufficient for both completion slots.

For completion created during submission:

1. TTY first replies to the submission call;
2. after successful reply, TTY sends or coalesces the completion
   notification.

For later input completion, TTY sends the notification after the successful
grant copy and state commit.

VFS receives the bit, then calls `COLLECT` for its known outstanding IDs.
TTY releases a completion record only after the token-bound collect reply has
been accepted by the kernel. An impossible reply or notify failure is fatal;
there is no silent completion loss.

When a write retry was armed by `BUSY`, TTY sends or coalesces
`MICROS_TTY_EVENT_WRITABLE` exactly once on the resident-output busy-to-free
transition. VFS may retry the same unaccepted request ID because the last
accepted ID did not advance. The writable event may coalesce with a completion
event, so VFS must inspect every set bit and its own request table rather than
infer ordering.

If VFS instead obtains acceptance for a higher request ID before retrying, the
older unaccepted ID becomes stale and must be abandoned; any later submission
uses an ID greater than the new last accepted value. The writable bit is a
state-change hint, not ownership of one particular unaccepted ID.

The later VFS implementation owns application identity, descriptor state,
bounce-buffer grants, and cancellation policy. It must cancel an outstanding
TTY read before releasing the corresponding application operation. TTY binds
state only to exact VFS endpoint, request ID, grant token, offset, and length.

Unexpected VFS service loss remains a fatal static-service failure before RS;
it is not modeled as an ordinary application cancellation.

### TTY service loop and failure handling

TTY is single-threaded. After commit and readiness acknowledgment it accepts:

- exact source-`NONE` UART notifications;
- exact VFS calls;
- the launcher ready acknowledgment during startup; and
- no other user messages.

For one UART notification it:

1. validates the canonical envelope and exact event bit;
2. drains UART conditions until `IIR` reports none;
3. advances canonical input, pending read, echo, and transmit state;
4. publishes any newly completed request;
5. invokes `IRQ_COMPLETE(10)`; and
6. returns to receive.

Malformed or foreign VFS calls receive stable errors when their reply token is
valid. Token-zero calls under the accepted profile graph, unknown kernel-event
bits, impossible grant results, duplicate notifications after completion, and
complete-state validation failures deliberately trap.

There is no inferred peer, automatic internal retry loop, alternate device,
polled normal mode, or fallback to kernel ownership.

### Native evidence

Native tests must cover:

- exact TTY role, tuple, one-page stack, image bound, and device fields;
- exact replacement TTY and VFS profiles;
- console and route phase validation;
- begin authority, ordering, event preflight, and failure preservation;
- one-shot operation-12 map shape, authority, state, PTE conflict, and
  publication ordering;
- exact device-leaf validation and managed-memory lookup, IPC-buffer, grant,
  PM-output, and inventory rejection;
- operation-14 shape, authority, commit, completion, duplicate, wrong-source,
  and state precedence;
- PLIC source-10 priority, enable, claim-zero, claim, retained in-service,
  completion, and unexpected-source behavior;
- supervisor- and user-origin external-interrupt return;
- `sie.SEIE` and `sie.STIE` independence;
- canonical CR/LF, erase, queue-bound, drop, and echo behavior;
- read line boundaries, short reads, cancellation, grant failure without data
  loss, and collection;
- write grant-copy acceptance, LF-to-CRLF expansion, busy state, echo
  interleaving, empty-to-nonempty THRE enable, physical drain, writable retry,
  and lost-wakeup prevention;
- every message type, result, malformed payload, foreign source, stale ID,
  direction, offset, length, completion notification, and writable
  notification;
- deterministic replayable mixed input, request, cancellation, collect, and
  IRQ sequences;
- handoff/readiness timeout from mapping, mapped, and starting phases without
  extending the manifest interval; and
- panic seizure from every console phase, route phase, and `LCR.DLAB` state.

The portable model must use the same production parsers and state-transition
functions rather than a second test-only interpretation.

### QEMU evidence

The implementation must add a stable `test-qemu-tty` workflow with a
dependency-closed manifest containing:

- the real launcher;
- the real VM service;
- the real PM service;
- the real TTY service; and
- one test VFS probe with exact call authority to TTY.

It must prove:

1. VM handoff completes before console begin;
2. ordinary early-console output stops at begin;
3. one guest-owned manifest deadline covers mapping through readiness;
4. exact VM authority installs the one UART PTE before TTY release;
5. launcher observes the exact mapped event before release;
6. TTY initializes and commits before ordinary readiness;
7. TTY's empty-to-nonempty transition enables THRE and emits
   `MICROS_TTY_INPUT_READY` through interrupt-driven UART output;
8. the host harness sends the fixed byte sequence only after that complete
   serial line is observed;
9. source 10 is claimed and remains in service until exact TTY completion;
10. canonical input transforms
   `micros-ttyx<DEL>-input<CR>` into
   `micros-tty-input<LF>`;
11. the test VFS probe receives that line through a write-direction grant;
12. the probe submits the final marker through a read-direction grant and
    collects the accepted byte count;
13. TTY emits:

```text
MICROS_TTY_TEST_PASS handoff=two-phase mapping=exact irq=deferred input=canonical output=interrupt-driven grants=checked
```

14. the marker is physically drained, source-10 claim and completion counts
    agree, at least one receive and one transmit interrupt occurred, and no
    kernel ordinary write occurred after begin; and
15. the isolated test reports success through a test-only kernel hook and
    clean SBI shutdown.

The production TTY never emits test markers or invokes the test hook.

The QEMU harness must replace `stdin=DEVNULL` only for this explicit scenario.
It starts QEMU with a pipe, reads serial output incrementally, writes the
configured bytes once after the exact trigger line, preserves the existing
absolute timeout, and captures the complete output for the ordinary marker
validator. It must not use sleeps or weaken readiness deadlines.

### Implementation sequence

The implementation remains one dependency-ready task but is committed in
small green slices:

1. add portable TTY constants, protocol parsing, canonical input/output
   state, and native tests;
2. add exact manifest/profile replacements and console/route transition
   models;
3. add kernel PLIC mappings, controller core, `SEIE` helpers, and native
   controller tests without enabling delivery;
4. add the one-shot device-leaf mapper, operation-12 command, managed-memory
   exclusions, and mapping tests;
5. add operation-11 begin, launcher/VM events, release and ready gates, and
   the retained handoff/readiness deadline plus native transition tests;
6. add operation 14, external-interrupt dispatch, retained claim/completion,
   panic seizure, and target component tests;
7. add the real TTY ELF, NS16550A backend, grant-backed service loop, and
   freestanding image checks;
8. add the test VFS probe, marker-triggered serial input harness, QEMU TTY
   scenario, and exact pass evidence; and
9. update validation ownership and run the complete milestone gate.

No permanent commit claims a working handoff while its required tests are red.

## Invariants

- Normal UART output has exactly one owner.
- The kernel owns ordinary UART output only in `EARLY`.
- TTY owns ordinary UART policy only in `TTY_OWNED`.
- Panic seizure is terminal and may override either owner.
- Console begin, UART mapping, TTY release, console commit, and TTY readiness
  occur in that order exactly once.
- One manifest-bounded absolute TTY deadline covers begin through readiness
  and is never restarted at release.
- The UART device leaf exists only in the exact live TTY root at
  `0x7fffe000`.
- The device leaf is never a managed frame, grant target, IPC buffer, PM
  output, or general mapping inventory entry.
- No post-handoff frame or page table is allocated by the TTY mapping.
- VM policy agreement and kernel PTE authority remain distinct.
- PLIC source 10 has one exact TTY endpoint-generation owner.
- A nonzero claimed source is completed exactly once by exact TTY authority.
- The kernel does not complete source 10 on notification delivery.
- TTY drains `IIR` to no-pending before invoking completion.
- Repeated UART conditions coalesce in device/controller state, not in an
  inferred message count.
- `SEIE`, `STIE`, and global interrupt enable remain independently controlled.
- TTY retains at most one read, one read completion, one write completion, and
  one physical VFS write buffer.
- An unaccepted `BUSY` write request does not consume its request ID, and one
  armed writable event closes the physical-drain retry race.
- Accepted request IDs are nonzero, strictly increasing, and accepted at most
  once; repeating an unaccepted `BUSY` submission is not ID reuse.
- Input is removed only after a successful grant copy.
- Output completion is published only after complete resident acceptance.
- No application endpoint calls or notifies TTY directly.
- A TTY fault or endpoint loss is fatal before RS defines replacement.

## Consequences

- Step 11 establishes a real interrupt-driven user-space driver without
  prematurely designing general VM mapping.
- The fixed adjacent UART page consumes no post-handoff allocator resource.
- Managed-memory copy paths remain unable to turn device MMIO into delegated
  byte authority.
- Kernel-origin notification and PLIC in-service state compose without a new
  IPC envelope.
- VFS can remain responsive while a terminal read is pending despite the
  one-thread-per-process limit.
- The portable protocol and line discipline can be tested before production
  VFS exists.
- The initial terminal policy is intentionally small; later POSIX breadth
  requires a new reviewed extension rather than hidden behavior.
- Normal post-handoff kernel diagnostics remain absent until a TTY-visible log
  protocol is designed.

## Alternatives considered

### Map the kernel's UART virtual address into TTY

Rejected. The existing `0x10000000` leaf is supervisor-only in a root region
shared from the kernel page table. Making it user-accessible would expose the
mapping to every user root rather than only TTY.

### Pre-map UART as a managed static user page before VM handoff

Rejected. UART MMIO is not managed RAM and cannot truthfully carry a
`PROCESS_USER` to `VM_WIRED` frame-owner transition. Pretending otherwise
would weaken the typed ownership ledger and VM snapshot.

### Add the general VM mapping protocol now

Rejected. TTY needs one fixed device PTE with no allocation, rollback, caller
choice, or lifecycle reuse. General mapping requires frame selection,
mapping generations, map/unmap rollback, page faults, prepared-space sealing,
and PM/VFS transaction integration.

### Let the launcher trust a VM user message

Rejected. The kernel is the PTE mechanism and already owns exact event
injection. A kernel-origin mapped event directly reports the authoritative
commit and avoids a second token protocol.

### Complete the PLIC source before notifying TTY

Rejected. A level UART source can immediately retrigger while device state is
still pending and starve useful work. Explicit completion preserves the MINIX
driver-acknowledgment baseline.

### Give TTY a user mapping of PLIC registers

Rejected. Claim routing, ownership validation, and completion are privileged
mechanisms. TTY needs only the exact completion command.

### Keep VFS blocked in one long TTY call

Rejected. A pending canonical read could occupy VFS's only thread and prevent
other descriptor and cancellation work. Submit, notify, and collect preserve
asynchronous backend behavior with bounded state.

### Reply to writes only after physical drain

Rejected for the initial protocol. MINIX completes after bounded resident
acceptance, and waiting for `TEMT` would reduce throughput and conflate write
acceptance with a separate drain operation.

### Poll UART from TTY

Rejected. It would not prove PLIC routing, deferred completion, or
interrupt-driven input/output and would waste CPU while the service is idle.

### Restore kernel early-console ownership if TTY fails

Rejected. A normal rollback after user ownership risks concurrent writers and
ambiguous IRQ state. Before RS, owner loss is fatal and panic seizure is the
only override.

# ADR-0016: Supervisor Timer Interrupts

- Status: Accepted
- Date: 2026-10-05
- Superseded in part by: ADR-0025 scheduling-quantum interpretation

## Context

Milestone 2 needs a real asynchronous interrupt source before scheduling and
preemption can be implemented. ADR-0015 provides a complete trap frame and
return path, but every interrupt is still fatal. The next dependency-ready
mechanism is the OpenSBI supervisor timer path selected by ADR-0001.

The timer slice must prove interrupt delivery, dispatch, rearming, and `sret`
without introducing scheduler policy or depending on wall-clock sleeps.
RISC-V exposes supervisor timer delivery as interrupt cause code `5`, gated by
`sie.STIE` and global `sstatus.SIE`. SBI TIME programs an absolute deadline and
clears the pending timer interrupt when a future value is installed.

The current runtime has one hart and no hart object. Timer state must therefore
have an explicit one-hart ownership boundary that can move into the future hart
object without changing the trap-frame ABI or scheduler-facing tick semantics.

## Decision

### Platform interface

The platform layer adds:

```c
intptr_t sbi_set_timer(uint64_t absolute_time);
uint64_t riscv_read_time(void);
```

`sbi_set_timer` uses SBI TIME extension ID `0x54494d45` and function ID `0`.
On RV64, the absolute time value is passed in `a0`; the SBI error result is
returned from `a0`. Every call is checked even though the ratified SBI TIME
contract specifies `SBI_SUCCESS`.

`riscv_read_time` reads the 64-bit `time` CSR with one `rdtime` instruction.
The QEMU/OpenSBI target exposes that counter to S-mode. An unavailable counter
therefore becomes an explicit illegal-instruction panic rather than a silent
fallback.

The kernel does not parse or publish a wall-clock frequency in this slice. All
intervals are counter ticks, and no public ABI claims that a tick equals a
millisecond or another real-time unit.

### One-hart timer state

The timer module owns one static boot-hart state object containing:

- initialized and active flags;
- the programmed interval in counter ticks;
- the next absolute deadline;
- the number of accepted deadline expirations;
- a test-only stop-after count in the isolated timer image.

This object is timer mechanism state, not a process, thread, or global current
execution pointer. The future hart object adopts one instance with the same
fields and handler contract.

Lifecycle operations use an internal one-hart IRQ-save critical section:

1. atomically capture the prior `sstatus` while clearing SIE;
2. perform the complete state and STIE transition;
3. restore only the caller's prior SIE state.

The save/restore CSR helpers include compiler memory barriers. Callers do not
manipulate global interrupt state as an implicit lock. `active` and `ticks` are
volatile because the target test observes them across interrupt entry;
`interval` and `deadline` are protected by the IRQ-save critical section and
the fact that trap dispatch runs with SIE clear. No atomics or spinlocks are
required on the one-hart MVP.

The initial API is:

```c
bool micros_timer_initialize(void);
bool micros_timer_start(uint64_t interval);
bool micros_timer_stop(void);
uint64_t micros_timer_ticks(void);
enum micros_timer_interrupt_result micros_timer_handle_interrupt(void);
```

Initialization:

1. enters the IRQ-save critical section;
2. clears `sie.STIE`;
3. programs `UINT64_MAX` through SBI TIME to clear pending delivery;
4. resets the one-hart state and marks it initialized;
5. restores the caller's prior SIE state.

Start rejects an uninitialized, already active, or zero-interval request. In
its IRQ-save critical section, it:

1. reads `time`;
2. rejects `time + interval` overflow;
3. programs that absolute deadline through SBI;
4. records the interval and deadline;
5. marks the timer active;
6. sets `sie.STIE`.

Start preserves global `sstatus.SIE`; it changes only STIE. The future scheduler
bootstrap may call it with SIE already enabled after all interrupt sources and
hart state are ready.

Stop enters the same IRQ-save critical section, clears STIE before programming
`UINT64_MAX`, reports failure if the SBI call fails, marks the timer inactive
only after successful disarm, and restores the caller's prior SIE state.

### Interrupt handling

The trap dispatcher recognizes an interrupt when the top `scause` bit is set
and routes cause code `5` to the timer module before the generic
`unexpected-interrupt` panic.

An active timer interrupt:

1. reads `time`;
2. if unsigned `time < deadline`, returns `handled-spurious` without counting
   or rearming;
3. rejects tick-count overflow;
4. increments the accepted deadline-expiration count exactly once;
5. in the test image, disarms when the configured stop-after count is reached;
6. otherwise rejects `time + interval` overflow, programs that new absolute
   deadline, and records it only after successful programming;
7. returns without changing the interrupted trap frame.

Rearming from the current time avoids an immediate catch-up interrupt storm
after a delayed handler. Tick count represents accepted scheduling quanta,
not exact elapsed time. Later timekeeping may separately account for elapsed
counter values.

RISC-V permits STIP to remain asserted briefly after a future deadline is
programmed. The deadline comparison prevents that stale pending state from
being counted as another tick. A spurious trap may return and retrap until STIP
deasserts, but it does not mutate timer state.

The handler does not write UART output, allocate, lock, or enable interrupts.
SBI timer programming occurs while trap entry keeps SIE clear. `sret` restores
the interrupted SIE state through SPIE.

The dispatcher handles timer results explicitly:

- stale pending indication: return through the unchanged frame;
- inactive timer: trap-aware panic `unexpected-timer`;
- tick overflow: trap-aware panic `timer-tick-overflow`;
- rearm or disarm failure: trap-aware panic `timer-rearm-failed`;
- handled tick: return through the unchanged frame.

Other interrupt causes retain the existing `unexpected-interrupt` panic.

### QEMU component test

A separate `MICROS_BUILD_TIMER_TEST` image runs after FDT readiness. It:

1. initializes the timer while preserving the caller's clear SIE state;
2. configures the test-only stop-after value to three ticks;
3. starts a `0x00000000000186a0`-tick interval;
4. keeps global SIE clear while checking the volatile active flag;
5. when active, executes one race-free wait sequence:
   `wfi`; set SIE; clear SIE;
6. takes a pending timer trap between the adjacent SIE set/clear operations,
   or continues after a spurious `wfi` return and rechecks the flag;
7. exits with SIE clear after the third accepted expiration disarms STIE;
8. verifies exactly three accepted expirations and a cleared STIE bit.

Because SIE is clear between the active check and `wfi`, the final handler
cannot run in that window. If the deadline becomes pending there, local STIE
wakes `wfi`; setting SIE then takes the trap before the adjacent clear. After
`sret`, the clear executes and the loop observes the inactive state. The
sequence includes a compiler memory barrier.

Only then does it emit:

```text
MICROS_TIMER_TEST_PASS ticks=0x0000000000000003 interval=0x00000000000186a0
```

The host gate requires normal boot and FDT evidence, exactly one pass record
after `MICROS_FDT_READY`, no panic or explicit failure, clean SBI shutdown, and
no timeout. Host regressions reject missing, duplicated, early, or malformed
tick and interval fields.

The normal, direct-panic, trap-recovery, and unexpected-trap images link the
timer dispatch path but do not initialize, enable STIE, or set SIE. Their
observable behavior remains unchanged.

## Consequences

- Real asynchronous supervisor entry and return are verified before scheduling
  policy exists.
- Timer source enable and global interrupt enable remain separate operations.
- The handler is bounded and contains no serial output.
- Three accepted deadline expirations prove initial programming plus two
  rearms and a final disarm.
- The one-hart state has a direct migration path into the future hart object.
- The fixed test interval is deterministic in counter units but is not a
  wall-clock ABI.

## Alternatives considered

### Program QEMU CLINT or ACLINT MMIO directly

The selected OpenSBI platform already provides SBI TIME. Direct machine-timer
MMIO would couple S-mode to firmware-owned hardware and contradict ADR-0001.

### Enable SIE inside the generic timer start operation

Global SIE controls every supervisor interrupt source. Coupling it to one
device makes future PLIC and critical-section ordering implicit. Start enables
only STIE; the boot or scheduler layer controls global delivery.

### Rearm from the previous deadline

This preserves a fixed phase but can create a rapid catch-up storm when a
handler is delayed past one or more periods. The MVP schedules relative to the
current counter and counts accepted quanta.

### Add preemption in the same pull request

Preemption needs runnable thread objects, hart-local current-thread state, and
context selection. This slice proves only the timer mechanism and leaves
scheduler policy to its dependency-ready task.

## Specification basis

- [SBI TIME extension](https://docs.riscv.org/reference/sbi/v3.0/ext-time.html)
- [RISC-V Privileged Architecture, Supervisor-Level ISA](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html)
- [ADR-0001: Target Platform](0001-target-platform.md)
- [ADR-0010: Testing and Observability](0010-testing-and-observability.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0015: Supervisor Trap Entry](0015-supervisor-trap-entry.md)

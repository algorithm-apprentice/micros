# ADR-0027: Return-Boundary Timer Rearming

- Status: Accepted
- Date: 2026-10-07
- Supersedes in part: ADR-0026 timer preparation before user and idle return

## Context

The first scheduler QEMU run exposed a progress failure even after switch
commit stopped repeating address-space validation. Timer interrupt handling
rearms SBI TIME before the common return selector validates roots, contexts,
and ownership. When that validation takes longer than the timer interval, the
new deadline is already pending when `sret` executes. The hart immediately
retraps at the same user PC, repeatedly spending time in the kernel rather
than advancing the thread.

An interrupt trace of the production scheduler image showed repeated
supervisor timer entries at the same payload PC. The eight-second component
gate timed out without reaching its alternating-switch or idle proof.

MINIX already handles this at the return boundary:

- `minix/kernel/proc.c:466-472` restarts the local timer immediately before
  user-context restore;
- `minix/kernel/proc.c:198-204` restarts the boot CPU timer before idle wait;
- `minix/kernel/arch/i386/arch_clock.c:168-174` delegates to the APIC timer;
- `minix/kernel/arch/i386/apic.c:574-579` restarts only when the previous
  counter has expired.

The RISC-V SBI TIME implementation needs the equivalent bounded future
deadline before user entry or idle wait. This is a required platform
adaptation, not a different quantum policy.

## Decision

Add an explicit timer operation:

```c
bool micros_timer_prepare_return(struct micros_hart *hart);
```

It runs with the same IRQ-save discipline as the existing timer lifecycle:

1. require an initialized active timer with a nonzero interval;
2. read the platform counter;
3. if `counter < deadline`, succeed without programming or changing state;
4. otherwise reject `counter + interval` overflow;
5. program that future absolute deadline through the existing checked SBI
   TIME boundary;
6. store the new deadline only after successful programming.

It never counts a tick, changes the interval, changes STIE, or changes the
caller's SIE state. A rejected or failed operation preserves timer state.

The scheduler calls it after complete context/queue/root preflight and before
the first return-plan mutation. Failure from a captured trap is a trap-aware
fatal timer-programming failure, not a partial context switch. First scheduler
start still arms its inactive timer only after all preflight has succeeded.

The same operation prepares the idle wait boundary. A forced spurious-idle
test iteration must execute its SIE window before a real timer is due, and an
empty idle selection must not immediately repeat an expired timer.

Thread quantum continues to represent elapsed thread execution only. Moving
an already-expired timer deadline does not renew the thread's quantum,
consume an extra tick, or change queue/current selection. The common return
selector remains the only authority for selection.

All ordinary timer interrupt handling from ADR-0016 remains unchanged:
deadline acceptance, stale-pending rejection, tick counting, rearm, disarm,
and explicit errors.

## Required tests

The scheduler component must prove:

- a still-future return preparation does not attempt SBI programming;
- an expired deadline is moved strictly into the future without changing
  accepted ticks, interval, STIE, or SIE;
- failure injected at the SBI programming boundary preserves timer and
  scheduler ownership;
- repeated actual U-mode execution advances both private counters and
  alternates threads;
- the real idle path rejects one forced spurious iteration before a later
  real interrupt wakes a held thread.

The existing timeout and interrupt trace are the observed Red evidence.
The normal timer component retains its original three-tick contract.

## Consequences

- Long kernel preflight cannot create an immediate same-PC timer storm.
- Timer frequency is not misinterpreted as quantum consumption.
- Return commit remains bounded stores and CSR operations.
- The timer policy matches MINIX's expired-only return-boundary restart.
- SBI programming failure remains explicit and observable.

## Specification basis

- [ADR-0016: Supervisor Timer Interrupts](0016-supervisor-timer-interrupts.md)
- [ADR-0026: MINIX-Baseline Kernel Scheduler](0026-minix-baseline-kernel-scheduler.md)
- [MINIX scheduler and context-switch study](../research/minix-scheduler-and-context-switch.md)

# MINIX IRQ Notification Study

## Purpose

This study traces how the fixed MINIX reference turns a hardware interrupt
into a coalesced IPC notification, how the driver acknowledges the interrupt,
and how ownership is removed at process exit. It defines the behavioral
baseline for the first `micros` kernel-origin notification mechanism without
pulling PLIC routing, console handoff, or TTY into the current task.

The reference is MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

MINIX source is behavioral evidence only. `micros` remains an independent C17
and RISC-V implementation.

## Authority and registration

MINIX drivers use `SYS_IRQCTL` to establish an IRQ policy. The kernel verifies
that the requested vector is in the caller's privilege IRQ table, limits the
driver-selected notification identifier to the pending-interrupt bitmap width,
and binds the hook to the caller's exact endpoint
(`minix/kernel/system/do_irqctl.c:35-120`).

Enable, disable, and remove operations require the same hook owner endpoint
(`minix/kernel/system/do_irqctl.c:41-52,122-132`). Process cleanup removes every
hook owned by the exiting endpoint before clearing IPC identity
(`minix/kernel/system/do_clear.c:40-49`).

The baseline authority boundary is therefore:

- service management grants a driver a bounded IRQ set;
- the kernel owns the interrupt hook and controller mechanism;
- the exact driver endpoint owns acknowledgment authority for its hook;
- endpoint exit removes the hook before identity reuse.

## Interrupt dispatch

MINIX keeps one hook list per interrupt line. Dispatch masks the line, marks
each hook active, invokes every registered handler, and clears an active bit
only when that handler requests automatic reenable. The line is unmasked only
when no hook remains active, then the architecture controller is acknowledged
(`minix/kernel/interrupt.c:108-158`).

The generic user-driver handler validates that its endpoint is still live,
ORs the driver-selected notification bit into the owner's pending hardware
bitmap, and calls `mini_notify()` from the `HARDWARE` pseudo-source
(`minix/kernel/system/do_irqctl.c:140-171`).

This path is bounded and allocation-free. Repeated delivery of one hook before
the driver receives a message coalesces into one bit.

## Notification delivery

`mini_notify()` never blocks. If the destination is already receiving from the
source or `ANY` and is not waiting for a `SENDREC` reply, the kernel stages a
notification and wakes it immediately. Otherwise the source privilege bit is
marked pending (`minix/kernel/proc.c:1120-1166`).

When an `ANY` receive later consumes a pending hardware notification, MINIX:

1. selects pending notifications before asynchronous messages and blocked
   senders;
2. removes the selected pending source;
3. builds a zero-initialized notification from the `HARDWARE` pseudo-source;
4. transfers the complete coalesced hardware bitmap into the message;
5. clears that bitmap;
6. stages the message for common return delivery
   (`minix/kernel/proc.c:99-115,965-1035`).

Pending notification sources are selected deterministically by specific source
or lowest privilege identifier (`minix/kernel/proc.c:770-847`). The public
protocol still requires the recipient to inspect device state rather than
infer event ordering from IPC order.

## Driver acknowledgment

MINIX supports both automatic and explicit reenable policies. With automatic
reenable, the generic handler clears its active bit before interrupt return.
Without it, the hook remains active and the line stays masked until the owner
calls `IRQ_ENABLE` (`minix/kernel/interrupt.c:138-165`,
`minix/kernel/system/do_irqctl.c:41-52,169-171`).

The ARM serial driver uses explicit acknowledgment: it registers without
`IRQ_REENABLE`, drains the UART after observing its notification bit, then
calls `sys_irqenable()` (`minix/drivers/tty/tty/arch/earm/rs232.c:543-565,
588-603`).

The baseline behavior is therefore not "notify and immediately forget." The
kernel retains controller-side outstanding state until either policy permits
automatic reenable or the exact owner acknowledges after servicing the
device.

## `micros` classification

| Topic | MINIX behavior | `micros` classification |
| --- | --- | --- |
| Kernel pseudo-source | Hardware notifications originate from `HARDWARE` | Required ABI adaptation: use reserved endpoint value `NONE` as fixed by ADR-0030 |
| Notification semantics | Kernel envelope carries the complete coalesced event bitmap | Baseline parity |
| Envelope layout and time | MINIX includes a monotonic timestamp beside the hardware bitmap | Required ADR-0004 ABI adaptation: `micros` carries only the 64-bit mask in its fixed payload and zeroes the tail; recipients inspect authoritative device state rather than notification time |
| Immediate delivery | Wake a compatible receiver unless it is waiting for a call reply | Baseline parity through existing notification and completion rules |
| Deferred delivery | OR event bits and deliver them together on a later receive | Baseline parity with one dedicated kernel-event mask per destination endpoint |
| Source selection | Specific source or `ANY`; lowest pending privilege ID wins | Required ABI adaptation: kernel-origin events are consumable only by `ANY`; they precede endpoint-origin pending notifications |
| Driver-selected bit | Hook registration chooses a bit in the hardware-event bitmap | Staged substitution: the future PLIC route supplies an opaque nonzero mask; mapping and manifest authority are not part of this task |
| Controller acknowledgment | Hook active state controls masking and explicit reenable | Required RISC-V adaptation governed by ADR-0012: retain a PLIC source in service until `irq_complete` |
| Shared interrupt lines | Several hooks may share one legacy line and acknowledge independently | Required QEMU `virt`/PLIC adaptation for v0.1: each supported PLIC source has one exact manifest owner; shared-source fanout requires a later IRQ-routing ADR before a target device needs it |
| Endpoint reuse | Cleanup removes endpoint-owned hooks before slot reuse | Compatible extension: generation-safe endpoint routing will reject stale owners and detach routes before IPC close |
| Failure handling | Invalid vectors, hooks, or owners fail explicitly | Compatible extension: injection preflights complete IPC and scheduler state and preserves every byte on recoverable failure |

## Current substrate and missing mechanism

`micros` already has:

- a canonical notification envelope and little-endian 64-bit event mask;
- immediate receiver wake and deferred OR coalescing for live endpoint sources;
- notification-before-sender receive selection;
- call-reply exclusion;
- generation-safe endpoint validation;
- selected-thread completion return and user-buffer copy;
- endpoint-close cleanup for pending endpoint-origin notifications.

It does not yet have:

- a kernel pseudo-source accepted by staged-delivery validation;
- pending kernel-event storage independent of process slots;
- receive selection and consumption for that storage;
- an internal injection API that has no notifier thread or notifier
  completion.

PLIC MMIO mapping, claim/complete, route ownership, IRQ privilege tables,
console handoff, and a user `irq_complete` operation are separate
dependency-ready work under ADR-0012.

## Derived implementation boundary

The smallest baseline mechanism is an internal, serialized IPC operation that:

1. accepts one exact active destination endpoint and a nonzero opaque event
   mask;
2. uses source `NONE`, the existing kernel notification type, token zero, and a
   zero payload tail;
3. wakes the first compatible `ANY` receiver outside reply wait, with no
   notifier thread transition;
4. otherwise OR-coalesces into destination-owned kernel-event state;
5. lets later `receive(ANY)` or `reply_receive(..., ANY, ...)` consume all
   pending kernel bits before endpoint-origin notifications and senders;
6. preserves specific-source receive behavior;
7. clears pending kernel bits during ordinary endpoint-close cancellation,
   while a successfully staged delivery must still drain before close;
8. performs no controller acknowledgment, route lookup, or user-visible
   syscall dispatch.

This boundary is final for IPC. The future PLIC route may call it after
validating claim ownership and retaining in-service state, without changing
the message ABI or notification state machine.

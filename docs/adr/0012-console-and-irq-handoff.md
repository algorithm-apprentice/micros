# ADR-0012: Console and IRQ Handoff

- Status: Accepted
- Date: 2026-10-05

## Context

The kernel needs serial output before user services exist. TTY later owns UART
policy and interrupt-driven input/output. If both write the UART concurrently,
output corrupts. If the kernel completes a level-triggered PLIC interrupt
before TTY drains the device, the source may immediately retrigger and starve
the system.

## Decision

### Early console

Before TTY readiness, the kernel owns a polled transmit-only UART path.
Interrupt-driven UART operation is disabled. Early output is limited to boot
diagnostics and panic reporting.

### Handoff

TTY's manifest entry grants its process the fixed UART MMIO page and UART PLIC
source. Handoff is an explicit two-phase transition:

1. the launcher invokes the bootstrap-only `console_handoff_begin` operation
   for the exact manifest TTY endpoint;
2. the kernel disables ordinary early-console output, keeps the UART PLIC
   source disabled, and records the transition owner;
3. VM installs the authorized UART MMIO mapping while TTY remains inhibited;
4. the launcher releases TTY;
5. TTY initializes the UART and drains stale receive/status state;
6. TTY invokes its manifest-authorized `console_handoff_commit` operation;
7. the kernel verifies the endpoint and transition state, marks TTY as owner,
   enables the PLIC source, and revokes the transition operation;
8. TTY sends a versioned console-ready message;
9. the launcher treats TTY as ready.

After the handoff, user-service output goes through TTY. The v0.1 kernel emits
no routine UART output and does not write the UART during normal operation.
Failure between begin and commit is a fatal boot error; panic output remains
available through the exceptional path below.

### Interrupt protocol

On an external interrupt, the kernel claims the PLIC source and verifies its
manifest owner. For a user-owned source it:

- records the source as in service;
- sends or coalesces a notification to the owner;
- does not complete the PLIC source.

TTY drains all currently reported UART conditions, then invokes a privileged
`irq_complete` operation. The kernel verifies ownership and in-service state,
completes the PLIC source, and permits another notification.

An unexpected owner exit is fatal during the shell MVP. Later RS recovery must
define source masking and reassignment.

### Panic

A fatal kernel panic disables interrupts and may seize the UART for polled
output regardless of the normal owner. No normal execution resumes after that
seizure, so serialization is unnecessary.

## Consequences

- Early boot remains observable before IPC and TTY exist.
- Normal UART output has exactly one owner.
- Level-triggered interrupt completion occurs only after device state is
  drained.
- The ownership/acknowledgment protocol can be reused by later user drivers.
- Nonfatal kernel logging after handoff needs a TTY-visible log path or must be
  omitted from v0.1; direct UART writes are not allowed.

## Alternatives considered

### Complete the interrupt in the kernel before notification

This can immediately retrigger a level source while the driver has not drained
the device.

### Let both kernel and TTY poll the UART

Concurrent writers corrupt output and polling does not validate user-space IRQ
routing.

### Keep UART permanently in the kernel

This avoids a handoff but moves terminal device policy into privileged code.

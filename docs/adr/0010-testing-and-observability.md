# ADR-0010: Testing and Observability

- Status: Superseded
- Date: 2026-10-05
- Superseded by: ADR-0031

## Context

Kernel failures often destroy the environment needed to report them.
Target-only tests are slow and difficult to isolate, while host-only tests
cannot exercise privilege, MMU, trap, interrupt, or context-switch behavior.

## Decision

`micros` uses a layered verification strategy:

- native unit and property tests for portable algorithms and state machines;
- QEMU component tests for target privilege and hardware mechanisms;
- full-image integration and shell tests for service protocols;
- deterministic stress and fuzz tests with reproducible seeds;
- debug-build assertions for ownership and state invariants;
- compiler warnings as errors;
- ASan and UBSan for supported native tests;
- machine-readable TAP output over serial;
- explicit success, failure, panic, timeout, and unexpected-exit results.

Stable build targets are:

```text
test-unit
test-qemu-smoke
test-qemu-integration
test-stress
```

The native unit suite should normally complete within 2 seconds. The QEMU smoke
suite should normally complete within 10 seconds. Unit tests do not sleep;
time-dependent logic uses injected ticks or deterministic events.

Native model tests exercise multiple threads per process even though the v0.1
runtime limit is one. They cover reply-token routing, process/thread ownership,
hart-local current-thread state, and queue invariants so the deferred
multithreading path remains an enforced design property rather than a comment.

Every blocking QEMU test has a host timeout. Randomized failures print their
seed and operation trace. Assertions remain enabled in debug images and stop
at the first invariant violation with process, endpoint, trap, and source
context.

Portable kernel logic is structured for native compilation where doing so does
not distort the target design. Hardware abstractions are not added solely to
mock behavior that is clearer to test in QEMU.

## Consequences

- Most logic receives fast feedback without booting a guest.
- Assembly, MMU, and interrupt behavior still receive real target execution.
- Diagnostics and test hooks are architectural work, not optional cleanup.
- Separate host and target build graphs add initial build-system complexity.
- Tests reduce risk but do not prove the entire kernel correct.

## Alternatives considered

### QEMU tests only

This exercises the target but makes algorithm failures slower and harder to
minimize.

### Host tests only

This cannot validate the mechanisms most likely to fail differently on the
target CPU.

### Full formal verification

Formal verification can provide stronger guarantees, but applying it to the
whole system would dominate the educational MVP. Small protocols may be
modeled later when their state space justifies it.

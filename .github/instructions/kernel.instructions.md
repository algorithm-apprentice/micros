---
applyTo: "arch/**,kernel/**,include/micros/**"
---

Follow the Accepted kernel-boundary, IPC, grant, execution-model, and
console/IRQ ADRs.

Portable logic must remain native-testable when doing so does not distort the
target design. Privileged, assembly, MMU, trap, context-switch, or interrupt
changes require failing QEMU component-test evidence before implementation.

Keep process, thread, endpoint, and hart state separate. Do not introduce a
global current process/thread, raw cross-process pointer authority, silent
fault recovery, or policy that belongs to a user-space service.

---
applyTo: "tests/**,tools/**"
---

Keep fast tests deterministic and free of timing sleeps. Use injected ticks or
explicit events for time-dependent behavior.

Print replayable seeds for randomized tests. QEMU tests must distinguish pass,
failure, panic, unexpected exit, and timeout through machine-readable output.

Test observable contracts and invariants rather than private implementation
details. A bug fix must retain a regression test that fails without the fix.

---
applyTo: "servers/**,lib/runtime/**,user/**"
---

Treat every peer as isolated and potentially faulty. Validate endpoint
generation, message type, payload shape, request state, grant direction, and
bounds before changing service state.

Use the documented privilege profiles, one-shot reply tokens, and
non-transitive direct-grant paths. Do not add direct application access to VM,
RAMFS, TTY, devices, or IRQs.

When a downstream peer is not dependency-ready, require a failing native
protocol/transition model before implementation. Require a QEMU integration
test once every participating service exists.

# Documentation

This directory contains the design baseline for `micros`.

## Architecture

- [System overview](architecture/system-overview.md) defines the target system,
  trust boundary, component responsibilities, boot phases, and invariants.
- [Development dependency DAG](architecture/development-dag.md) converts the
  cyclic runtime architecture into an acyclic implementation sequence.

## Research

- [MINIX dependency analysis](research/minix-dependency-analysis.md) records the
  source baseline, component relationships, strongly connected component, and
  bootstrap lessons used to derive the `micros` design.
- [MINIX baseline parity audit](research/minix-baseline-parity-audit.md)
  classifies every merged kernel foundation as baseline behavior, required
  target adaptation, compatible extension, staged substitution, or correction.
- [MINIX scheduler and context-switch study](research/minix-scheduler-and-context-switch.md)
  traces runnable queues, accounting, context selection, restore, and
  user-scheduler authority before scheduler implementation.
- [MINIX endpoint and blocking IPC study](research/minix-endpoint-and-ipc.md)
  traces endpoint validation, privilege checks, send/receive queues,
  request/reply blocking, notifications, deadlock detection, and exit cleanup.
- [MINIX IRQ notification study](research/minix-irq-notification.md) traces
  interrupt ownership, kernel pseudo-source delivery, event coalescing,
  explicit driver acknowledgment, and endpoint cleanup.
- [MINIX direct grant and safe-copy study](research/minix-direct-grants-and-safecopy.md)
  traces grant identity, table authority, direction and bounds checks, fault
  behavior, revocation, and checked cross-address-space copy.
- [MINIX runtime safe-copy and post-handoff resolution study](research/minix-post-handoff-wired-address-resolution.md)
  traces safe-copy mapping authority after VM startup and derives the
  wired-only `micros` handoff replacement.
- [MINIX user grant and safe-copy syscall study](research/minix-user-grant-syscall-boundary.md)
  traces direct IPC traps, user-managed grant publication, safe-copy kernel
  calls, and the required unified `micros` register ABI.
- [MINIX user-service runtime study](research/minix-user-service-runtime.md)
  traces process entry, libc initialization, IPC vectors, grant and safe-copy
  wrappers, and the service-startup boundary used by the freestanding runtime
  design.
- [MINIX RS and SEF bootstrap study](research/minix-rs-sef-bootstrap.md)
  traces static service descriptors, inhibited privilege setup, endpoint
  visibility, initialization callbacks, readiness replies, boot ordering,
  timeouts, and the deferred RS recovery breadth.

## Planning and verification

- [Roadmap](roadmap.md) defines milestones, exit criteria, deferred scope, and
  major risks.
- [Testing strategy](testing-strategy.md) defines the host, QEMU, integration,
  stress, and observability layers.

## Development

- [Build and smoke-test guide](development/building.md) defines prerequisites,
  tool discovery, build artifacts, and the deterministic QEMU command.
- [AI-native development workflow](development/ai-native-workflow.md) defines
  the shared human/agent task, TDD, review, and merge contract.
- [Contributing guide](../CONTRIBUTING.md) is the contributor entry point.
- [Agent contract](../AGENTS.md) is the tool-neutral AI agent entry point.

## Decisions

- [ADR index](adr/README.md) lists all architecture decisions and their current
  status.

All project documentation, source code, identifiers, comments, tests, and
commit messages are written in English.

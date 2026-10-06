# Architecture Decision Records

## Status meanings

- **Proposed**: drafted for review; implementation must not depend on it
  irreversibly.
- **Accepted**: reviewed and approved as the current project decision.
- **Rejected**: reviewed but not selected.
- **Superseded**: replaced by a later ADR.

Changing an Accepted decision requires a new ADR that explains the new evidence
and explicitly supersedes the old record. Existing ADR history is not edited
to make the project appear more consistent than it was.

## Initial decision set

| ADR | Title | Status |
| --- | --- | --- |
| [0001](0001-target-platform.md) | Target platform | Accepted |
| [0002](0002-language-and-toolchain.md) | Language and toolchain | Accepted |
| [0003](0003-kernel-responsibility-boundary.md) | Kernel responsibility boundary | Accepted |
| [0004](0004-ipc-and-endpoint-abi.md) | IPC and endpoint ABI | Accepted |
| [0005](0005-direct-memory-grants.md) | Direct memory grants | Accepted |
| [0006](0006-vm-bootstrap-and-handoff.md) | VM bootstrap and handoff | Accepted |
| [0007](0007-static-launcher-before-rs.md) | Static launcher before recovery services | Accepted |
| [0008](0008-ramfs-first-filesystem.md) | RAMFS-first filesystem | Accepted |
| [0009](0009-spawn-before-fork.md) | Spawn before fork | Accepted |
| [0010](0010-testing-and-observability.md) | Testing and observability | Accepted |
| [0011](0011-process-thread-and-hart-model.md) | Process, thread, and hart model | Accepted |
| [0012](0012-console-and-irq-handoff.md) | Console and IRQ handoff | Accepted |
| [0013](0013-ai-native-test-first-development.md) | AI-native test-first development | Accepted |
| [0014](0014-panic-diagnostics.md) | Panic diagnostics | Accepted |
| [0015](0015-supervisor-trap-entry.md) | Supervisor trap entry | Accepted |
| [0016](0016-supervisor-timer-interrupts.md) | Supervisor timer interrupts | Accepted |
| [0017](0017-bootstrap-physical-frame-allocator.md) | Bootstrap physical frame allocator | Accepted |
| [0018](0018-sv39-kernel-address-space.md) | Sv39 kernel address space | Accepted |
| [0019](0019-kernel-object-identity-and-ownership.md) | Kernel object identity and ownership | Accepted |
| [0020](0020-typed-bootstrap-frame-ownership.md) | Typed bootstrap frame ownership | Accepted |
| [0021](0021-generation-safe-user-address-spaces.md) | Generation-safe user address spaces | Accepted |
| [0022](0022-user-execution-contexts-and-u-mode-entry.md) | User execution contexts and U-mode entry | Accepted |
| [0023](0023-canonical-user-status-summary-bits.md) | Canonical user status summary bits | Accepted |
| [0024](0024-preemptive-round-robin-scheduler.md) | Preemptive round-robin scheduler | Superseded |
| [0025](0025-minix-behavioral-baseline-before-optimization.md) | MINIX behavioral baseline before optimization | Accepted |
| [0026](0026-minix-baseline-kernel-scheduler.md) | MINIX-baseline kernel scheduler | Accepted |
| [0027](0027-return-boundary-timer-rearming.md) | Return-boundary timer rearming | Accepted |
| [0028](0028-squash-merge-task-pull-requests.md) | Squash-merge task pull requests | Accepted |
| [0029](0029-endpoint-and-privilege-substrate.md) | Endpoint and privilege substrate | Accepted |
| [0030](0030-minix-baseline-blocking-ipc.md) | MINIX-baseline blocking IPC | Accepted |

# ADR-0007: Static Launcher Before Recovery Services

- Status: Accepted
- Date: 2026-10-05

## Context

MINIX RS participates in privilege setup, VM setup, scheduling, discovery,
startup, and recovery. Implementing that control plane before the underlying
services are stable would make RS depend on nearly every unfinished component.

## Decision

The shell MVP uses a static bootstrap launcher.

- A build-generated manifest lists embedded boot services, their endpoint
  slots, memory limits, named privilege profiles, device ranges, IRQ sources,
  and dependency order.
- The kernel creates or reserves the initial process objects from this
  manifest.
- Only the launcher receives bootstrap authority to release services and apply
  their predefined privileges.
- The kernel limits that authority to exact manifest entries; the launcher
  cannot invent a process, endpoint, or privilege set.
- A service sends a versioned ready message before the launcher releases its
  dependents.
- For TTY only, the launcher may invoke the manifest-bound
  `console_handoff_begin` operation before releasing TTY. It cannot name any
  endpoint or device other than the manifest TTY assignment.
- Startup order follows the accepted development DAG.
- Unexpected exit, malformed readiness, or timeout is a fatal boot error with
  structured diagnostics.
- Bootstrap authority is permanently revoked after init is released.

The immutable profile table also contains one `APPLICATION` profile. PM's own
manifest profile grants a separate narrow operation that can bind only that
profile to an unpublished process reserved by PM's current spawn transaction.
The same transaction-scoped authority lets PM prepare the process with a valid
VM load-complete token and activate or abort the resulting prepared process. It
cannot assign service profiles, device ranges, IRQs, arbitrary masks, or
operate on a process outside the active spawn transaction. These operations
remain available after launcher authority is revoked because they are part of
normal process creation, not boot service management.

The manifest is not a general package, policy, or runtime service format. The
later RS implementation may consume a revised manifest only after a separate
protocol decision.

## Consequences

- Initial service startup is deterministic and easy to trace.
- Recovery and dynamic reconfiguration do not block the shell MVP.
- The launcher is intentionally limited and disposable.
- Services must expose a small readiness protocol from their first version.
- Crashed core services require a reboot until the recovery milestone.

## Alternatives considered

### Implement RS first

RS cannot be tested meaningfully before VM, PM, scheduler, IPC privileges, and
service protocols exist.

### Let the kernel start every service unconditionally

This hides dependency readiness inside privileged code and provides no clean
transition to a user-space control plane.

### Start services from init

Init appears after the core process, memory, terminal, and filesystem services
that it would need to launch.

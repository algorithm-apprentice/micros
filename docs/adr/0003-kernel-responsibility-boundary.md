# ADR-0003: Kernel Responsibility Boundary

- Status: Accepted
- Date: 2026-10-05

## Context

A microkernel becomes difficult to understand when policy gradually moves into
privileged code. Conversely, forcing every bootstrap decision into a server
before IPC and address spaces exist creates circular dependencies.

## Decision

The kernel permanently owns:

- trap and interrupt entry;
- process, thread, endpoint, and hart objects;
- thread execution contexts and context switching;
- page-table activation and validated mapping mechanisms;
- kernel-reserved physical memory;
- endpoint allocation and generation validation;
- IPC queues, blocking transitions, and permission checks;
- interrupt routing and notifications;
- direct grant registration and safe-copy validation;
- scheduling mechanism;
- privileged bootstrap transitions with an explicit end condition.

The initial kernel also contains a round-robin scheduling policy and a
bootstrap physical allocator. These are temporary bootstrap policies with
defined replacement points.

Processes and threads remain separate even though v0.1 permits exactly one
thread per process. Scheduling, blocking, faults, and register state belong to
threads. Address spaces, grants, privilege profiles, and endpoint ownership
belong to processes. Hart-local state identifies the running thread. The full
decision is recorded in
[ADR-0011](0011-process-thread-and-hart-model.md).

User-space services own:

- user-frame allocation and mapping policy after VM handoff;
- process relationships, lifecycle semantics, spawn, exit, and wait;
- pathname, mount, descriptor, and filesystem policy;
- terminal buffering and line policy;
- dynamic service discovery;
- scheduling policy after the scheduler server is introduced;
- restart and recovery policy after RS is introduced.

The kernel does not contain filesystems, network stacks, device policy, POSIX
process semantics, or a general service manager.

## Consequences

- Privileged code remains small enough to audit directly.
- Some mechanisms have an intentionally simple in-kernel policy during
  bootstrap.
- Every temporary kernel authority needs a tested revocation or handoff.
- Service failure can affect availability without corrupting kernel state.
- Cross-service transactions require explicit protocols and rollback.

## Alternatives considered

### Monolithic teaching kernel

A monolithic kernel would reach a shell sooner but would not teach the intended
service isolation, IPC, grant, and recovery architecture.

### Move all scheduling and memory decisions out immediately

This would reproduce the startup cycles before the communication substrate is
stable. The staged handoffs preserve the final boundary without making it a
prerequisite for boot.

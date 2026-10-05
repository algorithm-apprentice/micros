# ADR-0011: Process, Thread, and Hart Model

- Status: Accepted
- Date: 2026-10-05

## Context

The shell MVP needs only one user thread per process and one hart. Treating a
process, schedulable context, IPC waiter, and current CPU state as the same
object would be smaller initially but would make future multithreading or SMP a
kernel-wide redesign.

The project needs explicit extension boundaries without implementing thread
creation, synchronization APIs, inter-processor interrupts, or load balancing
before they are required.

## Decision

The kernel represents four distinct object types from the first implementation.

### Process

A process owns:

- an address-space handle;
- one primary endpoint in v0.1;
- its immutable privilege profile;
- created grants;
- resource accounting and lifecycle state;
- a collection of threads.

PM owns parent/child and wait semantics. The kernel process object contains only
the mechanism state required to enforce ownership.

### Thread

A thread owns:

- saved user and kernel register context;
- a kernel stack;
- runnable, running, or blocked state;
- run-queue and IPC-wait links;
- a fault or syscall continuation;
- an owning process;
- an outstanding call/reply-token wait, if any.

The scheduler, IPC blocking, page-fault blocking, wakeup, and context switch
operate on threads. They do not enqueue or block a process object.

v0.1 enforces at most one live thread per process and exposes no thread-create,
join, detach, or thread-local-storage ABI. This is a checked policy limit, not a
structure equivalence.

### Endpoint

An endpoint is a process-owned IPC identity with its own generation and receive
queue. v0.1 gives each process one endpoint. Several threads may wait on one
endpoint in a future version; each blocked operation already records the exact
thread. One-shot reply tokens route a reply to the caller thread rather than
merely to its process.

### Hart

Each hart has a hart-local object containing:

- the current thread;
- scheduler and trap stack state;
- interrupt nesting and preemption state;
- pending reschedule state;
- later, per-hart run-queue or load-balancing state.

There is no global `current_process` or `current_thread` variable. v0.1 creates
one hart-local object and rejects secondary-hart startup.

### Synchronization boundary

Shared kernel objects are accessed through explicit critical-section and lock
interfaces with documented ownership. On the single-hart MVP, the
implementation may reduce to interrupt/preemption exclusion where valid.
Callers do not manipulate global interrupt state as an implicit lock.

Spinlocks, atomic reference counts, inter-processor interrupts, TLB shootdown,
and multi-hart load balancing are deferred. Adding them must not change process
or thread identity or the public IPC message layout.

Executable preparation is exposed as one PM-only kernel transition rather than
an incidental instruction in the loader. VM freezes a mapping generation and
returns a kernel-issued load-complete token; it cannot call the preparation or
activation operations. PM presents that token, and v0.1 performs a local
`fence.i` before the sole hart can run the new thread. A future multi-hart
implementation may extend the same transition with per-address-space
instruction-synchronization generations and remote hart notification without
changing spawn or IPC ABIs.

The kernel stores the validated mapping generation in the prepared thread
state and seals that address space until PM activates or aborts it. This keeps
instruction synchronization and future per-hart synchronization tied to the
exact mappings that will execute.

## Consequences

- The MVP has a few more explicit objects and invariants.
- Thread creation can later be added without replacing process identity,
  grants, address spaces, or scheduler queues.
- Concurrent calls from one process can be matched to exact caller threads.
- SMP still requires substantial synchronization and TLB work, but not a
  global-current-state rewrite.
- Native model tests can exercise multi-thread object relationships before the
  target exposes multithreading.

## Alternatives considered

### Treat each process as one schedulable context

This minimizes initial structures but couples resource ownership to blocking
and register state, forcing broad changes for threads.

### Make every thread an independent process

This avoids a thread object but cannot naturally share one address space,
grants, descriptors, and process lifecycle.

### Implement multithreading in v0.1

User synchronization, cancellation, thread exit, stack management, and server
concurrency would expand the shell MVP without being required to validate the
microkernel architecture.

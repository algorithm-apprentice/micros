# ADR-0004: IPC and Endpoint ABI

- Status: Accepted
- Date: 2026-10-05

## Context

All core user-space services depend on process identity and message delivery.
Changing either representation after several services exist would create broad
and error-prone churn.

## Decision

### Endpoints

`endpoint_t` is an unsigned 32-bit value:

- bits 0 through 11 contain a process slot;
- bits 12 through 31 contain a generation;
- generation zero is invalid;
- `0xffffffff` represents `ANY` and `0xfffffffe` represents `NONE`;
- those two reserved bit patterns are never allocated to a live process;
- an endpoint resolves only when both slot and generation match the live
  process.

The initial ABI therefore supports up to 4096 simultaneous process slots and
1,048,575 nonzero generation values. If advancing a slot would wrap its
generation or produce a reserved endpoint value, the kernel quarantines that
slot instead of permitting a stale endpoint to become valid again.

An endpoint is a process-owned IPC identity, not a thread ID. v0.1 assigns one
primary endpoint to each process. Receive queues belong to the endpoint, while
blocked send, receive, and call state belongs to a thread.

### Messages

Every IPC message is 64 bytes and aligned to 8 bytes:

```text
bytes 0..3    source endpoint, written by the kernel
bytes 4..7    protocol message type
bytes 8..15   opaque reply token, written by the kernel for a call
bytes 16..63  protocol-defined payload
```

The target ABI is little-endian. A sender cannot select the delivered source
field or reply token. Protocols define the 48-byte payload with fixed-width
integer types and compile-time size assertions. Raw pointers in a payload never
authorize or transfer memory.

### Operations

The first ABI provides:

- blocking `send`;
- blocking `receive` from a specific endpoint or `ANY`;
- atomic `call`, which sends and then waits for a reply from the destination;
- `reply`, which consumes a request-derived reply token;
- `reply_receive`, which replies and atomically waits for the next request;
- coalesced `notify` for interrupts and non-payload events.

For `call`, the kernel creates an opaque nonzero token bound to the exact
blocked caller thread and the callee endpoint generation. The callee receives
the token in the request and must present it to `reply` or `reply_receive`.
The token grants one reply only, wakes that exact thread, and is canceled if
either endpoint exits or changes generation. Ordinary sends and notifications
carry token zero. The reply operation accepts the token as a syscall argument;
the delivered reply message contains token zero.

The high bit of the message type is reserved for kernel-generated envelopes.
A notification uses a reserved kernel message type, stores a 64-bit event mask
in payload bytes 0 through 7, and zeroes the remaining payload. Repeated
notifications from the same source to the same destination are coalesced by
bitwise OR. There is no ordering guarantee between a notification and an
ordinary message; notification recipients must inspect the authoritative
device or service state.

Blocking send-chain cycles are rejected before enqueueing the final edge. The
wait graph records blocked threads rather than treating a process as its only
thread. The initial scheduler does not promise priority inheritance.

### Authorization

The kernel contains an immutable table of named privilege profiles. A profile
limits call, send, notify, kernel-operation, device, and IRQ authority.

- The bootstrap launcher may install only the exact profile named by a
  service's manifest entry.
- PM receives a narrow capability to install only the single
  `APPLICATION` profile on a process reserved by its active spawn transaction.
- PM's spawn authority also permits prepare and activate operations only for
  that reserved process and only with a valid VM load-complete token.
- Preparation seals the validated mapping generation in kernel state. PM may
  activate or abort that state; VM cannot change its mappings while sealed.
- VM may finish and freeze mappings and obtain a load-complete token, but it
  cannot initialize, publish, activate, or run the child.
- `APPLICATION` may use basic user IPC and grant operations and may call PM and
  VFS. It has no direct VM, RAMFS, TTY, device, IRQ, notification, or
  privileged kernel-operation authority.
- A valid reply token authorizes the exact one-shot reply even when the
  server's static profile does not permit ordinary sends to the caller.
- The kernel installs the profile before publishing the endpoint or running
  the first thread.

Kernel ABI operations return zero or a nonnegative result on success and a
negative, project-defined error number on failure.

Asynchronous queued messages, scatter/gather IPC, and compatibility with the
MINIX message union are deferred.

## Consequences

- Endpoint reuse does not silently redirect a stale client to a new process.
- Fixed messages make copying and validation predictable.
- A future multithreaded process can issue concurrent calls without changing
  the message ABI or allowing one thread to consume another thread's reply.
- The protocol payload is 48 bytes rather than the MINIX-style 56 bytes.
- Bulk data requires grants rather than expanding the IPC ABI.
- A 64-byte message may require multi-step protocols for large metadata.
- Generation exhaustion needs an explicit safe policy before it becomes
  reachable, but no migration machinery is required for the MVP.

## Alternatives considered

### PID-only identity

Slot-only identity allows stale references to target an unrelated process after
slot reuse.

### Variable-length kernel messages

Variable messages complicate kernel allocation, blocking, restart, and bounds
validation while duplicating the grant data path.

### Asynchronous IPC first

Asynchronous queues improve decoupling but require buffering and backpressure
policy before the basic state machine is proven.

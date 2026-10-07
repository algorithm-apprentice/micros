# ADR-0037: Kernel-Origin IPC Notification Injection

- Status: Accepted
- Date: 2026-10-08
- Refines: ADR-0012 and ADR-0030

## Context

ADR-0030 completes notifications from live endpoint sources and reserves the
later IRQ-routing boundary: an internal kernel notification must use the same
envelope and event mask with source `NONE`, without adding an IPC syscall.

ADR-0012 separately fixes the final PLIC contract. The kernel claims a
user-owned source, records it in service, notifies the exact owner, and does
not complete the source until that owner invokes `irq_complete` after draining
the device.

The completed IPC substrate currently assumes every pending or staged
notification source resolves to one active endpoint generation. That is
correct for user `notify`, but it cannot represent the kernel pseudo-source.
It also requires a scheduler-held notifier thread so the user operation can
return a completion. A hardware-origin event has no notifier thread and no
notifier completion.

The fixed MINIX baseline:

- transforms a hardware interrupt into a nonblocking notification from a
  kernel pseudo-source;
- OR-coalesces per-driver event bits;
- wakes a compatible receiver immediately or retains the complete bitmap;
- excludes call-reply waits;
- lets explicit driver acknowledgment retain controller-side outstanding
  state;
- removes IRQ ownership before endpoint reuse.

The canonical evidence is
[the MINIX IRQ notification study](../research/minix-irq-notification.md).

This decision defines only the IPC injection mechanism that the future PLIC
route consumes. It does not pull controller routing or TTY ahead of their
development-DAG position.

## Decision

### Scope

This slice defines:

- one internal portable kernel-origin notification operation;
- source-`NONE` notification envelope validation;
- destination-owned pending kernel-event storage;
- immediate receiver wake without a notifier thread;
- `ANY` receive selection and consumption of pending kernel events;
- endpoint-close and validator integration;
- native model and QEMU IPC-component acceptance evidence.

It does not define:

- PLIC MMIO mapping, priorities, enables, thresholds, claim, or complete;
- supervisor external-interrupt trap routing;
- IRQ source ownership or in-service state;
- manifest IRQ or device authority;
- `console_handoff_begin`, `console_handoff_commit`, or `irq_complete`;
- a new IPC operation number, ecall, endpoint, reply token, or user ABI;
- TTY, UART interrupt policy, launcher behavior, or service recovery;
- shared IRQ sources, multihart routing, or affinity.

Those mechanisms remain governed by ADR-0012 and receive separate reviewed
design and implementation tasks.

### Internal interface

The portable IPC layer adds:

```c
enum micros_ipc_error micros_ipc_inject_kernel_notification(
    struct micros_endpoint_registry *registry,
    struct micros_kernel_objects *objects,
    micros_endpoint_t destination,
    uint64_t event_mask
);
```

This is a kernel-internal mechanism, not a user-callable operation. It accepts
no source endpoint or thread handle, so a caller cannot construct a
kernel-origin notification through the user `notify` path.

The caller must provide:

- externally serialized registry and scheduler state;
- the authoritative target registry and object table in production;
- one exact active destination endpoint;
- one nonzero opaque event mask.

The IPC layer does not interpret event-bit meaning. The future IRQ route owns
the mapping from a validated hardware source to route-defined event bit or
bits. That mapping is deliberately outside this ADR.

The operation returns existing portable IPC errors. It does not add a stable
syscall result because no syscall invokes it.

### Canonical kernel-origin envelope

Every immediate or deferred kernel-origin notification is:

```text
source       MICROS_ENDPOINT_NONE
type         MICROS_IPC_TYPE_KERNEL_NOTIFICATION
reply_token  0
payload[0:8] event mask, little-endian
payload[8:]  zero
```

The mask is always nonzero. No payload byte authorizes device, memory, route,
or completion access.

A user process cannot name `NONE` as a receive source. Kernel-origin
notifications are therefore consumable only by `receive(ANY)` or the receive
half of `reply_receive(..., ANY, ...)`. This keeps the public IPC ABI unchanged
and prevents `NONE` from becoming a user-callable pseudo-endpoint.

### Pending state

Each endpoint record gains one independent field:

```c
uint64_t pending_kernel_events;
```

It belongs to the destination endpoint. It is not indexed by a process slot
and carries no source endpoint generation.

The existing fields remain unchanged:

- `pending_notification_sources` identifies live endpoint-source slots;
- `pending_events[slot]` stores each endpoint source's coalesced mask.

The representations never alias. `pending_kernel_events` is zero for every
free or reserved endpoint record. An active endpoint may hold any nonzero
mask.

Repeated kernel-origin injections OR into the field. Coalescing is idempotent,
bounded, allocation-free, and intentionally loses event count. The recipient
must inspect authoritative device or route state.

### Immediate injection

After validating the combined endpoint/object state and resolving the exact
active destination, the operation builds the canonical envelope and searches
the destination receiver queue using source `NONE`.

Because user receive rejects `NONE`, only an `ANY` receiver can match. Existing
reply-wait exclusion remains authoritative: a receiver carrying `IPC_REPLY`
does not consume the event.

For an eligible receiver, complete preflight verifies:

- exact receiver handle and queue membership;
- canonical `IPC_RECEIVE` state;
- no existing staged completion;
- scheduler wake feasibility;
- destination ownership and receive buffer invariants.

Commit then:

1. clears only the receiver's `IPC_RECEIVE` flag through the existing
   scheduler transition;
2. unlinks that exact receiver;
3. clears its queue link and saved receive source;
4. stages the canonical source-`NONE` message at its retained receive buffer.

There is no notifier transition, notifier completion, reply authority, or
outbound message state.

If wake preflight fails, the queue, receiver, pending field, scheduler, and
output state remain byte-for-byte unchanged.

### Deferred injection and receive order

If no eligible receiver is consumed, commit ORs the mask into
`pending_kernel_events` and returns success. It never blocks.

For `receive(ANY)` and `reply_receive(..., ANY, ...)`, pending work selection
is:

1. complete `pending_kernel_events`;
2. lowest-slot pending live endpoint notification;
3. first compatible blocked sender.

The selected kernel mask is delivered in one envelope and the field is cleared
in the same commit.

A specific-source receive:

- ignores pending kernel events;
- retains existing exact endpoint resolution and selection;
- may block even while kernel events are pending.

The cross-class order remains an internal deterministic rule, not a public
ordering guarantee.

### Staged-delivery validation

A successful staged message with source `NONE` is valid only when:

- its type is the reserved kernel notification type;
- its reply token is zero;
- its event mask is nonzero;
- its payload tail is zero;
- its destination endpoint remains exact and active;
- the receiving thread otherwise has the canonical message-completion shape.

Any non-notification staged message with source `NONE` is invalid.

Endpoint-origin notifications continue to require one exact active source
endpoint generation. Reply-token and user-message validation remain
unchanged.

### Queue and registry invariants

Combined endpoint/object validation additionally requires:

- free and reserved endpoint records have zero pending kernel events;
- a queued `ANY` receiver outside reply wait cannot coexist with nonzero
  pending kernel events;
- a specific receiver does not make kernel events immediately matchable;
- pending kernel events do not set an endpoint-source bitmap bit or any
  `pending_events[slot]` entry;
- kernel-origin staged delivery has no source endpoint lifecycle dependency;
- every failure preserves the pending field, queues, completion bytes, and
  scheduler state.

Validation detects corruption and never repairs it.

### Endpoint close and future route ownership

Portable endpoint close treats pending kernel events like other undelivered
notifications owned by the destination: it clears them during the existing
failure-atomic cancellation commit.

A successfully staged kernel notification is an ordinary successful message
completion and must be drained before close, matching the existing close rule.

The future PLIC route must detach or fail its exact route and in-service state
before invoking ordinary endpoint close. ADR-0012 already makes an unexpected
IRQ-owner exit fatal during the shell MVP. This IPC operation does not weaken
that route-level invariant or silently complete hardware.

### Serialization and return behavior

The operation uses the existing one-hart external serialization contract. SIE
remains clear across validation, scheduler preflight, queue mutation, and
pending-state mutation.

If injection wakes a higher-priority receiver while a user thread is current,
the scheduler records the normal preemption state. The common selected-thread
return path owns eventual context selection, message copy, `a0` completion,
root activation, and register restore.

If injection occurs while no user thread is current, the existing idle-wake
return path owns selection. If a later PLIC interrupt originates in kernel
mode, interrupt return preserves the kernel continuation and defers user
selection to the next reschedulable boundary, as required by the established
trap model.

### Failure ordering

Recoverable checks occur in this order:

1. non-null registry/object arguments and nonzero mask;
2. complete combined registry/object validation;
3. exact active destination resolution;
4. canonical envelope construction;
5. receiver search and receiver-state validation;
6. scheduler wake preflight when immediate delivery is possible.

Only then may commit unlink and stage a receiver or OR pending bits.

Malformed destination values, stale generations, inactive endpoints, zero
masks, scheduler transition failures, and ordinary state errors return an
explicit existing portable error without mutation. Structural corruption
returns invariant and must not be converted into success.

The future target IRQ route classifies an impossible stale owner or injection
invariant according to ADR-0012; the injection primitive does not add a silent
drop or fallback destination.

### Resource bounds

The mechanism:

- adds one 64-bit field per endpoint record;
- allocates no memory;
- runs the existing complete fixed-capacity endpoint/object validator on every
  injection, including its endpoint-pair, thread-pair, token, and bounded queue
  scans while SIE is clear;
- scans at most `MICROS_THREAD_CAPACITY` receiver links;
- scans existing endpoint notification slots only during later receive
  selection;
- performs at most one scheduler wake transition per injection;
- retains no user pointer, physical address, PLIC claim, or route authority.

The whole-table validator cost is consciously accepted for the v0.1
one-hart, 64-endpoint bound because this first baseline prioritizes one
authoritative corruption check and failure-atomic transition. A narrower
prevalidated interrupt fast path is an optimization only after measured IRQ
latency justifies a separate reviewed design.

## Test-first evidence

### Native deterministic tests

The notification suite must prove:

- zero-mask, malformed, stale, reserved, and inactive destination rejection;
- exact complete-state preservation on every rejected injection;
- immediate wake of one blocked `ANY` receiver with source `NONE`;
- specific receivers remain blocked while kernel bits become pending;
- reply-wait exclusion;
- no notifier thread state or notifier completion is required or modified;
- repeated kernel masks OR-coalesce;
- `receive(ANY)` consumes the complete kernel mask before endpoint
  notifications and blocked senders;
- `reply_receive(..., ANY, ...)` atomically consumes its reply and then either
  receives a pending kernel mask or blocks normally;
- specific receive ignores pending kernel events;
- canonical type, zero token, little-endian mask, and zero payload tail;
- late scheduler failure preserves the complete registry and object state;
- validator rejection of source-`NONE` ordinary messages, zero event masks,
  bitmap aliasing, and matchable queued `ANY` receivers;
- endpoint close clears pending kernel bits but rejects a staged successful
  kernel notification until it is drained.

The replayable notification model and persistent 8,192-transition IPC model
add kernel injection, `ANY` consumption, specific-source blocking,
`reply_receive`, coalescing, and close transitions. They compare the new
field, queues, staged messages, scheduler state, and all existing
endpoint-origin notification state after every operation.

### QEMU IPC component

The existing trusted-message IPC component adds production mechanism evidence:

1. an exact destination thread blocks in `receive(ANY)`;
2. kernel-origin injection wakes it without a notifier thread;
3. common selected-thread return delivers source `NONE`, the reserved type,
   token zero, the exact event mask, and a zero tail;
4. repeated masks coalesce while the destination is not receiving;
5. a later `receive(ANY)` consumes the complete mask before queued endpoint
   work;
6. endpoint, thread, scheduler, root, stack, message, and frame baselines are
   restored.

The existing pass record is extended with:

```text
kernel-events=injected
```

No PLIC, UART, launcher, or TTY behavior is claimed by this gate.

### Validation ownership

Changes to the portable IPC implementation, endpoint record, scheduler IPC
transition use, or authoritative target runtime select:

- complete native tests including the notification model;
- `test-qemu-ipc`;
- existing IPC syscall and scheduler gates selected by the fail-closed
  validation planner;
- documentation and all three diff checks.

## Consequences

- The completed IPC mechanism can represent hardware-origin events without a
  fake process or user-visible pseudo-endpoint.
- Future PLIC routing reuses the canonical notification, queue, completion, and
  scheduler paths.
- One extra endpoint field separates kernel authority from generation-bound
  endpoint sources.
- User receive and syscall ABIs remain unchanged.
- Controller acknowledgment, owner installation, and exit policy remain
  explicit later work rather than being hidden inside IPC.

## Alternatives considered

### Create a synthetic live `HARDWARE` endpoint

Rejected. It would consume process identity, profile, lifecycle, and
generation state for a kernel mechanism and expose a targetable pseudo-process
to user IPC.

### Reserve one endpoint slot in `pending_events`

Rejected. Every implemented slot may hold a live generation, and aliasing
kernel authority with one process slot would make reuse and close semantics
ambiguous.

### Let user `notify` accept source `NONE`

Rejected. User `notify` derives source from the exact caller endpoint and
enforces profile authority. Allowing a caller-selected kernel source would be
an authority escalation.

### Deliver IRQs as ordinary messages

Rejected. Ordinary messages block or require a sender thread, do not coalesce,
and would import queueing and backpressure policy into interrupt handling.

### Implement PLIC routing in the same task

Rejected. PLIC ownership, in-service claims, explicit completion, manifest
authority, and console handoff have separate lifecycle and target acceptance
requirements under ADR-0012. Combining them would enlarge review and start
launcher/TTY policy before those DAG nodes are ready.

## Specification basis

- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0004: IPC and Endpoint ABI](0004-ipc-and-endpoint-abi.md)
- [ADR-0012: Console and IRQ Handoff](0012-console-and-irq-handoff.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0029: Endpoint and Privilege Substrate](0029-endpoint-and-privilege-substrate.md)
- [ADR-0030: MINIX-Baseline Blocking IPC](0030-minix-baseline-blocking-ipc.md)
- [ADR-0033: Reviewed Design Before Implementation](0033-reviewed-design-before-implementation.md)
- [MINIX IRQ notification study](../research/minix-irq-notification.md)
- [Development dependency DAG](../architecture/development-dag.md)

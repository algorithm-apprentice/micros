# ADR-0030: MINIX-Baseline Blocking IPC

- Status: Accepted
- Date: 2026-10-07
- Refines: ADR-0004 IPC operations, reply tokens, notifications, and deadlock

## Context

ADR-0029 defines exact endpoint identity, staged visibility, immutable
privilege profiles, and authorization queries. The next dependency-ready
mechanism is synchronous fixed-message IPC integrated with the completed
scheduler.

The MINIX baseline provides:

- blocking send;
- specific-source or `ANY` receive;
- combined send/receive request calls;
- FIFO blocked-sender queues;
- notification coalescing;
- blocked-chain deadlock detection;
- endpoint-exit cancellation.

`micros` must reproduce that behavior while preserving accepted differences:

- process, thread, and endpoint objects are distinct;
- a blocked operation belongs to an exact thread;
- reply authority is a one-shot token, not symmetric ordinary send permission;
- message payload pointers are data only;
- asynchronous sends and scatter/gather IPC remain deferred.

The canonical evidence is
[the MINIX endpoint and blocking IPC study](../research/minix-endpoint-and-ipc.md).

## Decision

### Scope

This slice defines:

- exact 64-byte message representation;
- endpoint sender and receiver queues;
- thread IPC run-time flags and saved operation state;
- blocking `send` and `receive`;
- atomic `call`;
- one-shot `reply`;
- failure-atomic `reply_receive`;
- coalesced `notify`;
- bounded deadlock detection;
- endpoint-close cancellation;
- RISC-V syscall registers and bootstrap buffer policy;
- native and QEMU acceptance gates.

It does not define:

- asynchronous send or nonblocking send;
- grants or safe copy;
- VM-assisted IPC-buffer faults;
- user-space service protocols;
- priority inheritance;
- signals, cancellation APIs, or timeouts;
- multiple public endpoints per process;
- SMP synchronization.

### Message representation

ADR-0004 remains authoritative:

```c
struct micros_ipc_message {
    uint32_t source;
    uint32_t type;
    uint64_t reply_token;
    uint8_t payload[48];
};
```

The type is 64 bytes, aligned to eight bytes, little-endian, and verified with
compile-time offset and size assertions.

For send-like operations, the kernel:

- ignores the user-provided source and reply-token fields;
- copies type and payload into kernel-owned storage before mutation;
- writes the exact live source endpoint;
- writes token zero except for a delivered `call` request.

For replies, the delivered source is the replying endpoint and the delivered
token is zero.

The high type bit remains reserved for kernel envelopes. Notification delivery
uses one reserved type, places the 64-bit event mask in payload bytes 0 through
7, and zeroes payload bytes 8 through 47.

No payload value authorizes memory access.

### Thread IPC state

The scheduler run-time word gains:

```text
MICROS_THREAD_RTS_IPC_SEND    = 1 << 3
MICROS_THREAD_RTS_IPC_RECEIVE = 1 << 4
MICROS_THREAD_RTS_IPC_REPLY   = 1 << 5
```

The existing zero/nonzero scheduler boundary remains authoritative.

Each live thread gains:

- one IPC queue link and exact queue-kind tag;
- saved outbound message;
- receive source endpoint or `ANY`;
- receive user-buffer virtual address;
- staged inbound message and result;
- optional outstanding reply token;
- exact callee endpoint for a reply wait.

A thread occurs in at most one endpoint sender or receiver queue. Reply wait
state is a run-time flag, not another intrusive queue.

Free and quarantined threads contain zero IPC state. Scheduler removal,
context detach, and thread release require no queue membership, no pending
delivery, and no active token.

### Endpoint IPC state

ADR-0029 endpoint records gain:

- blocked-sender head and tail thread handles;
- blocked-receiver head and tail thread handles;
- a 64-bit pending-notification source bitmap;
- one 64-bit pending event mask for every implemented endpoint slot.

The endpoint capacity is 64 in the current implementation, so the source
bitmap has one exact bit per endpoint slot.

Pending notification state is generation-safe because closing a source
endpoint clears that source slot from every destination before the process
slot can be reused. Delivery revalidates the complete source endpoint.

### Serialization and commit boundary

Portable IPC operations require external serialization. The one-hart target
holds SIE clear across:

- endpoint and profile resolution;
- message snapshot;
- authorization;
- deadlock preflight;
- queue and RTS mutation;
- reply-token mutation;
- scheduler return planning.

Every recoverable check occurs before the first state mutation. Once an
operation begins commit, only bounded stores and already validated scheduler
operations remain.

Handlers update IPC state but never restore a user context directly. Every
U-origin syscall converges on the common scheduler return selector.

### Sender and receiver queues

Blocked senders append to the destination endpoint sender tail.

Blocked receivers append to their own active endpoint receiver tail. A receiver
records either one exact source endpoint or `ANY`.

Queue handles carry complete thread generations. Validation requires:

- each linked thread resolves exactly;
- queue-kind and endpoint ownership agree;
- every sender has `IPC_SEND`;
- every receiver has `IPC_RECEIVE`;
- every tail has null next;
- every walk terminates within thread capacity;
- no thread appears in two queues.

When selecting a receiver for a sender, the kernel chooses the first FIFO
receiver whose source is `ANY` or the exact sender endpoint.

When a receiver searches pending work, it uses this baseline order:

1. first matching pending notification source by lowest endpoint slot;
2. first matching blocked sender in sender-queue order.

ADR-0004 still exposes no cross-class ordering guarantee to clients.

### `send`

`send(destination, message)` requires:

- current thread and exact active source endpoint;
- profile `send` operation and destination-profile authorization;
- exact active destination endpoint;
- readable, aligned 64-byte source message.

The kernel snapshots the message before queue mutation.

If a matching receiver is blocked:

1. stage the message on that exact receiver;
2. unlink the receiver;
3. clear its `IPC_RECEIVE` flag;
4. leave the sender runnable;
5. let the common return selector handle any priority preemption.

Otherwise:

1. run deadlock detection;
2. save the message and destination on the sender;
3. set `IPC_SEND`, removing a runnable current from its scheduler queue;
4. append the sender to the destination sender queue;
5. select another thread or idle through the common return path.

There is no nonblocking send in v0.1.

### `receive`

`receive(source, message)` requires:

- current thread and exact active destination endpoint;
- profile `receive` operation;
- source `ANY` or one exact active endpoint;
- writable, aligned 64-byte receive buffer.

The kernel records the user buffer only after complete validation.

If a matching notification is pending, it stages and consumes that source's
complete event mask.

Otherwise, if a matching sender is queued:

1. stage the sender's saved message;
2. unlink the sender;
3. clear only the sender's `IPC_SEND`;
4. retain `IPC_REPLY` when the sender is a `call`;
5. leave unrelated sender flags unchanged.

If no item is available:

1. run deadlock detection for a specific source; `ANY` has no dependency edge;
2. set `IPC_RECEIVE`;
3. append the receiver to its endpoint receiver queue;
4. select another thread or idle.

### `call`

`call(destination, message)` uses call authorization, not ordinary send
authorization.

Before mutation the kernel:

1. validates and snapshots the request;
2. records the validated in/out message buffer as the future reply buffer;
3. validates the destination and deadlock path;
4. allocates one nonzero reply token;
5. binds it to the exact caller thread handle and destination endpoint
   generation.

The token allocator is one monotonically increasing 64-bit counter. Zero is
never issued. Tokens are not reused; counter exhaustion returns an explicit
error before mutation. At most one token is active per thread.

If a receiver is ready, the request is delivered with the token and the caller
sets `IPC_REPLY`.

If no receiver is ready, the caller sets both `IPC_SEND` and `IPC_REPLY` and is
queued as a sender. When a receiver consumes the request, only `IPC_SEND`
clears.

Notifications and ordinary messages never clear `IPC_REPLY`. Only a valid
reply or endpoint cancellation completes the call.

### `reply`

`reply(token, message)` never blocks.

It requires:

- current thread and exact active replying endpoint;
- profile `reply` operation;
- one active token with exact value;
- token callee equal to the replying endpoint generation;
- exact live caller thread still carrying `IPC_REPLY`;
- readable, aligned reply message.

The kernel scans at most the fixed thread capacity to resolve the opaque token.
Token uniqueness is a registry invariant.

On success it:

1. stages the reply on the exact caller;
2. clears caller `IPC_REPLY`;
3. clears and consumes the token;
4. makes the caller runnable when no independent flag remains.

Failure preserves the token and every thread/queue/message byte.

The token is dynamic reply authority and bypasses the ordinary send
destination mask. It does not bypass the replier's `reply` operation bit.

### `reply_receive`

`reply_receive(token, reply_message, source, receive_message)` is one
failure-atomic operation.

Preflight validates:

- the complete reply exactly as `reply`;
- the replier's `reply_receive` operation bit;
- source endpoint or `ANY`;
- writable receive buffer;
- every immediate-notification or sender candidate that would be consumed;
- the receive deadlock graph as it will exist after the reply token is
  consumed and the caller is made runnable.

Commit then:

1. delivers and consumes the reply token;
2. immediately performs receive selection;
3. either stages one incoming item and remains runnable, or sets
   `IPC_RECEIVE` and queues the server;
4. invokes the common return selector once.

No observer can see a consumed reply token while the server has not yet
committed its receive transition.

### `notify`

`notify(destination, event_mask)` requires:

- nonzero event mask;
- current process's exact active endpoint;
- profile `notify` operation and destination-profile authorization;
- exact active destination endpoint.

It never blocks.

If the first matching receiver is waiting for the source or `ANY`, and that
thread is not carrying `IPC_REPLY`, the kernel stages an immediate notification
and wakes it.

Otherwise the kernel ORs the event mask into:

```text
destination.pending_events[source_slot]
```

and sets the source bit. Repeated events from one source coalesce.

A thread blocked in `call` cannot consume a notification as its reply. With
the v0.1 one-thread process policy, the notification remains pending until the
thread later performs receive.

This syscall path covers live endpoint sources. The later IRQ-routing task adds
an internal kernel-notification injection that uses the same envelope and event
mask with source `NONE`; it does not add a user-callable endpoint or change the
IPC syscall ABI.

### Deadlock detection

Deadlock is checked only after matching delivery has failed and before any
blocking mutation.

The v0.1 dependency of one blocked thread is:

1. `IPC_SEND`: saved destination endpoint;
2. otherwise `IPC_REPLY`: saved callee endpoint;
3. otherwise specific `IPC_RECEIVE`: saved source endpoint;
4. otherwise none.

This precedence mirrors MINIX `SENDREC` sending-before-receiving behavior.

The walk:

- resolves every endpoint and process generation exactly;
- resolves the endpoint owner's sole live thread through the checked v0.1
  one-thread policy;
- stops at no dependency or `ANY`;
- rejects a return to the candidate caller;
- rejects repeated intermediate threads as corruption;
- stops after at most `MICROS_THREAD_CAPACITY` steps.

Because send/receive matching is attempted first, a complementary two-party
send/receive pair is delivered rather than represented as a blocked cycle.
`micros` therefore does not reproduce MINIX's function-bit special case.

Future multithreading replaces only this internal one-thread dependency
projection with wait-set exploration. Endpoint, thread, token, and syscall
ABIs remain unchanged.

### Endpoint close and cancellation

Closing an endpoint runs with SIE clear and performs one bounded cancellation
transaction:

1. preflight every queue, token, notification, and exact-generation reference;
2. unlink every closing-process thread from any sender or receiver queue;
3. unlink and wake every other blocked sender queued on it with
   `DEAD_ENDPOINT`;
4. clear every receiver owned by the closing endpoint;
5. scan all blocked specific receivers and reply waits that name the closing
   endpoint, waking them with `DEAD_ENDPOINT`;
6. cancel every token whose caller or callee belongs to the closing process;
7. clear pending notifications from and to the closing endpoint slot;
8. verify all endpoint/thread IPC state is detached;
9. invoke ADR-0029 endpoint close, atomically making the endpoint non-active.

`ANY` receivers belonging to other endpoints remain blocked.

After preflight, commit contains no recoverable operation. The close transition
does not report success while any exact-generation queue or token reference
remains.

### User-buffer ownership and bootstrap failure

Outgoing messages are copied from the current process before mutation.
Incoming messages remain in a kernel-owned staged buffer until the receiving
thread is selected to return.

The target layer validates:

- canonical user address;
- eight-byte alignment;
- complete 64-byte readable or writable mapping;
- exact process-generation address-space ownership.

The common return preflight copies a staged inbound message through the
existing process-root physical lookup before scheduler commit.

Before VM fault delivery is dependency-ready, an IPC buffer that was valid at
syscall entry but becomes invalid before return is fatal for the trusted
bootstrap process with reason `invalid-bootstrap-ipc-buffer`. Immediate invalid
arguments return `FAULT` without mutation. A later fault ADR replaces only the
fatal bootstrap policy.

### RISC-V syscall ABI

U-mode invokes IPC through `ecall` with:

```text
a7  operation
a0  endpoint or reply token
a1  primary message pointer or notification event mask
a2  reply_receive source endpoint
a3  reply_receive receive-message pointer
```

Operation numbers are:

```text
1 SEND
2 RECEIVE
3 CALL
4 REPLY
5 REPLY_RECEIVE
6 NOTIFY
```

Arguments:

| Operation | a0 | a1 | a2 | a3 |
| --- | --- | --- | --- | --- |
| SEND | destination | message | zero | zero |
| RECEIVE | source or `ANY` | message | zero | zero |
| CALL | destination | in/out message | zero | zero |
| REPLY | token | message | zero | zero |
| REPLY_RECEIVE | token | reply message | source or `ANY` | receive message |
| NOTIFY | destination | event mask | zero | zero |

Unused registers must be zero. Unknown operations or nonzero unused arguments
fail without mutation.

The kernel advances `sepc` by one 32-bit `ecall` instruction only after syscall
argument capture. `a0` returns zero on success or one negative project-defined
error. Other GPRs retain ordinary context semantics.

### Errors

The portable layer distinguishes at least:

- argument;
- stale/dead endpoint;
- unauthorized operation;
- invalid state;
- not ready/internal queue corruption;
- deadlock;
- message fault;
- reply-token invalid;
- reply-token exhausted;
- endpoint closing;
- invariant.

The target syscall layer maps them to stable negative ABI values. No operation
returns success after partial mutation.

## Test-first evidence

### Native deterministic tests

Tests must cover:

- exact message layout and source/token overwrite;
- immediate and blocked send;
- specific and `ANY` receive;
- FIFO matching with unmatched senders preserved;
- every zero/nonzero RTS transition;
- call request queued and immediately delivered;
- token uniqueness, exact-thread routing, one-shot consumption, and stale
  callee rejection;
- reply without ordinary send permission;
- failure-atomic `reply_receive`;
- notification immediate delivery, source coalescing, lowest-slot selection,
  and call-reply exclusion;
- unauthorized call/send/notify/reply;
- every deadlock chain length through capacity;
- endpoint close queue/token/notification cancellation;
- stale queue handles and generations;
- complete state and output preservation on every failed operation.

### Seeded model

A replayable model uses at least three processes and multiple model threads per
process. Reply-token cases exercise those multiple threads; deadlock cases
retain the checked v0.1 one-live-thread dependency projection. The model mixes
all operations, endpoint close/reuse, profile denial, and malformed input for
at least 8,192 transitions.

It compares:

- thread RTS and IPC fields;
- endpoint sender/receiver queues;
- pending notification masks;
- active tokens;
- staged messages and results;
- scheduler queue/current state after wakeups.

Failures print the seed, operation, endpoint/thread generations, and complete
queue/token trace.

### QEMU component tests

Trusted payloads in at least three address spaces must prove:

1. immediate send to a specific blocked receiver;
2. blocked send and exact wakeup;
3. `ANY` and specific receive selection;
4. call request plus exact token reply;
5. token reuse rejection;
6. `reply_receive` reply plus atomic block/next request;
7. coalesced notification delivery;
8. unauthorized and stale endpoint rejection;
9. deterministic deadlock rejection;
10. endpoint close waking exact dependents;
11. complete register, root, stack, scheduler, and message preservation.

The success image emits:

```text
MICROS_IPC_TEST_PASS endpoints=generation-safe queues=blocking calls=tokenized notifications=coalesced deadlock=rejected
```

Expected-panic images remain isolated for impossible queue corruption or the
trusted-bootstrap invalid-buffer policy.

## Consequences

- The first IPC mechanism reproduces MINIX blocking and wakeup behavior.
- Thread blocking integrates directly with the existing scheduler RTS model.
- One-shot tokens preserve reply authority without broad reverse send masks.
- Notification delivery is bounded and allocation-free.
- Fixed kernel message snapshots avoid retaining sender pointers.
- The one-thread deadlock projection is explicit policy, not process/thread
  structural equivalence.
- Endpoint close becomes more expensive because it scans bounded thread and
  endpoint tables, but exit is not a fast path.
- VM-assisted message-buffer faults and bulk data remain later work.

## Alternatives considered

### Use ordinary send for replies

This reproduces MINIX more literally but requires broad reverse send
permission and cannot route concurrent future calls to exact caller threads.

### Add asynchronous queued messages now

This requires buffering limits and backpressure policy before the synchronous
state machine is stable. It is outside the shell MVP.

### Use one endpoint queue for both senders and receivers

The queue members represent different blocking contracts and matching rules.
Separate queues keep validation and wakeup ownership explicit.

### Encode thread identity directly in reply tokens

The token is intentionally opaque. A monotonic value plus exact kernel-owned
binding avoids exposing internal handle layout and makes stale use explicit.

### Let notifications satisfy call reply waits

MINIX explicitly suppresses notifications during `SENDREC` reply wait.
Allowing it would make request completion ambiguous and violate one-shot reply
authority.

### Copy directly between user buffers

The fixed message is small. Kernel-owned snapshots simplify blocking,
generation checks, failure atomicity, and later fault handling.

## Specification basis

- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0004: IPC and Endpoint ABI](0004-ipc-and-endpoint-abi.md)
- [ADR-0010: Testing and Observability](0010-testing-and-observability.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0026: MINIX-Baseline Kernel Scheduler](0026-minix-baseline-kernel-scheduler.md)
- [ADR-0029: Endpoint and Privilege Substrate](0029-endpoint-and-privilege-substrate.md)
- [MINIX endpoint and blocking IPC study](../research/minix-endpoint-and-ipc.md)
- [Development dependency DAG](../architecture/development-dag.md)

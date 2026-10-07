# ADR-0036: Reply Authority Begins at Request Return

- Status: Accepted
- Date: 2026-10-07
- Refines: ADR-0030 and ADR-0035

## Context

ADR-0030 binds each nonzero reply token to one exact caller thread and callee
endpoint generation. The callee endpoint is process-owned, so reply authority
is not permanently bound to the particular callee thread that receives the
request.

ADR-0035 requires an inbound message to remain in a kernel-owned completion
until the receiving thread is selected to return. Selecting or running another
thread must not consume or alter that deferred completion.

The existing portable reply path permits this sequence:

1. caller `C` issues `call` with token `T`;
2. the token-bearing request is staged on callee thread `R`;
3. before `R` returns, another held thread `P` owned by the same callee
   endpoint presents `T`;
4. the reply consumes the active caller binding while `R` still retains the
   staged request containing `T`.

No legitimate callee execution can obtain `T` from that request before the
completion returns. Permitting step 3 therefore relies on guessing the
monotonic token and creates a state that cannot be validated without adding
separate historical call provenance. The current endpoint validator correctly
requires every nonzero staged token to retain its exact active caller binding.

MINIX has no token analogue. Its server observes the completed receive and
then performs an ordinary authorized reply send. The `micros` token is a
compatible least-privilege extension, so this ADR fixes when that dynamic
authority becomes presentable while preserving process-owned endpoint
authority after delivery.

## Decision

### Scope

This outcome defines:

- the request-return boundary at which a callee may present a reply token;
- portable `reply` and `reply_receive` preflight for an undelivered request;
- staged call-token uniqueness validation and unchanged deferred-completion
  bytes;
- native and model acceptance evidence for multithreaded callees.

It does not change:

- the public message layout or syscall ABI;
- token allocation, monotonicity, opacity, or one-shot use;
- exact caller-thread and callee-generation binding;
- reply-operation or reply/receive-operation authorization;
- the thread that may ultimately reply after request return;
- endpoint close or cancellation behavior;
- the v0.1 one-live-thread target policy.

### Authority boundary

The authoritative reply right remains the live caller thread state:

- nonzero `ipc_reply_token`;
- exact `ipc_reply_callee`;
- exact `IPC_REPLY` run-time flag;
- canonical reply-wait state.

Call commit creates that binding before the caller blocks. The binding routes a
future reply but is not yet presentable by the callee while the only copy of
its numeric value remains in kernel-owned queued or staged request state.

The token becomes presentable when the token-bearing request completion is
consumed by the shared selected-thread return path. That commit copies the
request to the validated user buffer and clears the completion atomically.

After request return, any held thread owned by the exact callee endpoint and
authorized for `reply` or `reply_receive` may present the token. The token is
not bound to the receiving thread, and the kernel does not track user-space
intra-process handoff.

### Reply preflight

The existing active-token, caller, callee, authorization, message, and
scheduler checks remain authoritative.

After resolving the exact active token and before scheduler or IPC mutation,
`reply` and `reply_receive` perform one bounded live-thread scan for a pending
successful inbound message carrying that token.

If such a completion exists:

- endpoint/object validation has already established that it is the one
  canonical staged call request bound to the same caller and callee;
- the operation returns `MICROS_IPC_ERROR_REPLY_TOKEN`;
- token, caller, replier, queues, messages, completions, outputs, and scheduler
  state remain byte-for-byte unchanged.

If no completion carries the token, normal reply preflight and commit continue.

A queued call request is already rejected because its caller still carries
`IPC_SEND | IPC_REPLY` rather than canonical reply-only wait. This ADR adds
only the staged-but-not-returned case.

The same rule applies to `reply_receive` before any notification, sender,
deadlock, queue-tail, or scheduler planning. No combined operation may consume
the reply and then discover that the request token was not yet delivered.

### Validation and completion state

Endpoint validation remains strict:

- a nonzero token in a staged successful request must have exactly one active
  canonical caller binding;
- at most one live staged successful request may carry a given nonzero token;
- the staged source and receiver owner must match that binding;
- token zero remains required for ordinary sends, notifications, and replies;
- duplicate or stale token bindings remain invariant corruption.

No valid state contains a consumed token in an undrained request. Reply
preflight prevents that transition, so no token-history field, completion-kind
tag, or consumed-token ledger is added.

ADR-0035 completion stability remains unchanged. Another thread's failed early
reply does not rewrite, clear, or otherwise consume the deferred request.

### Teardown and failure ordering

Existing endpoint-close rules remain authoritative. A staged request continues
to block closing its source or receiving endpoint until it drains. An active
token continues to be canceled only by successful reply or endpoint
cancellation.

Endpoint/object validation occurs before the undelivered-token scan. Corrupt
staged state therefore returns the existing invariant error rather than a
reply-token error.

Every new recoverable check precedes the first mutation. No rollback path or
new diagnostic is required.

### Serialization and bounds

No persistent state or synchronization boundary is added. The scan executes
under the existing serialized portable IPC operation.

`reply` and `reply_receive` remain bounded by `MICROS_THREAD_CAPACITY`. The
additional operation scan is linear and allocation-free. Staged-token
uniqueness uses another bounded thread-table scan inside endpoint validation
and does not change that validator's existing worst-case asymptotic bound.

## Test-first evidence

The implementation Red state is a native multithreaded-callee regression:

1. stage one call request with token `T` on receiver thread `R`;
2. use a distinct held thread `P` owned by the same callee endpoint to attempt
   `reply(T, ...)`;
3. require `MICROS_IPC_ERROR_REPLY_TOKEN` and byte-exact state preservation;
4. require endpoint/object validation to remain successful;
5. drain `R`, preserving the delivered request and token value;
6. use `P` to reply successfully with `T`;
7. verify canonical caller completion, one-shot token consumption, and reuse
   rejection.

Equivalent coverage must prove that early `reply_receive` fails before any
receive-side selection or mutation and succeeds after request return.

Native corruption coverage continues to reject:

- a staged nonzero token without one active caller binding;
- a staged token bound to a different source or callee;
- two staged successful requests carrying the same nonzero token;
- duplicate active token ownership;
- nonzero tokens on ordinary staged messages.

The persistent IPC model makes reply-action selection ignore active tokens
whose request completion is still pending. A deterministic prelude case
directly attempts the early reply and checks the reply-token error before
draining the request. The model then completes all 8,192 transitions with full
state comparison.

No new QEMU scenario is required because the target intentionally enforces one
live thread per process in v0.1. The implementation runs the complete native
IPC gate and the affected existing QEMU IPC regression selected by the
validation planner.

## Consequences

- Reply authority remains process-owned after the request reaches userspace.
- A guessed monotonic token cannot complete a call before its request returns.
- Deferred completion bytes remain unchanged, while validation additionally
  rejects duplicate staged delivery of one call token.
- No provenance or token-history state is added for a target policy that does
  not yet expose multiple live threads.
- Future multithreaded servers may hand a delivered token to any authorized
  sibling thread in user space.

## Alternatives considered

### Permit early reply and retain consumed-token provenance

Rejected because it adds completion-kind and historical token/source state to
support behavior that legitimate user execution cannot initiate.

### Permit an unbound consumed token in the staged request

Rejected because issued-token range and active endpoints cannot prove that the
token originated from that call. It would weaken corruption detection.

### Clear the token inside the staged request when replying

Rejected because ADR-0035 requires a deferred completion to remain unchanged
until the receiving thread is selected to return.

### Bind reply authority permanently to the receiving thread

Rejected because endpoints are process-owned and future multithreaded servers
must be able to hand a delivered request to another authorized worker.

## Specification basis

- [ADR-0004: IPC and Endpoint ABI](0004-ipc-and-endpoint-abi.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0029: Endpoint and Privilege Substrate](0029-endpoint-and-privilege-substrate.md)
- [ADR-0030: MINIX-Baseline Blocking IPC](0030-minix-baseline-blocking-ipc.md)
- [ADR-0035: RISC-V IPC Syscall and Bootstrap Buffers](0035-riscv-ipc-syscall-and-bootstrap-buffers.md)
- [MINIX endpoint and blocking IPC study](../research/minix-endpoint-and-ipc.md)

# ADR-0035: RISC-V IPC Syscall and Bootstrap Buffers

- Status: Accepted
- Date: 2026-10-07
- Refines: ADR-0030

## Context

ADR-0030 fixes the six-operation RISC-V register ABI and requires user-message
snapshot, staged return copy, and convergence through the common scheduler
selector.

The portable IPC implementation is complete before this boundary, but its
operations deliberately consume a scheduler-held, non-current thread. Current
U-mode trap entry instead:

1. accounts the running thread into kernel time;
2. captures the trap frame into the current thread object;
3. handles timer/test-specific traps;
4. panics for an ordinary production U-mode exception.

The current user-address-space lookup API accepts page-aligned virtual
addresses. IPC buffers are only eight-byte aligned and may cross one page
boundary. The current return selector also assumes a non-null outgoing current
thread and has no staged IPC-copy preflight.

The syscall adapter must bridge those mechanisms without weakening queue,
token, generation, scheduler, or failure-atomicity invariants.

## Decision

### Scope

This slice defines:

- production U-mode `ecall` routing for the six ADR-0030 operations;
- stable negative syscall error values;
- a current-thread IPC scheduler guard with rollback;
- arbitrary-address user translation and bounded two-page copy;
- outbound snapshot and inbound staged-copy rules;
- syscall completion state consumed at user return;
- a captured-context return selector for IPC;
- normal and expected-panic QEMU syscall gates.

It does not define:

- VM-assisted user-buffer fault delivery;
- retry or restartable syscalls;
- asynchronous IPC;
- IRQ-origin notification injection;
- grants or safe copy;
- user-space service protocols;
- multihart synchronization.

### Authoritative target IPC registry

The target kernel owns one static endpoint/IPC registry for the complete boot
lifetime. Portable host tests may continue to pass caller-owned registries,
but target bootstrap, endpoint lifecycle, IPC operations, close cancellation,
return completion, and trap dispatch all use the same authoritative storage.

The target runtime exposes:

```c
micros_ipc_runtime_initialize(profiles, profile_count);
micros_ipc_runtime_registry();
micros_ipc_runtime_validate();
```

Initialization:

- requires the kernel object runtime and one-hart serialization to be ready;
- copies one caller-provided immutable profile table through the existing
  failure-atomic registry initializer;
- is one-shot and has no reset;
- publishes readiness only after combined registry/object validation succeeds;
- preserves every runtime byte on failure.

The static bootstrap launcher will later provide the production profile table
before reserving or activating any user endpoint. Isolated endpoint, portable
IPC, and syscall QEMU images provide their own static const test profiles
through the same runtime initializer; they do not instantiate a second target
registry.

An internal mutable accessor is available only to serialized kernel lifecycle,
IPC, and trap code. Public inspection returns const storage. Both return null
before readiness.

A U-mode IPC `ecall` before runtime initialization is impossible in a valid
boot graph and causes invariant panic `ipc-runtime-not-initialized`; it is not
a user-visible `STATE` error. Runtime validation requires exact agreement with
the authoritative kernel object registry.

### Syscall numbers and registers

ADR-0030 remains authoritative:

```text
a7  operation
a0  endpoint or reply token
a1  primary message pointer or notification event mask
a2  reply_receive source endpoint
a3  reply_receive receive-message pointer
```

Operations remain:

```text
1 SEND
2 RECEIVE
3 CALL
4 REPLY
5 REPLY_RECEIVE
6 NOTIFY
```

The handler captures `a0` through `a3` and `a7` from the trapped frame before
any mutation. After that capture, it advances saved `sepc` by exactly four
bytes for the 32-bit `ecall`, including requests later rejected as invalid.

Unused arguments must be zero exactly as defined by ADR-0030. Values used as
endpoints must fit the unsigned 32-bit endpoint ABI; upper bits are rejected.
Reply tokens and notification event masks retain all 64 bits.

Only `a0` and `sepc` change as syscall return state. Every other saved GPR
retains its captured value.

### Stable syscall results

`a0` returns these signed RV64 values:

```text
  0  MICROS_IPC_ABI_OK
 -1  MICROS_IPC_ABI_ARGUMENT
 -2  MICROS_IPC_ABI_DEAD_ENDPOINT
 -3  MICROS_IPC_ABI_UNAUTHORIZED
 -4  MICROS_IPC_ABI_STATE
 -5  MICROS_IPC_ABI_DEADLOCK
 -6  MICROS_IPC_ABI_MESSAGE_FAULT
 -7  MICROS_IPC_ABI_REPLY_TOKEN
 -8  MICROS_IPC_ABI_REPLY_TOKEN_EXHAUSTED
 -9  MICROS_IPC_ABI_ENDPOINT_CLOSING
```

Portable `NOT_READY` is internal and must not cross the syscall boundary.
Portable invariant, scheduler invariant, or an unexpected portable error is a
kernel panic with a stable diagnostic, not a user-visible error.

### Current-thread IPC guard

The target adapter adds one bounded scheduler transaction:

```c
micros_scheduler_begin_current_ipc(..., guard);
micros_scheduler_rollback_current_ipc(..., guard);
micros_scheduler_commit_current_ipc(..., guard);
```

Begin requires:

- one exact registered hart;
- accounting owner `KERNEL` after U-trap accounting;
- one exact current thread;
- current runtime flags zero;
- current thread at the head of its scheduler-ready priority queue;
- attached execution context and valid scheduler policy.

Begin preflights every mutation, then:

1. removes the current thread from the ready queue;
2. sets exact `INACTIVE`;
3. clears `hart.current_thread`;
4. selects the hart's immutable idle primary trap stack so the no-current hart
   remains a valid scheduler object;
5. records the exact hart, thread, priority, thread trap-stack range, and
   rollback position in a
   one-use guard.

Portable IPC can then use its existing held/non-current contract.

On portable failure, rollback requires that the IPC operation preserved all
state. It:

1. clears `INACTIVE`;
2. restores the thread at the head of its original priority queue;
3. restores `hart.current_thread`;
4. restores the guarded thread's primary trap-stack range;
5. consumes the guard.

On portable success, commit validates the post-operation thread state and
consumes the guard without selecting a return thread. The current pointer
remains null and the idle primary stack remains selected; common return
machinery later changes current ownership and the primary stack together.

Portable immediate operations may temporarily re-enqueue the guarded caller at
its priority tail. If guard commit finds the caller runnable, it preflights and
relocates that exact caller from its temporary position to the head of its
original priority queue. This preserves its pre-syscall candidacy against
same-priority peers while still allowing a genuinely higher-priority wakeup to
win common selection. A blocked caller remains non-runnable and is not
relocated.

No error after begin may return to userspace without either rollback or commit.

### User-buffer translation

The user-address-space layer adds an arbitrary-address translation query that
does not change the existing page-aligned allocation/lookup ABI:

```c
micros_user_address_space_translate(
    process,
    user_address,
    physical_address,
    permissions,
    contiguous_bytes
);
```

It requires:

- an exact live process generation with a prepared address space;
- canonical address in `[MICROS_USER_VIRTUAL_BASE,
  MICROS_USER_VIRTUAL_END)`;
- a valid user leaf owned by that process generation.

It returns the exact physical byte address, access permissions, and bytes
remaining in the current page. It never returns pointer authority to another
process.

IPC message buffers require:

- nonzero address;
- eight-byte alignment;
- no `address + 64` overflow;
- complete range inside the user window;
- every covered page readable for outbound snapshot;
- every covered page writable for inbound copy;
- both readable and writable for `CALL`.

A 64-byte buffer spans at most two pages. All pages and permissions are
preflighted before the first copied byte. The kernel copies through its existing
physical mapping in bounded chunks and never dereferences the user virtual
address directly.

### Operation preflight and snapshot

The target syscall layer validates operation shape and buffers before beginning
the current-thread guard.

It snapshots exactly one outbound message for:

- `SEND`;
- `CALL`;
- `REPLY`;
- the reply half of `REPLY_RECEIVE`.

`RECEIVE`, `CALL`, and the receive half of `REPLY_RECEIVE` preflight writable
buffers. `NOTIFY` requires a nonzero event mask and no message buffer.

The snapshot ignores user-provided source and token authority; portable IPC
canonicalizes those fields as already specified.

The portable `CALL` boundary is refined to separate data from retained user
authority:

```c
micros_ipc_call(
    registry,
    objects,
    caller,
    destination,
    const struct micros_ipc_message *request_snapshot,
    uintptr_t reply_user_address
);
```

The request pointer always names kernel-owned snapshot storage. Only the
separately validated user virtual address is retained for the eventual reply.
Existing host callers pass their trusted buffer address explicitly. No portable
operation infers a user return address from a kernel message pointer.

After target preflight, begin-guard and portable IPC form one target
transaction:

- target validation failure returns without scheduler or IPC mutation;
- portable failure rolls back the guard and returns the mapped error;
- portable success commits the guard and enters common return selection.

### Completion state

Thread IPC state uses one explicit pending syscall completion rather than
treating every wake as a message delivery.

A completion contains:

- pending bit;
- portable result;
- optional inbound message;
- optional retained receive-buffer virtual address.

Canonical shapes are:

1. no-message success: result `OK`, zero message, zero buffer;
2. message success: result `OK`, canonical message, nonzero aligned buffer;
3. no-message failure: explicit portable error, zero message, zero buffer.

The operation postconditions are:

| Operation state | Completion |
| --- | --- |
| immediate `SEND` | caller no-message `OK` |
| blocked `SEND`, when consumed | sender no-message `OK` |
| immediate or blocked `RECEIVE`, when data arrives | receiver message `OK` |
| `CALL` request consumed | no caller completion; caller remains `IPC_REPLY` |
| `CALL`, when reply arrives | caller message `OK` |
| immediate `REPLY` | replier no-message `OK` |
| `REPLY_RECEIVE` reply plus immediate receive | caller message `OK`; replier message `OK` |
| `REPLY_RECEIVE` reply plus blocked receive | caller message `OK`; no replier completion until input arrives |
| immediate `NOTIFY` or coalescing | notifier no-message `OK` |
| endpoint cancellation | affected waiter no-message `DEAD_ENDPOINT` |

Guard begin requires no pre-existing completion on the caller. Target code
stages immediate no-message completion after portable success. Portable sender
dequeue stages ordinary blocked-send completion. Existing message delivery,
reply, notification, and cancellation commits stage their table-defined
completion before making a thread runnable.

Completion state has no endpoint or physical-address authority beyond the
validated canonical message source and retained user virtual address.

### Shared selected-thread return

Completion handling becomes part of the shared selected-thread return
machinery used by every path that can install a user context:

- ordinary U-trap/timer return with a captured outgoing frame;
- IPC return after a committed current-thread guard and already captured
  context;
- idle wake return.

The entry modes differ only in whether an outgoing frame must be validated and
stored before planning. Selected-thread validation, completion handling,
timer/accounting preparation, scheduler commit, root activation, and context
installation use one common sequence.

The shared machinery:

1. plans the next runnable thread or idle without mutation;
2. validates the selected thread and address-space root;
3. preflights any pending IPC completion;
4. loads and validates the exact selected context image that will be installed;
5. preflights timer programming and kernel accounting;
6. performs one non-failing commit sequence:
   1. bounded staged copy when a message buffer exists;
   2. patch the prevalidated selected context image `a0`;
   3. store that same patched image into the thread object;
   4. clear the consumed completion state;
   5. apply the scheduler plan;
   6. install the same patched context image and selected root, or enter idle.

For no-message completion, no user-memory lookup occurs.

A completion remains attached to its thread until whichever future return path
actually selects that thread. Selecting another runnable thread does not
consume or alter the deferred completion.

For message completion, return preflight revalidates the exact process
generation, address-space ownership, complete writable range, and permissions.
If the trusted bootstrap mapping is no longer valid, the kernel panics before
scheduler-plan mutation with:

```text
MICROS_PANIC reason=invalid-bootstrap-ipc-buffer
```

This is not a recoverable syscall error. A later VM fault ADR may replace only
this bootstrap policy.

Timer and accounting preparation occur before user-memory, context, completion,
or scheduler mutation. After they succeed, every commit operation above is a
bounded prevalidated store or a target activation whose failure is fatal.

### Trap routing

U-mode trap entry keeps the existing order:

1. account thread-to-kernel transition;
2. capture the complete user frame;
3. route timer and target-test traps;
4. route user `ecall` cause `8` to IPC in production and the dedicated IPC
   syscall image;
5. panic on every other unexpected U-mode trap.

Existing scheduler and user-execution test images retain their private `ecall`
handling and are not intercepted by production IPC dispatch.

SIE remains clear from trap entry through argument capture, buffer preflight,
guard transition, portable IPC, and return planning.

### Error and output preservation

Before guard begin, every recoverable failure preserves:

- thread and endpoint IPC bytes;
- scheduler queues/current/accounting;
- saved context except the captured `sepc + 4` and final `a0` error;
- user message bytes.

After guard begin, portable failure plus rollback restores scheduler and IPC
state exactly before writing the final saved error result.

Outbound snapshot never changes user memory. Inbound copy occurs only for a
selected runnable thread during return preflight.

Address-space adapter results are classified exhaustively:

| Address-space result | Syscall behavior |
| --- | --- |
| range, noncanonical, overflow, absent mapping, or insufficient read/write permission caused by the supplied user buffer | `MICROS_IPC_ABI_MESSAGE_FAULT` before guard begin |
| stale current process, missing prepared root, foreign ownership, malformed PTE, scratch busy/reentrancy, uninitialized runtime, or address-space invariant | invariant panic |
| lost range, mapping, or permission for a previously accepted staged return buffer | `invalid-bootstrap-ipc-buffer` panic |
| malformed completion, foreign staged source, or structural ownership corruption at return | invariant panic |

### Tests

Native target-boundary tests cover:

- one-shot authoritative runtime initialization, failed-init preservation, and
  pre-initialization access rejection;
- all operation and unused-register shapes;
- upper endpoint bits and 64-bit token/event preservation;
- exact error mapping;
- current-IPC guard begin, rollback, and commit;
- no-message, message, and failure completion validation;
- page-local and cross-page translation/copy;
- alignment, overflow, range, absent, permission, stale-process, and ownership
  rejection;
- complete state/output preservation on every recoverable failure.

The normal QEMU IPC syscall image uses at least three real address spaces and
trusted U-mode payloads that execute actual `ecall` instructions. It proves:

- immediate and blocked `SEND`/`RECEIVE`;
- `CALL`, exact-token `REPLY`, and `REPLY_RECEIVE`;
- notification coalescing and call-reply exclusion;
- cross-page message snapshot and return copy;
- stable negative errors;
- preserved non-result registers;
- context switching and blocked wakeups through the common selector;
- a completion-bearing thread deferred behind another runnable thread and
  later completed through timer or idle return;
- same-priority immediate operations retaining caller head position, with
  higher-priority wakeups still preempting;
- final root, stack, context, scheduler, message, ownership, and object
  baseline restoration.

It emits one exact pass record distinct from the pre-syscall portable IPC gate.

The existing endpoint and pre-syscall IPC QEMU images are migrated to the same
authoritative target runtime and prove that bootstrap lifecycle and trap
dispatch observe one registry instance.

An isolated expected-panic image invalidates a previously accepted receive
buffer before return and requires the exact
`invalid-bootstrap-ipc-buffer` diagnostic before any success marker.

## Consequences

- Portable IPC remains independent of RISC-V registers and user mapping.
- Target IPC uses existing generation, queue, token, and scheduler authority.
- The scheduler gains a reversible current-thread guard and a captured-context
  return path.
- User buffer authority is revalidated at return, not retained as a physical
  pointer.
- Bootstrap mapping loss is fatal until VM fault delivery is designed.
- The target adapter adds bounded page and thread operations only.

## Alternatives considered

### Let portable IPC accept the current runnable thread

Rejected because it would mix RISC-V trap/current semantics into portable queue
and token operations and weaken their host-testable contract.

### Copy through the user virtual pointer with SUM enabled

Rejected because it trusts the active root and live SUM state, complicates
cross-page atomicity, and bypasses exact process-generation ownership.

### Store physical receive-buffer authority while blocked

Rejected because mappings can change and physical authority must be
revalidated at return.

### Delay `sepc` advancement until syscall success

Rejected because rejected syscalls would trap repeatedly on the same `ecall`.

### Return a recoverable fault when a staged buffer disappears

Deferred until VM fault delivery is dependency-ready. The v0.1 bootstrap policy
requires a stable fatal diagnostic.

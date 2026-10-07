# ADR-0041: Unified RISC-V Grant Syscalls

- Status: Accepted
- Date: 2026-10-08
- Refines: ADR-0035, ADR-0038, ADR-0039, and ADR-0040

## Context

`micros` now has:

- one production RISC-V `ecall` path for six IPC operations;
- an authoritative kernel-managed direct-grant registry;
- generation-safe create, revoke, cancellation, and validation;
- page-bounded `copy_from` and `copy_to`;
- exact `PROCESS_USER` translation before handoff;
- exact `VM_WIRED` translation after handoff.

The next user-visible prerequisite is a stable grant syscall boundary. The
freestanding runtime cannot wrap grant operations until operation numbers,
registers, results, dispatch ownership, and failure semantics are fixed.

MINIX exposes direct IPC traps, creates and revokes individual grants in a
user-managed table, and invokes safe copy through separate libsys kernel-call
messages. `micros` deliberately has no mutable user grant table, hosted libc,
`errno`, or second kernel-call message protocol. The canonical evidence is
[the MINIX user grant and safe-copy syscall study](../research/minix-user-grant-syscall-boundary.md).

This decision defines only the kernel/user ABI and target acceptance boundary.
The next design task adds the freestanding C runtime, raw assembly stub,
startup, linker, and service-facing wrappers.

## Decision

### Scope

This slice defines:

- one unified RISC-V syscall operation namespace;
- grant create, revoke, copy-from, and copy-to operation numbers;
- exact `a0` through `a7` register layouts;
- stable generic syscall result values;
- one top-level user-`ecall` dispatcher;
- grant operation shape and width validation;
- exact mapping from portable grant results to user ABI results;
- current-process authority and nonblocking scheduler behavior;
- native ABI/error tests and one real U-mode QEMU grant-syscall component;
- fail-closed validation ownership and directly related documentation.

It does not define:

- freestanding C wrapper names or implementations;
- a raw user-side `ecall` assembly function;
- service startup, `_start`, linker layout, stack ABI, BSS initialization, or
  accidental-return behavior;
- launcher, manifest, readiness, or service protocols;
- grant inspection;
- indirect, magic, delegated, wildcard, or transitive grants;
- vectored, asynchronous, partial, or restartable copies;
- VM fault callbacks or transferable-frame mappings;
- process exit, PM operations, or general kernel-operation syscalls;
- SMP synchronization.

### Unified operation namespace

`a7` remains the sole syscall operation selector.

Existing IPC numbers remain unchanged:

```text
1  SEND
2  RECEIVE
3  CALL
4  REPLY
5  REPLY_RECEIVE
6  NOTIFY
```

Grant operations are:

```text
7  GRANT_CREATE
8  GRANT_REVOKE
9  GRANT_COPY_FROM
10 GRANT_COPY_TO
```

Operation zero is invalid. Unknown operations return `ARGUMENT` after normal
argument capture and `sepc` advancement.

Numbers 11 and above remain unassigned. Reserving a future operation requires a
reviewed ADR. No subsystem may define a private overlapping `ecall` namespace.

### Common trap boundary

Production U-mode `ecall` routing uses one top-level handler equivalent to:

```c
micros_syscall_handle_user_ecall(hart, frame);
```

The trap path has already:

1. accounted the user-to-kernel transition;
2. captured the complete trap frame into the exact current thread object.

The top-level syscall handler then:

1. resolves the authoritative object runtime, boot hart, exact current thread,
   and current process generation;
2. validates `sepc + 4` without overflow;
3. copies `a0` through `a7` into kernel-owned scalar request state;
4. advances saved `sepc` by exactly four bytes once;
5. dispatches IPC operations 1 through 6 to the existing IPC adapter;
6. dispatches grant operations 7 through 10 to the new grant adapter;
7. returns one common `NORMAL` or `CAPTURED` selector result.

IPC behavior, scheduler guards, completion state, blocking, and error values
remain unchanged. The IPC adapter is refactored to consume the already
captured request and already advanced frame; it does not advance `sepc` a
second time.

Grant operations never block and never use the current-thread IPC guard. They
return through the ordinary selected-thread path with the current thread still
queue-reachable. A later independently runnable higher-priority thread may win
ordinary selection, but the grant operation itself wakes or blocks no thread.

An unavailable authoritative object, endpoint, IPC, grant, address-space, or
scheduler runtime that is required by the selected operation is a kernel
invariant panic, not a user-visible `STATE` result.

### Register ABI

All values are RV64 little-endian scalar registers.

#### `GRANT_CREATE`

```text
a7  7
a0  grantee endpoint, unsigned 32-bit
a1  grantor user virtual base
a2  byte length
a3  permission mask, unsigned 32-bit
a4  zero
a5  zero
a6  zero
```

Success returns the opaque `micros_grant_t` token zero-extended in `a0`.
Because live tokens are at most `UINT32_MAX - 1`, they are nonnegative and
cannot alias a negative ABI result.

#### `GRANT_REVOKE`

```text
a7  8
a0  grant token, unsigned 32-bit
a1  zero
a2  zero
a3  zero
a4  zero
a5  zero
a6  zero
```

Success returns zero.

#### `GRANT_COPY_FROM`

```text
a7  9
a0  grantor endpoint, unsigned 32-bit
a1  grant token, unsigned 32-bit
a2  grant-relative byte offset
a3  grantee-local user virtual address
a4  byte length
a5  zero
a6  zero
```

Success returns zero.

#### `GRANT_COPY_TO`

```text
a7  10
a0  grantor endpoint, unsigned 32-bit
a1  grant token, unsigned 32-bit
a2  grant-relative byte offset
a3  grantee-local user virtual address
a4  byte length
a5  zero
a6  zero
```

Success returns zero.

Endpoint, token, and permission values with nonzero upper 32 bits return
`ARGUMENT`. Every operation rejects nonzero registers documented as zero
before mutation.

Address, offset, and length fields use their complete unsigned 64-bit values.
Portable grant and checked-copy rules remain authoritative for user-window,
overflow, maximum-size, direction, mapping, and permission checks.

Existing IPC operations retain their ADR-0035 layouts. This ADR does not make
`a4` through `a6` new IPC shape requirements; existing callers and preserved
register tests remain valid.

### Stable syscall results

The unified ABI names these signed RV64 values:

```text
  0  MICROS_SYSCALL_ABI_OK
 -1  MICROS_SYSCALL_ABI_ARGUMENT
 -2  MICROS_SYSCALL_ABI_DEAD_ENDPOINT
 -3  MICROS_SYSCALL_ABI_UNAUTHORIZED
 -4  MICROS_SYSCALL_ABI_STATE
 -5  MICROS_SYSCALL_ABI_DEADLOCK
 -6  MICROS_SYSCALL_ABI_MEMORY_FAULT
 -7  MICROS_SYSCALL_ABI_REPLY_TOKEN
 -8  MICROS_SYSCALL_ABI_REPLY_TOKEN_EXHAUSTED
 -9  MICROS_SYSCALL_ABI_ENDPOINT_CLOSING
-10  MICROS_SYSCALL_ABI_CAPACITY
-11  MICROS_SYSCALL_ABI_STALE_GRANT
-12  MICROS_SYSCALL_ABI_RANGE
```

Existing `MICROS_IPC_ABI_*` result names remain source-level aliases with the
same values. `MICROS_IPC_ABI_MESSAGE_FAULT` aliases
`MICROS_SYSCALL_ABI_MEMORY_FAULT`.

Grant result mapping is:

| Portable grant result | Syscall result |
| --- | --- |
| `OK` | zero, or token for create |
| `ARGUMENT` | `ARGUMENT` |
| `CAPACITY` | `CAPACITY` |
| `STALE_GRANT` | `STALE_GRANT` |
| `DEAD_ENDPOINT` | `DEAD_ENDPOINT` |
| `UNAUTHORIZED` | `UNAUTHORIZED` |
| `RANGE` | `RANGE` |
| `STATE` | `STATE` |
| `FAULT` | `MEMORY_FAULT` |

`ALREADY_INITIALIZED`, `NOT_INITIALIZED`, `PHASE`, `INVARIANT`, or an unknown
portable result is a kernel panic. Both supported ownership phases permit
checked copy, so `PHASE` indicates corrupt kernel state rather than recoverable
user input.

### Current-process authority

The exact current thread's owner process is:

- the grantor for create and revoke;
- the grantee for copy-from and copy-to.

Create and revoke cannot name another grantor process. Copy operations still
require the exact supplied grantor endpoint in addition to the opaque token.

The target adapter uses:

- the authoritative grant runtime;
- the authoritative endpoint/IPC runtime;
- the authoritative object runtime;
- the exact current process handle captured before mutation.

It never instantiates a second registry and never accepts a caller-provided
process handle.

Failure to resolve the captured current process handle is an invariant panic.
The adapter does not separately pre-resolve its active endpoint, token, or
other participant before the selected portable operation. Those checks remain
in their existing normative portable order. A missing or inactive current
endpoint therefore maps to `DEAD_ENDPOINT` or `STATE` only when the
ADR-0038/ADR-0039 operation reaches that condition.

### Operation preflight and precedence

Common preflight validates target runtime/current-thread invariants, captures
registers, and advances `sepc` before user-shape errors are returned.

For each grant operation:

1. unknown operation, upper-bit violation, or nonzero unused register:
   `ARGUMENT`;
2. failure to resolve the already captured exact current thread/process:
   invariant panic;
3. invoke the selected portable create, revoke, copy-from, or copy-to
   operation without hoisting any endpoint, token, participant, direction,
   bounds, phase, mapping, or owner check;
4. preserve the exact normative order from ADR-0038 for create/revoke and from
   ADR-0039 as refined by ADR-0040 for checked copy;
5. map each returned recoverable result only when that portable order reaches
   it;
6. treat `ALREADY_INITIALIZED`, `NOT_INITIALIZED`, `PHASE`, `INVARIANT`, or an
   unknown result as fatal only if that result is the outcome reached by the
   portable operation;
7. otherwise complete the already non-failing create, revoke, or copy commit.

Thus a stale token still precedes a stale supplied grantor endpoint, grantor
and bounds checks still precede phase classification, and structural
translation failure still precedes ordinary mapping fault exactly as required
by the Accepted grant ADRs.

Create publishes `a0` only after the token exists. A failed create never
returns a token-shaped value.

### State and output preservation

For every grant syscall:

- `sepc` advances by four after successful capture, including rejected
  requests;
- final `a0` contains the token, zero, or one stable negative result;
- every other saved GPR remains unchanged;
- the scheduler current pointer, queues, accounting, and trap-stack ownership
  remain unchanged until ordinary return selection;
- every recoverable failure preserves grant, endpoint, object, page-table,
  frame-owner, source-byte, and destination-byte state;
- create failure preserves grant count and every slot;
- revoke failure preserves the token and active record;
- copy failure transfers zero bytes;
- no operation reports success after partial mutation.

The outbound user register values are scalar data only. No user pointer is
retained after the operation returns.

### Serialization

Trap entry leaves SIE clear. The grant adapter keeps it clear across:

- current process and runtime validation;
- operation-shape checks;
- grant create or revoke;
- both checked-copy translations;
- overlap validation and copy commit;
- result publication.

The one-hart implementation therefore cannot race grant cancellation, endpoint
close, or address-space mutation. This is not an SMP locking design.

## Test-first evidence

### Native ABI tests

Native tests cover:

- operation values 1 through 10 and reserved zero/unknown values;
- exact result values and IPC aliases;
- every grant register shape;
- upper endpoint/token/permission bits;
- every nonzero unused-register rejection;
- 64-bit offset/address/length preservation;
- create token versus negative-result disjointness;
- exhaustive portable grant-error mapping;
- phase, unknown, and invariant result rejection as fatal;
- output/result preservation on every mapping failure.

### QEMU bootstrap grant-syscall component

A dedicated bootstrap-phase image uses at least three exact processes and real
U-mode `ecall` instructions through the production top-level dispatcher.

It must prove:

1. all existing IPC operation numbers and one representative IPC call retain
   their exact behavior;
2. the grantor creates read and write grants for the exact grantee and receives
   nonnegative opaque tokens;
3. the grantee completes page-local and cross-page copy-from and copy-to;
4. both zero-length directions validate capability state and succeed without
   byte access;
5. wrong grantee, wrong grantor, wrong direction, stale token, stale endpoint,
   range, overflow, oversized length, absent mapping, and permission failures
   return exact stable values and preserve every byte/state snapshot;
6. upper-bit and nonzero-unused-register inputs return `ARGUMENT`;
7. revoke immediately makes the old token stale;
8. endpoint generation reuse cannot resurrect copy authority;
9. only `a0` and `sepc` change across every accepted or rejected `ecall`;
10. scheduler current/queue/root/stack ownership remains valid across ordinary
    return selection;
11. final grant, endpoint, object, root, frame, and allocator state returns to
    baseline.

Only the complete sequence emits:

```text
MICROS_GRANT_SYSCALL_TEST_PASS namespace=unified phase=bootstrap lifecycle=checked directions=checked errors=stable registers=preserved cleanup=complete
```

This gate does not claim a C runtime wrapper, user linker, service startup, VM
fault retry, or service protocol.

### Post-handoff syscall integration

The existing irreversible `test-qemu-address-space-handoff` image is extended
after its exact `VM_WIRED` commit. It retains its existing direct mechanism
evidence and additionally executes real U-mode operations 7 through 10 through
the same production dispatcher:

1. the wired grantor creates read and write grants for the exact wired
   grantee;
2. the grantee completes page-local and cross-page copy-from and copy-to;
3. both zero-length directions succeed without byte access;
4. wrong direction, bounds, absent mapping, and permission failures return the
   same stable results as the bootstrap image and preserve every byte/state
   snapshot;
5. the grantor revokes both tokens and later grantee use returns
   `STALE_GRANT`;
6. every operation changes only `a0` and `sepc`;
7. an already prepared thread still returns through the ordinary scheduler
   path.

Because handoff is irreversible, this image intentionally retains its wired
processes, endpoints, roots, and frames. It does not claim endpoint reuse or
cleanup. In addition to the ADR-0040 marker, it emits:

```text
MICROS_GRANT_SYSCALL_HANDOFF_PASS phase=handed-off operations=create,revoke,copy-from,copy-to errors=stable registers=preserved
```

The bootstrap cleanup image and retained-state handed-off image together prove
the development-DAG requirement; neither gate claims the other's teardown
contract.

### Validation ownership

The implementation adds one dedicated QEMU workflow. Changes to:

- the unified syscall ABI or dispatcher;
- IPC syscall capture/dispatch shared by the top-level handler;
- grant ABI mapping or target adapter;
- grant registry/runtime or checked-copy paths;
- trap routing and user-return selection;
- relevant public headers;
- the new grant-syscall component or harness;

select:

- complete native tests and the persistent grant/IPC models;
- the new grant-syscall QEMU workflow;
- the handed-off grant-syscall marker in
  `test-qemu-address-space-handoff`;
- existing IPC ecall-core and complete IPC syscall gates;
- existing grant, endpoint, address-space-handoff, user-execution, and
  scheduler gates selected by the fail-closed planner;
- documentation and all three diff checks.

## Consequences

- The user/kernel grant ABI becomes stable before runtime wrapper names or
  startup policy are chosen.
- Existing IPC numbers and results do not change.
- Kernel-managed create/revoke replaces MINIX user-table publication without
  adding `SYS_SETGRANT`.
- Safe copy uses scalar registers rather than a second kernel-call message ABI.
- The runtime task becomes smaller: raw `ecall`, C wrappers, startup, linker,
  and target wrapper evidence only.
- Future kernel operations extend one reviewed namespace rather than inventing
  subsystem-local traps.

## Alternatives considered

### Combine grant syscalls with the complete freestanding runtime

Rejected. Kernel dispatch/error semantics and user startup/linker/compiler
support have distinct review and failure surfaces. Keeping them separate
follows the project's small-PR rule.

### Reuse IPC `CALL` to invoke a kernel grant service

Rejected. The kernel already owns grant authority. Encoding a local mechanism
as a synthetic service message would duplicate validation, require a reserved
endpoint, and entangle blocking IPC with byte-copy completion.

### Register a user-managed grant table

Rejected by ADR-0005 and ADR-0038. It would reintroduce mutable table TOCTOU,
growth, and table-registration state after the kernel registry is complete.

### Return the created token through a user pointer

Rejected. Returning one nonnegative scalar in `a0` avoids another user-memory
copy and gives failure-atomic output publication.

### Use one operation plus a direction flag

Rejected. Distinct copy-from and copy-to numbers preserve the baseline
direction boundary and prevent flag ambiguity.

### Start a second grant-specific syscall namespace

Rejected. One `a7` namespace makes unknown operations deterministic and avoids
overlapping operation values with future wrappers.

### Require zero in `a4` through `a6` for existing IPC operations

Rejected. ADR-0035 does not define those registers as IPC shape inputs, and
existing context-preservation tests intentionally treat non-result registers
as ordinary saved state.

## Specification basis

- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0004: IPC and Endpoint ABI](0004-ipc-and-endpoint-abi.md)
- [ADR-0005: Direct Memory Grants](0005-direct-memory-grants.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0030: MINIX-Baseline Blocking IPC](0030-minix-baseline-blocking-ipc.md)
- [ADR-0035: RISC-V IPC Syscall and Bootstrap Buffers](0035-riscv-ipc-syscall-and-bootstrap-buffers.md)
- [ADR-0038: Kernel-Managed Direct Grant Registry](0038-kernel-managed-direct-grant-registry.md)
- [ADR-0039: Page-Bounded Checked Grant Copy](0039-page-bounded-checked-grant-copy.md)
- [ADR-0040: Post-Handoff Wired Address-Space Resolution](0040-post-handoff-wired-address-space-resolution.md)
- [MINIX user grant and safe-copy syscall study](../research/minix-user-grant-syscall-boundary.md)
- [Development dependency DAG](../architecture/development-dag.md)

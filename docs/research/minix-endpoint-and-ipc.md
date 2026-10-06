# MINIX Endpoint and Blocking IPC Study

## Purpose

This study traces the MINIX endpoint, privilege, synchronous IPC,
notification, deadlock, and exit-cleanup paths before `micros` implements
development-DAG Step 6. It records behavior and authority boundaries rather
than source to copy.

The reference is MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

`micros` remains an independent C17 and RISC-V implementation. The
classification follows
[ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md).

## End-to-end synchronous IPC path

### Entry and operation validation

Architecture IPC stubs pass an operation, endpoint, and message pointer to
`do_ipc()` (`minix/lib/libc/arch/i386/sys/_ipc.S:16-83`,
`minix/lib/libc/arch/arm/sys/_ipc.S:7-69`). `do_ipc()` accounts the entry and
routes synchronous calls to `do_sync_ipc()`
(`minix/kernel/proc.c:599-683`).

`do_sync_ipc()` validates in this order (`minix/kernel/proc.c:479-598`):

1. the operation number is known;
2. only `RECEIVE` may use `ANY`;
3. a concrete endpoint resolves to a live slot with the same generation;
4. send-like operations pass the caller's destination mask;
5. the caller's trap mask permits the operation;
6. kernel-task restrictions are satisfied;
7. the operation-specific transition executes.

Invalid or stale endpoints fail before queue mutation. Receive authorization is
not a reverse send-mask check; senders are authorized when they attempt to send.

### Endpoint identity

MINIX endpoints combine a process-table slot and generation, while `ANY`,
`NONE`, and `SELF` occupy reserved generation-zero values
(`minix/include/minix/endpoint.h:1-70`). `isokendpt()` decodes the slot and
requires a nonempty process whose complete stored endpoint equals the supplied
value (`minix/kernel/proc.c:1828-1860`).

This prevents ordinary stale endpoint use but eventually reuses generations.
`micros` already selects a different fixed 12-bit slot/20-bit generation ABI
and quarantines exhausted slots.

### Message shape and kernel ownership

MINIX messages are exactly 64 bytes: an endpoint source, signed type, and
56-byte union payload (`minix/include/minix/ipc.h:2403-2425,2650-2676`).
The sender supplies a user pointer, but the kernel copies the value into a
kernel-owned destination or blocked-sender buffer before publishing the
transition (`minix/kernel/proc.c:868-960`).

The receive buffer address is recorded on the receiver. Delivery is staged in
`p_delivermsg`; the final copy to user memory occurs before return to user mode.
If that copy faults, MINIX suspends the receiver and asks VM to resolve it
(`minix/kernel/proc.c:255-290`).

Pointers inside the message remain protocol data. They do not authorize memory
access.

## Send transition

`mini_send()` first checks whether the destination is already receiving from
the sender or `ANY` (`minix/kernel/proc.c:868-928`).

If the receiver is ready:

1. copy the sender's message into the receiver's kernel buffer;
2. overwrite the source endpoint;
3. record the delivered call type;
4. clear reply-pending state when applicable;
5. clear `RTS_RECEIVING`, making the receiver runnable when no other reason
   remains.

If the receiver is not ready:

1. a nonblocking send fails with `ENOTREADY`;
2. deadlock is checked before mutation;
3. the message is copied into the sender's kernel buffer;
4. `RTS_SENDING` and the exact destination endpoint are installed;
5. the sender is appended to the destination's FIFO caller queue.

The message copy therefore precedes the blocking flag and queue publication.

## Receive transition

`mini_receive()` records the receiver buffer and checks available work in this
order (`minix/kernel/proc.c:965-1117`):

1. pending notification, unless the thread is waiting for a `SENDREC` reply;
2. pending asynchronous send;
3. the endpoint's FIFO blocked-sender queue.

A specific-source receive scans past unmatched senders without reordering
them. On a match, the kernel copies the sender's saved message, clears the
sender's `RTS_SENDING`, unlinks that exact sender, and leaves any independent
blocking reason intact.

If no acceptable item exists, deadlock is checked before installing
`RTS_RECEIVING` and the exact source endpoint or `ANY`.

`micros` does not implement asynchronous sends in the shell MVP. Its baseline
receive order can therefore be notification first, then the first matching
blocked sender. ADR-0004 deliberately does not expose that ordering as a
client guarantee.

## `SENDREC` and reply behavior

MINIX `SENDREC` sets `MF_REPLY_PEND`, performs send, then falls through into
receive from the same endpoint (`minix/kernel/proc.c:569-585`).

If the send blocks, the process may hold both `RTS_SENDING` and
`RTS_RECEIVING`; blocked-on traversal must inspect sending first
(`minix/kernel/proc.h:183-199`). Once the request is consumed,
`RTS_SENDING` clears but the caller remains reply-waiting. Pending
notifications do not satisfy that receive (`minix/kernel/proc.c:997-1007`).

The server reply is an ordinary authorized send. MINIX keeps send masks
symmetrical so an allowed request target can ordinarily reply
(`minix/kernel/system.c:300-344`).

This checkout documents a known defect: `SENDREC` is not fully atomic with
respect to signal-context replacement (`minix/kernel/ipc.h:24-44`).

`micros` already chooses an opaque one-shot reply token bound to the exact
caller thread and callee endpoint generation. This preserves the blocking
request/reply behavior without granting a server general send authority back
to every caller, and it remains unambiguous for future multithreaded clients.

## Deadlock detection

Before blocking a send or receive, MINIX follows the blocked-on chain through
exact endpoints (`minix/kernel/proc.c:701-767`). A send edge takes precedence
when a `SENDREC` process also has receive state. `ANY` terminates the walk.

Reaching the caller reports `ELOCKED`. A special two-process
send/receive-complement case is allowed; larger cycles are rejected.

The walk is finite because the process table is finite, but the implementation
does not carry generation-safe typed links or an explicit step bound.

For `micros`, every matching delivery attempt occurs before deadlock checking.
The later ADR must define a bounded walk over exact live endpoint and thread
generations, preserve all state on rejection, and state how the one-thread
v0.1 policy maps an endpoint dependency to its exact schedulable thread.

## Notifications

`mini_notify()` never blocks (`minix/kernel/proc.c:1120-1168`).

If the receiver is waiting for that source or `ANY`, and is not waiting for a
`SENDREC` reply, the kernel constructs and delivers a zero-initialized
notification immediately. Otherwise it sets one pending bit indexed by the
source privilege ID.

Pending sources are selected by exact source or lowest set privilege ID
(`minix/kernel/proc.c:770-845`). Repeated notifications from one source
coalesce. Hardware interrupt bits and system-signal sets are themselves
coalesced into the delivered notification (`minix/kernel/proc.c:96-116`).

ADR-0004 maps this to one reserved message type and a 64-bit event mask.
Implementing notification-before-sender selection reproduces the MINIX
baseline, while the public ABI continues to require clients to inspect
authoritative state instead of relying on cross-class ordering.

## Privilege authority

Each MINIX privilege structure contains:

- an allowed IPC-operation trap mask;
- an allowed destination bitmap;
- a separate kernel-call bitmap;
- notification and interrupt pending state
  (`minix/kernel/priv.h:20-66`).

RS constructs service privilege data and IPC target masks, then installs them
while the target remains inhibited
(`minix/servers/rs/main.c:250-286`,
`minix/kernel/system/do_privctl.c:35-165`). RS derives both forward and
backward IPC relationships from service labels
(`minix/servers/rs/manager.c:2240-2335`).

MINIX send-mask symmetry gives request targets ordinary reply authority.
`micros` instead has immutable named profiles with distinct call, send,
notify, kernel-operation, device, and IRQ permissions. A valid reply token is
the only dynamic reply authority. This is a compatible least-privilege
extension already fixed by ADR-0004.

## Endpoint exit and cancellation

Endpoint teardown first marks the process unable to communicate, removes it
from any sender queue, and clears its receive state
(`minix/kernel/system.c:500-545`).

It then scans live processes:

- pending notifications from the dead privilege ID are removed;
- blocked senders and specific receivers whose blocked-on endpoint matches are
  awakened with `EDEADSRCDST`;
- unrelated `ANY` receivers remain blocked
  (`minix/kernel/system.c:545-610`).

The `micros` endpoint lifecycle must additionally invalidate one-shot reply
tokens bound to either endpoint generation and wake the exact caller thread
with a dead-peer error.

## Baseline classification

| Topic | MINIX behavior | `micros` classification |
| --- | --- | --- |
| Endpoint slot plus generation | Full endpoint equality validates liveness | Baseline parity; accepted ABI adaptation for 12/20 unsigned layout |
| Generation wrap | Eventually reuses a generation | Compatible safety extension: quarantine before reuse |
| Fixed 64-byte messages | Kernel overwrites source; 56-byte payload | Baseline parity; accepted 48-byte payload/reply-token layout |
| Send and receive queues | FIFO senders, specific or `ANY` receive | Baseline parity, stored on distinct thread/endpoint objects |
| Request/reply | `SENDREC` plus ordinary reply send | Compatible extension: one-shot exact-thread reply token |
| Reply-and-wait | Separate server operations | Compatible extension: failure-atomic `reply_receive` |
| Notifications | Nonblocking, source-coalesced, delivered before senders | Baseline parity with 64-bit event-mask adaptation |
| IPC permissions | Trap mask, target mask, symmetric reply permission | Baseline parity with stricter named profiles and token reply authority |
| Deadlock | Walk blocked endpoint chain before blocking | Baseline parity with bounded generation-safe validation |
| Async send and sendnb | Supported | Deferred outside the shell MVP |
| Receive-buffer VM fault | Kernel suspends and asks VM | Staged substitution: resident validated IPC buffers before VM fault delivery |
| Endpoint exit | Unlink, cancel, and wake exact dependents | Baseline parity plus reply-token cancellation |

## Required decisions before implementation

The endpoint/IPC ADR must define:

1. endpoint object lifecycle and its binding to one process generation;
2. immutable profile representation and operation-specific authorization;
3. exact thread RTS bits and endpoint sender/receiver queue links;
4. message snapshot and receive-delivery ownership;
5. call token allocation, validation, consumption, and cancellation;
6. failure-atomic `reply_receive`;
7. notification pending representation and deterministic selection;
8. bounded deadlock-chain rules;
9. endpoint-exit wake and error semantics;
10. the RISC-V syscall register ABI and bootstrap invalid-buffer policy;
11. native model, corruption, and QEMU component acceptance gates.

## Source map

- `minix/kernel/proc.c`
- `minix/kernel/proc.h`
- `minix/kernel/ipc.h`
- `minix/kernel/priv.h`
- `minix/kernel/system.c`
- `minix/kernel/system/do_privctl.c`
- `minix/include/minix/endpoint.h`
- `minix/include/minix/ipc.h`
- `minix/lib/libc/arch/i386/sys/_ipc.S`
- `minix/lib/libc/arch/arm/sys/_ipc.S`
- `minix/servers/rs/main.c`
- `minix/servers/rs/manager.c`

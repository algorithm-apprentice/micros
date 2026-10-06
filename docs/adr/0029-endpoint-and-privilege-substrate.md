# ADR-0029: Endpoint and Privilege Substrate

- Status: Accepted
- Date: 2026-10-07
- Refines: ADR-0004 endpoint lifecycle and immutable privilege profiles

## Context

The scheduler is complete, so development-DAG Step 6 can begin with process
IPC identity and authorization. ADR-0004 fixes the public endpoint and message
ABI, but does not yet define:

- the concrete endpoint object and lifecycle;
- when an endpoint becomes externally resolvable;
- how one endpoint is bound to one process generation;
- how immutable named privilege profiles are represented and installed;
- how later blocking IPC queues attach without collapsing endpoint and thread
  ownership.

The MINIX baseline uses generation-aware endpoints and privilege structures
containing operation, destination, and kernel-call masks. RS installs those
privileges while a process is inhibited. MINIX also makes send masks
symmetrical so a request target may reply.

The canonical evidence is
[the MINIX endpoint and blocking IPC study](../research/minix-endpoint-and-ipc.md).

`micros` already chooses stricter behavior:

- process, thread, endpoint, and hart remain distinct;
- process generation exhaustion quarantines a slot;
- request/reply authority uses a one-shot token instead of symmetric general
  send permission;
- profiles distinguish call, send, notify, and privileged operations.

This ADR defines only the endpoint and profile substrate. Blocking IPC state,
reply tokens, notifications, deadlock detection, and the RISC-V syscall ABI
remain the next ADR and implementation slice.

## Decision

### Scope

This slice defines:

- a fixed-capacity endpoint registry;
- endpoint packing and exact generation validation;
- reserved and active endpoint lifecycle states;
- one primary endpoint per v0.1 process;
- an initialization-time immutable privilege-profile table;
- one-time profile installation on a process generation;
- operation-specific profile authorization queries;
- endpoint close preconditions and stale-reference behavior;
- native and QEMU substrate tests.

It does not define:

- sender or receiver queues;
- thread IPC run-time flags;
- message delivery;
- call/reply tokens;
- notifications;
- deadlock detection;
- endpoint-exit cancellation of active IPC;
- user-visible IPC syscalls;
- grants or safe copy.

### Endpoint ABI

ADR-0004 remains unchanged. A public endpoint is an unsigned 32-bit value:

```text
bits 0..11   process slot
bits 12..31  process generation
```

Generation zero, `ANY`, and `NONE` are invalid live endpoint identities.

Portable helpers provide:

```c
micros_endpoint_pack(process_handle, endpoint);
micros_endpoint_unpack(endpoint, process_handle);
```

Packing requires:

- a process slot encodable in 12 bits;
- a nonzero generation no greater than `0x000fffff`;
- a result unequal to `ANY` and `NONE`.

Unpacking special or malformed values returns a stale/invalid endpoint error
without modifying the output.

The existing process allocator remains authoritative for generation advance
and quarantine. The endpoint layer never advances or wraps a generation
independently.

### Endpoint registry

The endpoint registry owns one slot for every implemented process slot:

```text
MICROS_ENDPOINT_CAPACITY == MICROS_PROCESS_CAPACITY
```

This is an implementation bound, not a reduction of the 4096-slot public ABI.
Endpoint slot `n` is permanently associated with process slot `n`, while the
complete process generation remains the lifetime authority.

Each endpoint record owns:

- endpoint lifecycle state;
- exact owner process handle;
- exact packed endpoint value.

The process record continues to own its primary endpoint value. The endpoint
record owns communication mechanism state. Neither object is treated as the
other.

### Lifecycle

Endpoint lifecycle is:

```text
FREE -> RESERVED -> ACTIVE -> FREE
```

A quarantined process slot keeps its corresponding endpoint slot unavailable;
the endpoint record does not own a second quarantine state.

`RESERVED` means:

- the endpoint is bound to one exact live process generation;
- the process records it as its primary endpoint;
- external endpoint resolution rejects it;
- IPC authorization and delivery cannot target it.

Reservation requires:

- a live process with no primary endpoint;
- no live endpoint record for the same process slot;
- a packable, non-special process slot/generation;
- a process generation not already quarantined.

`ACTIVE` means:

- one valid privilege profile is installed on the same process generation;
- the endpoint is externally resolvable;
- later IPC operations may authorize it.

Activation requires the exact `RESERVED` endpoint and installed profile. It is
failure-atomic. The later launcher and PM activation transitions will combine
endpoint publication with their already documented readiness or spawn commit;
ordinary user code cannot activate an endpoint.

Close requires:

- the exact live endpoint generation;
- no hart current thread owned by the process;
- every process thread held non-runnable for teardown.

The substrate implementation checks the fields it owns and exposes an internal
close operation. The blocking IPC ADR adds queue, notification, and reply-token
cancellation preconditions before PM uses close for ordinary exit.

Close clears the process primary endpoint, profile assignment, and endpoint
record. The process generation remains unchanged until process release. If the
process slot is later quarantined, the endpoint slot remains unavailable with
it.

### Resolution

Two resolution modes are explicit:

- internal resolution accepts exact `RESERVED` or `ACTIVE` ownership;
- IPC resolution accepts only exact `ACTIVE` endpoints.

Both require:

- a non-special packed endpoint;
- slot within the implemented capacity;
- a live process at the decoded slot/generation;
- a live endpoint record with the same owner and packed value;
- agreement with the process's primary endpoint field.

No API silently treats a stale endpoint as `NONE`, `ANY`, or a new generation.

### Privilege profile table

The endpoint registry is initialized once with a caller-provided table copied
into fixed kernel storage and then sealed for the registry lifetime.
Production passes a `static const` table; native tests pass small model tables.
There is no runtime profile creation, deletion, or mutation.

The initial mechanism supports at most 32 profiles. Profile ID zero is invalid.
Destination-mask bit `n` names profile ID `n`; IDs must therefore be in the
range 1 through 31. Diagnostic names occupy a fixed 32-byte field and must be
NUL-terminated.
Each profile has:

- stable numeric ID;
- English diagnostic name;
- allowed IPC operation bits;
- allowed destination-profile masks for `call`, ordinary `send`, and
  `notify`;
- allowed privileged-kernel-operation mask.

The operation bits reserve positions for:

- receive;
- call;
- send;
- reply;
- reply/receive;
- notify.

The blocking IPC ADR defines exact operation behavior. This ADR defines only
profile validation and authorization queries.

Profile initialization rejects:

- duplicate or zero IDs;
- empty or unterminated diagnostic names;
- unknown operation bits;
- destination bits outside the installed table;
- duplicate names;
- capacity overflow.

A rejected initialization leaves every registry byte unchanged. Initialization
is one-shot and has no reset operation.

### Profile installation

A live process begins with no profile. Installation requires:

- exact live process handle;
- exact `RESERVED` primary endpoint;
- known nonzero profile ID;
- no previous profile;
- every live thread owned by the process having exactly
  `MICROS_THREAD_RTS_INACTIVE`;
- no process thread assigned to a scheduler;
- no thread current on any hart.

Installation copies only the profile ID into the process. The immutable table
remains the source of policy.

Installation is authorized only through internal bootstrap/PM transitions:

- the static launcher may install only the profile named by the manifest entry;
- PM may later install only `APPLICATION` for the exact reserved spawn child;
- user processes cannot install or replace profiles.

Those authority wrappers arrive with the launcher and PM tasks. The substrate
operation itself accepts an exact expected profile ID and never infers or
falls back to another profile.

Once installed, a profile cannot be changed. Abort or exit closes the endpoint
and releases the process generation instead of replacing policy in place.

### Authorization queries

The portable substrate provides pure checked queries equivalent to:

```c
micros_privilege_allows_receive(process);
micros_privilege_allows_call(source, destination);
micros_privilege_allows_send(source, destination);
micros_privilege_allows_notify(source, destination);
micros_privilege_allows_reply(source);
micros_privilege_allows_reply_receive(source);
micros_privilege_allows_kernel_operation(source, operation);
```

Every query:

- resolves exact live process and profile generations;
- requires the source process to own the exact `ACTIVE` primary endpoint;
- resolves the destination as an `ACTIVE` endpoint where applicable;
- checks the source operation bit;
- checks the operation-specific destination profile mask;
- returns an explicit authorization error without mutation.

There is no automatic send-mask symmetry. The later reply operation may bypass
the ordinary send destination mask only with a valid one-shot reply token, as
already required by ADR-0004.

Receive authorization is an operation bit and does not grant authority to
senders. A sender must independently pass call, send, or notify authorization.

### Serialization

Portable operations require external serialization. The one-hart target uses
the existing IRQ-save discipline and keeps SIE clear across reserve, profile
installation, activation, close, and later IPC queue transitions.

This is not an SMP implementation. Locks and remote coordination remain a
later ADR without changing endpoint or profile identity.

### Invariants

Full validation additionally requires:

- every live endpoint owner resolves one exact live process generation;
- each process owns at most one endpoint;
- process and endpoint primary values agree exactly;
- `FREE` endpoint slots contain zero ownership state;
- a `RESERVED` endpoint has no externally resolvable authority;
- an `ACTIVE` endpoint names one installed immutable profile;
- no process profile is installed without a reserved or active endpoint;
- profile IDs and names are unique;
- all operation and destination masks reference installed definitions only;
- free and quarantined process slots have no endpoint or profile;
- scheduler-assigned or current threads cannot receive a new profile.

Validation detects corruption and never repairs it.

## Test-first evidence

### Native tests

The portable endpoint/profile suite must cover:

- every endpoint slot/generation boundary;
- `ANY`, `NONE`, zero generation, overflow, and reserved packed values;
- lowest-slot reservation and exact process ownership;
- stale process and endpoint generations;
- hidden `RESERVED` versus externally resolvable `ACTIVE`;
- profile-table duplicate, unknown-bit, bad-name, and capacity rejection;
- one-time exact profile installation;
- profile replacement rejection;
- operation-specific target-profile authorization;
- no automatic reply symmetry;
- close preconditions and complete zeroing;
- process-release rejection while an endpoint remains bound;
- output and complete-state preservation on every failure;
- validator corruption of owner, packed value, profile, lifecycle, and future
  queue placeholders.

A seeded model mixes process create/release, endpoint reserve/activate/close,
profile installation, and authorization queries. It validates after every
operation and prints the seed and failing transition.

### QEMU component

An isolated endpoint/profile image must:

1. create two exact process generations and inactive threads;
2. reserve endpoints and prove they are hidden;
3. install two immutable test profiles;
4. activate both endpoints;
5. accept and reject distinct call/send/notify relationships;
6. reject stale endpoint reuse after one process generation advances;
7. reject process release while the endpoint remains bound;
8. close endpoints, detach contexts, release threads/processes, and restore the
   object baseline.

Only then may it emit:

```text
MICROS_ENDPOINT_TEST_PASS generation=validated profiles=immutable visibility=staged authorization=separate
```

## Consequences

- Endpoint identity is explicit before IPC queues depend on it.
- Process generations remain the single stale-reference authority.
- Hidden reservation supports launcher readiness and PM spawn transactions.
- Profiles are immutable and operation-specific without importing full service
  protocols early.
- Reply authority no longer requires symmetric general send permission.
- One endpoint slot per implemented process is sufficient for v0.1 but does
  not add a second public endpoint per process.
- Blocking queues, tokens, notifications, deadlock, and syscall copying remain
  dependency-ready follow-up work.

## Alternatives considered

### Store IPC queues directly on the process

This collapses process resource ownership and endpoint communication state,
contradicting ADR-0011 and making future endpoint lifecycle changes invasive.

### Allocate endpoint generations independently

ADR-0004 packs the process slot and generation. A second generation authority
would permit disagreement and stale aliasing without enabling another public
endpoint per process.

### Install mutable per-process permission masks

Mutable masks reproduce MINIX flexibility but weaken named policy review and
make spawn/bootstrap rollback broader. Immutable profile IDs keep the policy
table auditable.

### Automatically grant reverse send permission

MINIX uses symmetry so servers can reply. One-shot reply tokens provide the
required response authority without giving a server unrestricted sends to all
clients.

### Define every service profile now

VM, PM, TTY, RAMFS, VFS, launcher, and application protocols are not all
dependency-ready. The substrate defines immutable mechanics; each later
protocol task adds its reviewed profile entry before use.

## Specification basis

- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0004: IPC and Endpoint ABI](0004-ipc-and-endpoint-abi.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0019: Kernel Object Identity and Ownership](0019-kernel-object-identity-and-ownership.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0026: MINIX-Baseline Kernel Scheduler](0026-minix-baseline-kernel-scheduler.md)
- [MINIX endpoint and blocking IPC study](../research/minix-endpoint-and-ipc.md)
- [Development dependency DAG](../architecture/development-dag.md)

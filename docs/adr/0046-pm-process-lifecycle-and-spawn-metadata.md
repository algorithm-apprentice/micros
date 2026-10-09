# ADR-0046: PM Process Lifecycle and Spawn Metadata

- Status: Accepted
- Date: 2026-10-09
- Refines: ADR-0003, ADR-0004, ADR-0005, ADR-0007, ADR-0009,
  ADR-0011, ADR-0019, ADR-0021, ADR-0022, ADR-0025, ADR-0029,
  ADR-0030, ADR-0037, ADR-0042, ADR-0043, and ADR-0045
- Supersedes in part:
  - ADR-0009's initial reservation order. Step 10 reserves only an empty
    kernel process object. The first thread, reserved endpoint,
    `APPLICATION` profile, and executable context are created together by the
    later PM-only prepare transition after VM has returned a load-complete
    token.
  - ADR-0043's version-1 bootstrap role set and production PM tuple. The
    defined role mask adds one PM bit, and the production PM entry carries
    that role instead of being an ordinary static service.

## Context

Development-DAG Step 9 is complete:

- one real VM service validates the complete static frame database;
- ownership handoff is irreversible;
- every static user mapping remains wired;
- post-handoff read-only translation, IPC, grants, activation, and return are
  available; and
- bootstrap allocation, mapping mutation, root creation and destruction, and
  generic execution-context preparation are unavailable.

PM is the next dependency-ready service. It must become the sole owner of:

- application process IDs;
- parent/child relationships;
- spawn transaction metadata;
- normal exit status;
- zombie retention;
- wait selection and completion; and
- the decision to publish a prepared application.

The fixed MINIX baseline is documented in
[the PM process-lifecycle study](../research/minix-pm-process-lifecycle.md).
MINIX PM tracks semantic lifecycle independently from kernel, VM, and VFS
cleanup; retains a table entry until both cleanup and parent collection allow
reuse; reparents children to init; and coordinates creation across VM, VFS,
the kernel, and scheduling.

The complete `micros` spawn path is not dependency-ready. ADR-0045 explicitly
forbids post-handoff frame allocation and mapping mutation until a separate VM
mapping protocol is reviewed. VFS, descriptor actions, executable buffering,
load-complete tokens, and init are later DAG outcomes.

Step 10 therefore establishes the real PM service, its authoritative portable
state machine, exact exit/wait wire protocol, bootstrap-to-runtime gate, and a
PM-only empty-process reservation. It does not claim a successful spawn or a
running dynamic child.

## Decision

### Scope

This outcome defines:

- one real statically embedded PM service ELF;
- one exact PM bootstrap role and immutable privilege profile;
- one kernel-origin bootstrap-sealed notification to PM;
- a fixed-capacity application lifecycle table;
- generation-safe PM record handles and monotonic semantic PIDs;
- init-reaper, parent/child, spawn, exit, zombie, and wait state machines;
- exact version-1 `EXIT`, `WAIT`, and result messages;
- one PM-only kernel syscall namespace with empty-process `RESERVE` and
  `ABORT_RESERVED` commands;
- complete prepublication spawn rollback ordering;
- the later full-spawn extension boundary;
- native transition/model evidence;
- one real QEMU PM startup and reservation-rollback scenario; and
- fail-closed validation ownership.

It does not implement:

- a successful application spawn;
- post-handoff frame allocation, release, map, unmap, root creation, root
  destruction, scratch aliases, or page-fault delivery;
- VFS executable lookup, buffering, descriptor state, or grants;
- VM loading, mapping generations, or load-complete tokens;
- creation of an application thread, endpoint, profile, context, or runnable
  state;
- running-process resource teardown on the target;
- init, shell, `ps`, or an application runtime;
- `fork`, destructive `exec`, copy-on-write, process groups, sessions,
  signals, tracing, credentials, `rusage`, or unrestricted descriptor
  inheritance;
- static-service restart, PM reconstruction, RS lifecycle, or recovery; or
- a production six-service image.

The portable PM model defines the complete lifecycle and rollback contracts
that later peers must satisfy. Production target code may reach only the
states whose dependencies exist.

### PM bootstrap role

The manifest role set adds:

```text
MICROS_BOOTSTRAP_ROLE_PM = 0x00000008
```

A dependency-closed manifest may contain zero or one PM role. The production
six-service manifest contains exactly one. The PM role:

- has no device range or IRQ;
- is not inferred from service ID, name, image, profile, or prerequisite
  position;
- identifies the exact process and thread handles, endpoint generation, image
  ID, and profile ID recorded in the PM kernel binding; and
- is the only static role that may hold the PM-control kernel-operation bit.

This replaces ADR-0043's version-1 role validation. The complete defined set
is:

```text
MICROS_BOOTSTRAP_ROLE_CONTROLLER    = 0x00000001
MICROS_BOOTSTRAP_ROLE_VM            = 0x00000002
MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER = 0x00000004
MICROS_BOOTSTRAP_ROLE_PM            = 0x00000008
```

Exactly one active entry is the controller. At most one entry carries each of
the VM, console-owner, and PM roles. No entry may combine roles. The
production PM tuple remains service ID 3, process slot 2, profile ID 3,
profile name `PM`, and direct prerequisite VM, but its exact role is now PM.
Every ordinary production prefix that includes PM must match that complete
replacement tuple.

The immutable kernel-operation bit is:

```text
MICROS_KERNEL_OPERATION_PM_CONTROL = 0x0000000000000004
```

No other static or application profile may contain it.

The production PM profile has:

```text
operations =
    RECEIVE | CALL | REPLY | REPLY_RECEIVE
call targets =
    BOOTSTRAP_LAUNCHER
send targets = 0
notify targets = 0
kernel operations = PM_CONTROL
```

The launcher target is used only for bootstrap readiness. Later reviewed VM
mapping and VFS executable/lifecycle protocols add their exact PM call targets
only when those dependencies exist. A dependency-closed test manifest may use
an exact test profile, but it may not grant broader authority.

The immutable table adds this stable non-bootstrap profile identity:

```text
MICROS_PRIVILEGE_PROFILE_APPLICATION = 7
name = APPLICATION
operations = CALL
call targets = PM | VFS
send targets = 0
notify targets = 0
kernel operations = 0
```

The ordinary Step 10 PM prefix installs the seven stable production profiles,
including otherwise inactive TTY, RAMFS, and VFS profiles, so every target bit
names an installed immutable profile. Only launcher, VM, and PM have processes
in that prefix; no process or endpoint receives `APPLICATION`. Validation
requires exact ID 7, the complete zero-padded diagnostic name, exact operations
and targets, and the presence of PM and VFS profiles. No runtime transition
creates or mutates a profile.

Grant syscalls retain their separate exact participant checks. Applications
have no direct call, send, notify, device, IRQ, or kernel-operation authority
to VM, RAMFS, TTY, the launcher, or another application.

PM validates its ordinary bootstrap configuration, sends the existing
versioned ready call, receives the exact token-bound acknowledgment, and then
enters its server loop. Readiness means that PM's table and protocol parser are
initialized. It does not mean that runtime process creation is enabled.

### Bootstrap-sealed runtime gate

Launcher `COMPLETE` gains one PM-specific extension. If a PM role exists, the
completion preflight requires:

- the exact PM service is `READY`;
- its endpoint is the exact active manifest generation;
- its thread and endpoint state validate; and
- one kernel-origin notification can be committed without a fallible action
  after launcher sealing begins.

The completion commit:

1. performs the existing irreversible launcher seal;
2. injects this event into the exact PM endpoint:

```text
MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED = 0x0000000000000001
```

The delivered envelope is the existing canonical kernel notification:

```text
source      = NONE
type        = MICROS_IPC_TYPE_KERNEL_NOTIFICATION
reply token = 0
payload[0..7] = little-endian event mask
payload[8..47] = 0
```

Notification injection is nonblocking and coalescing. If PM is not receiving,
the bit remains pending. A manifest with no PM role has no PM event and retains
the existing completion behavior.

PM sets `runtime_enabled` only after receiving the exact source, type, token,
mask, and zero tail. Duplicate delivery is an invariant failure because
launcher completion is one-shot. Unknown kernel-event bits are also an
invariant failure in this outcome.

Before `runtime_enabled`, PM may complete bootstrap readiness and reject
ordinary client requests, but it may not reserve a child process or begin an
init transaction.

This event is the later trigger for PM's internal init spawn after VFS and VM
support the full transaction. It does not restore launcher authority.

### PM application identity

PM manages exactly:

```text
MICROS_PM_PROCESS_CAPACITY = MICROS_PROCESS_CAPACITY = 64
```

Static manifest services are not application records. PM retains their
immutable service-ID/endpoint inventory only for validation and later
diagnostics. They have no PM PID, parent, child, wait, zombie, or ordinary exit
state. Their failure remains fatal until RS.

Each application record has an internal handle:

```c
struct micros_pm_process_handle {
    uint16_t slot;
    uint32_t generation;
};
```

Generation zero is invalid. Reuse advances the generation. A slot is
quarantined instead of wrapping or making a stale handle valid.

The externally visible semantic PID is an unsigned 64-bit value:

- zero means no PID or any-child wait selection where explicitly stated;
- the first committed init application receives PID 1;
- later reservations receive monotonically increasing values;
- a PID is never reused in v0.1, including after an aborted spawn;
- `UINT64_MAX` may be assigned once, after which an explicit exhausted flag
  makes every later allocation fail without wrap or mutation; and
- no PID encodes or aliases a kernel process slot, process generation, thread,
  endpoint, service ID, or transaction token.

PM is the sole PID allocator.

### Application record

Each record contains at least:

```text
slot state and generation
process state
semantic PID
parent PM handle or NONE
reaper flag
exact running endpoint or NONE
spawn or exit transaction stage and opaque resource tokens
normal exit status
wait selector, flags, and one-shot reply token
```

The process states are:

```text
FREE
SPAWNING
RUNNING
EXITING
ZOMBIE
QUARANTINED
```

The required shapes are:

| State | Kernel resources | PM-published endpoint | Parent | Exit status | Waitable |
| --- | --- | --- | --- | --- | --- |
| `FREE` | none | `NONE` | none | clear | no |
| `SPAWNING` | exact hidden shape for the current spawn stage | `NONE` | exact live parent or none for init | clear | no |
| `RUNNING` | exact live generation with root and one thread | exact active generation | exact live parent or none for init | clear | yes, not complete |
| `EXITING` | teardown generation until release | exact closing generation until close, then `NONE` | exact reaper-capable parent | complete | yes, not complete |
| `ZOMBIE` | none | `NONE` | exact live parent | complete | yes, complete once |
| `QUARANTINED` | none | `NONE` | none | clear | no |

The `SPAWNING` kernel shape is stage-dependent:

- `PM_RECORD_RESERVED`: no kernel process;
- `KERNEL_PROCESS_RESERVED` and `VFS_IMAGE_PREPARED`: one empty process with
  no root, thread, endpoint, profile, or frame;
- `VM_MAPPINGS_FROZEN` and `VFS_DESCRIPTORS_PREPARED`: one rooted process in
  the exact frozen-mapping phase with no thread, endpoint, or profile; and
- `KERNEL_EXECUTION_PREPARED` and `PM_VFS_COMMITTED`: that rooted process plus
  one held thread, reserved endpoint, `APPLICATION` profile, prepared context,
  and sealed mapping generation.

The reserved endpoint in the last shape remains kernel-internal transaction
state. PM's published endpoint field stays `NONE`, source lookup cannot resolve
the child, and no thread is runnable until activation atomically changes the
record to `RUNNING`.

`SPAWNING` may contain only one active transaction per parent and one active
kernel reservation globally in v0.1. The global limit matches PM's
single-threaded directed transaction and may be widened only by a later ADR
that defines concurrent collaborator calls and token storage.

The first successful internal init transaction:

- has no parent;
- receives PID 1;
- becomes the sole designated reaper; and
- cannot exit normally. Its termination is a fatal system failure until
  recovery policy exists.

Failure of that initial transaction is also fatal in the pre-recovery system;
PM does not retry PID 1 or continue without a reaper.

Every other `RUNNING`, `EXITING`, or `ZOMBIE` application has one exact live
parent handle. Parent and child cannot be the same record.

### PM-control syscall namespace

The unified syscall namespace adds:

```text
MICROS_SYSCALL_ABI_PM_CONTROL = 13
```

Operations 1 through 12 remain unchanged. The generic ADR-0042 runtime is not
expanded; PM uses one private raw-syscall wrapper.

Version 1 has two commands:

```text
MICROS_PM_CONTROL_RESERVE = 1
MICROS_PM_CONTROL_ABORT_RESERVED = 2
```

No command creates a root, mapping, thread, endpoint, profile, execution
context, scheduler assignment, or runnable child.

#### Reservation output

PM exports one aligned writable output object:

```c
struct micros_pm_reservation_result {
    uint32_t version;
    uint32_t size;
    uint64_t transaction;
    uint64_t reserved[2];
};
```

The object is exactly 32 bytes. Version 1 requires:

```text
version = 1
size = 32
transaction != 0
reserved = 0
```

The transaction is opaque. It does not encode a process handle and grants no
authority without exact PM or later exact VM caller authorization.

#### Reserve registers

```text
a0  MICROS_PM_CONTROL_RESERVE
a1  PM output-object user address
a2  exact size 32
a3..a6  zero
a7  MICROS_SYSCALL_ABI_PM_CONTROL
```

The output address must be 8-byte aligned and wholly writable through PM's
exact handed-off wired mapping. The kernel validates and retains the complete
physical write plan before state mutation.

#### Abort registers

```text
a0  MICROS_PM_CONTROL_ABORT_RESERVED
a1  opaque transaction
a2..a6  zero
a7  MICROS_SYSCALL_ABI_PM_CONTROL
```

#### Authority and phase

Both commands require:

- the exact current process and thread equal the recorded manifest PM binding;
- the active PM endpoint and profile equal that binding;
- the profile contains only the expected PM-control kernel-operation bit;
- VM handoff and frame ownership are `HANDED_OFF`;
- launcher bootstrap is `SEALED`;
- the exact caller is the manifest PM service; and
- PM-control state validates.

The kernel seal is authoritative. The PM-local `runtime_enabled` flag is a
required service invariant but is not trusted by the syscall.

#### Reserve transition

`RESERVE` preflights:

- exact command shape and output range;
- complete kernel object, endpoint, scheduler, ownership, bootstrap, VM, and
  PM-control invariants;
- no active PM kernel reservation;
- one free, nonquarantined process slot with an available next generation;
- no live root, thread, endpoint, profile, grant, scheduler, or frame state for
  that slot;
- a nonzero next transaction value without wrap; and
- unchanged output bytes on every failure.

Only after full preflight does one non-failing commit:

1. create one live kernel process generation;
2. leave its root zero, live-thread count zero, primary endpoint `NONE`,
   profile zero, and endpoint lifecycle unconsumed;
3. bind that exact process generation and the new transaction in fixed
   PM-control state; and
4. write the complete result through the retained PM output plan.

The reserved process is not a runnable process and is not visible through IPC.
It satisfies the accepted empty-process shape: no thread, endpoint, address
space, or process-bound frame exists.

#### Abort transition

`ABORT_RESERVED` requires:

- the exact active nonzero transaction;
- the exact reserved process generation;
- no root, thread, endpoint, profile, grant, scheduler state, or process-bound
  frame;
- no later PM-control phase; and
- complete registry validation.

One non-failing commit releases the process generation, clears the reservation,
and retains the consumed transaction value so stale aborts cannot affect a
later reservation.

Successful abort releases every live resource but does not restore
byte-identical history. The selected process slot retains its one newly
consumed generation and follows ADR-0019's exact free-or-quarantined release
rule; PM-control retains its one newly consumed transaction value. Endpoint,
scheduler, and ownership state remain unchanged, and both stale identities
must remain invalid.

#### Syscall results and failure ordering

Successful reserve and abort return `MICROS_SYSCALL_ABI_OK`. Returning
failures use the existing syscall results:

```text
MICROS_SYSCALL_ABI_ARGUMENT      unknown command, nonzero reserved register,
                                bad size/alignment, or zero abort token
MICROS_SYSCALL_ABI_UNAUTHORIZED exact PM role/profile/binding mismatch
MICROS_SYSCALL_ABI_STATE        wrong handoff/seal phase, duplicate reserve,
                                stale/foreign abort, or duplicate abort
MICROS_SYSCALL_ABI_MEMORY_FAULT reserve output is not wholly writable
MICROS_SYSCALL_ABI_CAPACITY     no usable process generation or no next
                                nonzero transaction value
```

The returning preflight order is:

1. command and scalar shape;
2. current process/thread resolution, whose impossible failure is fatal;
3. exact PM authority;
4. complete object, endpoint, scheduler, ownership, bootstrap, and PM-control
   validation, whose corruption is fatal;
5. launcher, VM-handoff, reservation, and transaction phase;
6. reserve-output translation where applicable;
7. process-generation and transaction capacity; and
8. one non-failing commit.

Every returning failure preserves authoritative state and reserve-output bytes
exactly. Unknown commands, stale tokens, and duplicate operations never become
success-shaped fallbacks.

### Spawn transaction contract

The complete future spawn transaction has these PM-owned stages:

```text
PM_RECORD_RESERVED
KERNEL_PROCESS_RESERVED
VFS_IMAGE_PREPARED
VM_MAPPINGS_FROZEN
VFS_DESCRIPTORS_PREPARED
KERNEL_EXECUTION_PREPARED
PM_VFS_COMMITTED
ACTIVE
```

The opaque resources are:

```text
PM record handle and semantic PID
kernel process-reservation transaction
VFS executable token and VM-readable grant
VM load-complete token and mapping generation
VFS descriptor-preparation token
kernel prepared-process token, endpoint, and first thread
parent call reply token
```

`VM_MAPPINGS_FROZEN` is a later token-bound address-space lifecycle phase.
Only that exact phase may contain a live process with a root and mappings but
no thread, endpoint, or profile. The future VM mapping design must extend
complete validation for that shape and clear it during rollback or consume it
during PM-only execution preparation. It does not create a generic exception
for incomplete rooted processes, and Step 10 target code does not add or enter
the phase.

The native transition model exercises every stage. Step 10 production PM
exposes no successful spawn request before the later VM mapping and
executable-path ADRs. The isolated QEMU PM self-test invokes only the kernel
empty-process reservation and abort commands; it does not allocate an
application record or consume PID 1.

The later directed transaction is:

1. validate the exact running parent or begin the trusted init transaction;
2. reserve a PM record and never-reused PID;
3. invoke PM-control `RESERVE`;
4. ask VFS to resolve and buffer the bounded executable and argument metadata;
5. let VFS grant VM read-only access to its resident executable buffer;
6. ask VM to create the reserved process's root, allocate and initialize
   frames, remove scratch aliases, freeze the mapping generation, and return a
   kernel-issued load-complete token;
7. ask VFS to prepare bounded explicit descriptor actions;
8. invoke the later PM-control prepare command with the reservation,
   load-complete token, initial context, and scheduling policy;
9. have the kernel create the first thread and reserved endpoint, install only
   `APPLICATION`, execute local instruction synchronization, attach the
   context, seal the validated mapping generation, and leave the child hidden
   and held;
10. commit PM metadata and VFS descriptor state while the endpoint remains
    reserved;
11. invoke final activation as the publication point; and
12. reply to the parent with the child PID only after activation succeeds.

The later prepare command, activation command, VM mapping protocol, VFS
messages, spawn request payload, arguments, environment, and descriptor-action
encoding require their own reviewed designs. This ADR fixes their authority
and ordering but does not assign their wire fields prematurely.

### Prepublication rollback

Every expected failure before activation returns one explicit spawn failure to
the parent and releases resources in exact reverse ownership order.

| Highest completed stage | Required rollback |
| --- | --- |
| `PM_RECORD_RESERVED` | clear PM record; PID remains consumed |
| `KERNEL_PROCESS_RESERVED` | abort kernel reservation, then clear PM record |
| `VFS_IMAGE_PREPARED` | release executable grant/buffer, abort kernel reservation, clear PM record |
| `VM_MAPPINGS_FROZEN` | release VM mappings/root, release VFS image, abort kernel reservation, clear PM record |
| `VFS_DESCRIPTORS_PREPARED` | abort descriptors, release VM mappings, release VFS image, abort kernel reservation, clear PM record |
| `KERNEL_EXECUTION_PREPARED` | kernel abort clears context/thread/endpoint/profile/seal, then the preceding reverse order |
| `PM_VFS_COMMITTED` | revert hidden PM/VFS commit, kernel abort, then the preceding reverse order |

Each acknowledgment must carry the exact transaction identity and expected
stage. A stale, duplicate, foreign, or out-of-order acknowledgment is an
invariant failure, not an alternative rollback path.

Loss of the exact parent or cancellation of its spawn reply token before
activation is an expected prepublication abort. PM completes the same reverse
rollback without publishing the child or attempting a reply. Once activation
commits, the child is `RUNNING` and ordinary orphan reparenting applies.

Activation is the publication point. After the active endpoint and runnable
thread become visible, failure is process exit or fatal service failure; it is
never reported as a failed spawn with a hidden child.

### PM client protocol

PM uses three user message types:

```text
MICROS_PM_MESSAGE_EXIT   = 0x00010001
MICROS_PM_MESSAGE_WAIT   = 0x00010002
MICROS_PM_MESSAGE_RESULT = 0x00010003
```

The protocol version is:

```text
MICROS_PM_PROTOCOL_VERSION = 1
```

Every multibyte payload field uses little-endian encoding. Signed 32-bit
fields use the exact `int32_t` two's-complement bit pattern. PM decodes bytes
explicitly and does not cast a payload to a host C structure.

Every request must be a `call` carrying a nonzero kernel-issued reply token.
The `APPLICATION` profile has no ordinary `SEND` authority to PM. A token-zero
request is therefore an invariant violation in the accepted profile graph.

The kernel-written source endpoint, not a PID in payload data, identifies the
caller. PM resolves it to exactly one `RUNNING` application record.

#### Exit request

The 48-byte payload is:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..11   signed 32-bit normal exit code
bytes 12..47  zero
```

An accepted exit does not reply. The caller remains blocked until later
endpoint teardown cancels its call and makes return impossible.

Malformed, unknown-caller, early, duplicate, or invalid-state exit requests
receive `MICROS_PM_MESSAGE_RESULT` with an explicit error and do not mutate PM
state.

#### Wait request

The 48-byte payload is:

```text
bytes 0..3    protocol version = 1
bytes 4..7    wait flags
bytes 8..15   child PID selector; zero means any child
bytes 16..47  zero
```

The only version-1 flag is:

```text
MICROS_PM_WAIT_NOHANG = 0x00000001
```

Every other flag bit is invalid. Process-group selectors are not defined.

#### Result message

The 48-byte result payload is:

```text
bytes 0..3    protocol version = 1
bytes 4..7    original request type
bytes 8..11   signed PM result
bytes 12..15  flags = 0
bytes 16..23  child PID, or zero
bytes 24..27  exit kind, or zero
bytes 28..31  signed exit code, or zero
bytes 32..47  zero
```

Version 1 exit kinds are:

```text
MICROS_PM_EXIT_NONE   = 0
MICROS_PM_EXIT_NORMAL = 1
MICROS_PM_EXIT_FAULT  = 2
```

Only `NORMAL` is accepted from a user exit request. `FAULT` is reserved for a
later kernel/VM process-fault transition.

Stable PM results are:

```text
 0  OK
-1  BAD_TYPE
-2  BAD_VERSION
-3  MALFORMED
-4  CALLER
-5  STATE
-6  BUSY
-7  NO_CHILD
```

For every non-`OK` result, flags, child PID, exit kind, exit code, and the
reserved tail are zero. Spawn-specific result codes remain undefined until
the spawn request wire protocol is reviewed.

Internal corruption does not become an ordinary result.

Request validation order is:

1. known user message type;
2. protocol version and canonical fixed payload;
3. exact source endpoint to `RUNNING` PM record;
4. runtime-enabled phase;
5. operation-specific state and selector;
6. complete PM table invariants; and
7. one failure-atomic transition.

This order gives malformed clients stable errors without trusting their
claimed PID or state.

### Exit lifecycle

The portable PM model defines the running-process exit order even though the
Step 10 target cannot execute it yet.

For an exact `RUNNING` application:

1. validate the complete exit request and all PM relationships;
2. ask the later kernel exit-begin transition to make the sole thread
   permanently non-runnable, preserve its context and endpoint for cleanup,
   enter exact `EXIT_STOPPED`, and return one opaque exit transaction;
3. move PM state to `EXITING`, retain the normal status, and reparent every
   child to the designated reaper;
4. ask VFS to cancel process-owned operations and release descriptor state;
5. invoke the later PM-only exit-detach transition with that exact transaction;
   one atomic commit closes the endpoint, cancels IPC/reply rights and grants,
   detaches scheduler/context state, clears the process profile, releases the
   stopped thread generation, and enters exact `VM_RELEASE_PENDING`;
6. ask VM to present the same transaction and release mappings, private page
   tables, the root, and user frames, leaving the live process empty;
7. release the empty kernel process generation; and
8. clear endpoint and resource tokens, then move PM state to `ZOMBIE`.

`EXIT_STOPPED` and `VM_RELEASE_PENDING` are token-bound kernel lifecycle
phases, not ordinary process states or generic APIs. The later exit
implementation must extend complete object/address-space validation to accept
only their exact shapes. In particular, a process with a root and zero live
threads is valid only in `VM_RELEASE_PENDING` with the matching exit
transaction; every unrelated root/thread mismatch remains corruption. Step 10
target code does not add or enter either phase.

No wait result is delivered before step 8. The zombie retains only:

- PM record generation;
- PID;
- parent handle;
- exit kind and code; and
- any state required to complete one wait.

An expected failure is permitted only before kernel exit-begin commits. Once
the child is non-runnable, collaborator failure or identity mismatch is fatal.
PM never rolls an `EXITING` process back to `RUNNING`.

The accepted exit call is never replied to. Endpoint close cancels its
one-shot reply token.

### Wait lifecycle

PM supports exact-child and any-child wait.

For one exact running parent:

1. reject a second outstanding wait with `BUSY`;
2. identify children by exact internal parent handle, not by endpoint slot;
3. for an exact selector, require that PID to be a child;
4. for any-child selection, choose the lowest semantic PID among matching
   zombies for deterministic behavior;
5. if a matching zombie exists, prepare one result immediately;
6. if only matching live, spawning, or exiting children exist:
   - return `OK` with child PID, exit kind, and status all zero for `NOHANG`;
     or
   - store the selector, flags, and exact reply token and block;
7. if no matching child exists, return `NO_CHILD`.

When a child becomes `ZOMBIE`, PM checks the exact parent's stored selector.
For a match, it builds the result and attempts one token-bound reply.

PM marks the child reaped and frees or quarantines its record only after the
kernel has accepted the reply. Before the reply syscall, PM may stage a
reap-pending local transition; if the syscall returns an expected error, PM
restores the zombie and wait state before receiving another message. A
successful reply consumes the token exactly once.

If the parent disappears, endpoint-close or exit coordination clears its wait.
The child is reparented to init and remains a zombie until the reaper collects
it.

### Reparenting and reaper behavior

At parent exit-begin, PM first aborts every hidden `SPAWNING` child owned by
that parent and completes its exact prepublication rollback. It then rebinds
every `RUNNING`, `EXITING`, or `ZOMBIE` child to the sole live init reaper
before the parent can become a zombie.

After each reparent:

- a running or exiting child continues under init;
- a zombie child is tested immediately against init's outstanding wait; and
- no child retains a stale parent handle.

No ordinary application may exist before init is committed. Therefore absence
of a live reaper while an application child exists is an invariant failure.
Init exit is fatal and has no zombie/wait fallback in v0.1.

### Service loop and failure handling

PM is a single-threaded server using `receive`, `reply`, and
`reply_receive`.

It accepts:

- the exact bootstrap readiness acknowledgment;
- the exact bootstrap-sealed kernel notification;
- versioned application calls; and
- later exact VM/VFS transaction replies.

It rejects or treats as fatal:

- any delivered user envelope with the kernel-reserved high type bit, which
  the syscall boundary should already have rejected;
- kernel notifications from a non-`NONE` source;
- token-zero application requests under the accepted profile graph;
- stale or unregistered application endpoints;
- payload pointers used as authority;
- duplicate wait or spawn calls from one parent;
- stale transaction, load, descriptor, or preparation tokens;
- impossible collaborator phases; and
- any table state that fails complete validation.

Malformed user calls receive stable errors when their reply token is valid.
Internal invariant failures deliberately trap and use the existing exact
service-fault diagnostics. There is no silent fallback, inferred child, token
repair, or automatic retry.

### Target evidence

The Step 10 QEMU scenario uses a dependency-closed manifest containing:

- the bootstrap launcher;
- the real VM service;
- the real PM service; and
- one test probe allowed to call PM and the launcher.

It proves:

1. VM completes the irreversible handoff before PM release;
2. PM validates its configuration and sends exact readiness;
3. the probe receives stable `BAD_VERSION`, `MALFORMED`, and `CALLER`
   results without PM state mutation;
4. the probe reports ready and launcher completion seals bootstrap;
5. PM receives the exact coalesced bootstrap-sealed kernel event;
6. the PM test build performs one real operation-13 empty-process reserve;
7. PM validates the nonzero opaque result and aborts the exact reservation;
8. kernel process, endpoint, scheduler, and ownership topology return to zero
   live reserved resources, while the selected process generation and last
   PM-control transaction each advance exactly once and remain stale; and
9. the guest emits one deterministic PM success marker and shuts down cleanly.

The PM self-test path is compiled only in the isolated QEMU PM image.
Production PM does not reserve and abort a child merely to prove availability.

Native tests provide the behavior that cannot yet be integrated with absent
peers:

- exact PM role and tuple validation plus exact `APPLICATION` profile ID,
  name, operations, targets, and inert pre-activation state;
- table initialization and complete invariant validation;
- PID and PM-generation exhaustion;
- init and parent/child construction;
- exact and any-child wait;
- `NOHANG`, no-child, duplicate-wait, and reply-failure behavior;
- every exit and zombie transition;
- exact exit-transaction and `EXIT_STOPPED`/`VM_RELEASE_PENDING` phase
  validation;
- reparenting and reaper collection;
- every spawn stage and reverse rollback suffix;
- parent-loss rollback before activation and orphan reparenting after
  activation;
- stale, duplicate, foreign, and out-of-order collaborator tokens;
- PM-control authority, phase, output-buffer, reserve, abort, and rollback;
- successful-abort generation advancement and terminal-generation
  quarantine;
- byte-exact failure preservation; and
- a deterministic replayable mixed transition model.

The implementation must add fail-closed validation ownership for the new
source, public-header, service-runtime, syscall, manifest-role, model, and QEMU
paths.

## Invariants

- PM is the sole owner of application PID, parent, zombie, and wait state.
- A PM PID, PM handle, kernel process handle, endpoint, service ID, thread
  handle, and spawn token are distinct identities.
- PID zero is never assigned.
- A consumed PID is never reused in v0.1.
- Every non-init application in `RUNNING`, `EXITING`, or `ZOMBIE` has one
  exact live parent handle.
- Exactly one live init record is the reaper after the first successful init
  spawn.
- Static services are not ordinary application children or zombies.
- A `SPAWNING` child is not externally visible and has no active endpoint.
- Step 10's reserved kernel process has no root, thread, endpoint, profile,
  grant, scheduler state, or process-bound frame.
- Only exact PM authority can reserve or abort that process.
- PM-control transaction values are nonzero, monotonic, opaque, and
  nonreused.
- No successful spawn result exists before final activation.
- Final activation is the child publication point.
- Every prepublication failure releases exactly the resources owned by the
  transaction in reverse order.
- An accepted exit never returns to the exiting thread.
- No process becomes `ZOMBIE` before all non-PM execution resources are gone.
- A zombie is delivered to a parent at most once.
- A PM record is not reused before successful wait collection.
- One parent has at most one outstanding wait and one active spawn call.
- A blocked wait retains the exact kernel reply token from that call.
- Payload PIDs and tokens never replace kernel-written source identity.
- PM runtime creation remains disabled until the exact bootstrap-sealed
  kernel event.
- Launcher sealing never restores or transfers launcher authority to PM.

## Consequences

- PM becomes a real user-space service without pretending that dynamic memory
  or executable loading already exists.
- The first post-handoff child reservation is useful and fully rollbackable
  while preserving all accepted wired-only invariants.
- PID and parent semantics can be tested before VFS and VM mapping peers are
  available.
- Full spawn gains a safer ordering than the original ADR-0009 wording:
  thread, endpoint, profile, and context appear only after mappings are
  complete.
- Exit and wait behavior has one stable protocol before init and shell depend
  on it.
- The bootstrap-sealed event gives PM an explicit runtime start condition
  without polling or reviving launcher authority.
- Static service failure remains fatal until RS; PM does not become an
  accidental service manager.
- The implementation adds state and model tests that are not yet exercised by
  a real application, but each deferred integration point and replacement
  condition is explicit.

## Alternatives considered

### Implement dynamic VM mapping inside the PM task

This would violate ADR-0045's explicit boundary and combine two independently
reviewable authority changes. PM metadata does not justify pulling frame
allocation, PTE mutation, fault delivery, ELF loading, and mapping rollback
into one outcome.

### Reserve process, thread, endpoint, and profile immediately

A rootless live thread and endpoint would violate the accepted empty-process
shape and create lifecycle state that cannot be completed or safely released
through the current post-handoff memory API. Reserving only the empty process
keeps the scarce slot without publishing incomplete execution machinery.

### Keep all PM behavior as a host-only model

That would not establish a real PM bootstrap identity, service loop, privilege
profile, runtime gate, or user-to-kernel reservation authority. The QEMU slice
is small but necessary.

### Treat static services as PM children

The static launcher and later RS own different lifecycle semantics. Giving
them ordinary application PIDs, exit calls, and wait parents would conflate
bootstrap/recovery policy with application process semantics.

### Use endpoints as PIDs

Endpoints are reusable kernel IPC identities. Reusing them as semantic process
IDs would erase the PM authority boundary and make zombie identity disappear
when the endpoint closes.

### Omit zombie state and return status during teardown

That either forces expensive kernel/VM resources to remain until wait or loses
the result if a parent waits after teardown. The MINIX two-condition lifecycle
is the simpler correct boundary.

### Let PM poll launcher or VFS readiness

Polling adds timing assumptions and leaves no authoritative transition.
One kernel-origin event at irreversible launcher sealing is deterministic and
uses the existing notification mechanism.

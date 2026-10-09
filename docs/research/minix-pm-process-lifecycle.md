# MINIX PM Process-Lifecycle Baseline

## Purpose

This study fixes the MINIX behavioral baseline for development-DAG Step 10:
the process manager's semantic process table, parent/child relationships,
creation coordination, exit, zombie retention, wait, and failure cleanup.

It is the sole ADR-0025 classification ledger for the initial `micros` PM
outcome. The first implementation remains constrained by the accepted static
VM handoff: PM may own process metadata and reserve a hidden kernel process,
but it may not create a new address space, allocate user frames, publish an
endpoint, or run a child before a separately reviewed post-handoff VM mapping
protocol exists.

This study records behavior, authority, ordering, and failure evidence. It does
not authorize copying MINIX source, structures, identifiers, or wire formats.

## Reference

- Repository: local MINIX 3 source tree
- Commit: `4db99f4012570a577414fe2a43697b2f239b699e`
- Commit date: 2018-11-14
- Relevant areas:
  - `minix/servers/pm/main.c`
  - `minix/servers/pm/mproc.h`
  - `minix/servers/pm/forkexit.c`
  - `minix/servers/pm/exec.c`
  - `minix/servers/pm/utility.c`
  - `minix/servers/pm/schedule.c`
  - `minix/servers/vm/fork.c`
  - `minix/servers/vm/exit.c`
  - `minix/servers/vfs/main.c`
  - `minix/servers/vfs/misc.c`
  - `minix/servers/vfs/exec.c`
  - `minix/kernel/system/do_fork.c`

The local checkout was verified at the exact fixed reference commit before
this study was written.

## Fixed baseline

### PM owns semantic process state

MINIX PM owns one bounded `mproc` table. Each live entry contains the semantic
identity and lifecycle state that do not belong in the kernel:

- process ID;
- exact kernel endpoint;
- parent slot;
- wait selector and blocked-wait state;
- exit and termination status;
- zombie and parent-notified state;
- credentials, signal state, accounting, and process name; and
- scheduling-policy metadata
  (`minix/servers/pm/mproc.h:19-83`).

The kernel owns execution and IPC mechanism state, VM owns address-space
policy, and VFS owns descriptor state. PM's table is the authority for process
relationships and wait-visible lifecycle.

The table is bounded. Creation checks capacity before beginning work, and PM
keeps an exited child entry until both resource teardown and parent collection
have completed (`minix/servers/pm/forkexit.c:1-18,55-75,793-807`).

### PM initializes from authoritative boot identities

At startup, PM initializes every table slot, imports the kernel boot image,
records each exact boot endpoint, assigns process IDs and parent relationships,
and synchronizes the corresponding process inventory with VFS
(`minix/servers/pm/main.c:137-238`).

MINIX assigns init as its own parent and makes RS the parent of most system
services. These exact relationships are consequences of the MINIX RS boot
model, not a requirement to make the `micros` static launcher or PM pretend to
be RS.

The baseline behavior to preserve is:

- PM does not infer a live process from a name;
- imported identities are exact-generation endpoints;
- PM separately owns semantic identity and parentage; and
- cooperating services do not independently invent process-table entries.

### Process IDs are PM-owned and distinct from endpoints

MINIX allocates a free process ID by scanning PM's semantic table and avoiding
both live process and process-group identifiers
(`minix/servers/pm/utility.c:31-50`). The process ID is independent from the
kernel endpoint generation.

The reusable MINIX PID range and process-group collision rule are not required
ABIs for `micros`. The authority boundary is required: PM alone allocates and
publishes semantic process IDs, and an endpoint is never silently treated as a
PID.

### Creation is coordinated across VM, kernel, VFS, and scheduling

MINIX `fork` first rejects table exhaustion, chooses a free PM slot, and asks
VM to copy the parent's memory state. VM creates the child address space and
invokes the kernel `SYS_FORK`, which copies execution state, advances the
endpoint generation, and leaves the child inhibited and unscheduled
(`minix/servers/pm/forkexit.c:45-82`,
`minix/servers/vm/fork.c:32-100`, and
`minix/kernel/system/do_fork.c:26-117`).

After VM has created the kernel child, PM treats the transition as committed
enough that ordinary failure is no longer allowed. PM copies semantic parent
state, assigns a PID, and asynchronously asks VFS to duplicate descriptor
state. The parent and child do not receive successful fork replies until VFS
has acknowledged and scheduling has accepted the child
(`minix/servers/pm/forkexit.c:82-139` and
`minix/servers/pm/main.c:365-396`).

VFS duplicates the parent's descriptor references, root, and working
directory, then records the exact child PID and endpoint
(`minix/servers/vfs/misc.c:574-629`).

The behavioral baseline is:

1. reject known capacity or authority failures before child publication;
2. keep the child non-runnable and externally unpublished during coordination;
3. establish one consistent child identity across all owners;
4. make success visible only after every required owner is prepared; and
5. tear down the hidden child if a later prepublication stage fails.

MINIX's inability to fail safely after `vm_fork()` is not baseline behavior to
copy. It is evidence that creation requires an explicit transaction and
failure boundary.

### `exec` divides pathname and image policy from PM lifecycle policy

MINIX PM forwards `exec` to VFS and suspends the caller
(`minix/servers/pm/exec.c:34-55`). VFS:

- copies the argument frame before destroying the old image;
- resolves the path and executable format;
- coordinates memory replacement through VM-facing loader helpers;
- asks PM to update process metadata and credentials;
- copies the new stack;
- closes close-on-exec descriptors; and
- returns the final PC and stack pointer
  (`minix/servers/vfs/exec.c:183-365`).

PM then resets process-semantic state and asks the kernel to install the new
execution context (`minix/servers/pm/exec.c:60-198`).

MINIX marks a partially replaced image and kills the process if failure occurs
after the destructive point. That behavior demonstrates the importance of the
destructive boundary, but `micros` has already selected a safer spawn-first
transaction in ADR-0009:

- the child is new and hidden;
- VM freezes one complete mapping generation;
- PM prepares the execution context only with a load-complete token; and
- no old running image must be restored.

### Exit first prevents further execution

For ordinary exit, MINIX PM does not reply to the exiting caller
(`minix/servers/pm/forkexit.c:243-261`). It first makes the process
non-runnable, marks VM's process state as exiting, and then starts VFS cleanup
while the exact process identity still exists
(`minix/servers/pm/forkexit.c:265-369`).

The ordering is deliberate. VFS may need the still-valid endpoint to cancel
operations involving the exiting process before the kernel process is
destroyed. For a normal exit, PM marks the child zombie immediately after
starting VFS cleanup and may satisfy the parent's wait before VFS, kernel, and
VM teardown complete. Core-dump exit delays that notification until VFS
acknowledges the dump (`minix/servers/pm/forkexit.c:359-385,417-456,590-727`).

After the VFS acknowledgment, MINIX:

1. receives VFS's exit acknowledgment;
2. stops scheduling policy;
3. clears the kernel process;
4. releases VM mappings and VM process state; and
5. frees the PM entry only when both cleanup and parent collection permit it
   (`minix/servers/pm/main.c:353-364` and
   `minix/servers/pm/forkexit.c:417-468`).

The reusable baseline is:

- accepted exit never returns to the exiting thread;
- no new user execution begins after exit starts;
- service-owned resources are released before kernel identity disappears when
  those services require that identity for cleanup;
- semantic parent notification and execution-resource cleanup are tracked as
  separate completion conditions;
- the PM record is not reused until both conditions complete; and
- failure after the non-runnable transition is an invariant or system failure,
  not a rollback to running.

`micros` deliberately chooses a stricter publication order: it does not enter
`ZOMBIE` or deliver wait status until endpoint, IPC, grant, thread, mapping,
and process teardown complete. This is a compatible safety extension, not
MINIX baseline parity.

### Zombie state separates resource teardown from parent collection

MINIX marks an exited child as a zombie independently from VFS, kernel, and VM
teardown. For a normal exit, that zombie can be wait-visible while expensive
execution resources still exist; the PM table entry remains protected from
reuse until asynchronous cleanup also completes
(`minix/servers/pm/forkexit.c:371-468,590-727`).

The PM table entry becomes free only after:

1. exit has completed far enough that the child is a zombie;
2. a matching parent or tracer wait has consumed the status; and
3. no asynchronous VFS or event operation still references the entry
   (`minix/servers/pm/forkexit.c:466-468,793-807`).

This two-condition lifecycle is core PM behavior. Releasing the semantic entry
at endpoint close would lose the parent's wait result; keeping the complete
address space until wait would leak scarce execution resources. `micros`
preserves both conditions but completes resource teardown before making the
zombie wait-visible.

### Wait supports immediate, blocking, and no-child outcomes

MINIX `wait4` scans for children selected by exact PID, any child, or process
group. For a matching zombie, PM replies immediately with the child PID and
encoded status. For matching live children, PM either:

- returns zero for `WNOHANG`; or
- records one blocked wait selector and suspends the parent.

If no qualifying child exists, PM returns `ECHILD`
(`minix/servers/pm/forkexit.c:472-563`).

When a child later becomes a zombie, PM tests the stored selector, replies to
the exact waiting parent, clears the wait state, marks the child collected,
and accumulates child accounting before freeing the record when cleanup is
otherwise complete (`minix/servers/pm/forkexit.c:566-727`).

The Step 10 baseline requires:

- exact-child and any-child selectors;
- one blocking wait per v0.1 parent thread;
- optional nonblocking observation;
- explicit no-child failure;
- one status delivery per child; and
- no child-record reuse before successful status delivery.

Process-group selection, tracing, and resource-usage copying are later POSIX
breadth and are not part of this dependency-ready outcome.

### Parent exit reparents children to the designated reaper

MINIX reparents each child of an exiting process to init. A zombie child is
immediately reconsidered against init's wait state
(`minix/servers/pm/forkexit.c:388-411,637-663`).

This prevents orphaned zombie records and leaves one explicit semantic owner
for every child. The exact MINIX boot-time fiction that init is its own parent
is not required. `micros` can represent no parent directly and designate the
first init application as the reaper.

### PM validates exact message sources

The PM main loop receives one message, resolves the kernel-written source
endpoint to a live PM table entry, drops delayed calls from exiting processes,
dispatches only known PM calls or exact VFS replies, and sends a reply unless
the operation deliberately suspended
(`minix/servers/pm/main.c:45-101`).

VFS replies are associated with the exact process endpoint and one outstanding
VFS-call flag. A reply with no matching operation is a PM invariant failure
(`minix/servers/pm/main.c:292-414`).

For `micros`, source and one-shot reply token are kernel-owned. A PID or
transaction value in payload data cannot establish caller authority.

### Post-stop cleanup failures are fatal

MINIX panics when required kernel, VM, or VFS lifecycle operations violate
their contract after exit or committed fork progress
(`minix/servers/pm/forkexit.c:305-368,417-457`).

This is appropriate baseline evidence for the pre-recovery MVP. Before
publication, expected resource or input failures roll back and return an
explicit error. After the child is running or exit has made it non-runnable,
an impossible collaborator mismatch cannot be represented as ordinary success
or a return to the previous state.

## Accepted `micros` constraints

### Spawn replaces fork-first creation

ADR-0009 selects a new hidden child rather than copying a running parent.
There is no v0.1 `fork`, copy-on-write, unrestricted descriptor inheritance,
or destructive `exec`.

The directed PM/VFS/VM transaction preserves the relevant MINIX behavior:

- PM coordinates semantic identity and publication;
- VFS owns pathname, executable buffering, and descriptors;
- VM owns frame allocation, loading, mappings, and mapping completion;
- the kernel owns process, thread, endpoint, profile, context, and runnable
  mechanisms; and
- the child is not externally visible before all owners are prepared.

### Static VM handoff currently forbids a child address space

ADR-0045 exposes no post-handoff allocation, mapping, root creation, root
destruction, or load-complete-token operation. Every existing static mapping
is wired.

Step 10 therefore may:

- allocate PM metadata;
- reserve one hidden kernel process object with no root, thread, endpoint, or
  profile; and
- abort that empty reservation without touching VM-owned memory.

It may not:

- allocate or map a child frame;
- create a child thread or endpoint;
- install the `APPLICATION` profile;
- prepare an execution context;
- activate a child; or
- claim a successful spawn.

Those transitions remain blocked on the later VM mapping design and executable
path.

### Process, thread, endpoint, and PM identity remain distinct

The kernel already uses generation-safe process, thread, and endpoint objects.
PM adds a separate semantic PID and a separate generation-safe PM record
handle. A child transaction token is also distinct.

No representation may be treated as interchangeable merely because v0.1 has
one thread and one endpoint per running application.

### Fixed messages and reply tokens replace MINIX call-number replies

PM uses the accepted 64-byte IPC message ABI. The source endpoint and reply
token are kernel-written. Blocking wait retains the exact request token and
later consumes it once; it does not send to a PID or infer a reply destination
from mutable parent metadata.

### Static services are not application children

The static launcher, VM, PM, TTY, RAMFS, and VFS are prepared by the bootstrap
manifest. Until RS exists, their failure is fatal and their lifecycle is not
represented as an ordinary application parent/child tree.

PM may retain the immutable bootstrap service inventory for diagnostics, but
Step 10 assigns semantic PIDs only to spawned applications. The first
successfully committed init application becomes PID 1 and the designated
reaper.

## ADR-0025 classification

| MINIX behavior or difference | Classification | `micros` treatment |
| --- | --- | --- |
| PM owns process IDs, parent/child relationships, exit status, zombie state, and wait state | Baseline parity | One bounded PM table is authoritative for application lifecycle metadata |
| Kernel, VM, VFS, and PM own distinct parts of creation and teardown | Baseline parity | Preserve the authority split with explicit transaction tokens |
| A child remains hidden and non-runnable until every required owner is prepared | Baseline parity | Final endpoint publication and scheduler admission are the spawn publication point |
| Exit first prevents further execution and does not reply to the exiting caller | Baseline parity | Accepted exit retains the call token only until endpoint teardown cancels it |
| Semantic collection and execution-resource cleanup are independent, and record reuse waits for both | Baseline parity | Preserve separate teardown and wait completion conditions |
| MINIX may make a normal-exit zombie wait-visible before VFS, kernel, and VM teardown | Compatible extension | Defer `ZOMBIE` and wait delivery until kernel, grant, VM, and VFS resources are gone |
| Wait has immediate zombie, blocking live-child, nonblocking, and no-child outcomes | Baseline parity | Exact-child and any-child selectors plus one `NOHANG` flag |
| Parent exit reparents children to init | Baseline parity | Reparent to the designated init reaper; no self-parent fiction |
| PM validates exact source identity and matching collaborator replies | Baseline parity | Exact endpoint generations, transaction phase, and opaque token matching |
| MINIX PID reuse | Compatible extension | Monotonic nonzero 64-bit PIDs do not reuse in v0.1; exhaustion is explicit |
| PM table parent fields use raw table indices | Compatible extension | Internal slot/generation handles reject stale parent and child references |
| MINIX fork can no longer fail normally after VM has created the kernel child | Compatible extension | Full prepublication rollback remains possible through explicit prepare tokens |
| MINIX replies are associated with per-process suspended-call flags | Required adaptation | One-shot IPC reply tokens bind each blocked wait or spawn caller thread |
| MINIX copies parent memory and descriptors | Compatible extension | Spawn loads a fresh image and applies explicit bounded descriptor actions |
| MINIX creates the child thread and endpoint during fork | Staged substitution | Step 10 reserves only an empty kernel process; later prepare creates thread, endpoint, profile, and context after VM load completion |
| MINIX PM and VFS initialize from kernel boot slots and RS parentage | Staged substitution | Static manifest services remain outside the application child tree until RS |
| MINIX can immediately fork, exec, and free dynamic mappings | Staged substitution | Step 10 models the transaction but cannot allocate user memory until the reviewed VM mapping protocol |
| MINIX handles signals, process groups, tracing, credentials, and `rusage` in PM | Staged substitution | Deferred to the later POSIX milestone; the Step 10 lifecycle has normal exit and wait only |
| MINIX wraps endpoint generations | Compatible extension | Existing kernel generation quarantine remains authoritative |
| MINIX partial exec kills the old process after destructive replacement | Compatible extension | Spawn-first creation keeps the new child hidden and rollback-capable |
| Core-service exit may be coordinated by PM/RS | Staged substitution | Static service failure remains fatal until RS and recovery are dependency-ready |

No dependency-ready behavior is classified as divergence requiring correction.

## Step 10 boundary

The first PM implementation must provide:

- one real statically launched PM service;
- exact PM-role bootstrap authority and readiness;
- one bootstrap-sealed kernel notification that enables runtime PM policy;
- a bounded, validated application lifecycle table;
- monotonic semantic PID allocation;
- parent/child and reaper invariants;
- versioned `EXIT` and `WAIT` protocol decoding;
- zombie retention and one-shot wait completion;
- a complete native spawn/exit/wait transition model;
- one PM-only kernel operation that reserves and aborts an empty hidden process
  after bootstrap sealing;
- malformed, stale, duplicate, capacity, and rollback evidence; and
- a QEMU scenario proving real PM startup, role authority, seal notification,
  protocol rejection, and empty-reservation rollback.

The implementation must not claim a successful application spawn. The later
VM mapping and executable-path tasks must add:

- VM-selected child frames and mappings;
- mapping-generation freeze and load-complete tokens;
- VFS executable and descriptor preparation;
- PM-only thread, endpoint, profile, and context preparation;
- final endpoint publication and scheduler admission;
- running-process exit teardown across VFS, kernel, and VM; and
- init and shell integration.

## Specification basis

- [ADR-0003](../adr/0003-kernel-responsibility-boundary.md)
- [ADR-0004](../adr/0004-ipc-and-endpoint-abi.md)
- [ADR-0005](../adr/0005-direct-memory-grants.md)
- [ADR-0009](../adr/0009-spawn-before-fork.md)
- [ADR-0011](../adr/0011-process-thread-and-hart-model.md)
- [ADR-0019](../adr/0019-kernel-object-identity-and-ownership.md)
- [ADR-0021](../adr/0021-generation-safe-user-address-spaces.md)
- [ADR-0022](../adr/0022-user-execution-contexts-and-u-mode-entry.md)
- [ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0045](../adr/0045-static-vm-bootstrap-and-handoff.md)
- [System overview](../architecture/system-overview.md)
- [Development dependency DAG](../architecture/development-dag.md)
- [Testing strategy](../testing-strategy.md)

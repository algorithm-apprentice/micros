# ADR-0043: Static Bootstrap Launcher and Embedded Manifest

- Status: Accepted
- Date: 2026-10-08
- Refines: ADR-0003, ADR-0004, ADR-0006, ADR-0007, ADR-0011,
  ADR-0012, ADR-0025, ADR-0029, ADR-0030, ADR-0037, ADR-0040,
  ADR-0041, and ADR-0042
- Supersedes in part:
  - ADR-0007's statement that bootstrap authority is revoked after `init` is
    released. The static launcher instead seals after the last static manifest
    service is ready. ADR-0009's PM/VFS/VM spawn transaction creates `init`
    later without launcher authority.

## Context

Development-DAG Step 7 is complete:

- generation-safe process, thread, endpoint, and hart objects;
- immutable privilege-profile mechanics;
- blocking IPC and one-shot reply tokens;
- direct grants and checked copies;
- wired post-handoff address-space reads;
- one unified RISC-V syscall namespace for operations 1 through 10; and
- one checked freestanding service ELF and runtime.

The next dependency-ready outcome is the smallest user-space component that
can release a fixed set of already embedded service images in reviewed order
and refuse to continue until each released service proves initialization.

The fixed MINIX baseline combines:

- a kernel boot-image table;
- RS privilege, system, and device tables;
- process inhibition while RS installs privileges;
- fixed table-order release;
- an `RS_INIT` request;
- a service-specific SEF callback;
- an `RS_INIT` result; and
- a reply that lets service startup return.

MINIX endpoints are already named while a service is inhibited. Its complete
RS additionally owns dynamic commands, DS publication, heartbeat checks,
replicas, restart, backoff, scripts, state transfer, and live update.

The canonical trace and the sole ADR-0025 classification ledger for this
outcome are in
[the MINIX RS and SEF bootstrap study](../research/minix-rs-sef-bootstrap.md).

`micros` already requires stricter boundaries:

- endpoints are generation-safe and may remain `RESERVED`;
- profiles are immutable named table entries;
- all initial service mappings and contexts must exist before the irreversible
  VM handoff;
- VM readiness is not the ownership commit itself;
- TTY has a separate two-phase console and IRQ handoff; and
- recovery remains after DS, the user scheduler, and RS in the development
  DAG.

This decision defines the launcher, manifest, readiness protocol, kernel
authority, failure boundary, and acceptance evidence without implementing any
service or later handoff policy.

## Decision

### Single outcome

This outcome defines:

- one pointer-free immutable embedded-manifest format;
- one fixed static-service capacity and production service identity set;
- one generated immutable catalog of already linked service images;
- complete manifest validation and deterministic topological ordering;
- kernel preparation of every static service while it remains held;
- one launcher-only bootstrap control syscall;
- atomic exact-profile installation, endpoint publication, and thread release;
- one versioned readiness call and token-bound acknowledgment;
- deterministic readiness deadlines with no sleeps;
- fatal bootstrap diagnostics and rollback boundaries;
- one irreversible launcher-authority seal;
- native manifest and transition-model evidence;
- real QEMU success and expected-failure evidence;
- fail-closed validation ownership; and
- the later implementation task and green commit boundaries.

### Non-goals

This outcome does not define or implement:

- VM frame-allocation, mapping, page-fault, or ownership-handoff policy;
- the `VM_READY` payload or one-way handoff implementation;
- PM spawn, exit, wait, or process semantics;
- TTY buffering, UART programming, PLIC routing, `irq_complete`, or
  `console_handoff_commit`;
- RAMFS or VFS protocols;
- the application ELF spawn path;
- init, shell, or application behavior;
- DS publication or dynamic discovery;
- the user-space scheduler policy;
- RS restart, heartbeat, replica, recovery, or live update;
- a dynamic, mutable, file-backed, or network-provided manifest;
- a general service-framework callback layer;
- a runtime-library expansion beyond ADR-0042;
- service implementations;
- post-handoff process teardown; or
- SMP synchronization.

The implementation phase must not add placeholder production services. Its
QEMU probe services are test-only standalone ELFs and do not claim VM, PM,
TTY, RAMFS, or VFS behavior.

### Dependency position

This is development-DAG Step 8:

1. the kernel mechanisms and freestanding runtime are complete;
2. this launcher prepares and releases fixed embedded services;
3. the VM server and one-way ownership handoff are the next outcome;
4. PM, TTY, RAMFS, VFS, executable spawn, init, and shell remain later nodes.

All static service address spaces and contexts are prepared before any
possible VM handoff. After handoff, launcher release changes only bootstrap
state, one exact profile assignment, endpoint visibility, and scheduler
run-time state.

Implementation may begin only after this ADR is independently reviewed,
marked Accepted, and merged.

## Static service identity

### Capacity

The v0.1 static manifest has:

```text
MICROS_BOOTSTRAP_SERVICE_CAPACITY = 6
```

This bound covers exactly:

1. bootstrap launcher;
2. VM;
3. PM;
4. TTY;
5. RAMFS; and
6. VFS.

DS, the user scheduler, RS, init, shell, drivers other than TTY, and
applications are not static manifest entries. Adding another static service
requires a new reviewed need rather than increasing the bound speculatively.

Service IDs are nonzero values no greater than 63 so one `uint64_t`
prerequisite mask can name them. The production identities are:

| Service ID | Name | Process slot | Profile ID | Profile name | Direct prerequisite | Role |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `bootstrap-launcher` | 0 | 1 | `BOOTSTRAP_LAUNCHER` | none | controller |
| 2 | `vm` | 1 | 2 | `VM` | launcher | VM handoff owner |
| 3 | `pm` | 2 | 3 | `PM` | VM | ordinary static service |
| 4 | `tty` | 3 | 4 | `TTY` | PM | console owner |
| 5 | `ramfs` | 4 | 5 | `RAMFS` | TTY | ordinary static service |
| 6 | `vfs` | 5 | 6 | `VFS` | RAMFS | final static service |

The explicit chain preserves ADR-0007's development-DAG start order. A future
service design may add an additional prerequisite edge among these entries,
but may not remove a DAG predecessor or infer an edge from a name or profile.

Service ID, process slot, profile ID, and image ID are distinct domains even
when their initial numeric values happen to match. No implementation may
derive one from another.

During sequential development, a production manifest may contain a
dependency-closed prefix beginning with the launcher. It must not contain a
dummy implementation for an unavailable successor. Each later service
implementation adds its exact production entry and image.

An ordinary image requires every included ID to match the complete production
name, slot, profile ID/name, prerequisite, and role tuple above. Test-only
builds may select a separately compiled manifest class with structurally valid
probe identities and images. That class is unavailable in an ordinary build,
does not alter the production identity table, and is never accepted as a
production prefix.

## Canonical manifest

### Representation

Manifest version 1 is a fixed little-endian, pointer-free byte representation.
Its C declaration uses only fixed-width integer arrays and has compile-time
offset and size assertions.

The 64-byte header is equivalent to:

```c
struct micros_bootstrap_manifest_header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint16_t entry_size;
    uint16_t entry_capacity;
    uint16_t entry_count;
    uint16_t reserved0;
    uint32_t total_user_page_limit;
    uint32_t flags;
    uint32_t manifest_size;
    uint32_t reserved1;
    uint64_t reserved2[4];
};
```

The exact constants are:

```text
magic           = 0x3153424d  ("MBS1" in little-endian bytes)
version         = 1
header_size     = 64
entry_size      = 192
entry_capacity  = 6
manifest_size   = 64 + 6 * 192 = 1216
flags           = 0
```

Every reserved field is zero. `entry_count` is in `[1, 6]`.
`total_user_page_limit` is the exact sum of the active entries'
`user_page_limit` values. Active entries occupy indices zero through
`entry_count - 1`; every later entry byte is zero.

Each 192-byte entry is equivalent to:

```c
struct micros_bootstrap_manifest_entry {
    uint32_t service_id;
    uint32_t image_id;
    uint16_t process_slot;
    uint16_t stack_page_count;
    uint8_t profile_id;
    uint8_t reserved0[3];
    char service_name[32];
    char profile_name[32];
    uint64_t prerequisites;
    uint64_t ready_timeout_counter_ticks;
    uint32_t user_page_limit;
    uint32_t role_flags;
    uint64_t device_base;
    uint64_t device_length;
    uint32_t irq_source;
    uint32_t reserved1;
    uint64_t reserved2[8];
};
```

Both names:

- are nonempty;
- contain one NUL within the fixed field;
- contain only zero bytes after that NUL; and
- are unique in their respective manifest domain.

The representation contains no host pointer, target pointer, ELF pointer,
function pointer, command string, dynamic array, or mutable count.

### Versioning

The kernel and launcher accept exactly version 1. They do not:

- ignore an unknown version;
- accept a shorter or longer header;
- infer missing fields;
- treat nonzero reserved bytes as optional extensions; or
- fall back to a legacy interpretation.

A future manifest version requires a reviewed ADR plus coordinated kernel,
launcher, build-time checker, and acceptance changes.

### Service and page limits

For every entry:

- `service_id` is nonzero and at most 63;
- `process_slot` is less than `MICROS_PROCESS_CAPACITY`;
- `stack_page_count` is nonzero;
- `user_page_limit` is nonzero;
- image pages, stack pages, and launcher-only manifest-view pages do not
  exceed `user_page_limit`;
- all page-count addition is overflow checked; and
- the complete manifest limit fits the live bootstrap allocator after kernel
  reservations.

The controller has `ready_timeout_counter_ticks == 0`. Every non-controller
entry has one explicit nonzero timeout.

The page limit is an authorization bound, not a request to preallocate unused
anonymous memory. The loader allocates only the exact checked image, stack,
and launcher manifest-view pages required by this design.

The launcher stack is exactly one page in version 1. Its immutable manifest
view occupies the read-only, user-accessible, non-executable page immediately
below that stack:

```text
MICROS_BOOTSTRAP_MANIFEST_VIEW = 0x000000007fffe000

manifest view  [0x000000007fffe000, 0x000000007ffff000)
launcher stack [0x000000007ffff000, 0x0000000080000000)
```

The launcher image must end at or below `0x000000007fffe000`. Other services
retain ADR-0042's external stack ending at `MICROS_USER_VIRTUAL_END`.

### Prerequisites

Bit position `service_id - 1` names one prerequisite. Because version 1
service IDs are at most 63, bit 63 is always invalid. Any other set bit that
does not correspond to an active manifest service is also invalid.

Validation requires:

- every set bit names exactly one active manifest entry;
- no entry names itself;
- the launcher has an empty prerequisite mask;
- every non-launcher is reachable from the launcher;
- the installed production subset is dependency closed; and
- no role, service name, profile, process slot, image, device field, or source
  array position adds an implicit prerequisite.

### Role flags

Version 1 defines only:

```text
MICROS_BOOTSTRAP_ROLE_CONTROLLER     = 1 << 0
MICROS_BOOTSTRAP_ROLE_VM             = 1 << 1
MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER  = 1 << 2
```

Exactly one entry is the controller. At most one entry is VM and at most one
entry is the console owner. No entry may combine these roles.

Role flags select fixed bootstrap gate checks only. They do not grant:

- IPC operations or targets;
- kernel operations;
- device mappings;
- IRQ ownership;
- frame ownership;
- process lifecycle authority; or
- profile installation authority.

Those authorities come only from an exact immutable profile or a separate
reviewed kernel transition.

### Device and IRQ fields

ADR-0012 requires the static TTY assignment, so version 1 contains exactly one
optional device range and one optional IRQ source per entry.

For every entry without `CONSOLE_OWNER`:

```text
device_base   = 0
device_length = 0
irq_source    = 0
```

The production console entry contains:

```text
device_base   = 0x0000000010000000
device_length = 0x0000000000001000
irq_source    = 10
```

These values describe the fixed QEMU `virt` UART assignment. They do not map
the page, configure UART state, install a PLIC route, enable the source, or
authorize `irq_complete`. Those transitions remain under ADR-0012 and the
later TTY design.

No array of arbitrary device or IRQ resources is introduced. A second range
or source requires a later driver need and reviewed design.

## Embedded image catalog

### Image reference

`image_id` resolves one exact entry in a kernel-owned immutable image catalog.
Image ID zero is invalid. Active manifest entries use unique production image
IDs.

Each catalog entry contains:

- catalog version 1;
- exact image ID;
- exact ADR-0042 entry address;
- exact exported bootstrap-configuration address and size;
- exactly three generated load-segment descriptors;
- file-backed byte references;
- file and memory sizes;
- final permissions;
- exact image-page count; and
- the checked image-end address.

Pointer-bearing byte references exist only in the kernel-internal generated
catalog. They are not part of the pointer-free manifest copied to the
launcher.

### Build-time and boot-time validation

Every source ELF must first pass the existing ADR-0042 user-ELF checker.
Repository-owned generation then emits bounded immutable segment descriptors
and bytes. Boot does not add:

- a general ELF parser;
- relocation;
- dynamic linking;
- a file lookup;
- VFS involvement; or
- a reusable application loader.

The kernel still validates the generated descriptor before any
manifest-directed process, user-frame, endpoint, or scheduler mutation:

- catalog and image versions;
- exact three permission classes;
- entry address;
- segment alignment, bounds, nonoverlap, and W^X;
- page count;
- image end below the service's stack or launcher manifest page; and
- one aligned 128-byte bootstrap-configuration object wholly inside the
  writable segment with canonical initial bytes; and
- agreement with the manifest page limit.

The external image owner remains responsible for complete page zeroing, exact
file-byte copy, final permissions, external stack zeroing, `fence.i`, and
context preparation as required by ADR-0042.

## Static service bootstrap configuration

ADR-0042 deliberately gives `_start` no arguments. Each static service
therefore exports one 8-byte-aligned writable object at the exact generated
catalog address:

```c
struct micros_bootstrap_service_config {
    uint32_t version;
    uint32_t manifest_version;
    uint32_t service_id;
    uint32_t self_endpoint;
    uint32_t launcher_endpoint;
    uint32_t service_count;
    struct {
        uint32_t service_id;
        uint32_t endpoint;
    } services[MICROS_BOOTSTRAP_SERVICE_CAPACITY];
    uint64_t manifest_view_address;
    uint64_t reserved[6];
};
```

Its exact size is 128 bytes. The linked ELF contains all-zero initial bytes.
After reserving the exact endpoint and before preparing the thread context, the
kernel patches:

```text
version               = 1
manifest_version      = 1
service_id            = exact manifest service ID
self_endpoint         = exact reserved endpoint generation
launcher_endpoint     = exact launcher endpoint generation
service_count         = active manifest entry count
services              = every active {service ID, exact endpoint} pair in
                        ascending service-ID order, then zero entries
manifest_view_address = 0 for ordinary services
manifest_view_address = MICROS_BOOTSTRAP_MANIFEST_VIEW for the launcher
```

Every reserved field remains zero. Each listed endpoint may still be
`RESERVED`; knowing its value does not make it externally resolvable. The
kernel writes through the inactive root's exact physical translation, verifies
the final bytes, and retains no user pointer.

The configuration is startup data, not authority:

- the service ID cannot select another manifest entry;
- `self_endpoint` cannot replace the kernel-derived IPC source;
- `launcher_endpoint` is only the readiness-call destination;
- the service table is bounded static endpoint discovery and does not bypass
  endpoint activation, profile targets, prerequisite readiness, or DS's later
  dynamic publication role;
- the launcher manifest address is read-only mapped authority established by
  the kernel; and
- corrupting the object can only make the service fail its own startup or IPC
  checks.

This object is above the generic runtime boundary. It adds no `argc`, `argv`,
environment, TLS, constructor, global runtime state, or startup register
contract.

## Immutable privilege profiles

### Stable IDs and names

The bootstrap profile identities are:

| ID | Exact name |
| --- | --- |
| 1 | `BOOTSTRAP_LAUNCHER` |
| 2 | `VM` |
| 3 | `PM` |
| 4 | `TTY` |
| 5 | `RAMFS` |
| 6 | `VFS` |

The numeric ID and complete zero-padded name are both present in the manifest.
Validation resolves the immutable kernel table by ID and requires the stored
name to match byte for byte.

The launcher never builds, extends, combines, or edits a profile. There is no
fallback by service name, role, target, or device assignment.

### Bootstrap-minimum relationships

The launcher profile has:

- `receive`;
- `reply`, consumed by the specialized ready-acknowledgment transition;
- kernel-operation ID 0,
  `MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL`, which is bit zero in the
  existing 64-bit kernel-operation mask.

Every non-launcher bootstrap profile has:

- `receive`;
- `call`;
- the launcher profile in its exact call-target mask.

At this stage the launcher has no call, ordinary-send, or notify target, and a
non-launcher profile has no ordinary-send or notify target solely because it
participates in bootstrap. Future protocol ADRs add only their reviewed
relationships.

Future service ADRs add their protocol-specific operations, targets, grants,
and kernel operations to these stable IDs. This ADR does not guess those
permissions.

Only the launcher profile contains bootstrap-control authority. The profile
bit is necessary but not sufficient: the kernel also requires the exact
current launcher process generation and an unsealed bootstrap runtime.

### Source-only sealed launcher endpoint

This ADR refines the endpoint lifecycle only for the exact bootstrap launcher:

```text
ACTIVE -> SOURCE_ONLY -> FREE
```

`SOURCE_ONLY` preserves one exact endpoint generation solely so an already
staged readiness acknowledgment may retain and validate its canonical launcher
source. It is not an IPC destination and grants no new operation authority.

After the transition:

- ordinary call, send, notify, and receive-source destination resolution
  reject the launcher with `ENDPOINT_CLOSING` before queue or token mutation;
- immutable target-profile permission does not bypass that rejection;
- staged-message/completion validation may accept the exact endpoint only as
  the already recorded canonical source;
- endpoint close and later PM teardown may resolve it internally;
- no new reply token may name it as callee;
- the held launcher thread cannot originate IPC; and
- bootstrap-control operations reject the sealed controller binding.

Only `COMPLETE` may install `SOURCE_ONLY`, only for the exact launcher
generation, and only after all readiness calls have committed. Ordinary
services never enter this state. Registry validation requires a source-only
endpoint to own one exact live process/profile, have no sender or receiver
queue, no pending notification, no active token naming it as caller/callee,
and no runnable/current owner thread.

## Manifest validation and launch order

### Failure-atomic validation

The complete manifest, profile table, and image catalog are validated before
the first manifest-directed process, user-frame, endpoint, or scheduler
mutation.

Validation rejects:

- null or misaligned storage;
- bad magic, version, sizes, capacity, count, or total size;
- any nonzero reserved field or undefined flag;
- a nonzero unused entry byte;
- duplicate or invalid service IDs;
- duplicate or malformed names;
- an ordinary-build entry that differs from its exact production identity
  tuple;
- duplicate process slots;
- an out-of-range slot;
- duplicate or unresolved production image IDs;
- a missing, zero, or mismatched profile;
- a role/profile relationship violation;
- invalid page counts or sum overflow;
- an image that exceeds its page or virtual-address bound;
- malformed prerequisite bits;
- missing prerequisites;
- self-dependency;
- unreachable entries;
- duplicate controller, VM, or console roles;
- an invalid role combination;
- device or IRQ fields on a non-console entry;
- a console assignment different from the fixed UART page and source;
- a zero readiness timeout for a non-launcher; and
- any cycle.

Failure preserves all kernel objects, frame ownership, page tables, endpoint
state, scheduler state, bootstrap state, and output buffers.

### Deterministic topological order

The validator computes one order:

1. begin with an empty selected mask;
2. among unselected entries whose prerequisite mask is a subset of the
   selected mask, choose the lowest numeric service ID;
3. append it and set its selected bit;
4. repeat until every entry is selected.

The first entry must be the controller. If no candidate exists before all
entries are selected, validation reports a cycle. Source array order is never
a tie-break.

The kernel stores the validated order in fixed bootstrap state. The launcher
independently derives the same order from its read-only manifest view. A
release request that does not name the kernel's exact next entry is rejected.

## Kernel preparation before launcher entry

### Bootstrap runtime state

The kernel owns one fixed-capacity bootstrap runtime:

```text
UNINITIALIZED -> PREPARING -> RUNNING -> SEALED
                              \-> FAILED
```

Each active entry owns:

- exact manifest index and service ID;
- exact process handle;
- exact first-thread handle;
- exact private-root physical address;
- exact reserved endpoint;
- prepared page count;
- immutable intended scheduler priority, quantum, and preemptibility;
- state `PREPARED`, `STARTING`, or `READY`;
- readiness deadline while starting; and
- immutable role data copied from the validated manifest.

The runtime allocates no memory and has no reset operation.

### Preparation sequence

After complete non-mutating manifest, profile, and image preflight, the kernel
initializes the authoritative endpoint/IPC runtime once with the complete
immutable profile table. With SIE clear, it then uses two deterministic passes
in validated launch order.

Pass 1 establishes every endpoint identity:

1. reserve the exact manifest process slot and new process generation;
2. create its private root;
3. validate and install the exact embedded image;
4. create and zero its exact external stack;
5. for the launcher, create the read-only manifest-view page and copy the
   exact validated pointer-free manifest; and
6. reserve its generation-derived endpoint.

Only after every active endpoint is reserved does pass 2:

1. patch and verify each exact bootstrap service configuration, including the
   complete sorted service-ID/endpoint table;
2. create the sole v0.1 thread;
3. prepare its ADR-0042 entry context and perform the required `fence.i`;
4. retain the thread in exact `MICROS_THREAD_RTS_INACTIVE` state without
   scheduler assignment;
5. copy the immutable intended scheduler policy into bootstrap runtime state;
   and
6. validate the complete object, root, frame-owner, endpoint, configuration,
   unassigned-thread, and stored-policy relationship.

Every static service is therefore executable and context-prepared before the
launcher runs. This is required because generic preparation is unavailable
after ADR-0040's one-way handoff.

Non-launcher entries remain:

- endpoint `RESERVED`;
- profile uninstalled;
- thread inactive, scheduler-unassigned, and off ready queues; and
- externally unreachable.

After all entries are prepared, one final preflight validates scheduler
initialization, timer programming, launcher return context, root activation,
stack selection, exact launcher profile installation, endpoint activation,
exact scheduler admission from the stored policy, and the runnable-state
transition.

Its commit:

- installs only the launcher's exact profile;
- activates only the launcher endpoint;
- admits only the launcher thread with its exact stored scheduler policy;
- records the exact bootstrap controller process and endpoint generations;
- enters `RUNNING`; and
- starts the ordinary scheduler with the launcher as the sole runnable
  service.

Every fallible scheduler and timer check precedes the first profile, endpoint,
bootstrap-state, queue, current-thread, stack, or root mutation. The final
publication and first-entry commit contains no recoverable operation.

The launcher never reserves or loads an arbitrary object. It selects bounded
transitions over bindings that the kernel already created from the validated
manifest.

## Bootstrap-control syscall

### Unified namespace

Operation 11 is:

```text
MICROS_SYSCALL_OPERATION_BOOTSTRAP_CONTROL = 11
```

Operations 1 through 10 remain unchanged. The generic ADR-0042 runtime is not
expanded. Launcher code uses a launcher-private wrapper around the existing
raw eight-register syscall boundary.

The common register shape is:

```text
a7  11
a0  command
a1  service ID
a2  endpoint
a3  reply token or failure reason
a4  zero
a5  zero
a6  zero
```

Command, service ID, endpoint, and failure reason require zero upper 32 bits.
Unknown commands, nonzero unused registers, or malformed widths return the
existing `MICROS_SYSCALL_ABI_ARGUMENT` without bootstrap mutation.

The commands are:

```text
1  RELEASE
2  ACCEPT_READY
3  FAIL
4  COMPLETE
```

The operation is authorized only when:

- the exact current process is the recorded controller generation;
- its exact active endpoint is the recorded controller endpoint;
- its immutable profile allows the bootstrap kernel operation;
- bootstrap phase is `RUNNING`; and
- the current thread and scheduler state are canonical.

No caller-supplied process, thread, profile, root, or image handle is accepted.

### Common failure ordering

Every returning control command follows this order:

1. capture registers and advance `sepc` by four;
2. reject unknown command, width violation, or nonzero unused register with
   `MICROS_SYSCALL_ABI_ARGUMENT`;
3. resolve the exact current thread and process through authoritative runtime
   state; an impossible failure is an invariant panic;
4. require the exact controller endpoint and profile kernel-operation bit,
   otherwise `MICROS_SYSCALL_ABI_UNAUTHORIZED`;
5. require bootstrap phase `RUNNING`, otherwise
   `MICROS_SYSCALL_ABI_STATE`;
6. validate the complete bootstrap, object, endpoint, scheduler, address-space,
   and frame-owner relationships; corruption is an invariant panic;
7. validate command-specific service identity and state; an unknown service
   ID is `ARGUMENT`, while wrong order, unmet prerequisites, wrong lifecycle
   state, or incomplete role gate is `STATE`;
8. for `ACCEPT_READY`, resolve the supplied exact endpoint and token;
   stale/foreign endpoint identity is `DEAD_ENDPOINT`, while a missing,
   wrong-callee, already consumed, or not-yet-presentable token is
   `REPLY_TOKEN`;
9. classify deadline-addition overflow as `RANGE`; and
10. perform the command's complete preflight before the first mutation.

Every recoverable result preserves bootstrap, profile, endpoint, IPC,
scheduler, object, root, frame-owner, deadline, message, and output state.
Only `a0` and `sepc` change at the syscall boundary. The launcher treats every
negative result as fatal and invokes `FAIL`; recoverable results exist to keep
the kernel/user ABI explicit, not to create retry policy.

### `RELEASE`

Registers are:

```text
a0  RELEASE
a1  exact next service ID
a2  zero
a3  zero
```

Preflight requires:

- the service is `PREPARED`;
- no other service is `STARTING`;
- it is the exact next topological entry;
- every explicit prerequisite is `READY`;
- its process, root, image pages, stack, thread, context, reserved endpoint,
  and stored scheduler policy still validate;
- its thread remains `INACTIVE` and scheduler-unassigned;
- no profile is installed;
- its exact manifest profile resolves;
- its readiness deadline can be computed without counter overflow;
- a VM or console role satisfies the release gates defined below; and
- profile installation, endpoint activation, scheduler admission, and any
  priority preemption all have a complete non-failing commit plan.

Commit then:

1. installs only the entry's exact manifest profile;
2. changes only its exact endpoint from `RESERVED` to `ACTIVE`;
3. changes entry state to `STARTING`;
4. records the absolute readiness deadline;
5. admits its inactive thread with the exact stored priority, quantum, and
   preemptibility through the existing scheduler transition; and
6. lets the common return selector choose the launcher, released service, or
   another already runnable higher-priority thread.

Endpoint activation is the publication point. There is no recoverable
operation after the first commit store.

### `ACCEPT_READY`

Registers are:

```text
a0  ACCEPT_READY
a1  service ID
a2  exact readiness-message source endpoint
a3  nonzero readiness-call reply token
```

Preflight requires:

- one service is `STARTING`, and it is the named service;
- the supplied endpoint equals its exact active manifest endpoint generation;
- the reply token resolves one exact blocked caller thread owned by that
  endpoint;
- the token callee is the exact launcher endpoint generation;
- the token-bearing request has already returned to launcher user space;
- the deadline has not expired at the current `time` counter;
- every prerequisite remains `READY`;
- VM or console ready gates are satisfied;
- no earlier ready transition exists; and
- the caller can be woken through the ordinary reply path.

The kernel synthesizes the canonical acknowledgment message. The launcher
does not provide reply bytes.

Commit then:

1. changes the entry from `STARTING` to `READY`;
2. clears its readiness deadline;
3. stages the canonical acknowledgment on the exact caller;
4. consumes the one-shot reply token; and
5. wakes the service when no independent run-time flag remains.

The ready transition and acknowledgment are one failure-atomic operation.

### `FAIL`

Registers are:

```text
a0  FAIL
a1  implicated service ID, or zero
a2  implicated endpoint, or `MICROS_ENDPOINT_NONE`
a3  one defined launcher-detected failure reason
```

Launcher-submitted reason values are:

```text
1  ready-malformed
2  ready-foreign
3  ready-early
4  ready-duplicate
5  release-order
6  release-transition
7  authority
8  completion
9  ready-role-gate
```

For `ready-foreign`, `a1` is the sole currently starting service ID and `a2`
is the observed source. For `ready-early` or `ready-duplicate`, `a1` is the
manifest service resolved from the observed source. `ready-malformed` uses
that resolved service when one exists, otherwise the currently starting
service. For a release reason, `a1` is the requested service ID and `a2` is
`MICROS_ENDPOINT_NONE`. `authority` and `completion` use service ID zero
unless one validated entry is directly implicated. `ready-role-gate` uses the
currently starting service and its exact endpoint.

This command reports malformed, early, duplicate, or foreign readiness that
the launcher detected in user space. It validates the controller authority and
failure enum, records bootstrap phase `FAILED`, emits the stable failure
record, and enters the nonreturning panic path.

It cannot request recovery, skip a service, change an entry, or return a
success-shaped result.

### `COMPLETE`

All arguments after the command are zero.

Preflight requires:

- every active non-launcher entry is `READY`;
- no service is `STARTING`;
- no readiness deadline is armed;
- every VM and console role gate is complete;
- the launcher owns no pending reply token, message completion, queue link,
  grant, or foreign IPC dependency;
- its thread can be atomically held and removed from current selection;
- another runnable thread or the existing idle path can be selected.

The nonreturning commit:

1. changes phase to `SEALED`;
2. clears the recorded bootstrap controller authority;
3. changes the exact launcher endpoint from `ACTIVE` to `SOURCE_ONLY`;
4. holds the launcher thread and removes it from its ready queue;
5. commits the next scheduler selection or idle transition; and
6. never returns to launcher user mode.

The launcher process, private root, wired pages, saved context, and held thread
remain allocated. Post-handoff teardown belongs to later PM/VM lifecycle work.
Its source-only endpoint and immutable profile remain attached until that
teardown because a successfully staged readiness acknowledgment may still name
the launcher as its canonical source. They confer no destination or bootstrap
authority after the controller binding is cleared and phase becomes `SEALED`.
The launcher retains no runnable thread or operation that can use the profile.

There is no operation that unseals or reinitializes bootstrap state.

## Launcher algorithm and lifecycle

The launcher has no heap, dynamic service registry, recovery table, or
background worker. Its complete version-1 algorithm is:

1. read and locally validate the fixed read-only manifest view;
2. compute the same lowest-ID topological order as the kernel;
3. require itself to be the first controller entry;
4. for each later entry:
   1. invoke `RELEASE` for the exact next service ID;
   2. block in `receive(ANY)` for one message;
   3. classify any noncanonical message as one exact `FAIL` reason;
   4. invoke `ACCEPT_READY` with the canonical source and reply token; and
   5. continue only after that operation returns success;
5. invoke nonreturning `COMPLETE`.

The launcher never:

- polls a service state;
- sleeps;
- retries a failed control operation;
- receives several readiness calls concurrently;
- buffers a later service's message;
- changes the computed order;
- edits the manifest view;
- asks for a profile by name alone;
- supplies an image, process, thread, root, or profile handle; or
- remains as a service manager after completion.

Its active lifecycle is therefore:

```text
prepared by kernel
    -> sole initial runnable service
    -> serial release/readiness loop
    -> nonreturning COMPLETE
    -> held process with sealed authority
```

The local manifest validation is defense in depth and produces a launcher
failure if its view is corrupt. Kernel validation and every control-operation
check remain authoritative.

## Readiness protocol

### Request

Every released service performs one blocking `call` to the exact launcher
endpoint after its service-specific initialization is complete.

It obtains the destination, service ID, manifest version, and its expected own
endpoint from the kernel-patched bootstrap configuration, then verifies that
configuration before constructing the request.

The service may use one stack-local 64-byte in/out message. The external stack
was prepared before release, remains resident, and is planned `VM_WIRED` when
the VM role exists. The launcher likewise receives into one resident
stack-local message. Existing ADR-0035 buffer validation, blocking retention,
return-time revalidation, and failure output preservation remain unchanged.
No payload pointer grants memory authority.

The message has:

```text
source       exact service endpoint, written by the kernel
type         0x00000001  (MICROS_BOOTSTRAP_MESSAGE_READY)
reply_token  nonzero, written by the kernel
```

Both bootstrap message types keep ADR-0004's reserved high type bit clear.
Neither is a kernel-generated envelope.

Payload bytes are:

```text
0..3    protocol version, uint32 little-endian, value 1
4..7    service ID, uint32 little-endian
8..11   manifest version, uint32 little-endian, value 1
12..15  flags, uint32 little-endian, value 0
16..19  declared endpoint, uint32 little-endian
20..47  zero
```

The service ID and declared endpoint are data, not authority. Authority comes
from the kernel-written source endpoint and reply token.

The launcher accepts the request only when:

- type and every payload field are canonical;
- the token is nonzero;
- the source is an exact active manifest endpoint;
- source slot and generation equal the prepared service binding;
- declared endpoint equals source;
- service ID selects that same binding;
- the service is the sole `STARTING` entry;
- every prerequisite is ready; and
- no prior ready transition exists.

The launcher then invokes `ACCEPT_READY`.

### Acknowledgment

The kernel-synthesized reply has:

```text
source       exact launcher endpoint
type         0x00000002  (MICROS_BOOTSTRAP_MESSAGE_READY_ACK)
reply_token  0
```

Payload bytes are:

```text
0..3    protocol version, value 1
4..7    service ID
8..11   manifest version, value 1
12..15  result, value 0
16..19  acknowledged service endpoint
20..47  zero
```

Only after the call returns with this exact acknowledgment may the service
enter its ordinary request loop. The service requires syscall success, exact
launcher source generation, exact type, version, service ID, endpoint, zero
result, zero token, and zero reserved tail. A mismatch is fatal service
initialization failure; it is not retried or treated as readiness.

### Exactly one transition

Version 1 permits one ready call per service generation.

The launcher classifies one received readiness candidate in this exact order:

1. validate the fixed message type, version, flags, reserved bytes, and
   nonzero token shape; failure is `ready-malformed`;
2. resolve the exact active source generation to at most one manifest binding;
   an active source with no binding is `ready-foreign`;
3. if the source is the sole `STARTING` binding, validate its embedded service
   ID, self endpoint, launcher endpoint, and remaining payload fields; a
   mismatch is `ready-malformed`, otherwise the request is eligible for
   `ACCEPT_READY`;
4. a known source already in `READY` is `ready-duplicate`;
5. a known source still in `PREPARED` is `ready-early`;
6. any other active source/state combination is `ready-foreign`.

This ordering is normative. In particular, a readiness call from an already
ready service while another service is starting is `ready-duplicate`, not
`ready-foreign`; an unreleased known service is `ready-early`.

- A stale endpoint generation fails normal IPC resolution and cannot become a
  ready transition.
- A readiness send with token zero is malformed; ordinary send is not the
  protocol.
- An unrelated message or kernel notification received while waiting for
  readiness is fatal rather than ignored.

No malformed request receives an error reply and no dependent is released
after it.

## Deterministic progress and timeout

Each non-launcher entry has an explicit nonzero
`ready_timeout_counter_ticks`. There is no default inferred from service role
or source order.

At successful `RELEASE`, the kernel:

1. reads the RISC-V `time` counter;
2. preflights `now + timeout` for overflow; and
3. publishes the resulting absolute deadline in the same commit as service
   release.

The existing supervisor timer path checks the one active bootstrap deadline.
If `time >= deadline` while the service remains `STARTING`, the kernel emits
fatal `ready-timeout` before any further user return.

`ACCEPT_READY` independently reads `time` and refuses a request that reached
the launcher after the deadline even if the periodic timer has not yet
observed it. An expired deadline enters the same nonreturning
`ready-timeout` failure path; it is not returned as a recoverable control
result.

The deadline:

- uses no sleep;
- requires no userspace clock service;
- is not reset by IPC traffic, preemption, or partial progress;
- has no retry or grace extension; and
- is disarmed only by the atomic ready transition or fatal boot.

The host QEMU timeout remains a harness safety bound. It is not the missing
readiness detector.

## VM and console ordering

### VM

The launcher may release the VM role while frame ownership remains
`BOOTSTRAP`.

The VM service's separate `VM_READY` transition and the kernel's validated
ownership commit remain governed by ADR-0006 and the next DAG outcome.

The launcher accepts VM's ordinary bootstrap ready call only after the kernel
reports:

- frame ownership phase `HANDED_OFF`; and
- the exact VM handoff owner generation matching the manifest VM entry.

Thus the required order is:

```text
release VM
    -> VM initializes its complete wired working set
    -> VM_READY validation
    -> irreversible ownership commit
    -> VM sends bootstrap READY
    -> launcher acknowledges READY
    -> launcher may release PM
```

The launcher never performs the ownership commit and cannot claim it from a
payload bit.

If a dependency-closed development manifest contains no VM role, the VM gate
is absent. Such a build makes no handoff claim.

### Console owner

The production TTY entry carries the exact UART page and IRQ source.

When TTY becomes the exact next topological entry, later ADR-0012
implementation extends the release gate with:

1. the launcher invokes manifest-bound `console_handoff_begin`;
2. ordinary early-console output stops;
3. VM installs the exact UART mapping while TTY remains held;
4. only then may `RELEASE(TTY)` commit;
5. TTY initializes the device and invokes `console_handoff_commit`; and
6. only after that commit may TTY send its ordinary bootstrap ready call.

The generic ready message is the versioned console-ready message required by
ADR-0012 for the TTY role. No second ready transition is added.

This ADR records the gates but does not assign a console syscall number,
implement a mapping protocol, route a PLIC interrupt, or enable the source.

### Remaining services and init

PM, RAMFS, and VFS use the same release and readiness contract after their
predecessors are ready. Their service protocols remain separate ADRs.

After VFS, or the last entry in a dependency-closed development prefix, is
ready, the launcher invokes `COMPLETE`.

`init` is not a static manifest entry. ADR-0009's later PM/VFS/VM spawn
transaction:

- loads init's application ELF;
- installs the `APPLICATION` profile through PM's separate narrow authority;
- creates descriptors 0, 1, and 2 from VFS's synthetic console object; and
- activates init without reviving launcher authority.

This resolves the older ADR-0007 revocation wording while preserving the
static launcher as the bootstrap-cycle substitution.

## Invariants

- The manifest and image catalog are immutable for the complete boot.
- Every active manifest entry has one unique service ID, name, process slot,
  image ID, process generation, thread generation, and endpoint generation.
- Service identity is not process, thread, endpoint, image, or profile
  identity.
- Every non-launcher endpoint is hidden until its exact release commit.
- A profile is installed exactly once and equals both the manifest ID and
  name.
- No privilege is inferred from a role, prerequisite, image, service name,
  device range, or IRQ source.
- Every static image, stack, and context is prepared before VM handoff.
- A held service is off every ready queue and is current on no hart.
- At most one non-launcher service is `STARTING`.
- A service is released only when every explicit prerequisite is `READY`.
- Publication, starting state, deadline, and runnable transition commit
  together.
- A ready transition requires one exact active source generation and one
  token bound to the exact launcher generation.
- A service becomes `READY` at most once.
- Ready state and acknowledgment commit together.
- A dependent is never released after malformed, foreign, early, duplicate,
  expired, or failed readiness.
- VM readiness implies the irreversible handoff already committed.
- TTY readiness implies console ownership already committed.
- Bootstrap failure never triggers restart, fallback profile, skipped
  dependency, alternate image, or rollback to an externally hidden state.
- `SEALED` has no controller, readiness deadline, runnable launcher thread, or
  operation that can restore bootstrap authority. The retained launcher
  endpoint is exact-generation `SOURCE_ONLY`, rejects every new destination
  use, and exists only to validate already staged acknowledgment source state.

## Failure and rollback boundaries

### Before any launcher publication

Manifest, profile, image, page-limit, topology, and complete preparation
preflight precede launcher activation.

If preparation of an entry fails before the launcher becomes active, the
kernel releases the current and already prepared entries in reverse order:

1. remove held scheduler metadata;
2. detach execution context;
3. release thread;
4. close a reserved endpoint;
5. release the exact image, stack, and manifest-view pages;
6. destroy the inactive private root; and
7. release the exact process generation.

The rollback uses bootstrap-phase mutation only. Every released object and
frame must return to a valid no-live-service availability, ownership, and
reachability baseline before the fatal diagnostic. Process, thread, and
endpoint generations are never rewound merely to recreate byte-identical zero
storage; any consumed generation remains stale by design. One-shot
immutable table initialization may remain initialized because the kernel
shuts down immediately and no alternate boot attempt occurs.

### Before a service publication

`RELEASE` performs complete preflight before profile, endpoint, runtime-state,
deadline, or scheduler mutation.

A recoverable preflight result preserves:

- entry `PREPARED`;
- no installed profile;
- endpoint `RESERVED`;
- held thread and queues;
- all prerequisites and ready state;
- watchdog state; and
- every output register except the ordinary syscall result.

The launcher treats any non-success result as fatal through `FAIL`; it does
not retry with different arguments.

### Publication point and later failure

Endpoint activation is the publication point. Profile installation,
publication, `STARTING`, deadline, and runnable state are one non-failing
commit.

After publication, these are fatal:

- malformed or foreign readiness;
- duplicate or early readiness;
- timeout;
- role-gate failure;
- unexpected service trap, return, or endpoint loss;
- impossible scheduler or IPC state;
- profile, process, root, frame-owner, or endpoint corruption; and
- launcher authority mismatch.

The kernel does not:

- return the endpoint to `RESERVED`;
- uninstall or replace the profile;
- destroy and recreate the service;
- select an alternate image;
- restart the service;
- release a dependent; or
- continue with reduced functionality.

Recovery starts only at the later RS milestone.

## Stable diagnostics and markers

### Failure record

Every bootstrap fatal path emits exactly one record before the ordinary
ADR-0014 panic records:

```text
MICROS_BOOTSTRAP_FAILURE reason=<reason> service=0x<16 hex> endpoint=0x<16 hex> phase=<phase> state=<state> detail=0x<16 hex>
```

This record covers manifest preparation and the active launcher phase. After
phase `SEALED`, a later core-service failure is no longer a launcher bootstrap
failure and is diagnosed by that service's lifecycle/fault contract.

When no entry is implicated, `service` is zero and `endpoint` is
zero-extended `MICROS_ENDPOINT_NONE`. Otherwise `service` identifies the
implicated manifest entry. `endpoint` is that entry's exact generation except
for `ready-foreign` or source-field `ready-malformed`, where it is the
zero-extended observed source endpoint.

Defined reasons are:

```text
manifest-header
manifest-entry
manifest-profile
manifest-image
manifest-cycle
prepare
release-order
release-transition
ready-malformed
ready-foreign
ready-early
ready-duplicate
ready-role-gate
ready-timeout
service-fault
authority
completion
```

`phase` is exactly one of:

```text
uninitialized
preparing
running
sealed
failed
```

`state` is exactly one of:

```text
none
controller
prepared
starting
ready
```

`detail` is zero except for these stable cases:

- `manifest-cycle`: the unresolved service-ID bitmask;
- `release-order`: the expected next service ID;
- `ready-timeout`: the absolute expired counter deadline; and
- `ready-malformed`: one field code:

```text
1  message type
2  reply token
3  protocol version
4  service ID
5  manifest version
6  flags
7  declared endpoint
8  reserved payload tail
9  unrelated kernel notification or message class
```

The panic classifier is:

```text
MICROS_PANIC reason=bootstrap-failure
```

Unknown internal results use `authority` or `completion` only after a more
specific structural diagnostic cannot be emitted. They never become success.

Panic may seize the UART under ADR-0012, so the failure record remains
available after normal console ownership changes.

### Normal and test records

Before any console handoff, ordinary boot may emit:

```text
MICROS_BOOTSTRAP_MANIFEST_READY version=0x0000000000000001 services=0x<16 hex>
```

The isolated success image is intended to emit only after complete authority
sealing:

```text
MICROS_BOOTSTRAP_TEST_PASS manifest=immutable order=topological profiles=exact endpoints=staged readiness=acknowledged authority=revoked
```

These records do not claim VM, PM, TTY, RAMFS, VFS, ownership-handoff, PLIC,
or recovery behavior.

The production kernel emits no routine UART progress after
`console_handoff_begin`. Later integration tests obtain normal post-handoff
progress through TTY.

## Serialization and resource bounds

The one-hart target keeps SIE clear across:

- manifest and image validation;
- process, root, page, thread, endpoint, and scheduler preparation;
- each release preflight and commit;
- readiness-token validation and acknowledgment commit;
- deadline inspection;
- fatal state capture; and
- final launcher hold and bootstrap seal.

The design:

- allocates no bootstrap metadata dynamically;
- stores at most six service runtime records;
- stores one six-entry launch-order array;
- scans at most six entries for topology or state;
- resolves at most the existing fixed process, thread, endpoint, profile, and
  frame capacities;
- has at most one readiness deadline;
- has at most one starting service;
- maps one additional launcher manifest page;
- adds no SMP lock, IPI, affinity, or remote-TLB behavior; and
- retains no user pointer after a control syscall returns.

The full fixed-capacity validation cost is accepted for v0.1. Optimization
requires measured motivation after the baseline works end to end.

## Test-first evidence

### Native manifest tests

The later implementation begins with a failing native table of:

- exact manifest-header, manifest-entry, and service-configuration sizes and
  offsets;
- valid launcher-only and dependency-closed prefix manifests;
- valid complete production identity shape with test image descriptors;
- source-array permutations producing the same lowest-ID order;
- bad magic, version, sizes, capacity, count, total size, or reserved bytes;
- malformed or duplicate names;
- zero, duplicate, or out-of-range IDs and slots;
- unresolved or duplicate image references;
- missing, misaligned, wrong-sized, non-writable, or nonzero bootstrap
  configuration objects;
- image/page/stack bounds and arithmetic overflow;
- zero or mismatched profiles;
- role/profile relationship violations;
- missing, self, foreign, and cyclic prerequisites;
- unreachable services;
- duplicate or combined roles;
- invalid device/IRQ ownership and wrong UART values;
- zero timeouts;
- complete output and state preservation on every rejection; and
- exact failure reason and implicated service selection.

### Native transition model

A portable launcher model owns:

- global phase and controller identity;
- per-entry `PREPARED`, `STARTING`, and `READY` state;
- validated launch order;
- exact endpoint generations;
- installed-profile state;
- publication state;
- inactive/unassigned, scheduler-assigned, and runnable state;
- one active deadline; and
- final authority sealing.

Deterministic cases cover:

- launcher-only completion;
- full topological release;
- hidden endpoint before release;
- exact profile installation;
- scheduler assignment occurring only in the same publication transition;
- wrong-order and unmet-prerequisite release;
- a second simultaneous starting service;
- deadline overflow;
- exact ready and acknowledgment;
- stale, foreign, malformed, early, duplicate, and expired readiness;
- duplicate-over-foreign and early-over-foreign precedence for known manifest
  sources while another service is starting;
- token mismatch and request-not-yet-returned rejection;
- VM and console role gates;
- failure before publication;
- preparation rollback with no live resource leak and no generation rewind;
- fatal classification after publication;
- final launcher hold and exact source-only endpoint/profile state;
- post-seal launcher destination rejection with staged-source validity;
- operation rejection after sealing; and
- byte-exact state preservation on every recoverable failure.

A replayable model runs at least 4,096 mixed operations and compares the
complete reference and production state after every transition. Failures print
the seed, manifest, operation, service identities, state, deadline, and recent
trace.

### QEMU launcher acceptance

The implementation is intended to add one isolated success workflow named:

```text
test-qemu-bootstrap-launcher
```

This workflow does not exist at design time.

It uses:

- one production-linked launcher ELF;
- one production-linked test-only probe ELF loaded into several distinct
  process generations;
- one test-only immutable manifest and profile table; and
- the production kernel preparation, syscall, IPC, scheduler, address-space,
  and runtime paths.

The probe is not a VM, PM, TTY, RAMFS, or VFS implementation. A bounded
production bootstrap-configuration object tells each instance its service ID,
exact endpoint generation, and launcher endpoint. Test behavior is selected
only by the test manifest service ID.

The image must prove:

1. the manifest and image catalog validate before any manifest-directed
   process, user-frame, endpoint, or scheduler mutation;
2. every process uses the exact manifest slot and a nonzero generation;
3. every non-launcher endpoint is reserved and hidden;
4. every bootstrap configuration contains the exact service ID, self endpoint,
   launcher endpoint, complete sorted service/endpoint table, manifest
   version, and zero reserved fields;
5. every service image, stack, and context is prepared while its thread is
   held;
6. only the launcher is initially active and runnable;
7. a target probe cannot resolve an unreleased endpoint;
8. release order follows the computed prerequisites rather than source array
   order;
9. release installs only the exact named profile;
10. endpoint activation and runnable transition occur together;
11. each probe sends the exact readiness call through the production runtime;
12. the launcher validates source, generation, type, version, identity,
    reserved bytes, and token;
13. `ACCEPT_READY` commits state and canonical acknowledgment atomically;
14. no dependent is released before that acknowledgment;
15. all probes enter their post-ready receive loop;
16. `COMPLETE` holds the launcher, clears the controller binding, and seals
    authority while retaining a source-only endpoint/profile for later
    teardown;
17. a post-seal call to the launcher returns `ENDPOINT_CLOSING` without queue,
    token, scheduler, or byte mutation, while an already staged acknowledgment
    still validates the exact launcher source generation;
18. complete object, endpoint, scheduler, root, stack, frame-owner, and
    bootstrap invariants validate.

Only that sequence may emit the exact bootstrap pass marker. The test then
uses an isolated test hook for clean SBI shutdown because production
`COMPLETE` is nonreturning.

The success gate does not perform a VM handoff or console transition. It makes
no claim for those future role gates.

### Missing-readiness expected failure

The implementation is intended to add:

```text
test-qemu-bootstrap-ready-timeout
```

This workflow does not exist at design time.

One released probe deliberately omits the readiness call. The guest's own
counter deadline, not the host timeout, must produce:

```text
MICROS_BOOTSTRAP_FAILURE reason=ready-timeout
```

followed by `MICROS_PANIC reason=bootstrap-failure` and clean SBI
system-failure shutdown.

The gate rejects:

- a launcher pass marker;
- release of a dependent;
- host timeout;
- unexpected QEMU exit; or
- a different failure reason.

### Malformed-manifest expected failure

The implementation is intended to add:

```text
test-qemu-bootstrap-manifest-panic
```

This workflow does not exist at design time.

An isolated build embeds one structurally well-sized manifest with a
two-service cycle. Validation must fail before:

- IPC runtime publication;
- process or thread creation;
- user-frame allocation;
- endpoint reservation;
- scheduler initialization; or
- launcher entry.

It requires:

```text
MICROS_BOOTSTRAP_FAILURE reason=manifest-cycle
```

followed by the ordinary bootstrap panic and clean system-failure shutdown.
Native tests cover the remaining malformed-manifest classes.

### Later integration evidence

The first implementation of each real service extends integration evidence
without changing this protocol:

- VM proves `VM_READY`, ownership commit, then bootstrap readiness;
- TTY proves begin, exact mapping, release, commit, then bootstrap readiness;
- RAMFS and VFS prove their direct protocol prerequisites; and
- the final static-service integration proves launcher sealing before PM
  spawns init.

Those tests belong to their service and handoff outcomes, not to the launcher
implementation.

## Validation ownership

The implementation must extend the fail-closed validation planner when the new
files and workflows exist.

Changes to the manifest representation, validator, topology algorithm,
bootstrap transition model, or public readiness protocol select:

- complete native tests containing the manifest and transition model;
- all three launcher QEMU workflows;
- documentation and all three diff checks.

Changes to kernel bootstrap preparation, image installation, object/root/frame
rollback, endpoint publication, scheduler release, authority sealing, or
bootstrap deadline handling select:

- complete native tests and persistent models selected by existing shared
  ownership;
- all three launcher QEMU workflows;
- `test-qemu-user-runtime`;
- `test-qemu-endpoint`;
- `test-qemu-ipc`;
- `test-qemu-ipc-syscall`;
- `test-qemu-address-space-handoff`;
- `test-qemu-user-execution`;
- `test-qemu-scheduler`;
- every additional gate selected by the existing shared object, address-space,
  IPC, trap, timer, and scheduler paths;
- documentation and all three diff checks.

Changes to operation 11, the common syscall dispatcher, reply-token
integration, or selected-thread return additionally retain ADR-0041's complete
dispatcher ownership.

Changes to launcher startup, its private wrapper, manifest view, linker bound,
embedded image generation, or probe ELF select:

- complete native tests;
- the existing user-ELF and static checks;
- all three launcher QEMU workflows;
- `test-qemu-user-runtime`;
- every shared toolchain, linker, fixture-generator, and post-link gate chosen
  by the planner;
- documentation and all three diff checks.

Changes to future VM or console role gates retain both this launcher evidence
and the complete evidence owned by ADR-0006 or ADR-0012.

The implementation pull request records exact commands only after those
commands and workflows exist. This design does not claim they are currently
available.

## Implementation task and commit plan

The dependent implementation todo remains pending until this design is
reviewed, marked Accepted, and merged. The implementation starts from a fresh
branch based on then-current `main`.

The later implementation uses four green commits.

### 1. Add the immutable manifest and native transition model

- Red: native tests fail because version-1 manifest types, validation,
  topology, readiness messages, and bootstrap states do not exist.
- Green: add the pointer-free declarations, immutable profile identities,
  manifest/image validation, deterministic topology, pure transition model,
  deterministic cases, and replayable 4,096-operation model.
- No target image or build workflow is added in this commit.

### 2. Add kernel preparation and bootstrap control

- Red: target-boundary tests cannot prepare exact held services or execute
  operation 11.
- Green: add fixed bootstrap runtime storage, exact-slot preparation, bounded
  image installation, rollback plans, manifest view, controller
  authorization, release/ready/fail/complete transitions, deadline hook, and
  native target-boundary tests.
- Existing target gates selected by shared code remain green.

### 3. Add the launcher and successful QEMU acceptance

- Red: the isolated image cannot produce the bootstrap pass marker.
- Green: add the standalone launcher ELF, launcher-private wrapper, test-only
  probe ELF and manifest, generated embedded-image descriptors, normal QEMU
  scenario, host parser regressions, and exact pass evidence.
- No real VM, PM, TTY, RAMFS, or VFS code is included.

### 4. Add fatal-path images and fail-closed ownership

- Red: missing readiness and a cyclic manifest do not produce the required
  internal failures.
- Green: add the deadline and malformed-manifest expected-panic images,
  structured failure diagnostics, parser regressions, intent-based workflows,
  validation-planner mappings, and directly related documentation.

Each commit remains buildable and green for every gate that exists at that
commit. The implementation does not add VM handoff, console commit, service
protocol, recovery, dynamic manifest, init, or application behavior.

## Consequences

- Boot service identity and policy become reviewable data rather than kernel
  source-order accidents.
- Every static service image and context is ready before VM removes generic
  bootstrap preparation authority.
- Exact profiles are installed before endpoint visibility and execution.
- Explicit prerequisites and cycle rejection replace implicit table-order
  assumptions.
- One readiness call plus acknowledgment preserves the MINIX startup gate
  without importing SEF restart or live-update machinery.
- Generation and reply-token checks prevent stale or guessed readiness from
  becoming authority.
- Serial release makes diagnostics, timeout ownership, and rollback
  attributable to one service.
- Endpoint publication is irreversible within bootstrap; recovery is not
  disguised as rollback.
- The dormant launcher consumes one process, thread, root, and wired working
  set after sealing, which is the explicit cost of deferring teardown.
- Init remains on the PM/VFS/VM spawn path and does not keep bootstrap
  authority alive.

## Alternatives considered

### Let the kernel unconditionally start every service

Rejected. It would move dependency and readiness policy into supervisor mode
and remove the user-space control-plane boundary.

### Give the launcher arbitrary process and profile syscalls

Rejected. The launcher may select only manifest-bound transitions over objects
the kernel already prepared. It cannot invent an identity, image, profile,
mapping, endpoint, or privilege mask.

### Activate every endpoint during preparation

Rejected. MINIX exposes inhibited endpoints, but `micros` already has an
explicit reserved state. Keeping endpoints hidden until exact profile and
release commit prevents premature observation.

### Publish only after readiness

Rejected. A service needs one active source endpoint to perform its readiness
call. Publication therefore occurs atomically with release, and any later
failure is fatal.

### Use ordinary send for readiness

Rejected. A blocking call plus one-shot reply token reproduces the SEF
result/reply gate and lets the kernel bind acknowledgment to the exact service
caller.

### Let the launcher provide arbitrary acknowledgment bytes

Rejected. Kernel synthesis makes the state transition and reply one canonical
failure-atomic operation.

### Use a host timeout or sleep

Rejected. A guest-owned counter deadline makes missing progress observable and
deterministic on the real target path.

### Release several independent services concurrently

Rejected for v0.1. It enlarges timeout, failure attribution, and rollback
state without a dependency-ready performance requirement.

### Parse ELF files in the boot kernel

Rejected. Build-time checking and bounded generated descriptors are sufficient
for immutable embedded images. A general loader belongs to the later
PM/VFS/VM spawn path.

### Put init in the static manifest

Rejected. ADR-0009 assigns init's executable, application profile, prepared
mapping, activation, and console descriptors to the PM/VFS/VM spawn
transaction.

### Close the launcher endpoint during completion

Rejected. The final readiness acknowledgment may be a successfully staged
message that still names the launcher endpoint as its source, and accepted IPC
close rules require that completion to drain first. Authority is instead
revoked by clearing the exact controller binding, sealing bootstrap state, and
holding the only launcher thread. Later PM/VM teardown closes the inert
endpoint after ordinary lifecycle support exists.

### Implement RS recovery in the same outcome

Rejected. Restart, endpoint replacement, heartbeat, replicas, DS publication,
state transfer, and live update depend on later stable services and protocols.

## Specification basis

- [MINIX RS and SEF bootstrap study](../research/minix-rs-sef-bootstrap.md)
- [System overview](../architecture/system-overview.md)
- [Development dependency DAG](../architecture/development-dag.md)
- [Roadmap](../roadmap.md)
- [Testing strategy](../testing-strategy.md)
- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0004: IPC and Endpoint ABI](0004-ipc-and-endpoint-abi.md)
- [ADR-0006: VM Bootstrap and Handoff](0006-vm-bootstrap-and-handoff.md)
- [ADR-0007: Static Launcher Before Recovery Services](0007-static-launcher-before-rs.md)
- [ADR-0009: Spawn Before Fork](0009-spawn-before-fork.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0012: Console and IRQ Handoff](0012-console-and-irq-handoff.md)
- [ADR-0014: Panic Diagnostics](0014-panic-diagnostics.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0026: MINIX-Baseline Kernel Scheduler](0026-minix-baseline-kernel-scheduler.md)
- [ADR-0029: Endpoint and Privilege Substrate](0029-endpoint-and-privilege-substrate.md)
- [ADR-0030: MINIX-Baseline Blocking IPC](0030-minix-baseline-blocking-ipc.md)
- [ADR-0032: Fail-Closed Change-Aware Validation](0032-fail-closed-change-aware-validation.md)
- [ADR-0033: Reviewed Design Before Implementation](0033-reviewed-design-before-implementation.md)
- [ADR-0034: Author Validation Before Review](0034-author-validation-before-review.md)
- [ADR-0035: RISC-V IPC Syscall and Bootstrap Buffers](0035-riscv-ipc-syscall-and-bootstrap-buffers.md)
- [ADR-0036: Reply Authority Begins at Request Return](0036-reply-authority-begins-at-request-return.md)
- [ADR-0037: Kernel-Origin IPC Notification Injection](0037-kernel-origin-ipc-notification-injection.md)
- [ADR-0040: Post-Handoff Wired Address-Space Resolution](0040-post-handoff-wired-address-space-resolution.md)
- [ADR-0041: Unified RISC-V Grant Syscalls](0041-unified-risc-v-grant-syscalls.md)
- [ADR-0042: Freestanding User-Service Runtime](0042-freestanding-user-service-runtime.md)

# ADR-0049: VFS Service and Application I/O Protocol

- Status: Accepted
- Date: 2026-10-10
- Refines: ADR-0004, ADR-0005, ADR-0007, ADR-0008, ADR-0009,
  ADR-0011, ADR-0012, ADR-0025, ADR-0029, ADR-0030, ADR-0031,
  ADR-0032, ADR-0033, ADR-0034, ADR-0038, ADR-0039, ADR-0041,
  ADR-0042, ADR-0043, ADR-0045, ADR-0046, ADR-0047, and ADR-0048
- Supersedes in part:
  - ADR-0041's assignment boundary above syscall operation 14. Operation 15
    becomes one reviewed non-copying direct-grant validation operation.
  - ADR-0043's six-entry manifest storage bound and six-element service
    configuration table. Storage capacity becomes seven solely so one
    dependency-closed VFS QEMU fixture can add an isolated application probe;
    the production static service count remains exactly six.
  - ADR-0045's six-address-space VM boot-information capacity and dependent
    complete-object layout. Capacity becomes seven and every consumer moves
    atomically to the exact version-1 layout below.
  - ADR-0048's immutable VFS profile. VFS gains exact reply and
    reply/receive authority for the application-facing protocol.

## Context

Development-DAG Step 12 is complete:

- launcher, VM, PM, TTY, RAMFS, and the reserved VFS identity form the static
  production service chain;
- VM owns the wired service mappings;
- PM owns application lifecycle metadata but cannot yet publish a running
  dynamic application;
- TTY owns the UART and exposes asynchronous submit, completion, collection,
  cancellation, and writable notification;
- RAMFS owns one mounted namespace protocol with generation-safe node handles,
  separate link/reference counts, bounded sparse files, and fixed directory
  records; and
- applications already have a reserved immutable profile that may call PM and
  VFS but not TTY or RAMFS.

The next dependency-ready outcome is production VFS. The system overview and
roadmap already require VFS to own:

- per-process descriptor identity;
- shared open-file descriptions and positions;
- process root and working-directory routing;
- the sole root mount;
- the application-facing file-I/O protocol;
- a synthetic console object for initial descriptors 0, 1, and 2; and
- translation between application records and RAMFS records.

The fixed MINIX baseline is recorded in
[the VFS process, descriptor, and device-routing study](../research/minix-vfs-process-descriptor-and-device-routing.md).
The already accepted VFS-to-filesystem baseline remains in
[the VFS/MFS protocol study](../research/minix-vfs-mfs-filesystem-protocol.md).

MINIX relies on transitive magic grants: VFS authorizes a filesystem or
character driver to copy directly to or from an application buffer. `micros`
direct grants are deliberately non-transitive. VFS must therefore terminate
one application grant, use resident storage, and create a separate grant for
RAMFS or TTY.

That extra hop introduces a correctness requirement. A terminal read must not
be consumed by TTY and then lost because the later VFS-to-application copy
discovers that the application's write grant is stale, wrongly directed,
out-of-range, or unmapped. Existing checked-copy operations validate a complete
range only while copying it. VFS therefore needs one grantee-only, non-copying
preflight for the exact remote grant range before it starts a consuming second
hop.

Production spawn, executable buffering, dynamic mappings, initial descriptor
transactions, process activation, and target exit teardown remain Step 14.
Step 13 must not implement those early. It must nevertheless establish the
complete VFS state machine and prove real application-to-VFS-to-RAMFS/TTY data
flow. One isolated test-only application process is therefore required in the
VFS QEMU fixture without making `init` a production manifest service.

## Decision

### Scope

This outcome defines:

- one real static VFS service ELF;
- one exact production VFS tuple and immutable privilege profile;
- one mount of the exact RAMFS endpoint before VFS readiness;
- fixed-capacity VFS process, descriptor, open-file-description, and vnode
  state;
- per-process root and working-directory ownership;
- one VFS-owned synthetic console object;
- exact version-1 application `OPEN`, `CLOSE`, `READ`, `WRITE`, `GETDENTS`,
  `MKDIR`, `CHDIR`, and result messages;
- bounded absolute and relative pathname routing;
- shared regular-file and directory positions;
- one fixed application directory-record ABI translated from RAMFS records;
- two separate one-page direct-grant hops through resident VFS buffers;
- syscall operation 15 for non-copying grant-range validation;
- one asynchronous VFS-to-TTY operation state with completion, cancellation,
  and writable retry;
- trusted process attach/detach core transitions for later PM integration;
- deterministic native state, protocol, and replayable model evidence;
- one real seven-process QEMU fixture with the six production services plus
  one isolated application probe;
- a standalone VFS image and resident-page bound; and
- fail-closed change-aware validation ownership.

It does not implement:

- production PM-to-VFS executable or descriptor transaction messages;
- successful PM/VM/VFS spawn, activation, or target exit teardown;
- init, shell, executable loading, arguments, environment, or application
  runtime startup;
- dynamic VM mappings, page faults, scratch aliases, or file-backed mappings;
- named device nodes or `/dev`;
- seek, append, truncate, unlink, rename, link, symlink, stat, chmod, chown,
  mount, unmount, or multiple filesystems;
- descriptor duplication as an application call, close-on-exec, or
  unrestricted inheritance;
- pipes, sockets, block devices, ioctl, select, poll, nonblocking I/O, or
  signals;
- UID/GID/ACL enforcement, configurable umask, sessions, or controlling-TTY
  policy;
- concurrent application-operation queues;
- service restart, reconstruction, recovery, or migration compatibility; or
- a seventh production static service.

### Dependency position and fixed service tuple

Production VFS is:

```text
service ID       = 6
process slot     = 5
profile ID       = MICROS_PRIVILEGE_PROFILE_VFS = 6
direct prerequisite = RAMFS service ID 5
stack pages      = 1
resident page limit = 64
```

The production manifest remains exactly:

```text
launcher -> VM -> PM -> TTY -> RAMFS -> VFS
```

VFS validates:

- manifest/configuration version;
- exact service ID and self endpoint;
- exact launcher, TTY, and RAMFS endpoints;
- six active production services;
- canonical endpoint generations and unique service identities;
- zero manifest-view address;
- zero reserved bytes; and
- no configured application endpoint in a production image.

VFS traps instead of reporting ready when this static authority is malformed.

The isolated QEMU build is a separate compile-time fixture shape, not a
runtime flag. It requires exactly seven configured services with IDs 1 through
7, derives the sole test application endpoint from service ID 7, and rejects
every other count or identity. The production build has no code path that
accepts or attaches that endpoint.

### Seven-entry storage capacity, six-entry production manifest

The pointer-free bootstrap storage bound becomes:

```text
MICROS_BOOTSTRAP_SERVICE_CAPACITY = 7
```

The manifest header remains version 1 and changes atomically before v0.1:

```text
entry_capacity = 7
manifest_size  = 64 + 7 * 192 = 1408
```

All kernel, launcher, VM boot-information, generated fixture, service
configuration, host parser, and test consumers rebuild from the same header.
There is no deployed or persistent manifest that needs a compatibility path.
An old six-capacity object and a new seven-capacity object are never mixed.

The production manifest header still has:

```text
entry_count = 6
```

and its seventh entry bytes remain zero. Production validation continues to
require the exact six service identities and relationships.

The only manifest with seven active entries in this outcome is the VFS QEMU
fixture. Its seventh entry is a test application probe, not `init`, not a
production service, and not a precedent for adding applications to the
production static chain.

`MICROS_VM_MAX_STATIC_ADDRESS_SPACES` follows the storage capacity and becomes
seven. The production VM boot object still reports six address spaces. The
VFS fixture reports seven and remains under the existing aggregate 4,096
mapping limit.

#### Capacity-seven service-configuration ABI

The generic service configuration remains version 1, 8-byte aligned, and
exactly 128 bytes. Its capacity-seven layout is:

```text
offset   0  uint32 version
offset   4  uint32 manifest_version
offset   8  uint32 service_id
offset  12  uint32 self_endpoint
offset  16  uint32 launcher_endpoint
offset  20  uint32 service_count
offset  24  seven 8-byte {service_id, endpoint} records
offset  80  uint64 manifest_view_address
offset  88  uint64 reserved[5]
end    128
```

Reducing the reserved tail from six words to five preserves the complete
128-byte object while moving `manifest_view_address` from offset 72 to 80.
Every producer, generated catalog, static assertion, service consumer, and
host parser rebuilds atomically from the same header. No old offset is
accepted.

`service_count` is the active manifest entry count, not the storage capacity.
The first `service_count` records contain exact ascending service IDs and
endpoints; every remaining storage record and every reserved byte is zero.
Generic consumers validate `service_count` in `[1, 7]` plus their own exact
fixture or production requirements. In particular:

- production VFS requires six;
- the compile-time VFS QEMU build requires seven; and
- the same RAMFS binary accepts six for production/RAMFS integration or seven
  when the isolated VFS application follows it.

RAMFS no longer compares active `service_count` with storage capacity.

#### Capacity-seven VM boot-information ABI

The VM boot-information header remains version 1 and 192 bytes. Address-space
capacity becomes seven; every other record capacity and record size is
unchanged. The exact complete-object layout is:

```text
offset      0  192-byte header
offset    192  16 memory-range records, 256 bytes
offset    448  80 reserved-range records, 1,280 bytes
offset  1,728  96 managed-range records, 2,304 bytes
offset  4,032  seven address-space records, 224 bytes
offset  4,256  4,096 mapping records, 98,304 bytes
offset 102,560 262,144 frame-state bytes
end   364,704
```

Version 1 therefore requires:

```text
total_size             = 364704
address_space_capacity = 7
address_space_count    = 6 in production, 7 in the VFS fixture
```

Retained predecessor fixtures continue to report their own exact active
address-space counts under the same capacity-seven layout.

The complete object still fits in 90 pages. The digest covers all 364,704
bytes with the digest field treated as zero. The VM image catalog records the
same exact size, and every kernel, VM, fixture generator, post-link check, and
host parser uses the new mapping offset, frame-state offset, total size, and
digest extent. An inactive seventh production address-space record is
canonical zero.

### Immutable privilege profiles

The production VFS profile becomes:

```text
operations =
    RECEIVE | CALL | REPLY | REPLY_RECEIVE
call targets =
    BOOTSTRAP_LAUNCHER | TTY | RAMFS
send targets = 0
notify targets = 0
kernel operations = 0
```

TTY retains:

```text
notify targets = VFS
```

RAMFS retains exact VFS-only call acceptance. No application profile gains a
TTY or RAMFS target.

The production application profile remains:

```text
operations = CALL
call targets = PM | VFS
send targets = 0
notify targets = 0
kernel operations = 0
```

The VFS QEMU fixture adds one exact test-only profile ID 8:

```text
name = VFS_TEST_APPLICATION
operations = RECEIVE | CALL
call targets = BOOTSTRAP_LAUNCHER | VFS
send targets = 0
notify targets = 0
kernel operations = 0
```

`RECEIVE` exists only because the probe is a static launcher participant. No
profile targets it. The launcher target exists only for its versioned
readiness call. The profile cannot call PM, TTY, RAMFS, VM, or itself.

Its exact manifest tuple is:

```text
service ID          = 7
image ID            = 107
process slot        = 6
profile ID          = 8
service name        = vfs-test-application
profile name        = VFS_TEST_APPLICATION
direct prerequisite = VFS service ID 6
stack pages         = 1
resident page limit = 16
role/device/IRQ      = zero
```

Post-link validation rejects a larger probe image. The limit includes every
`PT_LOAD` page and its external stack and remains inside the unchanged
aggregate 4,096-page manifest bound.

Native manifest tests continue to prove the exact production `APPLICATION`
profile separately. The QEMU fixture proves the cross-address-space
application protocol and both direct-grant hops without granting the probe a
backend target.

### Non-copying grant-range validation

The unified syscall namespace gains:

```text
15 GRANT_VALIDATE
```

Its register ABI is:

```text
a7  15
a0  grantor endpoint, unsigned 32-bit
a1  grant token, unsigned 32-bit
a2  grant-relative byte offset
a3  byte length in [0, MICROS_GRANT_COPY_MAX]
a4  required grant permission, unsigned 32-bit READ or WRITE
a5  zero
a6  zero
```

Success returns zero. The existing stable syscall results are reused.
Grantor endpoint, grant token, and required permission reject nonzero upper
32 bits. Offset and length use their complete unsigned 64-bit values. The
required permission is exactly one existing `READ` or `WRITE` bit, never zero,
both bits, or an unknown bit.

The operation is available to the exact grantee under the same authority as
checked copy. ABI capture first rejects noncanonical upper bits or nonzero
reserved registers with `ARGUMENT`. After that capture, validation is the
exact ADR-0039/ADR-0040 checked-copy order with only the local-range plan,
copy, and overlap stages removed:

1. required permission other than exactly `READ` or `WRITE`, or a special
   grantor endpoint: `ARGUMENT`;
2. length above `MICROS_GRANT_COPY_MAX` or `grant_offset + length` overflow:
   `RANGE`;
3. malformed grant-token packing: `ARGUMENT`;
4. grant, endpoint, or object-registry corruption: invariant failure;
5. stale/dead current grantee process or missing active grantee endpoint:
   `DEAD_ENDPOINT`;
6. free/quarantined grant slot or token-generation mismatch: `STALE_GRANT`;
7. active token naming another grantee: `UNAUTHORIZED`;
8. stale/dead supplied grantor endpoint: `DEAD_ENDPOINT`;
9. active but wrong grantor endpoint or missing required direction:
   `UNAUTHORIZED`;
10. grant-relative end beyond the grant or remote-address arithmetic/range
    failure: `RANGE`;
11. unrecognized ownership phase: kernel invariant failure under the existing
    syscall mapping;
12. zero length: success without address translation;
13. prepare the complete phase-aware remote range plan;
14. structural ownership, PTE, root, scratch, or runtime corruption:
    invariant failure;
15. absent mapping, user range, or insufficient permission: `MEMORY_FAULT`;
16. otherwise success without copying.

It validates no local range, transfers no byte, retains no pointer or token,
and changes no grant, endpoint, address-space, scheduler, or frame state.
Tests combine overflow with a stale token, another-grantee binding with a
stale supplied grantor, wrong grantor with wrong direction, zero length with
invalid authority, and mapping failure with structural corruption so no
adapter precheck can reorder the accepted precedence.

Checked copy remains authoritative and repeats its complete validation at the
actual transfer. `GRANT_VALIDATE` is a preflight, not a lease or pin.

VFS may rely on the validated application range remaining stable only under
all of these v0.1 invariants:

- the application has one thread and is blocked in the VFS call;
- only the grantor can revoke the grant;
- PM asks VFS to cancel/release process-owned state before endpoint and grant
  teardown;
- VM does not mutate a running application's mappings; and
- the later exit transaction keeps the endpoint and mappings intact until VFS
  cleanup acknowledges.

A future multithreaded, asynchronous mapping, or independent grant-revocation
design must not assume that preflight alone makes a later copy infallible.

The freestanding runtime adds:

```c
micros_runtime_result_t micros_runtime_grant_validate(
    micros_endpoint_t grantor,
    micros_grant_t grant,
    size_t grant_offset,
    size_t length,
    uint32_t required_permission
);
```

### VFS phases and startup

VFS has:

```text
UNINITIALIZED -> MOUNTING -> MOUNTED
```

There is no unmount transition in version 1.

After release, VFS:

1. validates its exact configuration;
2. initializes empty process, descriptor, open-file, vnode, pending-operation,
   and bounce-buffer state;
3. calls exact RAMFS `MOUNT` once;
4. validates the returned root handle, directory mode, and size;
5. creates vnode slot zero as the permanent mount root with:
   - one VFS mount reference;
   - one backend reference;
   - the exact root handle;
   - directory mode;
   - size zero;
6. enters `MOUNTED`;
7. in the VFS QEMU build only, atomically attaches the configured test probe
   with root, working directory, and synthetic descriptors 0, 1, and 2; and
8. sends the ordinary versioned readiness call.

Readiness means:

- RAMFS is mounted exactly once;
- the root vnode and reference counts are valid;
- both bounce pages are resident and unused;
- no backend grant is live;
- no application operation is pending; and
- every VFS table passes complete validation.

Production readiness does not mean that any application process exists.

### Fixed capacities

Version 1 has:

```text
MICROS_VFS_PROCESS_CAPACITY            = MICROS_PROCESS_CAPACITY = 64
MICROS_VFS_DESCRIPTOR_CAPACITY         = 16 per process
MICROS_VFS_OPEN_FILE_CAPACITY          = 64 * 16 = 1024
MICROS_VFS_VNODE_CAPACITY              = MICROS_RAMFS_NODE_CAPACITY = 64
MICROS_VFS_TRANSFER_MAX                = 4096
MICROS_VFS_PATH_MAX                    = 4096
MICROS_VFS_NAME_MAX                    = 60
MICROS_VFS_DIRECTORY_RECORD_SIZE       = 80
MICROS_VFS_BACKEND_REFERENCE_THRESHOLD = 256
MICROS_VFS_PENDING_OPERATION_CAPACITY  = 1
MICROS_VFS_RESIDENT_PAGE_LIMIT         = 64
```

The VFS service uses no heap or dynamic allocation.

It has two distinct aligned resident pages:

```text
client bounce page   = 4096 bytes
backend bounce page  = 4096 bytes
```

They are not aliases.

The client page holds:

- copied application paths and create names; or
- translated application directory records.

The backend page holds:

- regular-file source or destination bytes;
- RAMFS directory records; or
- retained TTY read/write bytes.

Two pages are required because directory translation needs a backend source
record and application output staging at the same time. A pending TTY grant
may also retain the backend page across receive iterations.

Capacity exhaustion returns `NO_SPACE`, including exhaustion of the global
grant registry when VFS attempts to create a second-hop RAMFS or TTY grant.
That expected `CAPACITY` result occurs before backend submission, releases any
local staging ownership, and preserves descriptor positions and filesystem or
terminal state. There is no heap fallback, eviction, descriptor growth, vnode
growth, or hidden overwrite.

The complete linked VFS image, including all `PT_LOAD` pages and the external
stack page, must not exceed 64 resident pages. The exact limit is enforced
post-link and remains part of the aggregate manifest page bound.

### VFS process records

Each live VFS process record contains:

```text
record state
exact application endpoint generation
root vnode slot
working-directory vnode slot
16 descriptor slots, each FREE or one open-file-description slot
```

Record states are:

```text
FREE
ACTIVE
```

The record has no PID authority. PM owns semantic PIDs. VFS does not derive a
record index from endpoint slot bits and does not treat endpoint value as a
PID.

An application request resolves only when:

- the kernel-written source endpoint is canonical;
- exactly one `ACTIVE` record stores that complete endpoint generation; and
- every referenced descriptor, open-file, vnode, and pending-operation
  relationship passes complete validation.

A payload cannot select another process record.

Production attach authority belongs to the later PM-directed spawn
transaction. Step 13 exposes no application attach call and no production
PM-to-VFS attach message.

The VFS QEMU build has one compile-time test hook. It accepts only the exact
configured service-7 endpoint, before that endpoint is released, and performs
the same trusted core attach transition that Step 14 will call after its own
transaction validation.

### Descriptors and open-file descriptions

A descriptor is a process-local integer in:

```text
[0, MICROS_VFS_DESCRIPTOR_CAPACITY)
```

Each live descriptor names one global open-file-description slot.

An open-file description contains:

```text
state
object kind
access mode
open flags
reference count
vnode slot when kind is RAMFS
shared regular-file offset or directory cursor
```

Object kinds are:

```text
RAMFS
CONSOLE
```

The access bits are:

```text
MICROS_VFS_ACCESS_READ  = 0x1
MICROS_VFS_ACCESS_WRITE = 0x2
```

At least one bit is present. Unknown bits are invalid.

Open-file reference count equals the number of live descriptor slots that name
it. Although Step 13 has no application `DUP` call, the core supports shared
references so Step 14 descriptor actions and inheritance do not require a
different position model.

A wire-free trusted core transition used only by native/model evidence may
share one exact live source descriptor into one exact free destination
descriptor and increment the same open-file reference. It is not reachable
from an application or the Step 13 service loop. Step 14 decides the PM/VFS
transaction and descriptor-action encoding that may invoke this already
tested transition.

Regular-file position is a byte offset. Directory position is the opaque
RAMFS cursor. Console position is always zero.

### Vnode state and backend reference aggregation

Each live vnode contains:

```text
exact RAMFS node handle
canonical mode
authoritative regular-file size or zero for a directory
VFS local reference count
accumulated RAMFS reference count
mount-root flag
```

The mount root occupies slot zero permanently.

VFS local references are owned by:

- the global mount;
- process roots;
- process working directories; and
- RAMFS open-file descriptions.

RAMFS references are acquired by:

- `MOUNT`;
- successful `LOOKUP`; and
- successful `CREATE`.

A successful `LOOKUP` or `CREATE` response is an owned temporary backend
reference until VFS either integrates it with one exact local owner or sends
`PUTNODE(count = 1)` before returning an expected failure. No backend
reference can exist only in a discarded reply payload.

When lookup or create returns a node already present in the vnode table, VFS
increments both:

- the local VFS reference for the new owner; and
- the accumulated RAMFS reference count returned by the backend.

When a new local reference does not come from RAMFS, such as duplicating root,
cwd, or a descriptor, only the VFS count changes.

When accumulated RAMFS references reach 256 while the vnode remains live, VFS
sends:

```text
PUTNODE(count = accumulated - 1)
```

and retains one backend reference.

When the final non-root VFS reference disappears, VFS sends one `PUTNODE` for
the complete accumulated backend count, then frees the vnode slot.

The root's permanent mount reference prevents its VFS count from reaching
zero. VFS never attempts to release RAMFS's root mount pin.

Any VFS-generated `PUTNODE` failure is a fatal cross-service invariant
failure. It is never shaped as application success or a recoverable close
result.

### Synthetic console object

VFS owns one synthetic console object that is not:

- a RAMFS node;
- a pathname;
- a device number;
- a TTY pointer;
- a grant; or
- an application-visible backend handle.

Trusted process attach may create:

```text
fd 0 -> one read-only console open-file description
fd 1 -> one write-only console open-file description
fd 2 -> one write-only console open-file description
```

All three refer to the same synthetic object but have independent open-file
descriptions. Closing one does not close TTY and does not affect the other two.

No Step 13 application request can reopen the console by path. Children later
receive console descriptors only through explicit Step 14 descriptor actions.

### Path ownership and routing

Application paths are byte strings copied through one application read grant.

The supplied length:

- is in `[2, MICROS_VFS_PATH_MAX]`;
- includes the final NUL;
- has no earlier NUL; and
- leaves at least one non-NUL path byte.

VFS rejects empty application paths even though the internal RAMFS protocol
supports an empty relative path for its own bounded semantics.

Absolute paths select the process root vnode as both:

```text
start node
confinement root
```

Relative paths select:

```text
start node       = process working directory
confinement root = process root
```

VFS then grants exact RAMFS read access to the copied path. RAMFS remains the
sole path walker and enforces repeated separators, `.`, `..`, component
length, trailing separator, and confinement semantics.

For regular-file create, VFS identifies the final lexical component after
preserving a trailing separator as a directory requirement. A missing
regular-file target with a trailing separator returns `NOT_DIRECTORY` and is
not created.

`MKDIR` first resolves the complete supplied path through RAMFS. Any success,
including `.`, `..`, `dir/..`, repeated separators, an all-separator root
path, or a trailing-separator existing directory, releases that temporary
reference and returns `EXISTS`. Only terminal `NOT_FOUND` proceeds to parent
resolution. VFS may then remove trailing separators only to identify the
final nonempty create component, resolves the parent through RAMFS, invokes
`MKDIR`, and releases the temporary parent reference. A missing prefix and a
nondirectory prefix retain RAMFS `NOT_FOUND` and `NOT_DIRECTORY` precedence;
`.` and `..` are never submitted as create names.

There is one root mount, no mount table, and no mount crossing.

### Application protocol identity

Version 1 uses:

```text
MICROS_VFS_PROTOCOL_VERSION      = 1

MICROS_VFS_MESSAGE_OPEN          = 0x00040001
MICROS_VFS_MESSAGE_CLOSE         = 0x00040002
MICROS_VFS_MESSAGE_READ          = 0x00040003
MICROS_VFS_MESSAGE_WRITE         = 0x00040004
MICROS_VFS_MESSAGE_GETDENTS      = 0x00040005
MICROS_VFS_MESSAGE_MKDIR         = 0x00040006
MICROS_VFS_MESSAGE_CHDIR         = 0x00040007
MICROS_VFS_MESSAGE_RESULT        = 0x00040008
```

Every request is a `call` from the kernel-written exact application endpoint
and has a nonzero reply token. Every multibyte payload field is little-endian
and decoded bytewise. Payload bytes are never cast to a C structure.

Flags and reserved bytes must be zero. Unknown versions, unknown flag bits,
noncanonical fixed fields, and legacy layouts are rejected.

### Stable application results

Version 1 results are:

```text
  0  OK
 -1  BAD_TYPE
 -2  BAD_VERSION
 -3  MALFORMED
 -4  CALLER
 -5  STATE
 -6  BUSY
 -7  DESCRIPTOR
 -8  ACCESS
 -9  NOT_FOUND
-10  EXISTS
-11  NOT_DIRECTORY
-12  IS_DIRECTORY
-13  NO_SPACE
-14  GRANT
-15  RANGE
```

`CALLER` means that no active VFS process record matches the exact source
endpoint. `STATE` means that VFS is not mounted or that the matching process is
not in a request-accepting state. `BUSY` means the single global asynchronous
console slot contains another retained application call or an exact cleanup
notification debt.

`DESCRIPTOR` covers an out-of-range or closed descriptor.
`ACCESS` covers a descriptor whose open access excludes the requested
operation. `NO_SPACE` covers fixed VFS/RAMFS capacity, request-ID exhaustion,
or lack of one global grant slot for VFS's second hop. `GRANT` covers a
malformed, stale, wrongly directed, unauthorized, out-of-range,
dead-participant, or unmapped application grant.

RAMFS semantic results map as follows:

| RAMFS result | VFS result |
| --- | --- |
| `NOT_FOUND` | `NOT_FOUND` |
| `EXISTS` | `EXISTS` |
| `NOT_DIRECTORY` | `NOT_DIRECTORY` |
| `IS_DIRECTORY` | `IS_DIRECTORY` |
| `NO_SPACE` | `NO_SPACE` |
| `RANGE` | `RANGE` |
| malformed path/name data | `MALFORMED` |

RAMFS `BAD_TYPE`, `BAD_VERSION`, `CALLER`, `STATE`, `NODE`, `GRANT`, or
`REFERENCE` from a VFS-constructed request is a fatal invariant failure.

TTY `BUSY` and `PENDING` are internal scheduling states. They are not exposed
as application failures. One exact `REQUEST` result is also internal: a
`CANCEL` for the retained accepted read may race that read's completion and
must be followed by exact `COLLECT` as specified below. Every other TTY
protocol, request, caller, state, grant, reply, or notification inconsistency
is fatal.

### Result message

Every application call receives:

```text
type = MICROS_VFS_MESSAGE_RESULT

bytes 0..3    protocol version = 1
bytes 4..7    original request type
bytes 8..11   signed VFS result
bytes 12..15  flags = 0
bytes 16..19  descriptor, or zero
bytes 20..23  mode, or zero
bytes 24..31  transferred byte count, or zero
bytes 32..39  resulting file offset or directory cursor, or zero
bytes 40..47  zero
```

On non-`OK`, bytes 12 through 47 are zero.

On successful `OPEN`, descriptor and mode are set.

On successful `READ`, `WRITE`, or `GETDENTS`, transferred count and resulting
position are set. Console position is zero.

Every other successful result has zero operation fields.

### OPEN request and flags

`OPEN` uses:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..11   application read-grant token
bytes 12..15  path length including final NUL
bytes 16..23  grant-relative path offset
bytes 24..27  open flags
bytes 28..31  create permission bits
bytes 32..47  zero
```

Open flags are:

```text
MICROS_VFS_OPEN_READ       = 0x01
MICROS_VFS_OPEN_WRITE      = 0x02
MICROS_VFS_OPEN_CREATE     = 0x04
MICROS_VFS_OPEN_EXCLUSIVE  = 0x08
MICROS_VFS_OPEN_DIRECTORY  = 0x10
```

Rules are:

- at least one of `READ` or `WRITE` is required;
- unknown bits are malformed;
- `EXCLUSIVE` requires `CREATE`;
- `CREATE | DIRECTORY` is malformed; directory creation uses `MKDIR`;
- create mode contains only permission bits `0777`;
- create mode is zero when `CREATE` is absent;
- a directory may be opened only with `READ`;
- `DIRECTORY` requires the resolved node to be a directory;
- `CREATE` expresses a regular-file expectation;
- `EXCLUSIVE | CREATE` returns `EXISTS` for any already resolved target before
  applying regular-file type checks;
- an absent target without `CREATE` returns `NOT_FOUND`;
- an existing directory with a regular-file create or write expectation
  returns `IS_DIRECTORY`;
- an existing nondirectory with `DIRECTORY` returns `NOT_DIRECTORY`;
- existing regular data is never truncated; and
- append behavior does not exist.

Open performs:

1. common message and exact caller validation;
2. open-flag and path scalar validation;
3. complete path copy and content validation from the application;
4. preflight of one free process descriptor;
5. preflight of one free open-file description;
6. preflight of one free vnode scratch slot, even if the target may already
   be cached;
7. existing-node lookup;
8. when absent and `CREATE` is set, preflight two simultaneously free vnode
   scratch slots before parent lookup and regular-file create, because an
   uncached temporary parent and the new referenced child may both be live;
9. exact type and access checks;
10. vnode/backend-reference integration;
11. open-file initialization at position zero; and
12. atomic descriptor publication.

All expected failures before create preserve namespace and local tables.
After a successful create, all remaining operations are non-failing under
validated invariants. VFS does not return a success-shaped fallback after a
created node cannot be represented.

### CLOSE request

`CLOSE` uses:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..11   descriptor
bytes 12..47  zero
```

Close:

1. resolves the exact process descriptor;
2. clears that descriptor;
3. decrements the open-file reference;
4. on the final reference, releases the RAMFS vnode reference or the synthetic
   console open-file state; and
5. batches backend `PUTNODE` if the vnode reaches its final local release.

Close returns `OK` after descriptor ownership is removed. An impossible
backend-reference failure is fatal.

### READ, WRITE, and GETDENTS request

All three use:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..11   descriptor
bytes 12..15  application grant token
bytes 16..23  grant-relative data offset
bytes 24..27  requested byte count
bytes 28..47  zero
```

Count is at most `MICROS_VFS_TRANSFER_MAX`.

For count zero:

- grant token and offset must both be zero;
- no backend or TTY call occurs;
- position is unchanged; and
- result is `OK` with count zero.

Zero count skips grant and transfer work, not caller, descriptor, access, or
object-kind validation. `READ` or `WRITE` on a directory returns
`IS_DIRECTORY`; `GETDENTS` on a regular file or console returns
`NOT_DIRECTORY`.

For nonzero count:

- `READ` and `GETDENTS` require a write-direction application grant whenever
  they attempt to return data;
- `WRITE` requires a read-direction application grant; and
- the exact requested range must fit one page-bounded checked operation.

### Regular-file READ

Regular-file read:

1. requires descriptor read access and a regular vnode;
2. if position is at or beyond the cached authoritative size, returns
   zero-byte EOF without touching the grant or RAMFS;
3. validates the complete application write-grant range through operation 15;
4. creates one exact write-direction VFS-to-RAMFS grant over the backend
   bounce range;
5. calls RAMFS `READ` at the shared open-file position;
6. validates node, size, next-position, and count consistency;
7. revokes the RAMFS grant;
8. copies the exact returned bytes from the backend bounce page to the
   application grant in one checked copy; and
9. commits vnode size and shared position only after that copy succeeds.

An application grant failure changes no file position. RAMFS read changes no
filesystem data, so retry observes the same bytes.

### Regular-file WRITE

Regular-file write:

1. requires descriptor write access and a regular vnode;
2. copies the complete application source range into the backend bounce page;
3. creates one exact read-direction VFS-to-RAMFS grant;
4. calls RAMFS `WRITE` at the shared open-file position;
5. validates all-or-error count, size, and next-position;
6. revokes the RAMFS grant; and
7. commits vnode size and shared position.

An application grant failure occurs before RAMFS mutation. RAMFS `NO_SPACE` or
`RANGE` leaves VFS position and size unchanged.

### Application directory records and GETDENTS

The application-facing record size is 80 bytes:

```text
bytes 0..3    record size = 80
bytes 4..7    canonical mode
bytes 8..11   name length in [1, 60]
bytes 12..15  flags = 0
bytes 16..75  60-byte name field
bytes 76..79  zero
```

Only the first `name length` name bytes are nonzero. The record contains no
RAMFS node handle, inode number, pointer, mount identity, or backend cursor.

`GETDENTS` requires:

- descriptor read access;
- a directory vnode;
- count zero or a count in `[80, 4096]`;
- an exact multiple of 80; and
- a valid application write grant for nonzero count.

For a nonzero request VFS:

1. returns zero-byte EOF without touching the application grant when the
   current cursor is the known RAMFS end cursor;
2. otherwise validates the complete application destination range;
3. computes how many 80-byte records fit and caps the request at 32 records,
   the number of complete 128-byte backend records that fit one page;
4. asks RAMFS for that many complete records in the backend page;
5. validates every backend node handle, mode, name length, name padding, and
   reserved byte without acquiring a child reference;
6. writes translated 80-byte records to the client page;
7. copies all translated records to the application in one checked copy; and
8. commits the RAMFS-returned cursor only after that copy succeeds.

At RAMFS cursor end, VFS returns zero-byte EOF without touching the grant.

No partial application record is returned. A failed application copy does not
advance the shared directory cursor.

### MKDIR request

`MKDIR` uses:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..11   application read-grant token
bytes 12..15  path length including final NUL
bytes 16..23  grant-relative path offset
bytes 24..27  permission bits
bytes 28..47  zero
```

Mode contains only `0777` permission bits.

VFS:

1. copies and validates the complete path;
2. preflights one vnode scratch slot;
3. resolves the complete target;
4. on success, releases its backend reference and returns `EXISTS`;
5. on terminal `NOT_FOUND`, identifies one nonempty final component, resolves
   its parent, and preserves missing-prefix or nondirectory-prefix results;
6. invokes RAMFS `MKDIR` with directory type plus the supplied permissions;
7. releases the temporary parent reference; and
8. returns zero result fields.

Any full-target result other than `OK` or `NOT_FOUND` is returned or treated
as fatal according to the stable RAMFS mapping above. No path spelling that
resolves to `.` or `..` reaches the RAMFS create-name validation.

### CHDIR request

`CHDIR` uses:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..11   application read-grant token
bytes 12..15  path length including final NUL
bytes 16..23  grant-relative path offset
bytes 24..47  zero
```

VFS:

1. copies and validates the complete application path;
2. preflights one vnode scratch slot;
3. resolves the path from root or current working directory;
4. requires a directory;
5. integrates the returned backend reference;
6. releases the old working-directory local reference; and
7. publishes the new reference.

Process root is immutable in version 1.

### One global asynchronous console state

RAMFS calls are synchronous and bounded. TTY reads and physical-output
backpressure are asynchronous.

VFS retains at most one asynchronous console state globally. States are:

```text
NONE
TTY_READ_WAIT_COMPLETION
TTY_WRITE_WAIT_WRITABLE
TTY_WRITE_WAIT_COMPLETION
TTY_COMPLETION_NOTICE_DEBT
TTY_WRITABLE_NOTICE_DEBT
TTY_TEST_DRAIN_WAIT_WRITABLE   QEMU build only
```

The first three non-`NONE` states retain exactly:

```text
VFS process slot and exact endpoint
application reply token
descriptor and open-file-description slot
application grant token, offset, and count when still required
TTY request ID
VFS-to-TTY grant token when still live
```

The two debt states retain no process, reply token, descriptor, grant, bounce
page, or request ownership. They mean that cleanup resolved the exact TTY
request before its already-required notification was received. VFS keeps the
global asynchronous slot unavailable until it consumes the corresponding
exact event bit.

The test-only drain state retains only the exact probe reply token and one
unaccepted TTY request ID. It owns no grant or bounce page and is absent from
the production build.

While any non-`NONE` state exists:

- exact TTY notifications are processed;
- the later exact PM cleanup request may cancel or collect a retained
  application operation;
- another well-formed registered application call returns `BUSY`; and
- unknown types, noncanonical fixed/scalar fields, foreign callers, and local
  descriptor/access/type errors still receive their earlier stable validation
  result rather than being hidden by `BUSY`.

There is no application queue and no second pending bounce buffer.

### Console READ

Console read:

1. requires a read-only or read/write console open-file description;
2. validates the complete application write-grant range through operation 15;
3. allocates the next nonzero monotonic TTY request ID;
4. creates an exact write-direction TTY grant over the backend bounce page;
5. submits TTY read;
6. retains the application reply token and returns to receive;
7. on completion notification, collects the exact request;
8. revokes the TTY grant;
9. copies the exact transferred bytes to the prevalidated application grant;
10. replies with the transferred source-byte count and position zero; and
11. clears the pending record.

Preflight before TTY submission prevents a malformed or unmapped application
destination from consuming terminal input at the second hop.

After TTY has consumed input, failure of the repeated application checked copy
is a fatal violation of the blocked-thread, mapping, grant, or cleanup
ordering invariants. It is not converted into a recoverable `GRANT` result
that would silently lose terminal input.

If later process cleanup reaches an exact pending read, VFS calls TTY
`CANCEL` for the retained request ID:

- `CANCEL/OK` with the exact ID and zero count means cancellation succeeded
  without consuming input;
- `CANCEL/REQUEST` with the exact ID and zero count means the accepted read
  completed before cancellation, so VFS immediately calls exact `COLLECT`;
- that follow-up must return `COLLECT/OK` for the same ID, after which VFS
  validates and discards the completed bytes, revokes the grant, and enters
  `TTY_COMPLETION_NOTICE_DEBT`; and
- every other cancel/collect type, ID, count, or result combination is fatal.

The exact race is not treated as a generic TTY request inconsistency.

### Console WRITE

Console write:

1. requires a write-only or read/write console open-file description;
2. copies the complete application source into the backend bounce page;
3. creates an exact read-direction TTY grant;
4. submits the next TTY request ID.

When TTY returns `OK`:

- TTY already owns the complete source bytes;
- VFS revokes its TTY grant;
- VFS enters `TTY_WRITE_WAIT_COMPLETION`; and
- completion collection returns the exact source count to the application.

When TTY returns `BUSY` because resident output still drains:

- the request ID is not accepted;
- VFS retains the same ID, grant, bounce bytes, and application reply token;
- VFS enters `TTY_WRITE_WAIT_WRITABLE`; and
- one exact writable event retries the unchanged request.

An uncollected earlier TTY completion is impossible because VFS has only one
asynchronous operation. Observing that condition is fatal.

Accepted TTY writes cannot be cancelled. Cleanup performs exact `COLLECT`,
requires `OK` for the retained ID and source count, discards that completion,
and enters `TTY_COMPLETION_NOTICE_DEBT`; every other result is fatal. An
unaccepted writable retry may be abandoned after revoking the grant; any
already armed writable notification becomes `TTY_WRITABLE_NOTICE_DEBT`.

Application write success means TTY accepted the complete source into its
resident state. It does not mean that the UART is physically drained.

### TTY notification handling

VFS accepts only:

```text
source = exact TTY endpoint
type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION
reply token = 0
event bits subset of COMPLETION | WRITABLE and nonzero
remaining payload bytes = 0
```

For every received bit VFS inspects its own pending state. It never infers
request identity or event order from one bit alone.

Completion collection and writable retry use the exact retained request ID.
If cleanup collected a completion before its notification was delivered, the
next exact completion bit clears `TTY_COMPLETION_NOTICE_DEBT`. If cleanup
abandoned an armed writable retry, the next exact writable bit clears
`TTY_WRITABLE_NOTICE_DEBT`. Unknown, duplicate, impossible, or foreign
notifications outside those exact debt states are fatal.

In the QEMU build, an exact writable bit in
`TTY_TEST_DRAIN_WAIT_WRITABLE` retries the unchanged test barrier request
defined below.

TTY request IDs are nonzero, monotonically increasing, and never wrap. A
`BUSY` submission does not consume the ID. Once VFS abandons an unaccepted ID,
the next candidate is greater; it never reuses an ID for unrelated data.
Exhaustion returns `NO_SPACE` before a new console operation begins.

### Trusted process attach

The portable trusted attach transition receives:

```text
exact future application endpoint
root vnode
one bounded descriptor-action declaration
```

Step 13's test attach declaration is exactly:

```text
install console read at fd 0
install console write at fd 1
install console write at fd 2
```

Attach preflights:

- one free VFS process record;
- exact endpoint uniqueness;
- two root vnode local references;
- three free open-file descriptions;
- free descriptor slots 0, 1, and 2; and
- complete table invariants.

One non-failing commit publishes the record, root/cwd references, and three
descriptors. Failure leaves every VFS table unchanged.

Step 14 will define PM transaction identity, prepare/commit/abort messages,
general bounded descriptor actions, and hidden-child rollback. This ADR does
not assign those wire fields prematurely.

### Trusted process detach

Trusted detach receives one exact active endpoint.

It:

1. resolves and cancels, collects, or abandons that process's pending TTY
   operation;
2. revokes every VFS-owned TTY grant;
3. clears every process descriptor;
4. decrements every referenced open-file description;
5. releases final vnode references and batches all required `PUTNODE` calls;
6. releases working-directory and root local references;
7. clears the exact endpoint; and
8. returns the process record to `FREE`.

If cleanup collects an already completed read or accepted write before the
completion bit is delivered, it enters `TTY_COMPLETION_NOTICE_DEBT`. If it
abandons an unaccepted write retry after TTY armed writability, it enters
`TTY_WRITABLE_NOTICE_DEBT`. The trusted detach transition completes only
after the exact debt bit is consumed; the later Step 14 wire protocol may
retain its cleanup reply while that bounded state finishes.

The root mount reference, unrelated processes, unrelated descriptors, and
unrelated pending state remain unchanged.

The native model must prove that attach followed by arbitrary valid operations
and detach returns all non-mount VFS state and all VFS-owned grants to
baseline.

Production PM must later invoke detach after kernel exit-begin has made the
application thread permanently non-runnable but before endpoint, grant,
mapping, thread, or process teardown. That ordering remains authoritative from
ADR-0046.

### Request validation order

Application calls use:

1. known message type;
2. protocol version and canonical fixed payload;
3. exact kernel-written source endpoint and nonzero reply token;
4. exact active VFS process record;
5. operation-specific flags and scalar ranges;
6. descriptor, access, and object-kind validation where applicable;
7. global asynchronous-console availability;
8. complete pre-state VFS invariants;
9. application grant preflight or copy;
10. local capacity preflight;
11. operation-specific backend validation and call only after every local
    failure that must precede backend mutation;
12. one failure-atomic local transition; and
13. complete post-state VFS invariants.

An earlier failure is not masked by a later `BUSY`, grant, descriptor, or
backend condition.

Unknown user messages with a valid reply token receive `BAD_TYPE`. A token-zero
user message under the accepted profile graph is impossible and fatal.

### Service loop and failure handling

VFS is single-threaded.

After readiness it accepts:

- versioned application calls from registered exact endpoints;
- in the compile-time QEMU build only, the exact service-7 drain-barrier call;
- exact TTY notifications;
- the launcher readiness acknowledgment during startup; and
- later exact PM lifecycle calls only after Step 14 assigns them.

Synchronous application requests may use `reply_receive` only when no local
state must be inspected after the reply. Asynchronous TTY operations retain
the exact application reply token until completion.

Expected application errors receive stable results and preserve all unrelated
state.

The following are fatal:

- complete-state validation failure;
- a backend protocol result impossible for a VFS-constructed request;
- a VFS-owned grant-creation result other than expected global `CAPACITY`, or
  any failed revocation after successful creation;
- impossible RAMFS reference release;
- TTY notification or request-state mismatch;
- TTY reply or notify loss;
- application reply failure outside the exact later cleanup protocol;
- endpoint/profile/configuration disagreement;
- unexpected static-service loss; or
- any silent fallback to another endpoint, descriptor, node, or request ID.

There is no inferred caller, stale-token repair, automatic remount, alternate
filesystem, kernel console fallback, or recovery path.

### Complete state invariants

At every operation boundary:

- every active process endpoint is canonical and unique;
- every live descriptor names one live open-file description;
- every open-file reference count equals its descriptor users;
- every RAMFS open-file names one live vnode;
- every console open-file names no vnode;
- every live vnode has a canonical live RAMFS handle and mode;
- vnode local reference counts equal the mount, root, cwd, and open-file
  owners that name them;
- vnode backend reference counts are nonzero and below the cleanup threshold,
  except during one validated cleanup transition;
- root vnode slot zero retains the permanent mount reference and backend pin;
- no non-root vnode with zero local references remains live;
- regular-file positions and sizes stay within the RAMFS file bound;
- directory cursors stay within the RAMFS cursor bound;
- console positions are zero;
- at most one asynchronous console state exists;
- a pending TTY grant names the exact TTY endpoint, bounce page, direction,
  and retained request;
- a notification-debt state owns no process, reply token, grant, bounce page,
  or request and names exactly one required TTY event bit;
- the QEMU-only drain state owns only the exact probe reply token and one
  unaccepted request ID and names no grant, descriptor, or bounce page;
- the client and backend bounce pages have at most one current owner each;
- every VFS-owned live grant is reachable from one current operation; and
- every free record and slot has canonical zero state.

Internal corruption deliberately traps.

### Native evidence

Native tests start red and cover:

- operation-15 register shape, zero length, direction, authority, range,
  mapping, phase, zero-mutation, stable syscall results, and dual-invalid
  precedence combinations;
- runtime wrapper register preservation;
- the exact 128-byte capacity-seven service configuration, 364,704-byte VM
  boot-information offsets/digest extent, and six-entry production manifest;
- exact production VFS and application privilege profiles;
- process attach/detach and endpoint-generation uniqueness;
- descriptor and open-file allocation, capacity, sharing, and final release;
- root/cwd references and absolute/relative routing;
- vnode cache hits, scratch-slot preflight, backend-reference aggregation,
  threshold cleanup, final `PUTNODE`, and root pin preservation;
- open existing, create, exclusive create, directory type checks, malformed
  flags, trailing-separator directory expectations, and all rollback prefixes;
- close and shared open-file positions;
- zero-length, EOF, short read, full write, no-space, range, wrong-direction,
  stale, unmapped, and full-global-grant-registry behavior;
- application directory-record translation, exact padding, multi-record
  continuation, EOF, and failed-copy cursor preservation;
- mkdir full-target existence, `.`, `..`, trailing separators, missing
  prefixes, parent creation, and chdir;
- console read completion, completion-during-submit, cancel-before-complete,
  complete-before-cancel, queued-notification cleanup debt,
  application-grant preflight, and no input loss;
- console write acceptance, completion collection, physical-drain `BUSY`,
  writable retry, coalesced event bits, abandoned-retry notification debt,
  and request-ID exhaustion;
- fixture-only drain-barrier `BUSY`, writable retry, terminal `GRANT`, no
  accepted request, and zero output publication;
- foreign callers, malformed messages, token-zero impossibility, and every
  stable VFS result; and
- complete state validation after every expected failure.

One replayable model performs at least 8,192 mixed transitions across:

- process attach and detach;
- open, create, close, read, write, getdents, mkdir, and chdir;
- descriptor and vnode capacity pressure;
- shared open-file references;
- RAMFS semantic failures;
- application grant failures;
- TTY submit, completion, writable, cancel, and collect events;
- malformed and foreign calls; and
- injected reply/backend failures that are expected to be fatal.

After every transition it compares the complete production VFS state with an
independent reference model. Failure prints the seed and full replayable
operation trace.

The persistent workflow is:

```text
test-vfs-model
```

### Standalone VFS image evidence

The build exposes:

```text
build-vfs-service-image
```

Post-link validation requires:

- fixed-address freestanding ELF rules;
- no relocation, hosted runtime, heap, TLS, constructor, or undefined symbol;
- exactly one external stack page;
- canonical RX/R/RW-NX closure;
- both resident bounce pages;
- all fixed VFS tables; and
- no more than `MICROS_VFS_RESIDENT_PAGE_LIMIT` total resident pages.

### QEMU application-to-VFS integration

The QEMU fixture contains:

```text
service 1  launcher              profile BOOTSTRAP_LAUNCHER
service 2  VM                    profile VM
service 3  PM                    profile PM
service 4  TTY                   profile TTY
service 5  RAMFS                 profile RAMFS
service 6  production VFS        profile VFS
service 7  VFS application probe profile VFS_TEST_APPLICATION
```

The probe:

- uses process slot 6;
- directly depends on VFS;
- has no TTY or RAMFS target;
- sends ordinary launcher readiness;
- uses public application VFS messages and direct-grant syscalls after
  readiness, plus the exact compile-time drain barrier below; and
- is never called `init` or treated as a production PM child.

The QEMU build alone recognizes:

```text
MICROS_VFS_TEST_MESSAGE_DRAIN = 0x0004ff01
```

The request comes only from the configured service-7 endpoint and contains
the ordinary protocol version followed by 44 zero bytes. Production VFS
builds treat that type as `BAD_TYPE`.

For this barrier, ADR-0047's operation-specific `SUBMIT_WRITE` validation is
made explicit: after canonical type, payload, caller, phase, request-ID, and
count checks, an occupied completion or resident-output slot returns `BUSY`
before grant validation. Only an otherwise reusable slot reaches grant
validation and returns `GRANT` for `MICROS_GRANT_NONE`. This is the ordering
already exercised by the accepted TTY physical-drain gate.

After the final console write has completed, the barrier:

1. allocates the next unaccepted TTY request ID;
2. submits a one-byte TTY write with `MICROS_GRANT_NONE`;
3. if TTY returns `BUSY`, retains the probe reply token and unchanged request
   ID in `TTY_TEST_DRAIN_WAIT_WRITABLE`;
4. retries only after the exact writable event; and
5. requires TTY `GRANT` once resident output is empty, proving request
   validation reached the deliberately invalid grant without accepting an ID
   or publishing new output.

That exact `GRANT` is test-barrier success, not a production application
result. VFS replies only then with the ordinary VFS result envelope, original
test request type, `OK`, and zero operation fields. The report trap can
consequently stop user scheduling while the kernel polls UART `THRE|TEMT`:
TTY has no resident marker byte left to feed, and the barrier added no byte of
its own. Any other TTY result, completion, or notification is fatal.

The scenario proves:

1. all six production services perform their accepted startup and readiness
   behavior;
2. VFS mounts RAMFS once before reporting ready;
3. VFS trusted test attach installs exact root/cwd and console descriptors;
4. the probe opens `/etc/motd`, reads its exact bytes through both grant hops,
   observes zero-byte EOF, and closes the descriptor;
5. the probe creates `tmp` relative to root cwd through public `MKDIR`;
6. the probe uses relative `tmp/note` creation and writing from root cwd;
7. the probe reopens `/tmp/note` by absolute path and reads the exact bytes;
8. the probe changes cwd to `/tmp`, resolves `note` relatively, then returns
   cwd to `/` and releases the non-root working-directory reference;
9. one directory descriptor enumerates complete translated 80-byte
   application records with a shared cursor and no exposed RAMFS handle;
10. the probe writes one exact serial-input-ready line through descriptor 1;
11. the host injects one configured byte sequence only after that complete
    line, with no sleep;
12. descriptor 0 returns the exact canonical TTY line through the
    TTY-to-VFS and VFS-to-application grants;
13. every transient file and directory descriptor is closed;
14. the probe writes the exact pass marker through descriptor 1, revokes its
    final application grant after the write result, while the VFS result
    contract guarantees its TTY grant is already revoked;
15. the test-only drain barrier proves no marker byte remains in TTY resident
    output without granting the probe a TTY target; and
16. a fixed-width report trap from the exact probe thread lets the kernel
    verify launcher sealing, VM handoff, TTY ownership, all seven readiness
    states, zero live grants, physical UART drain, and clean SBI shutdown.

The input-ready line is:

```text
MICROS_VFS_TEST_INPUT_READY
```

The final line is:

```text
MICROS_VFS_TEST_PASS mount=single descriptors=owned paths=absolute,relative files=two-hop directories=translated console=two-hop grants=balanced
```

The stable workflow is:

```text
test-qemu-vfs
```

Its host absolute timeout is 180 seconds. Guest readiness uses the existing
manifest-owned 300,000,000-counter-tick budget. The host sends input only on
the exact configured line and never uses a sleep to establish correctness.

Target PM/VFS process teardown is not claimed by this fixture because the
kernel exit-begin and VM release transitions are Step 14. Native attach/detach
and model evidence prove the complete VFS-owned cleanup algorithm now; Step 14
must prove the directed PM/kernel/VM target ordering.

### Validation ownership

The change-aware planner owns:

- VFS public protocol, core, service, test probe, fixture generator, and
  service-image files;
- operation-15 kernel, runtime, ABI, and tests;
- bootstrap-capacity and VM-boot-info dependents;
- VFS-relevant manifest/profile and service-configuration inputs;
- TTY and RAMFS protocol headers used by VFS;
- VFS research, ADR, architecture, roadmap, and testing documentation.

At minimum:

- portable VFS core or host test changes select fast native tests;
- VFS model or core changes select `test-vfs-model`;
- operation-15 changes select native grant tests, the real grant-syscall QEMU
  gate, the handed-off grant gate, user-runtime evidence, and VFS QEMU;
- bootstrap capacity or VM boot-information changes select every launcher,
  VM-handoff, PM, TTY, RAMFS, and VFS QEMU workflow whose generated object
  embeds that ABI;
- VFS service, application protocol, fixture, TTY/RAMFS integration, or test
  app changes select `test-qemu-vfs`;
- VFS linker, tables, or image inputs select `build-vfs-service-image`;
- shared TTY/RAMFS inputs retain their existing image and QEMU owners; and
- public header or build-graph changes select all three diff checks.

Representative QEMU inputs must continue to have exact parity with the
authoritative QEMU workflow inventory. No new workflow may be omitted from
representative ownership.

### Implementation task and commit plan

Implementation follows only after this ADR is independently reviewed,
Accepted, and merged.

#### 1. Add grant preflight and seven-entry test capacity

- Red: operation 15 and a seven-entry VFS fixture shape fail ABI, grant,
  manifest, VM boot-object, runtime, and retained-workflow tests.
- Green: add non-copying grant validation, runtime wrapper, capacity-seven
  pointer-free objects, the exact 128-byte service configuration and
  364,704-byte VM layout, RAMFS active-count validation, and exact
  production-six validation.
- Keep every existing QEMU fixture green.

#### 2. Add the portable VFS core and replayable model

- Red: process, descriptor, open-file, vnode, path, regular-I/O,
  directory-translation, console-state, and cleanup tests fail.
- Green: implement fixed tables, complete validation, backend interfaces, and
  the 8,192-transition model.
- No production VFS ELF or QEMU fixture is added in this commit.

#### 3. Add the production VFS service and application protocol

- Red: protocol decode, exact profiles, mount/readiness, service loop, image,
  and 64-page checks fail.
- Green: add the public VFS header, service adapter, RAMFS and TTY calls,
  application replies, and standalone image.
- Production manifest count remains six.

#### 4. Add the seven-process VFS QEMU gate

- Red: the real cross-address-space application scenario fails before the
  application grant, VFS bounce, backend grant, TTY notification, report, and
  shutdown paths exist.
- Green: add the test-only probe profile, fixture generator, exact host input,
  public mkdir scenario, no-authority drain barrier, report handler, pass
  marker, and `test-qemu-vfs`.
- Retain all predecessor service gates.

#### 5. Complete validation ownership and documentation

- Red: representative ownership omits at least one VFS, operation-15,
  capacity, generated-fixture, image, or QEMU path.
- Green: add fail-closed planner ownership, underselection regressions,
  authoritative workflow parity, and final directly related documentation.
- Run the exact PR-tier plan, stage the complete tree, record tree identity,
  and obtain independent implementation review.

## Acceptance criteria

Implementation is complete only when:

- the local MINIX baseline and every intentional difference are documented;
- production VFS service ID 6 mounts exact RAMFS once before readiness;
- the production static manifest still contains exactly six entries;
- only the isolated VFS fixture activates the seventh storage entry;
- the 128-byte service configuration and 364,704-byte VM boot object match
  every exact offset, active-count, zero-tail, and digest rule above;
- operation 15 validates a complete remote grant range without copying or
  mutation and preserves accepted checked-copy error precedence;
- application messages use exact source identity and stable results;
- second-hop global grant exhaustion returns `NO_SPACE` without backend
  submission;
- descriptor, open-file, vnode, root, cwd, position, and backend-reference
  invariants hold after every operation;
- `MKDIR` resolves complete-target `.`, `..`, existing, and missing-prefix
  semantics before any create-name call;
- regular and directory data cross two separately authorized grant hops;
- terminal input cannot be consumed before the application destination grant
  is completely prevalidated;
- exact `CANCEL/REQUEST -> COLLECT/OK` cleanup is nonfatal and balances its
  queued completion notification;
- asynchronous TTY completion and writable retry do not block the VFS receive
  loop;
- trusted detach returns every non-mount VFS resource to baseline;
- the VFS ELF stays within 64 resident pages;
- native and replayable model evidence passes;
- the exact seven-process QEMU marker is observed after host-triggered input;
- the test-only no-authority drain barrier proves TTY resident output empty
  before the report trap;
- zero live grants and physical UART drain are verified before shutdown;
- change-aware ownership is fail-closed; and
- independent review reports no substantive issue.

## Consequences

- Applications gain one stable descriptor-based I/O boundary and still cannot
  call RAMFS or TTY.
- VFS becomes the sole owner of process-visible path routing, descriptors,
  open-file positions, and console binding.
- RAMFS remains a namespace/data backend and never learns application
  identity.
- TTY remains a canonical terminal backend and never learns application
  identity or descriptors.
- One non-copying grant preflight preserves consuming-read semantics across
  the required second copy without creating transitive authority.
- Fixed tables and one pending console operation keep the first implementation
  deterministic and modelable at the cost of explicit `BUSY` and capacity
  results.
- A 1,024-entry open-file table guarantees one slot for every possible
  process descriptor without dynamic allocation.
- The manifest grows by one entry and the VM boot object by one 32-byte
  address-space record; the generic service configuration remains 128 bytes
  with moved internal offsets. Production still boots exactly six services,
  and no compatibility machinery is added.
- The QEMU application probe proves the real data path without implementing
  init or production spawn early.
- Step 14 can add transaction wire messages around an already tested VFS state
  owner instead of inventing descriptor semantics during executable loading.

## Alternatives considered

### Let RAMFS or TTY use the application's grant directly

Rejected. Direct grants are non-transitive. VFS cannot delegate authority that
the application granted only to VFS.

### Submit a TTY read before validating the application destination

Rejected. TTY may consume canonical input after copying it into VFS's resident
page. A later application-grant failure would lose input and diverge from the
selected baseline.

### Validate by overwriting the application buffer before the backend call

Rejected. A write-direction grant does not authorize VFS to read and preserve
the old bytes, and pre-zeroing a destination would mutate application state
before successful I/O.

### Require read/write permission on every read destination

Rejected. It grants VFS unnecessary authority over application bytes and
weakens exact-direction least privilege.

### Add a two-phase TTY read acknowledgment

Rejected. ADR-0047 already defines a complete accepted TTY protocol. A
grantee-only grant preflight solves the VFS adaptation without replacing that
protocol.

### Use one VFS bounce page

Rejected. Directory translation requires simultaneous backend input and
application output staging. A pending TTY grant would also make one shared page
unavailable for safe translation or synchronous file work.

### Expose RAMFS 128-byte records to applications

Rejected. They include backend node identity and make the application ABI
depend on one filesystem implementation. VFS owns the application record.

### Expose the console as a RAMFS `/dev/console` node

Rejected. Step 13 has one synthetic VFS object and no device-filesystem or
driver-discovery design. Adding a namespace node would imply unsupported open,
mode, driver, and recovery semantics.

### Keep VFS blocked in one long TTY call

Rejected. VFS must receive TTY completion/writable notifications and later PM
cleanup while an application read is blocked.

### Queue many asynchronous application operations

Rejected. One shell-oriented v0.1 workload does not justify multiple retained
reply tokens, multiple TTY buffers, fairness policy, or cancellation queues.
One explicit global slot is sufficient and returns `BUSY` otherwise.

### Let the report trap establish console drain by timing

Rejected. VFS write completion proves resident acceptance, not that TTY has
fed every byte to the UART. Once the probe traps, TTY cannot run to empty
resident output. The fixture-only invalid-grant barrier first proves TTY
resident output empty without publishing another byte; only then can the
kernel poll hardware `THRE|TEMT`.

### Add seek, append, truncate, unlink, and rename now

Rejected. The shell MVP requires sequential reads/writes and directory
enumeration. These operations add independent mutation and position contracts
without a current dependency.

### Lazily create a VFS process record on the first application call

Rejected. An application cannot authorize its own root, cwd, descriptors, or
lifecycle. PM remains the production authority, and the QEMU fixture uses one
trusted exact endpoint.

### Use PM, launcher, RAMFS, or VFS itself as the application probe

Rejected. Their profiles and identities are static-service authority, not an
application address space. Such a test would not prove the first grant hop or
VFS caller isolation.

### Add `init` as the seventh static manifest entry

Rejected. ADR-0009 and ADR-0043 require PM/VFS/VM to create init only after
bootstrap authority is sealed.

### Implement production spawn before testing VFS

Rejected. It would collapse Step 13 and Step 14, entangle VFS correctness with
dynamic VM mapping and process activation, and violate the development DAG.

### Replace a predecessor service to keep a six-entry VFS fixture

Rejected. The gate must retain real launcher, VM, PM, TTY, and RAMFS behavior.
Removing one would stop being dependency-closed and weaken regression
evidence.

### Bump a persisted compatibility version and support both capacities

Rejected. `micros` has no deployed manifest or stored boot object. All
consumers rebuild atomically before v0.1, so dual-format migration machinery
would be unsupported complexity.

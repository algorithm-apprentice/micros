# ADR-0048: RAMFS Service and VFS Filesystem Protocol

- Status: Accepted
- Date: 2026-10-10
- Refines: ADR-0004, ADR-0005, ADR-0007, ADR-0008, ADR-0009,
  ADR-0025, ADR-0029, ADR-0030, ADR-0031, ADR-0032, ADR-0033,
  ADR-0034, ADR-0038, ADR-0039, ADR-0042, ADR-0043, ADR-0045,
  ADR-0046, and ADR-0047
- Supersedes in part:
  - ADR-0047's immutable RAMFS profile. RAMFS gains exact reply and
    reply/receive authority for the synchronous VFS filesystem protocol.
  - ADR-0047's immutable VFS profile. VFS gains RAMFS as one additional exact
    call target.

## Context

Development-DAG Step 11 is complete:

- VM owns the static wired user mappings;
- PM owns application lifecycle metadata and one empty-process reservation;
- TTY owns the UART and PLIC source 10;
- VFS has one stable profile identity and exact TTY call authority; and
- direct grants and checked copies remain available after VM handoff.

RAMFS is the next dependency-ready service. ADR-0008 already requires:

- separate VFS and RAMFS services;
- regular files and directories;
- lookup, create, open-related behavior, read, write, close-related reference
  handling, and directory enumeration;
- a build-generated in-memory root image;
- writable state after initialization;
- direct-grant file transfer; and
- no block layer, persistent metadata, journal, page cache, or DS discovery.

The fixed MINIX baseline is documented in
[the VFS/MFS filesystem protocol study](../research/minix-vfs-mfs-filesystem-protocol.md).
MINIX keeps descriptor and mount routing in VFS, accepts filesystem requests
only from VFS, leaves successful lookup/create results referenced, transfers
data through directional grants, returns short reads at EOF, reads sparse
holes as zero, grows files on write, enumerates only complete directory
records, and releases filesystem references through counted `PUTNODE`
requests.

The literal MINIX implementation does not fit the accepted `micros` boundary:

- ADR-0008 deliberately excludes a block device and on-disk MFS format;
- the freestanding service runtime has no heap;
- IPC messages have only a fixed 48-byte protocol payload;
- direct grants are kernel-managed and non-transitive;
- the only dependency-ready client is one exact VFS endpoint;
- no application credential model exists yet; and
- no service recovery exists before RS.

This decision defines the complete Step 12 RAMFS contract without pulling in
production VFS descriptors, applications, executable loading, or persistent
storage.

## Decision

### Scope

This outcome defines:

- one real statically embedded RAMFS service ELF;
- exact RAMFS and VFS privilege-profile replacements;
- one validated build-generated seed declaration and binary image;
- one fixed BSS-backed node table and file-data arena;
- generation-safe RAMFS node handles;
- separate namespace-link and VFS-reference counts;
- one bounded path resolver with root confinement;
- exact version-1 `MOUNT`, `LOOKUP`, `CREATE`, `MKDIR`, `READ`, `WRITE`,
  `GETDENTS`, `PUTNODE`, and result messages;
- one-page exact-direction VFS grants;
- failure-atomic namespace and file-data mutation;
- short EOF reads, sparse zeroes, and bounded file growth;
- cursor-based complete-record directory enumeration;
- native parser, seed, state-machine, capacity, and replayable model evidence;
- one deterministic QEMU scenario with an exact test VFS peer; and
- fail-closed validation ownership.

It does not implement:

- production VFS descriptors, open-file descriptions, application requests,
  descriptor duplication, or the synthetic console object;
- an application-to-VFS grant hop or complete two-hop application I/O;
- executable buffering, VM loading, PM spawn, init, shell, or user commands;
- more than one mounted filesystem, VFS peer, namespace root, or RAMFS
  service;
- unmount or remount;
- unlink, rmdir, rename, hard links, symbolic links, truncate, stat, chmod,
  chown, timestamps, extended attributes, file locks, pipes, sockets, or
  device nodes;
- nested mounts, mount-point crossings, chroot, or per-process mount
  namespaces;
- UID, GID, supplementary-group, ACL, or capability checks;
- mmap, page cache, file-backed VM, zero-copy mappings, retained grants, or
  asynchronous filesystem requests;
- a block layer, persistent format, journal, fsck, crash recovery, or
  writeback;
- DS publication, dynamic discovery, restart, endpoint replacement, or RS
  recovery;
- source, binary, wire, directory-entry, or on-disk compatibility with MINIX;
  or
- dynamic allocation, worker threads, SMP synchronization, or more than one
  active request.

### Dependency position

This is development-DAG Step 12:

1. the real launcher, VM, PM, and TTY services are dependency-ready;
2. RAMFS establishes one VFS-facing filesystem protocol and volatile object
   owner;
3. Step 13 implements production VFS descriptors, root mount, pathname
   routing, synthetic console, and complete bounce-buffer I/O;
4. Step 14 extends the seed with application ELFs and uses VFS, PM, and VM to
   spawn init and the shell.

Implementation may begin only after this ADR is independently reviewed,
marked Accepted, and merged.

### Fixed service tuple

The production RAMFS identity remains:

```text
service ID         = 5
process slot       = 4
profile ID         = 5
profile name       = RAMFS
direct prerequisite = TTY
role               = ordinary static service
```

The production VFS identity remains:

```text
service ID         = 6
process slot       = 5
profile ID         = 6
profile name       = VFS
direct prerequisite = RAMFS
role               = ordinary static service
```

Neither entry has a device range, IRQ source, or kernel-operation bit.
Service ID, process slot, profile ID, endpoint, node handle, grant token, and
filesystem cursor remain distinct identities.

### Immutable privilege profiles

The production RAMFS profile becomes:

```text
operations =
    RECEIVE | CALL | REPLY | REPLY_RECEIVE
call targets =
    BOOTSTRAP_LAUNCHER
send targets = 0
notify targets = 0
kernel operations = 0
```

`CALL` is required only for bootstrap readiness. `REPLY` and
`REPLY_RECEIVE` serve exact synchronous VFS calls. RAMFS has no ordinary send,
notification, device, IRQ, VM, PM, TTY, application, or kernel-operation
authority.

The production VFS profile becomes:

```text
operations =
    RECEIVE | CALL
call targets =
    BOOTSTRAP_LAUNCHER | TTY | RAMFS
send targets = 0
notify targets = 0
kernel operations = 0
```

Step 12 does not yet add VFS reply authority because the exact test VFS has no
application callers. Step 13 may add only the reply operations and other exact
targets required by its accepted protocol.

No application profile gains RAMFS authority. Applications later call VFS.
VFS creates a separate direct grant to RAMFS over its resident bounce buffer;
an application grant is never forwarded.

### Service startup and mount phase

RAMFS has these local phases:

```text
UNINITIALIZED
READY_UNMOUNTED
MOUNTED
```

Startup is:

1. validate the exact bootstrap configuration, service identity, self
   endpoint, launcher endpoint, and exact VFS endpoint from the static service
   table;
2. validate the complete embedded seed image without mutating runtime
   filesystem state;
3. initialize the complete node table, block map, data arena, link counts, and
   generation state in one non-failing local commit;
4. validate every RAMFS invariant;
5. send the ordinary versioned bootstrap readiness call;
6. require the exact token-bound readiness acknowledgment;
7. enter `READY_UNMOUNTED`; and
8. receive the first VFS call.

The VFS endpoint may still be reserved while RAMFS initializes. Its immutable
identity comes from the bootstrap configuration, but request authority still
requires the exact later active endpoint generation.

Only a canonical `MOUNT` may transition `READY_UNMOUNTED` to `MOUNTED`.
Every other well-formed operation before mount returns `STATE`. A duplicate
mount in `MOUNTED` also returns `STATE`. No unmount transition exists in
version 1.

RAMFS readiness means that the complete seed and internal invariants are
usable. It does not mean that VFS has mounted the root. Production VFS Step 13
must mount RAMFS before VFS reports its own readiness.

### Fixed capacities

Version 1 has:

```text
MICROS_RAMFS_NODE_CAPACITY             = 64
MICROS_RAMFS_BLOCK_SIZE                = 4096
MICROS_RAMFS_BLOCK_CAPACITY            = 64
MICROS_RAMFS_FILE_SIZE_MAX             = 262144
MICROS_RAMFS_TRANSFER_MAX              = 4096
MICROS_RAMFS_PATH_MAX                  = 4096
MICROS_RAMFS_NAME_MAX                  = 60
MICROS_RAMFS_DIRECTORY_RECORD_SIZE     = 128
MICROS_RAMFS_DIRECTORY_CURSOR_END      = 66
MICROS_RAMFS_SEED_IMAGE_MAX            = 270400
MICROS_RAMFS_RESIDENT_PAGE_LIMIT       = 192
```

The 64 data blocks provide 256 KiB of aggregate regular-file data. Each file
has 64 logical block slots and may therefore use any subset of the complete
arena up to the same 256 KiB maximum. A missing logical block inside a file's
size is a sparse hole.

The node capacity includes the root. Version 1 has no runtime node-removal
operation, so successful runtime allocation consumes the lowest free node slot
monotonically. Every capacity failure is explicit; there is no eviction,
compression, hidden overwrite, or heap fallback.

The one-thread service uses one resident 4 KiB scratch page for:

- path or name input;
- staged write input;
- assembled read output; or
- assembled directory records.

No request retains the scratch page, a grant token, a VFS pointer, or a reply
token after its result has been accepted.

The maximum linked seed image remains wired after initialization. At the fixed
bounds, the seed image, mutable data arena, and scratch page consume:

```text
270400 + 262144 + 4096 = 536640 bytes
```

Those separately retained regions consume no more than 132 resident 4 KiB
pages even when independently page aligned. Node and block metadata, code,
read-only data, runtime state, and stack are additional. The complete RAMFS
process image, including every ELF `PT_LOAD` and stack leaf, must not exceed
192 resident pages. The generated manifest must also remain within
ADR-0045's existing aggregate 4,096-static-mapping capacity. Seed generation
enforces the 270,400-byte image maximum; post-link ELF validation enforces the
192-page service limit; and manifest validation enforces both the per-service
count and aggregate mapping capacity. No implementation may assume that the
seed pages can be reclaimed after copying.

The capacity is sufficient for the shell MVP's bounded root tree and initial
executables without preallocating per-file data. Increasing a public bound or
the 192-page service budget, or adding dynamic allocation, requires a later
reviewed need.

### Node handles

A RAMFS node handle is an unsigned 64-bit value:

```text
bits 0..15   node slot
bits 16..31  zero
bits 32..63  nonzero generation
```

Handle zero is invalid. The root is:

```text
slot       = 0
generation = 1
```

A handle resolves only when:

- reserved bits are zero;
- the slot is below `MICROS_RAMFS_NODE_CAPACITY`;
- the generation is nonzero; and
- the live slot has the same generation.

A free slot's first allocation uses generation 1. Any future node-removal
extension must advance the generation and make a slot permanently unavailable
instead of wrapping to zero or making a stale handle valid. Version 1 has no
operation that reclaims a reachable node, but stale, wrong-generation, and
noncanonical handles are rejected from the first implementation.

The node handle is not:

- a VFS descriptor;
- a VFS vnode address;
- a process endpoint;
- a file position;
- a directory cursor; or
- grant authority.

### Modes and node types

RAMFS mode is a canonical 32-bit value:

```text
MICROS_RAMFS_MODE_TYPE_MASK   = 0x0000f000
MICROS_RAMFS_MODE_DIRECTORY   = 0x00004000
MICROS_RAMFS_MODE_REGULAR     = 0x00008000
MICROS_RAMFS_MODE_PERMISSIONS = 0x000001ff
```

Only one type bit above and permission bits `0777` may be present. All other
bits are invalid.

Ownership is fixed to the v0.1 system identity and is not carried on the wire.
The root-equivalent single-identity system may traverse every mode. Mode is
retained so VFS can expose and later enforce the reviewed application policy.
Adding UID, GID, supplementary groups, ACLs, or non-root permission behavior
requires a new protocol version and credential authority design.

### Runtime node state

Every live node contains:

```text
slot and generation
mode
parent node handle
canonical name bytes and length
namespace link count
VFS reference count
regular-file size
64 logical-to-physical block slots
```

Directories own no data blocks. Regular-file blocks are selected from one
global 64-block arena. A block slot is either `NONE` or names one exact
physical arena block.

The root has:

```text
parent = root
name length = 0
mode type = DIRECTORY
link count = 2 + number of live child directories
```

Every non-root live node has exactly one live directory parent and one
nonempty canonical name unique among that parent's children.

A regular file has:

```text
link count = 1
```

A non-root directory has:

```text
link count = 2 + number of live child directories
```

The two base directory links represent `.` and the parent's `..` entry.
Those entries are synthesized for lookup and enumeration rather than stored as
separate node-table records.

Every allocated physical block belongs to exactly one regular-file logical
block. Newly allocated blocks are zeroed before publication. No block is
shared, aliased, or exposed by address.

### References and open-related behavior

RAMFS owns one 32-bit VFS reference count per live node.

References are acquired by:

- successful `MOUNT`: one pinned root reference;
- successful `LOOKUP`: one reference to the final node; and
- successful `CREATE`: one reference to the new regular file.

`MKDIR` and `GETDENTS` do not acquire a reference. A node handle appearing in
a directory record is informational identity only; VFS must use `LOOKUP`
before operating on that child.

Every operation naming a node requires a live handle and an existing VFS
reference to that node. `LOOKUP` additionally requires referenced start and
root nodes. `CREATE` and `MKDIR` require a referenced parent directory.

VFS owns descriptors, open-file descriptions, and vnode aggregation.
Opening an existing path is represented by the reference returned from
`LOOKUP`; opening a newly created file uses the reference returned from
`CREATE`. There is no separate RAMFS `OPEN` message.

VFS releases one or several filesystem references through `PUTNODE`. There is
no separate RAMFS `CLOSE` message. The pinned mount reference cannot be
released below one while version 1 has no unmount operation.

An object is reclaimable only when both link count and reference count are
zero. Version 1 has no operation that removes a namespace link, so no
reachable runtime node is reclaimed.

### Path semantics

`LOOKUP` receives:

- one referenced start node;
- one referenced confinement root;
- one read-direction VFS grant;
- a grant-relative offset; and
- a byte length including the final NUL.

The length is in `[1, MICROS_RAMFS_PATH_MAX]`. The copied path:

- has the final NUL exactly at `length - 1`;
- has no earlier NUL;
- is compared byte for byte;
- may contain repeated `/`;
- may contain `.` and `..`; and
- may be empty before the final NUL.

An absolute path begins at the supplied root. A relative path begins at the
supplied start node. Repeated separators have no additional effect. `.` keeps
the current node. `..` at the supplied root keeps that root; otherwise it
selects the recorded parent.

An empty relative path is interpreted as `.`. Before consuming every
component, including `.` and `..`, the current node must be a directory.
Therefore an empty path, `.`, `..`, or `../name` beginning at a regular file
returns `NOT_DIRECTORY`. An absolute path performs that traversal check on the
supplied root rather than on the start node.

Every component except `.` and `..` has a byte length in
`[1, MICROS_RAMFS_NAME_MAX]` and contains neither NUL nor `/`.

Every intermediate component and every component followed by a trailing `/`
must resolve to a directory. The supplied root must be an ancestor of the
start node. Any invalid parent chain or cycle is internal corruption.

Successful lookup increments only the final node reference after complete
path and reference-count preflight. Failure changes no reference.

Version 1 has no symlink or mount-point result. VFS owns selection of the one
root mount; RAMFS owns traversal within that tree.

### Build seed declaration

The repository owns one versioned JSON seed declaration:

```text
servers/ramfs/seed.json
```

Its version-1 shape is:

```json
{
  "version": 1,
  "entries": [
    {
      "path": "/",
      "type": "directory",
      "mode": "0755"
    },
    {
      "path": "/etc",
      "type": "directory",
      "mode": "0755"
    },
    {
      "path": "/etc/motd",
      "type": "file",
      "mode": "0644",
      "source": "servers/ramfs/seed/etc/motd"
    }
  ]
}
```

The exact initial `/etc/motd` bytes are:

```text
micros ramfs
```

followed by one line-feed byte.

The declaration:

- contains exactly one root entry;
- uses canonical absolute paths with no empty, `.`, or `..` component;
- uses only `directory` or `file`;
- encodes mode as exactly four octal characters;
- combines each declared type with those permission bits to produce the
  canonical runtime mode;
- requires one repository-relative regular source file for each file;
- forbids a source field on directories;
- rejects unknown fields, duplicate paths, missing parents, symlink sources,
  external paths, and every capacity violation; and
- imports no host UID, GID, timestamp, inode number, ordering, or permission.

The generator orders records root-first, parent-before-child, then by
bytewise path. File payload bytes follow that same record order. Identical
repository input produces identical output independent of host directory
enumeration or metadata.

Step 14 may extend this declaration with bounded application ELFs and initial
files. It does not change the binary seed format or grant protocol.

### Binary seed image

The generated seed is a pointer-free little-endian blob linked into the
RAMFS read-only segment.

Its 64-byte header is equivalent to:

```c
struct micros_ramfs_seed_header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint16_t entry_size;
    uint16_t entry_capacity;
    uint16_t entry_count;
    uint16_t reserved0;
    uint32_t data_size;
    uint32_t image_size;
    uint64_t digest;
    uint64_t reserved[4];
};
```

The exact constants are:

```text
magic          = 0x31534652  ("RFS1" in little-endian bytes)
version        = 1
header_size    = 64
entry_size     = 128
entry_capacity = 64
entry_count    = [1, 64]
data_size      = [0, 262144]
image_size     = 8256 + data_size
```

Every reserved field is zero.

The header is followed by exactly 64 fixed 128-byte entry records and then
`data_size` payload bytes. Each entry is equivalent to:

```c
struct micros_ramfs_seed_entry {
    uint16_t parent_index;
    uint16_t name_length;
    uint32_t mode;
    uint32_t data_offset;
    uint32_t data_size;
    uint8_t name[64];
    uint64_t reserved[6];
};
```

Active records occupy indices zero through `entry_count - 1`. Every later
record byte is zero.

The root record is index zero and requires:

```text
parent_index = 0
name_length  = 0
mode type    = DIRECTORY
data_offset  = 0
data_size    = 0
name         = all zero
```

Every non-root record requires:

- `parent_index < current index`;
- a directory parent;
- `name_length` in `[1, 60]`;
- no NUL or `/` in the declared name bytes;
- zero bytes after the declared name;
- no sibling with the same byte-exact name;
- one canonical mode; and
- zero reserved bytes.

Directory records have zero data offset and size. A regular-file data offset
is relative to the first payload byte at image offset 8256. Regular-file
payload ranges:

- are ordered by record index;
- contain no gap or overlap;
- exactly cover the `data_size` payload;
- fit `MICROS_RAMFS_FILE_SIZE_MAX` individually; and
- require no more than 64 aggregate rounded-up data blocks.

The digest is 64-bit FNV-1a over the exact `image_size` bytes with the digest
field treated as zero:

```text
offset basis = 14695981039346656037
prime        = 1099511628211
```

The digest detects build or embedding corruption; it is not a cryptographic
authenticity claim.

RAMFS completely validates the blob and computes the block-allocation plan
before mutating runtime state. The commit then copies payload bytes into
zeroed BSS blocks, installs node metadata and block maps, derives exact link
counts, and leaves every VFS reference count zero. No fallible operation
follows the first runtime-state store.

The seed blob is an internal build format, not a disk ABI or application
interface.

### Protocol identity

Version 1 uses:

```text
MICROS_RAMFS_PROTOCOL_VERSION = 1

MICROS_RAMFS_MESSAGE_MOUNT    = 0x00030001
MICROS_RAMFS_MESSAGE_LOOKUP   = 0x00030002
MICROS_RAMFS_MESSAGE_CREATE   = 0x00030003
MICROS_RAMFS_MESSAGE_MKDIR    = 0x00030004
MICROS_RAMFS_MESSAGE_READ     = 0x00030005
MICROS_RAMFS_MESSAGE_WRITE    = 0x00030006
MICROS_RAMFS_MESSAGE_GETDENTS = 0x00030007
MICROS_RAMFS_MESSAGE_PUTNODE  = 0x00030008
MICROS_RAMFS_MESSAGE_RESULT   = 0x00030009
```

Every request is a `call` from exact VFS and carries a nonzero kernel-issued
reply token. Every multibyte field is little-endian and decoded bytewise.
Every operation-specific layout occupies the existing 48-byte
`micros_ipc_message` payload. Payload bytes are never cast to a host C
structure.

Flags and reserved bytes must be zero. Unknown versions, shorter conceptual
layouts, nonzero extension fields, and legacy fallbacks are rejected.

### Stable results

Version 1 results are:

```text
  0  OK
 -1  BAD_TYPE
 -2  BAD_VERSION
 -3  MALFORMED
 -4  CALLER
 -5  STATE
 -6  NODE
 -7  NOT_FOUND
 -8  EXISTS
 -9  NOT_DIRECTORY
-10  IS_DIRECTORY
-11  NO_SPACE
-12  GRANT
-13  RANGE
-14  REFERENCE
```

`NODE` means a zero, noncanonical, out-of-range, free, or stale-generation
node handle. `MALFORMED` means nonzero flags or reserved bytes, a noncanonical
mode, a missing or embedded NUL, a forbidden `/` in a create name, or reserved
create names `.` and `..`. `NO_SPACE` means a node or data-block capacity is
exhausted or a link/reference increment would overflow. `RANGE` means a path,
name, component, transfer, file-offset, grant-offset, buffer, or cursor scalar
is outside its operation range, arithmetic overflows, or a live lookup start
lies outside its supplied confinement root. `REFERENCE` means a live node
lacks the required VFS reference, or a `PUTNODE` count would release more
references than owned or drop the root mount pin.

The first failing condition in the common validation order selects the result.
Namespace-dependent `NOT_FOUND`, `EXISTS`, and `NOT_DIRECTORY` results occur
only after the required path or name copy. Grant failure therefore cannot
mask an earlier type, version, caller, phase, node, reference, scalar,
confinement, or capacity failure.

Internal corruption does not become an ordinary result.

### Result payload

Every reply has type `MICROS_RAMFS_MESSAGE_RESULT` and this 48-byte payload:

```text
bytes 0..3    protocol version = 1
bytes 4..7    original request message type
bytes 8..11   signed RAMFS result
bytes 12..15  flags = 0
bytes 16..23  node handle, or zero
bytes 24..31  file size, or zero
bytes 32..39  next file position or directory cursor, or zero
bytes 40..43  transferred byte count, or zero
bytes 44..47  mode, or zero
```

On non-`OK`, bytes 12 through 47 are zero.

On `MOUNT`, `LOOKUP`, and `CREATE`, the node, size, and mode fields describe
the referenced result.

On `READ` and `WRITE`, file size is the authoritative current size, position
is the next offset, and count is the transferred source-byte count.

On `GETDENTS`, position is the next cursor and count is the number of complete
record bytes copied.

Every other successful result has zero operation fields.

### MOUNT request

The payload is:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..47   zero
```

Preflight requires:

- exact VFS source and nonzero reply token;
- phase `READY_UNMOUNTED`;
- completely validated seed/runtime agreement;
- root slot zero with generation one;
- root directory mode, parent, name, link count, and block-free shape;
- every reference count zero; and
- root reference increment capacity, otherwise `NO_SPACE`.

One non-failing commit:

1. increments the root reference count to one; and
2. enters `MOUNTED`.

The result returns the root handle, mode, and size zero. The root reference is
the pinned mount reference and may not be released by `PUTNODE`.

### LOOKUP request

The payload is:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..15   referenced start node
bytes 16..23  referenced confinement root
bytes 24..27  VFS read-grant token
bytes 28..31  path length including final NUL
bytes 32..39  grant-relative path offset
bytes 40..47  zero
```

Validation and transition:

1. require `MOUNTED`;
2. resolve exact live referenced start and root nodes;
3. require the root to be a directory and return `RANGE` unless it is an
   ancestor of start;
4. validate length and grant-offset arithmetic;
5. copy the complete path into the resident scratch page;
6. validate the final NUL, absence of embedded NUL, separators, and component
   lengths;
7. traverse with the path rules above;
8. validate the final node and return `NO_SPACE` if its reference count cannot
   be incremented;
9. validate complete RAMFS invariants; and
10. increment only the final reference count.

The result returns the final handle, regular-file size or directory size zero,
and mode.

`NOT_FOUND` selects a missing component. `NOT_DIRECTORY` selects a
nondirectory current node before any component, intermediate,
trailing-separator target, or root where a directory is required. Grant
failure changes no reference.

### CREATE and MKDIR requests

Both use:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..15   referenced parent directory
bytes 16..19  requested canonical mode
bytes 20..23  VFS read-grant token
bytes 24..31  grant-relative name offset
bytes 32..35  name length including final NUL
bytes 36..47  zero
```

The name length is in `[2, MICROS_RAMFS_NAME_MAX + 1]`. The copied component:

- has its sole NUL at the final byte;
- contains no `/`;
- is neither `.` nor `..`; and
- has a byte length in `[1, 60]`.

`CREATE` requires regular-file mode. `MKDIR` requires directory mode.

Common preflight requires:

- `MOUNTED`;
- one exact referenced live parent directory;
- a canonical name grant and mode;
- no existing sibling with that name;
- one usable free node slot and next nonzero generation;
- every required parent link increment can succeed;
- no reference, block, or ownership inconsistency; and
- complete invariant validation.

`CREATE` commits one regular node with:

```text
parent          = exact parent
link count      = 1
reference count = 1
size            = 0
all blocks      = NONE
```

Its result returns the new referenced node, size zero, and mode.

`MKDIR` commits one directory node with:

```text
parent          = exact parent
link count      = 2
reference count = 0
size            = 0
all blocks      = NONE
```

and increments the parent's link count by one. Its result fields are zero.

No fallible operation follows the first node or parent mutation. `EXISTS`,
`NO_SPACE`, malformed name, invalid mode, and grant failure preserve the
complete table and output state.

### READ and WRITE requests

Both use:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..15   referenced regular-file node
bytes 16..23  file offset
bytes 24..27  VFS grant token
bytes 28..31  requested byte count
bytes 32..39  grant-relative data offset
bytes 40..47  zero
```

The requested count is in `[1, MICROS_RAMFS_TRANSFER_MAX]`. VFS handles a
zero-length application operation without a backend call. Offset, count, and
grant arithmetic are overflow checked.

The node must be live, referenced, and regular. A directory returns
`IS_DIRECTORY`. The file offset is at most
`MICROS_RAMFS_FILE_SIZE_MAX`.

#### READ

RAMFS computes:

```text
transferred = min(requested count, max(file size - offset, 0))
```

At or beyond EOF, `READ` returns `OK`, count zero, unchanged position, and the
current file size without touching the supplied grant.

For a nonzero result, RAMFS:

1. assembles the entire result in the resident scratch page;
2. copies allocated block bytes and writes zeroes for every sparse range;
3. performs one checked copy to the VFS write-direction grant; and
4. returns the exact next position and count.

Read changes no filesystem metadata. A grant failure leaves the VFS
destination unchanged because the kernel checked copy is all-or-error.

#### WRITE

`offset + count` must not exceed `MICROS_RAMFS_FILE_SIZE_MAX`.

RAMFS:

1. computes the exact existing and missing logical blocks touched by the
   request;
2. rejects insufficient free physical blocks with `NO_SPACE`;
3. performs one checked copy from the VFS read-direction grant into the
   resident scratch page;
4. revalidates the complete block, node, reference, and capacity plan; and
5. commits new zeroed physical blocks, copied bytes, sparse mappings, and
   `size = max(old size, offset + count)` in one non-failing transition.

The complete requested source count is written or no file state changes.
Unallocated logical blocks between the old size and write offset remain sparse
and read as zero. Any unwritten bytes in a newly allocated physical block are
zero.

The result returns the updated file size, `offset + count`, and the full count.
Partial write success is not a version-1 outcome.

### GETDENTS request

The payload is:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..15   referenced directory node
bytes 16..23  directory cursor
bytes 24..27  VFS write-grant token
bytes 28..31  output buffer size
bytes 32..39  grant-relative output offset
bytes 40..47  zero
```

The buffer size is:

- at least `MICROS_RAMFS_DIRECTORY_RECORD_SIZE`;
- at most `MICROS_RAMFS_TRANSFER_MAX`; and
- an exact multiple of `MICROS_RAMFS_DIRECTORY_RECORD_SIZE`.

The cursor is in `[0, MICROS_RAMFS_DIRECTORY_CURSOR_END]`.

The deterministic sequence is:

```text
cursor 0  -> "."
cursor 1  -> ".."
cursor 2 + slot -> a live direct child in ascending node-slot order
cursor 66 -> EOF
```

Slots that are free or are not direct children are skipped. Seed nodes use
canonical record order and runtime allocations use increasing slots, so the
version-1 order is deterministic. A child created after a cursor has passed
its slot need not appear in that enumeration; no snapshot guarantee exists.

Each 128-byte record is:

```text
bytes 0..7     node handle
bytes 8..11    mode
bytes 12..15   name length
bytes 16..75   60-byte name field
bytes 76..127  zero
```

Only the first `name length` bytes are nonzero name data. The remaining name
field and reserved tail are zero. `.` and `..` use the current and parent
handles and modes.

RAMFS assembles only complete records in the scratch page. If the next record
does not fit, the returned cursor remains at that first unreported record. It
then performs one checked copy to the VFS grant.

At cursor 66, `GETDENTS` returns `OK`, zero bytes, and cursor 66 without
touching the grant. Enumeration does not increment child references.

### PUTNODE request

The payload is:

```text
bytes 0..3    protocol version = 1
bytes 4..7    flags = 0
bytes 8..15   live node handle
bytes 16..19  reference release count
bytes 20..47  zero
```

The count is nonzero.

Preflight requires:

- `MOUNTED`;
- one exact live node;
- `count <= reference count`;
- for root, `reference count - count >= 1`;
- complete link/reference and node-table invariants; and
- no count underflow.

The commit subtracts the count. Version 1 never frees the node because every
live node retains at least one namespace link. A stale node returns `NODE`;
an excessive release or attempt to drop the root mount pin returns
`REFERENCE`.

### Common validation and failure ordering

Every delivered user request follows:

1. known request message type;
2. protocol version, flags, fixed reserved bytes, and scalar widths;
3. exact kernel-written VFS source endpoint and nonzero reply token;
4. service mount phase;
5. operation-specific live node, reference, mode, offset, count, cursor, and
   arithmetic validation that does not require copied bytes;
6. operation-specific capacity preflight that can be derived without copying;
7. one complete checked grant copy where required;
8. copied path/name canonical validation and namespace lookup where required;
9. complete RAMFS invariant validation; and
10. one non-failing local commit.

Specific namespace results such as `NOT_FOUND`, `EXISTS`, or
`NOT_DIRECTORY` follow the required path/name copy because those bytes are
needed to classify the request.

Unknown type, bad version, malformed payload, foreign caller, wrong phase,
stale node, namespace miss, capacity, range, grant, or reference failures
preserve:

- every node and generation;
- every name, parent, mode, size, link count, and reference count;
- every logical and physical block mapping;
- every data byte;
- mount phase;
- the resident scratch page's authority state; and
- every result field except the explicit canonical error reply.

A token-zero user request under the accepted profile graph is an invariant
failure because RAMFS has no reply authority for it. A nonzero-token foreign
caller in a dependency-closed test profile receives `CALLER`.

After a successful local mutation, RAMFS issues the token-bound reply or
`reply_receive`. An unexpected reply failure is fatal. RAMFS does not roll
back a committed filesystem transition after losing its sole static VFS peer,
because endpoint loss is a fatal core-service failure before RS recovery
exists.

### Service loop and failure handling

RAMFS is single-threaded.

After readiness acknowledgment it:

1. receives one request;
2. parses and executes the complete failure-atomic operation;
3. replies and atomically waits for the next request where possible; and
4. retains no caller-owned authority between requests.

RAMFS accepts:

- the launcher readiness acknowledgment during startup; and
- exact VFS calls after startup.

It receives no ordinary notification and sends no notification.

Malformed VFS calls receive stable results when their token is valid.
Unexpected kernel envelopes, token-zero user requests, impossible reply
failures, bootstrap identity mismatch, seed/runtime disagreement, duplicate
block ownership, parent cycles, link/reference corruption, or any complete
state-validation failure deliberately trap and use the existing exact
service-fault diagnostics.

There is no silent truncation, inferred node, implicit mount, automatic
capacity growth, grant retention, retry loop, fallback seed, alternate VFS
peer, or success-shaped error.

### Native evidence

Native tests must cover:

- exact protocol constants, node-handle packing, message payload offsets, seed
  header/entry sizes, directory-record size, seed-image maximum, and the
  192-page complete-service budget arithmetic;
- every valid and invalid seed header, record, name, parent, mode, data extent,
  digest, inactive byte, block demand, and capacity;
- deterministic JSON declaration ordering and rejection of host metadata,
  symlinks, external sources, duplicates, and unknown fields;
- post-link and manifest rejection above the exact 192-page RAMFS limit or
  aggregate 4,096-mapping limit;
- complete no-mutation behavior for rejected seed images;
- root initialization, exact link counts, unique names, parent acyclicity,
  physical-block uniqueness, sparse maps, and reference invariants;
- mount-before-operation gating and duplicate mount;
- zero, malformed, out-of-range, free, and stale node handles;
- absolute, relative, empty, repeated-separator, `.`, `..`, root-confinement,
  trailing-separator, overlong-component, missing, and nondirectory lookup,
  including empty, `.`, `..`, and `../name` from a regular-file start;
- create and mkdir success, duplicate names, type/mode errors, capacity
  exhaustion, and exact parent link counts;
- short reads, EOF, cross-block reads, sparse zeroes, and grant failure without
  output or state mutation;
- overwrite, append, cross-block, sparse extension, file maximum, block
  exhaustion, grant failure, and all-or-error writes;
- one-record, multi-record, exact-boundary, first-nonfit, skipped-slot,
  post-create, and EOF directory enumeration;
- counted reference release, overrelease, root-pin preservation,
  `NO_SPACE` reference-increment exhaustion, and link-count exhaustion;
- every message type, stable result, malformed field, reserved byte, foreign
  caller, token shape, grant direction, grant offset, and transfer length;
- byte-exact state and output preservation on every returning failure; and
- a deterministic replayable mixed transition model.

The persistent model runs at least 8,192 operations. It mixes:

- mount;
- absolute and relative lookup;
- create and mkdir;
- read and write, including sparse offsets;
- getdents cursor continuation;
- putnode batching;
- stale handles;
- capacity exhaustion;
- malformed messages; and
- injected grant-copy failures.

It invokes the production parser and state-transition functions and compares
complete production state with an independent compact reference after every
operation. Failure output contains the seed and complete recent operation
trace; success contains the seed and trace hash.

The implementation adds one slow workflow:

```text
test-ramfs-model
```

It is excluded from `test-unit-fast` and included transitively in
`test-unit`.

### QEMU evidence

The implementation adds:

```text
test-qemu-ramfs
```

The dependency-closed manifest contains:

- the real launcher;
- the real VM service;
- the real PM service;
- the real TTY service;
- the real RAMFS service; and
- one exact test VFS ELF using the production VFS profile.

The test VFS is not a production descriptor server. It exists only to exercise
the reviewed RAMFS client boundary.

The scenario proves:

1. VM handoff, PM readiness, and TTY ownership complete before RAMFS release;
2. RAMFS validates the canonical seed and reports readiness before VFS
   release;
3. exact VFS performs the sole mount and receives the referenced root;
4. VFS looks up `/etc/motd`, reads the exact `micros ramfs\n` bytes through a
   write-direction grant, observes short EOF, and releases the result;
5. VFS creates `/tmp`, looks it up, creates `note`, and writes known bytes at a
   sparse offset through a read-direction grant;
6. a read across that offset returns zero-filled hole bytes followed by the
   exact written data;
7. one-record `GETDENTS` calls enumerate `.`, `..`, `etc`, and `tmp` through
   cursor continuation without a partial record or acquired child reference;
8. a malformed version, wrong-direction grant, stale node, and excessive
   `PUTNODE` count return exact stable results with no filesystem mutation;
9. VFS releases every acquired non-root reference, every temporary grant is
   revoked, the root retains exactly its mount pin, and complete RAMFS
   invariants validate;
10. VFS sends its ordinary bootstrap readiness only after the filesystem
    scenario is complete, and launcher authority seals;
11. the exact test VFS writes this marker through the real TTY path:

```text
MICROS_RAMFS_TEST_PASS seed=validated mount=single lookup=bounded files=writable directories=cursor grants=checked refs=balanced
```

12. the marker physically drains, the kernel observes no live grants, and the
    isolated test shuts down cleanly through the existing test-only hook.

The scenario uses no sleep and no host input. Guest-owned bootstrap deadlines
and the host's absolute timeout remain authoritative.

Production RAMFS never emits a test marker or invokes the test shutdown hook.

### Validation ownership

The implementation must extend the fail-closed validation planner.

Changes to RAMFS protocol headers, seed parsing, node/path/file state, or the
replayable model select:

- complete native tests;
- `test-ramfs-model`;
- `test-qemu-ramfs`;
- documentation and all three diff checks.

Changes to RAMFS service startup, linker inputs, generated seed source, image
catalog entries, or user-service runtime integration additionally select:

- the repository-owned user-ELF checks;
- `test-qemu-user-runtime`;
- `test-qemu-bootstrap-launcher`;
- `test-qemu-vm-handoff`;
- `test-qemu-pm-service`;
- `test-qemu-tty`;
- every shared linker, image-generator, and post-link gate selected by the
  planner;
- exact RAMFS and aggregate manifest resident-page budget checks; and
- documentation and all three diff checks.

Changes to RAMFS/VFS profiles or production manifest validation select:

- complete native bootstrap/profile tests;
- every QEMU workflow whose dependency-closed profile table contains RAMFS or
  VFS;
- `test-qemu-ramfs`; and
- documentation and all three diff checks.

Changes to direct-grant participants, checked-copy use, or grant lifecycle
retain the complete evidence owned by ADR-0038 and ADR-0039.

The implementation pull request records exact commands only after the new
workflow and ownership mapping exist. This design does not claim that
`test-ramfs-model` or `test-qemu-ramfs` exists before implementation.

### Implementation task and commit plan

The dependent implementation todo remains pending until this design is
reviewed, marked Accepted, and merged. Implementation starts from a fresh
branch based on then-current `main`.

The later implementation uses five green commits.

#### 1. Add the protocol and seed parser

- Red: native tests fail because the RAMFS ABI, seed representation, JSON
  generator, validation, and deterministic blob output do not exist.
- Green: add the public constants/layouts, repository-owned generator,
  canonical production seed, seed validator, malformed corpus, and exact ABI
  assertions.
- No target RAMFS service is linked yet.

#### 2. Add the portable RAMFS core and model

- Red: node, path, namespace, reference, sparse-file, and directory-cursor
  tests fail.
- Green: add fixed runtime state, generation-safe handles, mount, lookup,
  create, mkdir, read, write, getdents, putnode, deterministic regressions, and
  the replayable 8,192-transition model.
- No QEMU image is added in this commit.

#### 3. Add profiles and the real RAMFS service

- Red: the production profile table and static service image cannot initialize
  the seed, perform readiness, or serve exact VFS calls.
- Green: add exact profile replacements, the freestanding RAMFS ELF, service
  loop, generated seed embedding, image checks, and target syntax/build
  evidence.
- Existing launcher, VM, PM, and TTY gates remain green.

#### 4. Add QEMU RAMFS integration

- Red: no six-service image can mount RAMFS or prove real grant-backed
  filesystem operations.
- Green: add the exact test VFS ELF, generated dependency-closed fixture,
  production mount/protocol exercise, TTY-routed pass marker, and
  `test-qemu-ramfs`.

#### 5. Add fail-closed ownership and final documentation

- Red: RAMFS paths or workflows are unknown or underselected by the validation
  planner.
- Green: add complete path ownership, inventory/parity regressions, documented
  commands, and directly related architecture/testing updates.

Each commit remains buildable and green for every gate that exists at that
commit. No permanent commit claims application I/O, production VFS,
executable loading, or shell behavior.

## Invariants

- Exact VFS is the sole RAMFS client.
- Every delivered RAMFS request is a call with one nonzero reply token.
- Exactly one mount transition occurs.
- Before mount, every VFS reference count is zero.
- After mount, root retains at least one pinned reference.
- A node handle resolves only when slot, reserved bits, and generation are
  canonical and match one live node.
- Root is slot zero, generation one, a directory, its own parent, and has an
  empty stored name.
- Every non-root live node has one exact live directory parent and one unique
  canonical sibling name.
- Parent relationships are acyclic and terminate at root.
- A regular file has link count one.
- A directory has link count two plus its live child-directory count.
- Link and reference counts never underflow or wrap.
- Each physical data block belongs to at most one regular-file logical block.
- Every block published to a file was zeroed before first use.
- File size never exceeds 262144 bytes.
- A sparse logical block reads entirely as zero.
- No directory owns a data block.
- A successful lookup or create adds exactly one reference.
- Getdents records do not add references.
- Putnode releases only references owned by the exact live node and cannot
  drop the root mount pin.
- No operation retains a grant token or VFS pointer after its reply.
- Every checked grant direction matches the data flow.
- Every returning failure preserves complete RAMFS state and non-result output
  bytes.
- A successful write publishes all requested source bytes and metadata or
  publishes none.
- A successful getdents result contains only complete 128-byte records and
  returns the first unreported cursor.
- The seed image is completely validated before the first runtime-state
  mutation.
- RAMFS readiness implies a valid writable seed state, not a completed mount.
- RAMFS endpoint loss, VFS endpoint loss, reply failure, or invariant failure
  is fatal before RS recovery exists.

## Consequences

- Step 12 establishes a real isolated filesystem owner without importing a
  block layer or production VFS prematurely.
- The whole-path lookup boundary preserves the proven MINIX VFS/filesystem
  split while one root mount keeps redirection state small.
- Exact fixed capacities make exhaustion deterministic and host-testable.
- Generation-safe handles and failure-atomic copies strengthen stale-reference
  and partial-failure behavior without changing successful filesystem
  semantics.
- Sparse files avoid allocating blocks merely to represent zero-filled holes.
- Fixed 128-byte backend directory records simplify bounded grant copies;
  production VFS later owns any application-facing representation.
- The seed blob is reproducible and writable after initialization but is not a
  persistent format.
- Open/close semantics are exercised through lookup/create references and
  counted putnode release before descriptors exist.
- The implementation retains both a seed image of up to 270,400 bytes and a
  256 KiB writable data arena. Together with the scratch page they require at
  most 132 wired pages, while the complete RAMFS service is capped at 192
  wired pages. This bounded duplication is an accepted shell-MVP cost.
- Credentials, removal, truncate, persistence, and recovery remain explicit
  extension points rather than accidental partial features.

## Alternatives considered

### Put RAMFS inside VFS

Rejected by ADR-0008. It would avoid the exact IPC and grant boundary that a
later filesystem server must use.

### Port MFS with a memory block device

Rejected. It adds an on-disk format, block protocol, cache, bitmap, and driver
discovery before the basic filesystem-service contract is proven.

### Send only one path component per lookup

Rejected for version 1. MINIX delegates traversal within one mounted
filesystem to the filesystem server. A bounded whole-path grant preserves
that authority split, reduces VFS/RAMFS round trips, and still leaves VFS in
control of mount routing.

### Use raw node-table indices

Rejected. A stale VFS identity could select an unrelated object after later
slot reuse. The generation field costs no dynamic state and preserves the
project's existing stale-handle discipline.

### Allocate one fixed data array per file

Rejected. It either wastes most RAM on small files or introduces a small
per-file limit. One shared block arena supports sparse files and lets the
actual seed and runtime workload consume the fixed capacity.

### Use variable-length native `dirent` records

Rejected. It would expose a host/ABI-dependent representation and complicate
one-page validation. VFS can translate the fixed internal record later.

### Retain VFS grants across requests

Rejected. Synchronous one-page operations need no retained authority.
Immediate copy and revoke keep teardown and failure state small.

### Add unlink, rename, and truncate now

Rejected. They are not required by ADR-0008's first shell-MVP operation set
and would introduce node reclamation, generation reuse, directory mutation
during enumeration, and additional rollback rules before any current command
needs them.

### Grow capacities dynamically

Rejected. The runtime has no heap, and silent or ad hoc expansion would make
resource ownership and test exhaustion nondeterministic.

### Treat a failed reply as a recoverable transaction rollback

Rejected. The local operation may already have committed, and version 1 has no
idempotency token or replacement VFS. Losing the sole static VFS is fatal
until RS defines recovery.

## Specification basis

- [MINIX VFS/MFS filesystem protocol study](../research/minix-vfs-mfs-filesystem-protocol.md)
- [System overview](../architecture/system-overview.md)
- [Development dependency DAG](../architecture/development-dag.md)
- [Roadmap](../roadmap.md)
- [Testing strategy](../testing-strategy.md)
- [ADR-0004: IPC and Endpoint ABI](0004-ipc-and-endpoint-abi.md)
- [ADR-0005: Direct Memory Grants](0005-direct-memory-grants.md)
- [ADR-0007: Static Launcher Before Recovery Services](0007-static-launcher-before-rs.md)
- [ADR-0008: RAMFS-First Filesystem](0008-ramfs-first-filesystem.md)
- [ADR-0009: Spawn Before Fork](0009-spawn-before-fork.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0029: Endpoint and Privilege Substrate](0029-endpoint-and-privilege-substrate.md)
- [ADR-0030: MINIX-Baseline Blocking IPC](0030-minix-baseline-blocking-ipc.md)
- [ADR-0031: Tiered Native Validation](0031-tiered-native-validation.md)
- [ADR-0032: Fail-Closed Change-Aware Validation](0032-fail-closed-change-aware-validation.md)
- [ADR-0033: Reviewed Design Before Implementation](0033-reviewed-design-before-implementation.md)
- [ADR-0034: Author Validation Before Review](0034-author-validation-before-review.md)
- [ADR-0038: Kernel-Managed Direct Grant Registry](0038-kernel-managed-direct-grant-registry.md)
- [ADR-0039: Page-Bounded Checked Grant Copy](0039-page-bounded-checked-grant-copy.md)
- [ADR-0042: Freestanding User-Service Runtime](0042-freestanding-user-service-runtime.md)
- [ADR-0043: Static Bootstrap Launcher and Embedded Manifest](0043-static-bootstrap-launcher.md)
- [ADR-0045: Static VM Bootstrap and One-Way Handoff](0045-static-vm-bootstrap-and-handoff.md)
- [ADR-0046: PM Process Lifecycle and Spawn Metadata](0046-pm-process-lifecycle-and-spawn-metadata.md)
- [ADR-0047: TTY Console Handoff and Serial Protocol](0047-tty-console-handoff-and-serial-protocol.md)

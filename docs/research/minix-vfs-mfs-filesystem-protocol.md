# MINIX VFS/MFS Filesystem Protocol Baseline

## Purpose

This study fixes the MINIX behavioral baseline for development-DAG Step 12:
one user-space filesystem server, its VFS-facing mount and node protocol,
pathname traversal, regular-file and directory mutation, grant-backed data
transfer, directory enumeration, and referenced-node lifetime.

It is the sole ADR-0025 classification ledger for the initial `micros` RAMFS
outcome. The Step 12 implementation remains deliberately smaller than MINIX
MFS: one volatile root filesystem, one exact VFS peer, one build-generated
seed image, no block driver, and no persistent or recovery state.

This study records behavior, authority, ordering, and failure evidence. It does
not authorize copying MINIX source, structures, identifiers, on-disk formats,
or wire layouts.

## Reference

- Repository: local MINIX 3 source tree
- Commit: `4db99f4012570a577414fe2a43697b2f239b699e`
- Commit date: 2018-11-14
- Relevant areas:
  - `minix/include/minix/fsdriver.h`
  - `minix/include/minix/vfsif.h`
  - `minix/lib/libfsdriver/fsdriver.c`
  - `minix/lib/libfsdriver/call.c`
  - `minix/lib/libfsdriver/lookup.c`
  - `minix/lib/libfsdriver/dentry.c`
  - `minix/lib/libfsdriver/utility.c`
  - `minix/fs/mfs/mount.c`
  - `minix/fs/mfs/path.c`
  - `minix/fs/mfs/open.c`
  - `minix/fs/mfs/read.c`
  - `minix/fs/mfs/inode.c`
  - `minix/servers/vfs/comm.c`
  - `minix/servers/vfs/request.c`
  - `minix/servers/vfs/mount.c`
  - `minix/servers/vfs/vnode.c`

The local checkout was verified at the exact fixed reference commit before
this study was written.

## Fixed baseline

### VFS is the sole filesystem client

MINIX filesystem servers accept filesystem requests only from VFS.
`libfsdriver` sends non-VFS messages to an optional service-specific handler
and does not reply to them. A valid VFS request is dispatched through the
filesystem call table and receives one synchronous reply
(`minix/lib/libfsdriver/fsdriver.c:18-66`).

VFS's `fs_sendrec()` records the request/reply buffer, sends or queues the
request against the mounted filesystem endpoint, yields the worker, and
returns only after the filesystem reply arrives
(`minix/servers/vfs/comm.c:134-159`).

The reusable authority boundary is:

- applications call VFS, not a filesystem server;
- VFS selects the mounted filesystem endpoint;
- a filesystem server validates one authoritative VFS source; and
- one request produces one token-bound reply before the next dependent VFS
  transition continues.

The complete MINIX VFS worker pool is not required for the one-thread
`micros` MVP. The synchronous authority and ownership boundary is required.

### Mount establishes the root reference and request gate

Before mount, `libfsdriver` accepts `REQ_READSUPER` and rejects other
filesystem requests. It permits only one mount
(`minix/lib/libfsdriver/fsdriver.c:37-46` and
`minix/lib/libfsdriver/call.c:8-70`).

MFS opens and validates its block device and superblock, obtains the root
inode, and returns the root inode number, mode, size, owner, and group while
leaving that inode referenced
(`minix/fs/mfs/mount.c:8-96`).

VFS records one filesystem reference and one VFS reference for the mounted
root vnode, then publishes it as the mount's root
(`minix/servers/vfs/mount.c:270-321`).

The reusable behavior is:

1. initialize all filesystem-owned state before mount succeeds;
2. accept one mount;
3. return one referenced root object plus its stable attributes;
4. reject ordinary operations before mount; and
5. retain the root reference for the lifetime of the mount.

Device opening, superblock parsing, read-only fallback, and on-disk dirty
state belong to MFS persistence rather than to the RAMFS boundary.

### Lookup resolves a bounded path and leaves one result referenced

MINIX VFS sends:

- a starting inode;
- a process-root inode;
- a grant containing the path;
- the caller's credentials; and
- lookup flags

to the filesystem server
(`minix/servers/vfs/request.c:424-518`).

`libfsdriver` copies the path from the grant, obtains one reference to the
starting inode, and walks components inside the filesystem
(`minix/lib/libfsdriver/lookup.c:118-176`).

The walker:

- ignores repeated separators;
- normalizes an empty whole path to `.`;
- requires the current inode to be a searchable directory before consuming
  every component, including `.` and `..`;
- keeps `..` at the supplied process root;
- obtains one referenced child for each descent;
- releases the previous temporary reference after a successful step;
- returns to VFS at mount crossings or absolute symlinks; and
- leaves exactly the final successful node referenced
  (`minix/lib/libfsdriver/lookup.c:178-284`).

MFS performs each single-component lookup by searching the parent directory
and obtaining the resulting inode
(`minix/fs/mfs/path.c:13-69`).

The Step 12 baseline preserves:

- a path grant rather than a cross-process pointer;
- a starting node and a confinement root;
- repeated-separator, `.`, `..`, and trailing-separator behavior;
- directory-type checks before every component, including empty-path `.`;
- one final referenced result; and
- release of all temporary ownership on failure.

Symlink expansion, credential groups, nested mounts, and cross-filesystem
redirection are broader VFS/MFS behavior. They require explicit later
protocol versions rather than hidden partial support.

### Create and mkdir mutate one parent atomically

VFS resolves and authorizes the parent directory, then sends the final
component through a read grant with the requested mode and ownership
(`minix/servers/vfs/request.c:166-207,528-566`).

`libfsdriver` rejects an empty name, overlong name, `.`, or `..` before
calling the filesystem
(`minix/lib/libfsdriver/call.c:400-469`).

MFS create:

1. references the parent;
2. allocates and initializes one inode;
3. creates one directory entry;
4. returns the created inode still referenced; and
5. releases the parent
   (`minix/fs/mfs/open.c:14-53,192-255`).

MFS mkdir performs the same node-and-parent insertion, creates `.` and `..`,
increments the new directory and parent link counts, and removes the parent
entry again if directory initialization fails
(`minix/fs/mfs/open.c:74-116`).

The reusable behavior is:

- duplicate names fail without selecting the existing object;
- a successful create returns one referenced regular file;
- successful mkdir creates a directory but does not return an additional open
  node reference;
- `.` and `..` are reserved;
- link counts reflect directory parentage; and
- failure does not leave a reachable partial namespace object.

MFS's on-disk ordering protects crash recovery. RAMFS has no crash-persistent
medium, but it still requires one complete non-failing namespace commit after
all capacity and grant preflight.

### Read and write use directional grants

For file reads, VFS grants the filesystem write access to the destination
buffer. For file writes, VFS grants the filesystem read access to the source
buffer. The request carries the inode, grant, offset, and byte count, and the
reply returns the new position and transferred count
(`minix/servers/vfs/request.c:780-924`).

`libfsdriver` validates nonnegative positions and bounded counts, constructs
one grant-backed data descriptor, invokes the filesystem, and advances the
returned position only by the reported count
(`minix/lib/libfsdriver/call.c:155-205`).

MFS:

- stops reads at EOF and may return a short count;
- returns zero for unwritten sparse blocks;
- allocates blocks on write;
- zeroes unwritten ranges when extending through a hole;
- grows the file size to the committed write end; and
- leaves file position policy in VFS
  (`minix/fs/mfs/read.c:20-116,120-199`).

The reusable data behavior is:

- read at or beyond EOF succeeds with zero bytes;
- reads never pass EOF;
- sparse ranges read as zero;
- writes may extend a file and create zero-filled holes;
- the reply carries the exact new position and count; and
- grant direction follows the data flow.

MFS may allocate or dirty earlier blocks before a later safe-copy failure and
may have copied an earlier output chunk before a later error. Those partial
failure effects are not behavior to reproduce. `micros` checked copies already
validate and copy one bounded range atomically, so RAMFS stages one complete
request in resident memory and commits no filesystem state until every
fallible copy and capacity check has succeeded.

### Directory enumeration is cursor-based and record-complete

VFS sends a directory inode, cursor, writable grant, and buffer size. A
successful reply returns both the byte count and the next cursor
(`minix/servers/vfs/request.c:285-337`).

MFS scans directory storage from the supplied aligned position, skips unused
entries, and adds complete `dirent` records. If the next complete record does
not fit, MFS returns the position of that first unreported entry. At EOF it
returns the directory end position
(`minix/fs/mfs/read.c:451-535`).

`libfsdriver` never emits a partial record. It zeroes record padding, flushes
bounded staging buffers through the grant, and reports an error if even one
record cannot fit
(`minix/lib/libfsdriver/dentry.c:8-99`).

The reusable behavior is:

- enumeration begins at an explicit cursor;
- `.` and `..` are ordinary visible entries;
- only complete records are returned;
- the cursor identifies the first entry not returned;
- EOF is a successful zero-byte result; and
- enumeration does not acquire open references to listed children.

The native MINIX `dirent` ABI and MFS disk offsets are not a `micros` backend
ABI. VFS may translate one fixed internal RAMFS record into its later
application-facing directory representation.

### PUTNODE releases filesystem references in batches

VFS separately tracks its own vnode references and the number of references
held in the filesystem. When it discards a vnode, it sends one `PUTNODE`
request with the accumulated filesystem reference count
(`minix/servers/vfs/vnode.c:279-314`).

`libfsdriver` rejects zero or excessively large counts
(`minix/lib/libfsdriver/call.c:86-111`).

MFS requires the inode to be referenced, subtracts the requested count, and
calls `put_inode()`. An inode is physically reclaimed only when both:

- its open/reference count reaches zero; and
- its namespace link count is zero
  (`minix/fs/mfs/inode.c:35-62,198-244`).

The reusable behavior is:

- VFS owns descriptor and vnode reference aggregation;
- the filesystem owns object reference and link counts;
- close does not require one message per descriptor;
- an object is not reclaimable while either count remains; and
- a stale or excessive release must not select another object.

MFS panics on a missing inode or excessive count. RAMFS instead returns a
stable error and preserves state, because a protocol mistake is not allowed to
corrupt or terminate an otherwise valid filesystem service.

### Persistent MFS breadth is outside Step 12

MFS also implements:

- block-device discovery and I/O;
- on-disk inode and zone bitmaps;
- buffer cache and read-ahead;
- timestamps and ownership;
- hard links, unlink, and rename;
- symlinks;
- truncate;
- device nodes;
- mount points and nested filesystems;
- stat and filesystem statistics; and
- read-only and crash-recovery behavior.

ADR-0008 deliberately selects a volatile RAMFS before any block layer,
persistent format, driver discovery, journal, or page cache. Pulling those MFS
features into Step 12 would violate the development DAG rather than improve
baseline fidelity.

## Accepted `micros` constraints

### Fixed messages and exact reply tokens

`micros` messages are fixed 64-byte values with a 48-byte protocol payload.
Every RAMFS operation is a `call` from the exact VFS endpoint and receives one
token-bound result. Raw pointers, MINIX message unions, and VFS transaction IDs
are not part of the ABI.

### Direct grants are non-transitive

The VFS/RAMFS hop uses a direct grant created by VFS for exact RAMFS. The
application/VFS hop is a different grant into a resident VFS bounce buffer.
RAMFS never receives or forwards an application grant.

The kernel validates the complete grant range before copying and either copies
every requested byte or returns an error. RAMFS therefore bounds each transfer
to one page and performs one checked copy after complete local preflight.

### Static services have no heap

The freestanding service runtime supplies no heap or allocator. RAMFS uses
fixed BSS-backed node metadata, block mappings, data pages, and one resident
scratch page. Every capacity failure is explicit and leaves state unchanged.

### VFS owns routing and the synthetic console

RAMFS owns its tree, objects, and regular-file data. VFS owns descriptor
identity, pathname routing to the root mount, open modes, application grants,
and the synthetic console object. The console is never a RAMFS node.

## ADR-0025 classification

| MINIX behavior or difference | Classification | `micros` treatment |
| --- | --- | --- |
| Only VFS sends filesystem requests | Baseline parity | Exact active VFS endpoint is the sole RAMFS client |
| Filesystem requests are synchronous request/reply operations | Baseline parity | Every operation is one `call` and one token-bound result |
| Only mount is accepted before the filesystem is mounted | Baseline parity | One successful `MOUNT` gates every ordinary operation |
| Mount returns one referenced root with attributes | Baseline parity | Root node handle, mode, and size are returned with one pinned mount reference |
| Lookup receives a start node, confinement root, and path grant | Baseline parity | RAMFS resolves repeated `/`, empty-path `.`, explicit `.`, `..`, and trailing separators inside one tree, with a directory check before each component |
| Successful lookup leaves one final node referenced | Baseline parity | Temporary traversal state is local; only the final result increments the external reference count |
| MINIX returns mount and symlink redirections to VFS | Staged substitution | Version 1 has one root mount and no symlink nodes; later protocol versions must add explicit redirection results |
| MINIX checks UID, GID, and supplementary-group search permission | Staged substitution | v0.1 has one root-equivalent application identity; mode is retained as metadata and credential enforcement awaits a reviewed PM/VFS credential design |
| Create returns a referenced regular file and duplicate names fail | Baseline parity | One failure-atomic parent/name insertion returns one referenced node |
| Mkdir creates `.` and `..` link semantics without returning an open node | Baseline parity | Entries are synthesized, while exact directory link counts preserve the same relationships |
| MFS stores directory entries and file data on a block device | Required adaptation | A fixed volatile node table and BSS data-block arena replace the persistent medium under ADR-0008 |
| MFS uses inode numbers that may be reused | Compatible extension | Slot/generation node handles reject stale references and never wrap into validity |
| MFS names are limited to 60 bytes and longer comparisons may truncate | Compatible extension | Preserve the 60-byte bound but reject overlong names instead of aliasing by truncation |
| VFS grants read/write authority according to data direction | Baseline parity | Exact-direction VFS-to-RAMFS direct grants carry paths, names, file bytes, and directory records |
| MINIX may use transitive magic grants to reach an application buffer | Required adaptation | VFS uses its own resident bounce buffer because `micros` grants are non-transitive |
| Reads stop at EOF and sparse ranges read as zero | Baseline parity | Short reads, zero-byte EOF, and zero-filled holes are required |
| Writes grow files and zero unwritten gaps | Baseline parity | Bounded sparse writes publish size and block mappings in one commit |
| MFS may partially mutate or copy before a later transfer error | Compatible extension | One-page resident staging makes each RAMFS request failure-atomic |
| Getdents returns complete records and resumes at the first record that did not fit | Baseline parity | A fixed 128-byte backend record and bounded cursor preserve the same semantics |
| MINIX exposes its native `dirent` format to VFS | Required adaptation | VFS receives one pointer-free internal record and later translates it for applications |
| VFS batches filesystem reference release through `PUTNODE` | Baseline parity | A counted release separates VFS references from RAMFS link ownership |
| MFS panics on missing or excessive `PUTNODE` counts | Compatible extension | Stable `NODE` or `REFERENCE` results preserve complete RAMFS state |
| Inode reclamation requires both zero links and zero references | Baseline parity | The invariant is retained; version 1 has no unlink operation and therefore reclaims no reachable node |
| MFS has 512 in-core inode slots and disk-defined file capacity | Required adaptation | Version 1 has explicit fixed node, block, path, name, file, and transfer bounds |
| MFS includes unlink, rename, hard links, symlinks, truncate, stat, timestamps, device nodes, nested mounts, and persistence | Staged substitution | These remain outside the shell-MVP RAMFS operation set and require later reviewed extensions |
| MFS root contents come from a persistent disk image | Required adaptation | A build-generated, validated, pointer-free seed blob initializes writable RAMFS state |
| MINIX on-disk validation is tied to superblock and bitmap consistency | Compatible extension | Canonical sizes, reserved bytes, parent order, payload coverage, capacity, and an FNV-1a digest are validated before readiness |

No Step 12 difference is a divergence requiring correction.

## Derived Step 12 boundary

The initial RAMFS outcome must therefore provide:

1. one real static RAMFS service with exact service ID 5, process slot 4, and
   profile 5;
2. exact VFS-only `MOUNT`, `LOOKUP`, `CREATE`, `MKDIR`, `READ`, `WRITE`,
   `GETDENTS`, and `PUTNODE` calls;
3. one mount, one root, fixed node and data capacities, and no dynamic
   allocation;
4. generation-safe node handles, separate link/reference counts, and stable
   stale-handle rejection;
5. a validated build-generated seed copied into writable runtime state before
   bootstrap readiness;
6. bounded path traversal with root confinement;
7. one-page exact-direction grants with failure-atomic local staging;
8. short EOF reads, sparse zeroes, file growth, and deterministic directory
   cursors;
9. stable malformed, caller, state, node, namespace, range, capacity, grant,
   and reference results;
10. native parser, state-machine, capacity, and replayable model evidence;
11. one QEMU scenario with the real launcher, VM, PM, TTY, and RAMFS plus an
    exact test VFS peer; and
12. no block layer, production VFS descriptor implementation, application
    I/O, executable loading, init, shell, persistence, or recovery.

## Specification basis

- [ADR-0004: IPC and Endpoint ABI](../adr/0004-ipc-and-endpoint-abi.md)
- [ADR-0005: Direct Memory Grants](../adr/0005-direct-memory-grants.md)
- [ADR-0008: RAMFS-First Filesystem](../adr/0008-ramfs-first-filesystem.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](../adr/0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0033: Reviewed Design Before Implementation](../adr/0033-reviewed-design-before-implementation.md)
- [ADR-0043: Static Bootstrap Launcher and Embedded Manifest](../adr/0043-static-bootstrap-launcher.md)
- [ADR-0047: TTY Console Handoff and Serial Protocol](../adr/0047-tty-console-handoff-and-serial-protocol.md)
- [System overview](../architecture/system-overview.md)
- [Development dependency DAG](../architecture/development-dag.md)
- [Testing strategy](../testing-strategy.md)

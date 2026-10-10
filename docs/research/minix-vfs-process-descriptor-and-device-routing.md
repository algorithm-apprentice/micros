# MINIX VFS Process, Descriptor, and Device-Routing Baseline

## Scope

This note records the fixed behavioral baseline for `micros` development-DAG
Step 13. It complements the earlier
[MINIX VFS/MFS filesystem-protocol study](minix-vfs-mfs-filesystem-protocol.md),
which already covers the VFS-to-filesystem mount, lookup, create, read, write,
getdents, and putnode boundary.

The reference is the local MINIX checkout at:

```text
/Users/alephling/Repo/minix
commit 4db99f4012570a577414fe2a43697b2f239b699e
```

This study focuses on behavior above that filesystem boundary:

- caller identity and per-process VFS state;
- file descriptors, shared open-file descriptions, and vnodes;
- root and working-directory routing;
- open, close, read, write, and directory-enumeration ownership;
- character-device suspension, completion, and cancellation; and
- process initialization, inheritance, and exit cleanup.

It is behavioral research, not permission to copy MINIX source. Accepted
`micros` ADRs remain authoritative where the systems differ.

## Caller identity selects one process record

MINIX receives the kernel-written source endpoint, derives the process-table
slot, and selects one `fproc` record. A live record must contain the same exact
endpoint generation; disagreement is treated as an internal consistency
failure rather than allowing a payload field to choose another process
(`minix/servers/vfs/main.c:580-630`).

The selected `fproc` owns:

- the semantic process and exact kernel endpoint identity;
- one root directory vnode;
- one working-directory vnode;
- one file-descriptor table;
- blocking-operation state, including retained character-device identity and
  grant authority; and
- credentials and other POSIX policy outside the Step 13 scope
  (`minix/servers/vfs/fproc.h:10-79`).

The reusable behavior is:

- VFS binds a request to the kernel-written source endpoint;
- one process record owns descriptor, root, working-directory, and pending-I/O
  state;
- a payload pointer or claimed process identifier never selects authority; and
- a stale or inconsistent endpoint cannot alias a new process generation.

`micros` does not reuse endpoint slot extraction as a VFS table index.
Generation-safe endpoints and VFS process-record slots remain distinct
identities.

## Descriptors select shared open-file descriptions

Each MINIX process has a descriptor table whose entries point to global
`filp` objects (`minix/servers/vfs/fproc.h:19-25`).

A `filp` is the shared open-file description. It owns:

- access mode;
- open flags;
- a reference count;
- the selected vnode; and
- the shared file position
  (`minix/servers/vfs/file.h:4-19`).

Before pathname lookup or creation, `get_fd` finds both:

1. one free descriptor in the calling process; and
2. one free global `filp`.

It initializes but does not publish the `filp`, because path resolution or
creation may still fail (`minix/servers/vfs/filedes.c:110-159`).

The reusable behavior is:

- descriptors are process-local small integers;
- open-file descriptions are global VFS objects;
- duplicated or inherited descriptors may share one open-file description and
  therefore one file position;
- descriptor and open-file capacity is preflighted before a backend operation;
  and
- failed open does not publish a partially initialized descriptor.

## Vnodes separate VFS references from filesystem references

MINIX vnodes cache the filesystem endpoint and inode identity, mode, size,
special-device identity, a VFS reference count, and a separate filesystem
reference count (`minix/servers/vfs/vnode.h:4-23`).

Path lookup first reserves a free vnode slot. If the returned filesystem
identity is already cached, VFS:

- discards the scratch slot;
- increments the existing vnode's filesystem-reference count because lookup
  acquired another backend reference; and
- increments the VFS-reference count for the new local owner
  (`minix/servers/vfs/path.c:36-121`).

Dropping a VFS reference does not send one backend `PUTNODE` for every local
release. MINIX retains and aggregates filesystem references. When the final
VFS reference disappears, it sends the complete accumulated count in one
request. It also periodically reduces an excessive accumulated count while
retaining one backend reference (`minix/servers/vfs/vnode.c:238-315`).

The reusable behavior is:

- VFS vnode identity is not an application descriptor;
- VFS local references and filesystem-owned references are separate counts;
- repeated lookup of a cached node still acquires a backend reference;
- final local release batches backend reference release; and
- bounded periodic cleanup prevents the backend count from approaching
  overflow while a vnode remains cached.

## Root mount installs process root and working-directory references

MINIX obtains one referenced root inode from the mounted filesystem, creates
the root vnode with one filesystem reference and one VFS reference, and then
duplicates that vnode into every live process's root and working-directory
fields (`minix/servers/vfs/mount.c:270-347`).

Absolute paths start from the process root. Relative paths start from the
process working directory (`minix/servers/vfs/path.c:130-140`).

The selected MINIX build uses its historical pathname mode and removes
trailing slashes before final-component resolution
(`minix/servers/vfs/path.c:24-31,190-195`). The already Accepted RAMFS
contract instead preserves a trailing separator as a directory requirement.
VFS must therefore classify and expose that deliberate adaptation rather than
silently claim byte-for-byte pathname parity.

Changing root or working directory:

- resolves the path first;
- requires a directory;
- releases the old local vnode reference;
- duplicates the new local vnode reference; and
- publishes the replacement only after validation
  (`minix/servers/vfs/stadir.c:95-137`).

The reusable behavior is:

- one mounted root is globally pinned;
- each process separately owns root and working-directory references;
- absolute and relative routing is selected by path syntax; and
- replacing a directory reference balances old and new ownership.

## Open preflights local capacity before backend publication

MINIX open maps the access flags, preflights one descriptor and one open-file
description, then resolves or creates the path
(`minix/servers/vfs/open.c:83-127`).

After a vnode is obtained, VFS publishes:

- the process descriptor;
- the open-file-description reference;
- access and open flags; and
- the vnode and initial position
  (`minix/servers/vfs/open.c:128-153`).

Existing regular files, directories, character devices, block devices, and
other POSIX objects then take type-specific paths. A directory may be opened
for reading but not writing (`minix/servers/vfs/open.c:154-180`).

The reusable Step 13 behavior is:

- reserve descriptor, open-file, and vnode capacity before backend mutation;
- resolve existing nodes or create one regular-file leaf;
- publish one descriptor only after complete success;
- reject writable directory opens; and
- keep backend node identity behind VFS-owned descriptor state.

Truncate, append, symlinks, block devices, pipes, sockets, and general special
files are outside the first `micros` VFS boundary.

## Read and write use the calling process's descriptor only

MINIX resolves a descriptor through the selected process record, checks the
open-file access mode, reads the position from the shared open-file
description, and routes by vnode type
(`minix/servers/vfs/read.c:120-275`).

For regular files:

- the filesystem receives the vnode identity, current position, user buffer
  authority, and requested count;
- VFS accepts the returned next position and transferred count; and
- the shared open-file position advances only according to the accepted
  result.

For character devices, MINIX routes through the device map instead of the
filesystem. The current implementation optimistically advances character
device positions while an asynchronous request is suspended, with documented
limitations (`minix/servers/vfs/read.c:150-209`).

The reusable behavior is:

- one descriptor determines access, object type, and shared position;
- regular-file I/O advances by accepted transfer results;
- character I/O routes to the device owner rather than the filesystem; and
- application buffer authority follows the data direction.

`micros` deliberately does not copy MINIX's optimistic character-device
position workaround. The synthetic console has no seekable position, and
regular-file or directory positions change only after accepted results.

## Directory enumeration uses the open-file cursor

MINIX `getdents`:

- resolves a readable descriptor;
- requires a directory vnode;
- passes the open-file description's current position to the filesystem; and
- advances that position only when complete records were returned
  (`minix/servers/vfs/read.c:279-320`).

The VFS-to-filesystem request uses a magic grant that lets the filesystem
write the native application buffer directly. The filesystem returns both the
byte count and next cursor (`minix/servers/vfs/request.c:285-337`).

The reusable behavior is:

- directory position belongs to the open-file description;
- only complete records are returned;
- zero-byte EOF is successful;
- a failed transfer does not advance the cursor; and
- listed children do not acquire open references.

The native MINIX `dirent` layout and transitive magic grant are required
adaptations. `micros` VFS must receive fixed RAMFS records into resident
storage, translate them into a pointer-free application record, and perform a
second separately authorized direct-grant copy.

## Character-device requests suspend without blocking all VFS work

MINIX character-device read and write creates a magic grant naming the
application buffer, sends an asynchronous driver request, records the exact
device, driver endpoint, and grant in the process record, and suspends the
calling process (`minix/servers/vfs/cdev.c:270-342`).

The driver later replies with the original process endpoint as request
identity. VFS either:

- wakes a worker waiting for an open, close, or cancel reply; or
- validates that the process is blocked on that exact driver and revives the
  suspended application request
  (`minix/servers/vfs/cdev.c:425-474`).

Cancellation sends an exact request to the driver, waits for the outcome, and
revokes the retained grant. The request may already have completed
(`minix/servers/vfs/cdev.c:376-424`).

The reusable behavior is:

- a blocking terminal read must not block VFS from receiving completion or
  lifecycle work;
- retained device state is bound to one exact process and request;
- cancellation races with completion and must handle either result; and
- grant authority remains owned until completion or cancellation is resolved.

The MINIX worker pool, asynchronous send protocol, driver map, controlling-TTY
policy, and signal cancellation are outside Step 13. `micros` instead uses the
already accepted TTY submit/notify/collect/cancel protocol and one explicit
single-threaded VFS pending-operation state.

## Close removes descriptor ownership before final release

MINIX close clears the process descriptor and decrements the shared
open-file-description reference. On the final reference, type-specific close
work runs and the vnode is released
(`minix/servers/vfs/filedes.c:410-520`).

The reusable behavior is:

- the process descriptor is no longer published once close commits;
- shared open-file descriptions survive until their final descriptor;
- final open-file release drops exactly one VFS vnode reference; and
- final vnode release balances the accumulated backend references.

Step 13 has no closeable named device node. Closing a synthetic console
descriptor only releases the VFS open-file description.

## PM initializes, inherits, and tears down VFS process state

At startup, MINIX VFS receives PM-owned process identities and initializes one
`fproc` slot per live process before requests are enabled
(`minix/servers/vfs/main.c:390-489`).

On fork, PM tells VFS the exact parent and child endpoints. VFS copies the
parent process record, increments every inherited open-file-description
reference, assigns the child identity, and duplicates root and
working-directory references (`minix/servers/vfs/misc.c:580-634`).

PM exit work is serialized with any operation already associated with the
target process (`minix/servers/vfs/main.c:760-848`).

Process release:

1. cancels a blocked operation;
2. closes every descriptor;
3. releases root and working-directory references; and
4. clears the process identity
   (`minix/servers/vfs/misc.c:637-704`).

The reusable behavior is:

- PM is the lifecycle authority for VFS process records;
- descriptor inheritance shares open-file descriptions explicitly;
- PM cleanup is serialized with process-owned I/O;
- pending device work is resolved before descriptor release; and
- all descriptors, root/cwd references, and retained grants are released
  before the process record becomes reusable.

Production `micros` spawn and PM/VFS wire transactions remain Step 14. Step 13
must nevertheless implement and model the trusted attach/detach core so later
transactions do not invent descriptor ownership or cleanup behavior.

## Behavior classification

| MINIX behavior | Classification | Step 13 treatment |
| --- | --- | --- |
| Kernel-written source endpoint selects one `fproc` | Baseline parity | Exact source endpoint selects one registered VFS process record; payload identity is data only |
| Per-process root, working directory, and descriptor table | Baseline parity | Fixed VFS process records own all three |
| Process-local descriptor indexes a global `filp` | Baseline parity | Fixed descriptor table indexes a global open-file-description table |
| Shared `filp` owns access, flags, reference count, vnode, and position | Baseline parity | Preserve the separation and shared-position semantics |
| Vnode has separate VFS and filesystem reference counts | Baseline parity | Cache exact RAMFS node identity and batch `PUTNODE` release |
| Periodically reduce accumulated backend references | Baseline parity | Use one fixed cleanup threshold while retaining one backend reference |
| Preflight descriptor, `filp`, and vnode capacity before open | Baseline parity | Reserve every local slot before lookup or create |
| Absolute paths use process root; relative paths use cwd | Baseline parity | Pass exact start and confinement-root handles to RAMFS |
| The selected MINIX build removes trailing path separators | Required adaptation | Preserve the Accepted RAMFS rule that a trailing separator requires a directory; regular-file create never strips that requirement |
| Root mount duplicates root/cwd references into processes | Baseline parity | One pinned root vnode is duplicated during trusted process attach |
| Writable directory open is rejected | Baseline parity | Return a stable application-facing `IS_DIRECTORY` result |
| Regular-file position advances by accepted transfer result | Baseline parity | Commit the RAMFS-returned next position only on successful application transfer |
| Directory enumeration uses the shared open-file cursor | Baseline parity | Translate complete RAMFS records and commit the returned cursor only after the application copy |
| Character-device I/O suspends the caller and retains exact request state | Baseline parity | Retain one exact application call while VFS continues receiving TTY events |
| Device cancellation may race completion | Baseline parity | Cancel an exact pending TTY read or collect an already completed request |
| Exit closes descriptors and releases root/cwd state | Baseline parity | Trusted detach returns every VFS-owned reference and grant to baseline |
| MINIX magic grants let a filesystem or driver reach the application buffer | Required adaptation | Use application-to-VFS and VFS-to-RAMFS/TTY direct grants with resident bounce pages |
| Magic-grant validation happens at the final data owner | Required adaptation | Add a grantee-only non-copying grant-range validation syscall before a consuming second hop |
| Native filesystem `dirent` bytes are returned to applications | Required adaptation | Translate fixed 128-byte RAMFS records into a fixed 80-byte VFS application record |
| Character nodes, device maps, controlling terminals, and driver discovery | Required adaptation | Use one VFS-owned synthetic console object bound only to initial descriptors |
| Worker threads keep unrelated VFS calls progressing | Required adaptation | One VFS thread retains at most one asynchronous console operation and returns `BUSY` to unrelated application work |
| Character-device position is optimistically advanced while suspended | Compatible extension | Synthetic console position is always zero; no optimistic advancement exists |
| Broad POSIX open flags, permissions, credentials, and umask | Deferred | Retain mode metadata but use the v0.1 single-identity policy and a bounded flag set |
| Fork, unrestricted descriptor duplication, and close-on-exec | Deferred | Keep open-file-description references capable of sharing; Step 14 defines descriptor-action messages |
| Signals, nonblocking I/O, select, pipes, sockets, block devices, and ioctl | Deferred | No Step 13 application ABI is assigned |
| Symlinks, multiple mounts, mount crossing, and chroot | Deferred | One RAMFS root and root-confined lookup only |
| Live update, service restart, and driver recovery | Deferred | Static-service failure remains fatal before RS |

## Step 13 boundary

The fixed baseline implies that Step 13 must define and test:

- one real static VFS service after RAMFS;
- a fixed process table, descriptor tables, open-file descriptions, and vnode
  cache;
- one mounted RAMFS root and per-process root/cwd ownership;
- bounded open, close, read, write, getdents, mkdir, and chdir requests;
- exact access checks and shared positions;
- fixed application directory-record translation;
- two resident one-page bounce buffers;
- non-copying validation of an application grant before a consuming read hop;
- asynchronous synthetic-console read/write state over the accepted TTY
  protocol;
- trusted process attach/detach core behavior;
- complete cleanup and backend-reference balancing; and
- one isolated QEMU application probe proving both grant hops.

Step 13 must not claim:

- production PM/VFS spawn or exit messages;
- executable lookup or buffering;
- dynamic VM mappings or process activation;
- init, shell, or ordinary application publication;
- named device nodes;
- seek, truncate, append, unlink, rename, symlink, or multiple mounts;
- signals, nonblocking I/O, select, pipes, sockets, or block devices; or
- service restart or recovery.

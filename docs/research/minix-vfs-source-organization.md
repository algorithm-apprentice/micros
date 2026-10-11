# MINIX VFS Source-Organization Baseline

This note records the MINIX 3 VFS source-organization baseline used to design
the first responsibility-based split of the `micros` VFS implementation. It is
an organizational baseline only. Runtime behavior remains governed by
[ADR-0049](../adr/0049-vfs-service-and-application-io-protocol.md) and
[ADR-0050](../adr/0050-post-handoff-elf-spawn.md).

## Reference

The reference tree is the local MINIX checkout at:

```text
/Users/alephling/Repo/minix
```

The inspected revision is:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

The relevant sources are under `minix/servers/vfs/`. The inventory commands
used for this note were:

```sh
git -C /Users/alephling/Repo/minix rev-parse HEAD
sed -n '1,120p' /Users/alephling/Repo/minix/minix/servers/vfs/Makefile
find /Users/alephling/Repo/minix/minix/servers/vfs -maxdepth 1 \
  -name '*.c' -print | sort
wc -l /Users/alephling/Repo/minix/minix/servers/vfs/*.c
```

At that revision, the MINIX VFS build contains 33 C source files and 16,735
C source lines. The largest individual C source file has 1,416 lines. These
counts are evidence that MINIX does not implement VFS as one core source file;
they are not line-count requirements for `micros`.

## Responsibility split in MINIX

The MINIX VFS Makefile assembles the service from responsibility-oriented
modules. Representative boundaries are:

| Responsibility | Representative MINIX files |
| --- | --- |
| Receive loop, dispatch, reply, and PM-control routing | `main.c` |
| Descriptor and shared open-file-description management | `filedes.c`, `file.h` |
| Vnode allocation, lookup, reference counts, and locks | `vnode.c`, `vnode.h` |
| Path traversal and mount-point crossing | `path.c`, `path.h` |
| Open, create, close, mkdir, and seek operations | `open.c` |
| Regular read, write, and directory-read mechanics | `read.c`, `write.c` |
| Character-device request, suspension, cancellation, and reply handling | `cdev.c` |
| Process lifecycle coordination with PM | `misc.c` |
| Executable lookup and executable-image coordination | `exec.c` |
| File-server protocol request construction | `request.c` |
| Mount-table and mounted-filesystem ownership | `mount.c`, `vmnt.c`, `vmnt.h` |
| Worker scheduling and asynchronous job ownership | `worker.c` |

The modules are not isolated libraries. They share VFS-owned process,
descriptor, vnode, mount, and worker state through private headers. The
important baseline is that each implementation file has a named ownership
area and that the receive loop does not also implement every operation.

MINIX's own VFS README identifies the same logical roles: application request
handling, process-owned descriptor state, PM lifecycle cooperation, mounted
filesystem routing, device routing, and suspended-operation resumption. The
source layout follows those roles.

## Current `micros` organization

Before this design, `micros` builds the portable VFS core primarily from
`servers/vfs/vfs_core.c`. The file has 6,043 lines and 100 function
definitions. It currently contains all of the following:

- canonical state validation and table lookup;
- process, descriptor, shared-open-file, and vnode reference ownership;
- RAMFS path lookup and vnode integration;
- application open, close, read, write, getdents, mkdir, and chdir handling;
- TTY submission, completion, cancellation, notification, and pending state;
- mount, attach, descriptor sharing, and detach transitions;
- executable-image and descriptor preparation, hidden publication, rollback,
  abort, and release transitions;
- application and PM-control dispatch.

The public state and API remain in `servers/vfs/vfs_core.h`. The production
service boundary is already split between `servers/vfs/vfs_service_core.c`,
which owns byte-exact message codecs and portable dispatch, and
`servers/vfs/vfs_service.c`, which owns the target IPC loop and backend
adapters.

The single portable core was useful while the VFS contract was still being
established. It now combines independently testable authorities and state
machines, so adding further Step 14 behavior there would increase review and
change-isolation risk.

## Difference classification

The classification follows
[ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md).

| Topic | MINIX baseline | `micros` adaptation | Classification |
| --- | --- | --- | --- |
| Service facade | Receive and reply routing are separate from operation modules | `vfs_service.c` remains the target loop, `vfs_service_core.c` remains the wire codec, and `vfs_core.c` becomes the portable dispatch facade | Compatible extension |
| Descriptor and vnode ownership | Separate descriptor and vnode modules own reusable primitives | One ownership module may contain both fixed-table concepts while preserving distinct object types | Compatible extension |
| Namespace operations | Path traversal and open-family operations are separate concerns | RAMFS-only path and namespace operations remain together because v0.1 has one filesystem peer | Compatible extension |
| Root mount ownership | Mount-table ownership is separate from process lifecycle | The one immutable RAMFS root mount belongs to namespace initialization rather than the process module | Required adaptation |
| Device suspension | Character-device code owns suspended I/O and replies | A console module owns the one bounded TTY pending operation and notification handling | Required adaptation |
| Process lifecycle | PM lifecycle work is separate from ordinary application operations | A process module owns attach, share, detach, and process-owned cleanup only | Baseline parity |
| Executable handling | Executable work has a dedicated module | The tokenized VFS side of the spawn transaction has a dedicated module | Required adaptation |
| Worker and lock infrastructure | Multiple workers and lock ordering are explicit modules | Accepted v0.1 VFS is single-threaded and adds no unused worker or lock framework | Required adaptation |
| Filesystem request wrappers | File-server message construction is separated | Checked grants and backend calls use a small internal transfer module | Required adaptation |
| Shared state | Private headers expose service-internal objects | `vfs_core.h` keeps the reviewed public state layout; cross-module helpers use one private header | Compatible extension |

## Derived constraints

The baseline and accepted `micros` contracts imply the following constraints:

1. Split by authority and lifecycle, not by an arbitrary equal line count.
2. Keep process, open-file, vnode, pending-console, and spawn-transaction
   ownership explicit and distinct.
3. Keep `vfs_service_core.c` as the byte-exact wire codec and
   `vfs_service.c` as the target IPC/backend adapter.
4. Pass `struct micros_vfs_state *` explicitly. Do not introduce mutable
   process-global VFS state to make source movement easier.
5. Keep direct-grant direction, second-hop grant ownership, bounce-page
   ownership, error precedence, and token validation unchanged.
6. Preserve the public `vfs_core.h` state layout and API during the first
   decomposition.
7. Do not import MINIX worker, lock, recovery, multi-filesystem, or dynamic
   allocation mechanisms that are outside the v0.1 requirement.
8. Keep complete-state validation centralized even though operation handling
   moves to responsibility-specific modules.

## Non-goals

This baseline does not authorize:

- VFS exit cleanup or PM/VFS exit-detach integration;
- a second filesystem server or a general mount namespace;
- worker threads, locking, crash recovery, or dynamic allocation;
- public ABI, wire-format, capacity, or state-layout changes;
- semantic changes to namespace, descriptor, console, or spawn behavior;
- copying MINIX source code into `micros`.

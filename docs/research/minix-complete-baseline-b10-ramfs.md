# B10 RAMFS Baseline Audit

## Status

```text
audit unit:       B10 RAMFS
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; six blocking findings; not certified
implementation:   forbidden during complete-audit phase
```

B10 audits the build seed, volatile node/data representation, mount gate,
node/reference ownership, pathname resolution, create/mkdir, sparse regular
I/O, directory enumeration, counted `PUTNODE`, service dispatch, and complete
runtime validation.

## Exact primary path manifest

B10 has 17 primary paths in the pinned tree.

### Public contract

| Path | Responsibility |
| --- | --- |
| `include/micros/ramfs.h` | RAMFS capacities, modes, messages, results, seed ABI, and image limits |

### Portable RAMFS owners

| Path | Responsibility |
| --- | --- |
| `servers/ramfs/ramfs_codec.c` | Wire decode/result encode and byte primitives |
| `servers/ramfs/ramfs_core.c` | Common preflight, dispatch, final validation, and reply preparation |
| `servers/ramfs/ramfs_core.h` | Node/state/request/reply/I/O public portable contract |
| `servers/ramfs/ramfs_directory.c` | Fixed directory-record construction and cursor enumeration |
| `servers/ramfs/ramfs_internal.h` | Private codec, ownership, validation, transfer, and operation contracts |
| `servers/ramfs/ramfs_io.c` | Sparse read staging, block allocation, write commit, and file growth |
| `servers/ramfs/ramfs_namespace.c` | Child lookup, confinement, pathname walk, create, and mkdir |
| `servers/ramfs/ramfs_ownership.c` | Handles, node resolution, free-node/free-block search, mount, and putnode |
| `servers/ramfs/ramfs_seed.c` | Seed digest, canonical validation, and allocation plan |
| `servers/ramfs/ramfs_seed.h` | Seed errors and plan interface |
| `servers/ramfs/ramfs_state.c` | Complete state validation and seed-to-runtime initialization |
| `servers/ramfs/ramfs_transfer.c` | Exact VFS grant-copy callback boundary |

### Service and seed owners

| Path | Responsibility |
| --- | --- |
| `servers/ramfs/ramfs_embedded_seed.h` | Generated embedded-seed declarations |
| `servers/ramfs/ramfs_service.c` | Bootstrap configuration, readiness, grant adapter, reply/receive loop |
| `servers/ramfs/seed.json` | Canonical build seed declaration |
| `servers/ramfs/seed/etc/motd` | Exact initial `/etc/motd` payload |

## Cross-owner source dependencies

| Path family | B10 operation | Primary/later owner |
| --- | --- | --- |
| frame ownership, user roots, resident-page budget | B10-O01/O02 static RAMFS image and mutable arena | B02/B07 |
| process/thread scheduling and service execution | B10-O02 service lifecycle | B03 |
| endpoint, call/reply/reply-receive, profile authority | B10-O02-O08 VFS/RAMFS transport | B04 |
| direct grants, checked copy, runtime byte primitives | B10-O04-O08 paths, names, file bytes, records | B05 |
| static manifest, release/readiness, fatal service loss | B10-O01/O02 service startup | B06 |
| PM service identity/lifecycle | B10-O02 RAMFS process ownership | B08 |
| TTY prerequisite and later console path | B10 startup dependency only | B09 |
| descriptors, root mount, application bounce buffers | B10 protocol consumer | B11 |
| executable lookup and application lifecycle | Seed/application use | B12 |
| init/shell/basic-command contents and traversal | Seed/application use | B13 |
| generators, native/model/QEMU evidence | All B10 operations | B14 |

## Fixed MINIX source evidence

The B10 comparison uses only local source:

- `minix/include/minix/fsdriver.h`
- `minix/include/minix/vfsif.h`
- `minix/lib/libfsdriver/fsdriver.c`
- `minix/lib/libfsdriver/call.c`
- `minix/lib/libfsdriver/lookup.c`
- `minix/lib/libfsdriver/dentry.c`
- `minix/lib/libfsdriver/utility.c`
- `minix/fs/mfs/table.c`
- `minix/fs/mfs/mount.c`
- `minix/fs/mfs/path.c`
- `minix/fs/mfs/open.c`
- `minix/fs/mfs/read.c`
- `minix/fs/mfs/write.c`
- `minix/fs/mfs/inode.c`
- `minix/fs/mfs/inode.h`
- `minix/fs/mfs/cache.c`
- `minix/fs/mfs/super.c`
- `minix/fs/mfs/stats.c`
- `minix/fs/mfs/super.h`
- `minix/fs/mfs/const.h`
- `minix/servers/vfs/comm.c`
- `minix/servers/vfs/request.c`
- `minix/servers/vfs/mount.c`
- `minix/servers/vfs/vnode.c`
- `common/lib/libc/arch/arm/string/memcpy.S`
- `common/lib/libc/arch/arm/string/memset.S`

## Logical variables

| Variable | Meaning |
| --- | --- |
| `n` | RAMFS node capacity, 64 |
| `a` | live RAMFS nodes |
| `d` | live/direct entries in one selected directory |
| `D` | physical slots in one selected MINIX directory, including holes |
| `C` | cached MINIX inode records, at most 512 |
| `h` | entries traversed in one selected MINIX inode-hash bucket; expected `C/128`, worst `C` |
| `y` | live directory entries emitted by one getdents result |
| `c` | path components traversed |
| `m` | compared name bytes, at most 60 |
| `b` | physical data-block capacity, 64 |
| `f` | currently free physical blocks |
| `z` | bytes per block, 4,096 |
| `s` | resident scratch capacity, 4,096 |
| `x` | bytes in one path/name/data transfer, at most 4,096 |
| `e` | active seed entries, at most 64 |
| `I` | complete embedded seed image bytes, at most 270,400 |
| `Mz` | bits represented by the mounted MINIX zone bitmap |
| `Vram` | complete runtime RAMFS-state validation with clear scratch |
| `Vram*` | complete runtime validation while scratch contains staged input |
| `Vseed` | complete seed-image validation |
| `Vgrant` | B05 complete grant validation/copy work |

## Complete validation cost definitions

One complete runtime validation:

1. scans every free node's name and 64 logical block slots;
2. scans every live node's name, parent, mode, counts, and 64 block slots;
3. compares each live non-root sibling name against earlier live nodes;
4. scans all nodes for each live directory's child-directory link count;
5. scans every physical block owner/reverse mapping;
6. checks all `z` bytes of every free physical block are zero; and
7. checks all `s` scratch bytes are zero.

```text
Vram = O(a^2*m + n*(b+m) + a*n + f*z + s)
Vram* = O(a^2*m + n*(b+m) + a*n + f*z)
```

The canonical production seed has three nodes and one allocated block.
Therefore one `Vram` examines at least:

```text
63*4096 free-block bytes + 4096 scratch bytes = 262144 bytes
```

before counting node/name/block-map predicates.

Seed validation hashes the complete image, scans fixed inactive records,
validates active names/data extents, and compares every non-root name with
earlier records under the same parent.

```text
Vseed = O(I + e^2*m)
```

## Operation ledger

### B10-O01 Seed declaration, blob validation, and runtime initialization

MINIX mounts an existing MFS image by reading the superblock, scanning the
complete zone bitmap to count free/used zones, and obtaining the root inode.
The bitmap accounting is:

```text
O(bitmap words + nonfull words * word bits) = O(Mz) worst case
```

It does not traverse every directory name or validate sibling uniqueness at
mount.

`micros` necessarily uses a pointer-free build seed because ADR-0008 selects a
volatile initial filesystem with no block driver. The JSON declaration,
fixed 64-record binary table, payload packing, digest, parent-before-child
ordering, capacity plan, and retained read-only blob are required adaptations
inside the current RAMFS stage. Replacing the volatile stage with persistent
filesystem storage remains the SOM-05 staged substitution.

The runtime validator performs `Vseed`, then:

1. clears the complete 281,368-byte mutable state byte by byte;
2. separately clears/initializes all 64 nodes and every node's 64 block
   entries;
3. installs metadata and link counts;
4. copies every payload byte individually into assigned blocks; and
5. runs `Vram`.

The pairwise seed-name algorithm and byte-serial state/payload movement are
not required by the pointer-free format. See B10-F006.

### B10-O02 VFS-only service authority, mount, and receive loop

Both systems accept filesystem operations only from VFS, gate ordinary calls
on one successful mount, return one referenced root with stable attributes,
and use synchronous request/reply ownership.

`micros` replaces dynamic VFS/FS endpoint discovery with one generation-safe
static VFS endpoint, one reply token, fixed messages, reply/receive, and fatal
peer loss. Those are required bootstrap/isolation adaptations. The service
configuration's pairwise endpoint uniqueness check is startup-only and
belongs to the B06 static-bootstrap agreement.

One successful `MOUNT` executes:

```text
2*Vram
```

around one root-reference and phase mutation. MINIX mount validates selected
superblock/root state and performs the `O(Mz)` zone-bitmap accounting above,
rather than validating every inode, directory relation, free data block, and
scratch byte.

### B10-O03 Node identity, references, and counted `PUTNODE`

MINIX separates:

- persistent inode numbers and allocation bitmap;
- a 512-entry in-core inode cache;
- a 128-bucket inode hash;
- one O(1) unused/LRU queue; and
- counted VFS references in each cached inode.

`micros` keeps every volatile node permanently resident and encodes the
node-table slot directly in the handle; that is a required adaptation for the
fixed all-resident RAMFS stage. Adding a generation and stable stale-handle
rejection is a compatible safety extension. SOM-05 owns persistent inode/
block allocation, reuse, cache eviction, physical reclamation, and durability.
SOM-12 owns user-visible link/unlink/rename/truncate and related namespace/
metadata operations, with SOM-05 already listed as its predecessor.

Counted `PUTNODE` preserves the reference/link ownership split and root pin.
It nevertheless pays `2*Vram` for one direct handle resolution and one scalar
decrement.

### B10-O04 Path copy, confinement, and namespace lookup

MINIX `libfsdriver` copies the complete path, references the start inode, and
walks components. MFS stores names as separate directory entries and
`search_dir()` scans only the selected directory's entry slots. Successful
descent obtains the child inode through the inode hash/cache and releases the
previous temporary reference.

One inode hash bucket has expected length proportional to cached inodes/128
under an even distribution and worst-case length 512. A cache miss may also
load the selected inode from persistent storage; that I/O is SOM-05 work.

`micros` embeds one parent handle and one name in every node. `find_child()`
scans all `n` node slots for each ordinary component and compares names only
after finding a live node with the selected parent. `GETDENTS` uses the same
global node-slot space rather than a per-directory entry position. Before it
copies or examines any path component, `is_ancestor()` also walks up to `n`
parent links to prove that start lies below the supplied root.

```text
MINIX component lookup:
    O(D + d*m + h_parent + h_child)

micros component lookup:
    O(n + d*m)

micros c-component path:
    O(path bytes + depth + c*(n + d*m))
```

With many nodes split across small directories, global `n` replaces the
selected directory's physical `D`; deleted MINIX holes may make `D > d`, but
unrelated nodes in other directories do not. `micros` direct slot resolution
avoids the expected/worst hash-chain term as a required all-resident
adaptation. For an empty or `.` relative path, current valid-path work still
includes the independent `O(depth)` ancestry prewalk; MINIX begins at the
selected start inode and enforces the supplied root incrementally only when
processing `..`. The embedded-parent representation removes the separate
directory-entry topology that underlies MFS insertion and enumeration. See
B10-F002.

### B10-O05 Create and mkdir

MINIX's `fsdriver` first copies and validates the name. MFS resolves the
parent, rejects a removed parent and directory `LINK_MAX`, checks whether the
name already exists, then allocates an inode from the bitmap using
`s_isearch`, inserts one directory entry using `i_last_dpos`, and initializes
`.`/`..` plus link counts for a directory.

For `CREATE` and `MKDIR`, MINIX returns `EEXIST` for the reserved names `.`
and `..` immediately after the name copy. `micros` classifies both as
`MALFORMED` only after its earlier parent/mode/capacity checks.

`micros` resolves the parent and validates mode/scalars, scans from node slot
zero for a free node, and checks parent link capacity before it copies the
name grant. It validates name bytes and scans the complete global node table
for a duplicate only afterward.

The non-failing post-preflight publication and no-partial-namespace guarantee
are compatible safety extensions. Three differences are blocking:

- free-node search restarts at zero instead of retaining a search cursor;
- current parent/mode/capacity checks can mask the earlier grant/name result;
- current node-capacity failure precedes duplicate lookup, while MINIX checks
  parent/link state and duplicate existence before inode allocation.

See B10-F003 and B10-F005.

### B10-O06 Sparse block mapping, read, and write

MINIX MFS stores ten inode zone words. Its mapping algorithm uses words zero
through six as seven direct zones, word seven as one single-indirect zone, and
word eight as one double-indirect zone; the tenth stored word is not traversed
by `read_map()`/`write_map()`. Mapping resolves at most two indirect levels.
`new_block()` retains per-inode `i_zsearch`; `alloc_zone()` uses the
superblock `s_zsearch` bitmap origin. Read/write advances by block-bounded
chunks, uses block/checked-copy primitives, reads holes as zero, zeroes new
blocks, and grows file size.

`micros` uses one direct 64-entry logical-to-physical array in every node and
one 64-block resident arena. The flat map, no metadata zones, and no
persistent buffer cache are part of the SOM-05 staged substitution; the fixed
resident arena and pointer-free access are required adaptations inside that
stage. Failure-atomic whole-request staging is a compatible safety extension.
None is hidden certification of the final MFS data layout.

Within that substitute, however:

- every missing block search restarts at block zero;
- each read byte independently recomputes logical block and block offset;
- each written byte repeats the same mapping arithmetic;
- every new block is cleared through a byte loop; and
- every nonzero staged path/name/data transfer clears all `s` scratch bytes,
  even when `x` is short.

```text
micros read excluding validation/Vgrant:
    O(x + s)

micros write excluding validation/Vgrant:
    O(missing_blocks*b + missing_blocks*z + x + s)
```

The zero-origin allocator is B10-F003. Byte-at-a-time data movement and
capacity-wide scratch clearing are B10-F004.

### B10-O07 Directory enumeration

MINIX stores `.` and `..` as ordinary directory entries and uses the physical
directory-entry position as the cursor. It scans only that directory's slots,
skips holes, emits complete variable-size records, and leaves the cursor at
the first record that did not fit.

`micros` synthesizes `.` and `..`, then interprets cursor `2 + slot` across
the complete global node table. It skips every free or unrelated node and
emits fixed 128-byte records in global allocation-slot order.

The fixed pointer-free backend record is a required wire adaptation and
hard-link/mutable-directory breadth remains SOM-12. The global-table scan and
absence of separate directory entries are B10-F002.

```text
expected complete MINIX directory scan:
    O(D + (y + 1)*(1 + C/128) + output bytes)

worst complete MINIX directory scan:
    O(D + (y + 1)*C + output bytes), C <= 512

complete micros directory scan: O(n + output bytes)
```

Here the MINIX directory-slot term is `D`, not only the `d` live entries;
unused physical slots are scanned and skipped. Each emitted live entry also
performs an inode-hash lookup; cache-miss storage I/O remains SOM-05.

### B10-O08 Complete state-validation cadence

`micros_ramfs_handle_call()` runs `Vram` before decoding every request and
again after every dispatched operation, including stable operation errors.
Successful lookup, create/mkdir, and write additionally execute `Vram*`
after the grant copy and immediately before their local commit.

| Operation/result class | Complete validations |
| --- | ---: |
| bad protocol, foreign caller, or wrong phase | `1*Vram` |
| mount, read, getdents, putnode, or ordinary operation error | `2*Vram` |
| successful lookup, create, mkdir, or write | `2*Vram + Vram*` |

At the canonical initial state, the two-validation paths inspect at least
524,288 free-data/scratch bytes. Successful lookup/create/mkdir and a write
that allocates no block inspect at least 782,336 such bytes. A successful
write allocating `r` new blocks inspects:

```text
782336 - r*4096 bytes, where 0 <= r <= 2
```

so its universal initial-state lower bound is 774,144 bytes. These counts
precede metadata comparisons and exclude `Vgrant`.

MINIX checks the selected message, inode, directory, bitmap, block, and
reference state. It does not prove the complete filesystem namespace,
link-count graph, block reverse map, every free block's zero contents, and
scratch page before and after one ordinary operation. See B10-F001.

### B10-O09 Node and block allocation

Because version 1 has no removal, live nodes and allocated blocks form compact
low-index prefixes. Despite that invariant:

- `ramfs_find_free_node()` scans from slot zero for every creation; and
- `ramfs_find_free_block()` scans from block zero for every allocated block.

For `q` additional nodes and `k` additional blocks:

```text
node allocation work:
    O(q*a + q^2)

block allocation work:
    O(k*allocated_blocks + k^2)
```

MINIX retains `s_isearch` for inode bitmap allocation, `s_zsearch` for global
zone allocation, `i_zsearch` for per-file locality, and `i_last_dpos` for
directory insertion. Bitmap searches resume at those origins and the in-core
inode slot comes from an O(1) free/LRU queue.

With fixed bitmap-word width `W`, monotonic MINIX bitmap allocation has
amortized `O(1)` word-local work per allocation. A fragmented or full bitmap
can still wrap and inspect every map word:

```text
MINIX inode/zone allocation worst case:
    O(capacity/W + W)
```

`i_last_dpos` reduces the `ENTER` search for a reusable/append slot and is
amortized constant in the no-deletion append case, but the preceding
duplicate `LOOK_UP` still scans the selected directory in `O(D + d*m)`. The
complete MINIX create path is therefore not O(1).

See B10-F003.

### B10-O10 Copy, clear, and short-operation cost

MINIX uses architecture/generic block `memcpy`/`memset`, block-bounded
read/write chunks, and bounded directory-output buffers.

`micros` uses source-level byte loops for:

- complete 281,368-byte state initialization;
- node, message, result, scratch, and block clearing;
- seed payload installation;
- read assembly and write publication; and
- fixed directory-record names.

A one-byte file read or name operation still clears a 4,096-byte scratch page.
The wire little-endian codecs and actual character/name comparisons are
semantic byte operations; general state, block, payload, and transfer
movement is not. See B10-F004/F006.

### B10-O11 Deferred MINIX/POSIX filesystem breadth

Current RAMFS lacks:

- DS/RS publication, endpoint replacement, recovery, and reconstruction;
- a block layer, persistent MFS image, cache, writeback, journal, fsck, and
  crash/reboot durability;
- dynamic VM/file-backed mappings and page-cache integration;
- credentials, search/access policy, ownership, and mode transitions;
- unlink, rmdir, rename, hard/symbolic links, truncate, stat/statvfs,
  timestamps, device nodes, pipes/FIFOs, locks, and durable sync;
- multiple/nested mounts, unmount, and mount-crossing topology;
- public POSIX filesystem/libc interfaces; and
- worker-pool or asynchronous filesystem transport.

| Omission family | Exact owner |
| --- | --- |
| DS/RS publication, replacement, and recovery | SOM-01 |
| Persistent inode/block allocation, cache/writeback, physical reclaim, mount topology, and durability | SOM-05 |
| Dynamic/file-backed VM regions and fault mechanism | SOM-04 |
| POSIX mapping/shared-memory interface built on that mechanism | SOM-14 |
| Credentials and access policy | SOM-11 |
| User-visible links, removal, rename, truncate, metadata, devices, pipes/FIFOs, locks, and sync | SOM-12, which already depends on SOM-05 |
| Public filesystem/libc interfaces | SOM-17 |
| Asynchronous/nonblocking filesystem transport | SOM-23 |

No new staged-omission row is required by B10.

## Difference summary

| Area | Result |
| --- | --- |
| VFS-only synchronous authority and one mount | Baseline parity |
| Static generation-safe VFS endpoint/reply-token transport | Required adaptation |
| Volatile filesystem, flat block map, and no persistent cache/device | Staged substitution under SOM-05 |
| Pointer-free seed and fixed resident arena inside RAMFS | Required adaptation |
| Generation-safe resident node handles | Compatible extension |
| Complete runtime validation cadence | Divergence requiring correction |
| Namespace/directory-entry topology, lookup dimension, and confinement prewalk | Divergence requiring correction |
| Node/block/directory search cursors | Divergence requiring correction |
| Sparse read/write semantics | Baseline parity |
| Whole-request failure-atomic staging | Compatible extension |
| Path/name grant and capacity failure order | Divergence requiring correction |
| Copy/clear and block-span algorithms | Divergence requiring correction |
| Seed-validation/initialization algorithm | Divergence requiring correction |
| Full persistent/POSIX filesystem breadth | Staged substitution |

## Time, stack, and auxiliary-space envelope

Persistent B10-owned target state includes:

```text
one RAMFS node:                    232 bytes
64-node table:                  14,848 bytes
complete mutable RAMFS state:  281,368 bytes
current canonical seed image:    8,269 bytes
maximum seed image:            270,400 bytes
maximum state plus seed:       551,768 bytes
```

The mutable state contains:

```text
file-data arena:                262,144 bytes
scratch page:                     4,096 bytes
block owner/reverse maps:           256 bytes
```

The maximum retained seed plus data arena plus scratch remains the Accepted
536,640-byte RAMFS adaptation; node and reverse-map metadata account for the
rest of the complete state above.

Representative automatic storage is:

```text
seed allocation plan:               268 bytes
decoded request:                      80 bytes
reply action:                         80 bytes
IPC message:                          64 bytes
runtime validator seen-block set:     64 bytes
```

The service uses one 4 KiB user stack. Seed validation/initialization nests
the 268-byte plan with parser locals. Ordinary operations nest the decoded
request, result fields, and reply action around B05 grant syscalls. No direct
target stack-usage evidence covers the complete service/runtime nesting.

The MINIX MFS persistent inode table, inode hash/LRU, bitmaps, buffer cache,
and disk image are not memory-comparable to the staged volatile RAMFS. Their
algorithmic roles remain relevant even where their storage is deferred.

## Staged omissions

| ID | B10 relation |
| --- | --- |
| SOM-01 | Filesystem publication, restart, reconstruction, and dependency recovery |
| SOM-04 | Owner of dynamic/file-backed VM regions and page-fault mechanism |
| SOM-05 | Persistent inode/block allocation and reuse, cache/writeback, physical reclaim, mount topology, and durability |
| SOM-11 | Credentials, users/groups, permissions, and access checks |
| SOM-12 | User-visible link/unlink/rename/truncate and metadata APIs, devices, pipes/FIFOs, locks, and sync; depends on SOM-05 persistent mechanisms |
| SOM-14 | Owner of the public POSIX mapping/shared-memory interface built on SOM-04 |
| SOM-17 | Public POSIX filesystem headers, libc calls, errno, and ABI wrappers |
| SOM-23 | Asynchronous/nonblocking filesystem transport where later concurrency requires it |

## Evidence gaps

| ID | Missing evidence | Owner |
| --- | --- | --- |
| B10-E001 | No counter records `Vram`/`Vram*` calls, sibling comparisons, node/block scans, free-data bytes, or scratch bytes per operation | B14 |
| B10-E002 | No lookup/create/getdents probe compares ancestry prewalks/global-node scans with incremental root handling, physical directory slots, live entries, and inode-hash traversal across deep sparse trees | B14 |
| B10-E003 | No allocation trace compares zero-origin node/block scans with retained MINIX inode/zone/directory search origins | B14 |
| B10-E004 | No transfer-work counter distinguishes per-byte map/divide/copy/clear work from block-span movement | B14 |
| B10-E005 | No matrix covers invalid grant/node/capacity/duplicate/reserved-name create ordering or missing-first/overlong-later lookup-component ordering | B14 |
| B10-E006 | No seed-validation counter records image bytes, pairwise name comparisons, state-clear bytes, or payload-copy bytes | B14 |
| B10-E007 | No structural evidence distinguishes the staged flat map from MINIX direct/single/double-indirect zones and buffer cache | B14 after SOM-05 design |
| B10-E008 | No direct 4 KiB RAMFS user-stack evidence covers seed initialization and worst request/grant nesting | B14 |

## Findings register

### B10-F001: Complete filesystem validation surrounds every ordinary request

- Severity: High
- `micros` evidence:
  - `ramfs_validate_state`
  - `micros_ramfs_state_validate`
  - `micros_ramfs_handle_call`
  - precommit validation in lookup, create/mkdir, and write
- MINIX evidence: selected `fsdriver` message checks and selected inode,
  directory, bitmap, block, and reference checks
- Difference: one request repeatedly proves all namespace parent/name
  relationships, all link counts, all logical/reverse block mappings, every
  free block's 4,096 zero bytes, and the complete scratch page.
- Impact: ordinary operations pay `2*Vram`; successful lookup/create/mkdir/
  write pay `2*Vram + Vram*`. At the canonical initial state this includes at
  least 524,288 byte checks for two-validation paths, 782,336 for
  lookup/create/mkdir and nonallocating writes, and at least 774,144 for a
  write allocating up to two blocks.
- Smallest correction boundary: retain complete state validation as an
  explicit startup/diagnostic operation; ordinary requests use one selected
  node/directory/block/reference plan and path-local invariants, with exact
  prevalidated commit helpers.
- Dependencies: explicit ADR-0048 supersession, B05-F001, and B14 validation
  counts.

### B10-F002: Namespace operations use global scans and a full ancestry prewalk

- Severity: High
- `micros` evidence:
  - embedded `parent` and `name` in `struct micros_ramfs_node`
  - `find_child`
  - `is_ancestor`
  - `directory_entry_for_cursor`
  - `build_directory_records`
- MINIX evidence:
  - separate `struct direct` directory slots
  - `search_dir`
  - physical directory-position `fs_getdents`
  - inode hash/cache resolution after a directory hit and for each emitted
    getdents entry
- Difference: lookup/create scans all `n` nodes for a child of one directory,
  and getdents scans the global slot space. Every lookup first walks the
  complete start-to-root parent chain even when the path is empty or `.`.
  MINIX scans only the selected directory's entries, maps the selected inode
  separately, and handles root confinement incrementally when `..` is
  encountered rather than through a full ancestry preflight.
- Impact: component lookup is `O(n + d*m)` rather than
  `O(D + d*m + h_parent + h_child)`. Getdents is `O(n + output bytes)`
  rather than expected
  `O(D + (y + 1)*(1 + C/128) + output bytes)` with a
  `O(D + (y + 1)*C + output bytes)`, `C <= 512`, hash-chain worst case. The
  direct resident handle removes cache-hash work, but the global namespace
  input dimension and topology still do not reproduce the baseline. Empty or
  `.` lookup additionally pays `O(depth)` where MINIX has no ancestry walk.
- Smallest correction boundary: introduce one bounded per-directory entry
  representation with independent node identity, directory-position cursor,
  and selected-directory lookup/insertion. Remove the complete valid-path
  ancestry prewalk and preserve confinement through MINIX-style incremental
  root handling; any stronger ancestry proof remains an explicit diagnostic
  or separately accepted indexed extension. Retain generation-safe node
  handles and stage multi-link mutation operations under SOM-12.
- Dependencies: explicit ADR-0048 supersession and B14 sparse-tree lookup/
  enumeration evidence.

### B10-F003: Node and block allocation restart from zero instead of retained search origins

- Severity: High
- `micros` evidence:
  - `ramfs_find_free_node`
  - `ramfs_find_free_block`
  - no allocation/search cursor in `struct micros_ramfs_state`
- MINIX evidence:
  - `s_isearch`
  - `s_zsearch`
  - per-inode `i_zsearch`
  - per-directory `i_last_dpos`
  - inode unused/LRU queue
- Difference: every node and physical block allocation scans from zero even
  though no current operation frees either compact prefix.
- Impact: repeated creation costs `O(q*a + q^2)` and repeated block
  allocation costs `O(k*allocated_blocks + k^2)`, instead of resuming bitmap/
  directory searches near the last result and taking an in-core slot from a
  free queue. MINIX monotonic bitmap allocation is amortized O(1) at fixed
  word width but retains `O(capacity/W + W)` fragmented/full-map worst cases;
  duplicate directory lookup remains `O(D + d*m)`.
- Smallest correction boundary: preserve validated next-search origins for
  node, global block, per-file block locality, and per-directory insertion;
  use bitmap/word or equivalent bounded allocation state rather than repeated
  prefix scans. Keep generation exhaustion and explicit no-space failure.
- Dependencies: explicit ADR-0048 supersession, B10-F002, and B14 operation
  counters for sequential and fragmented future cases.

### B10-F004: File and scratch movement is byte-at-a-time and capacity-wide

- Severity: Medium
- `micros` evidence:
  - `stage_read`
  - byte loop in `commit_write`
  - `ramfs_zero_bytes`
  - full scratch/block clears in every operation
- MINIX evidence:
  - block-bounded `fs_readwrite`/`rw_chunk`
  - one `read_map`/`new_block` decision per block span
  - architecture/generic `memcpy`/`memset`
- Difference: every file byte repeats block/offset mapping and assignment;
  short operations then clear all 4,096 scratch bytes. General block, state,
  message, and record movement also uses source-level byte loops.
- Impact: a short valid operation costs `O(s)` instead of `O(x)`, and block
  crossings perform per-byte mapping work rather than one mapping plus one
  span copy. Initialization and allocation inherit the project-wide block-
  copy constant-factor regression.
- Smallest correction boundary: split transfers into at most block-bounded
  spans, map once per span, and use independently written standard-defined
  RV64 word/block copy/fill primitives with byte tails. Track and clear only
  staged authority bytes unless a complete page clear is explicitly required.
- Dependencies: B05-F006, B06-F004, B07-F005, B08-F006, B09-F007, and B14
  transfer-work evidence.

### B10-F005: Path/name and component failure precedence differs from MINIX

- Severity: Medium
- `micros` evidence:
  - pre-grant node/confinement checks in `ramfs_handle_lookup`
  - whole-path `path_components_are_bounded` before traversal
  - pre-grant free-node/link checks in `ramfs_handle_create_or_mkdir`
  - duplicate lookup only in `create_from_staged_name`
- MINIX evidence:
  - `fsdriver_getname` before starting-inode or parent lookup
  - parent/link checks, duplicate `advance()`, then `alloc_inode` in
    `new_node`
  - immediate `EEXIST` for reserved create/mkdir names `.` and `..`
  - incremental `next_name()` followed by component resolution
- Difference: a stale node, confinement error, full node table, or link/mode
  error can be returned without touching a bad grant/name. At full capacity a
  duplicate valid name returns `NO_SPACE` instead of MINIX's `EXISTS`.
  Reserved names `.` and `..` return `MALFORMED` instead of MINIX's
  `EXISTS`.
  Lookup validates every component length before traversal, so
  `/missing/<overlong>` returns `RANGE`; MINIX resolves `missing` before
  parsing the later component and returns `NOT_FOUND`.
- Impact: externally visible result precedence and grant-touch behavior do
  not reproduce the pinned protocol.
- Smallest correction boundary: copy names/paths at the MINIX boundary;
  preserve parent/link-before-duplicate and duplicate-before-allocation
  ordering for create/mkdir; map reserved `.`/`..` creation to `EXISTS`;
  validate each lookup component only when reached, immediately before
  resolving it. Preserve overlong-name rejection and failure atomicity by
  delaying mutation, not by globally prevalidating later components or
  reordering observable errors.
- Dependencies: explicit ADR-0048 common-failure-order supersession and B14
  cross-product failure-precedence evidence.

### B10-F006: Seed acceptance and initialization use quadratic and byte-serial algorithms

- Severity: Medium
- `micros` evidence:
  - `has_duplicate_name`
  - `validate_active_entries`
  - `zero_plan`
  - `initialize_from_seed_plan`
- MINIX evidence: MFS mount reads selected superblock/root state, scans the
  complete zone bitmap in `count_free_bits`, performs no mount-time
  sibling-name uniqueness pass, and uses block copy/fill primitives
- Difference: each non-root seed record scans all earlier records for a
  sibling duplicate, giving `O(e^2*m)`. Runtime initialization clears the
  complete 281,368-byte state and copies every payload byte through explicit
  byte loops.
- Impact: one-time RAMFS startup has quadratic record work plus byte-serial
  state/payload work unrelated to the pointer-free seed requirement. MINIX
  mount separately pays its `O(Mz)` zone-bitmap accounting; this finding does
  not claim that persistent mount is constant-time.
- Smallest correction boundary: validate sibling uniqueness with one bounded
  indexed/sorted parent-name plan whose worst-case comparison envelope is
  explicit, and use block copy/fill primitives for plan/state/payload
  publication. Retain complete digest, extent, parent, capacity, and reserved-
  byte validation.
- Dependencies: B10-F004 and B14 seed comparison/movement counters.

## Unit conclusion

B10 primary-path and operation coverage is complete for the pinned tree. The
audit adds B10-F001 through B10-F006 as blocking findings, confirms B05-F001/
F006, B06-F004, B07-F005, B08-F006, and B09-F007. Deferred ownership uses
SOM-01, SOM-05, SOM-11, SOM-12, SOM-17, and SOM-23 directly; SOM-04 owns the
file-backed VM mechanism and SOM-14 owns its dependent public mapping
interface.

B10 is **audited but not certified**. The earlier behavior-only VFS/MFS ledger
remains useful for successful-path semantics and staged scope, but B10-F005
supersedes its failure-order/result classification, and its conclusion that
Step 12 had no correction divergence is not structural certification under
ADR-0080. No B10 correction may start during the read-only audit.

## References

- [ADR-0008](../adr/0008-ramfs-first-filesystem.md)
- [ADR-0048](../adr/0048-ramfs-service-and-vfs-filesystem-protocol.md)
- [ADR-0066](../adr/0066-ramfs-source-decomposition.md)
- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)
- [B05 grant/user-execution audit](minix-complete-baseline-b05-grants-user-execution.md)
- [B06 static-bootstrap audit](minix-complete-baseline-b06-static-bootstrap.md)
- [B09 TTY audit](minix-complete-baseline-b09-tty.md)
- [MINIX VFS/MFS protocol study](minix-vfs-mfs-filesystem-protocol.md)
- [MINIX RAMFS source-organization study](minix-ramfs-source-organization.md)
- [RAMFS source organization](../architecture/ramfs-source-organization.md)

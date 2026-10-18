# B11 VFS Baseline Audit

## Status

```text
audit unit:       B11 VFS
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; six blocking findings; not certified
implementation:   forbidden during complete-audit phase
```

B11 audits VFS mount ownership, process records, descriptors, shared open-file
descriptions, vnodes, namespace routing, application/RAMFS/TTY transfer,
directory translation, asynchronous console state, close/detach/exit cleanup,
the local spawn transaction, service dispatch, and complete state validation.

## Exact primary path manifest

B11 has 15 primary paths in the pinned tree.

### Public contract

| Path | Responsibility |
| --- | --- |
| `include/micros/vfs.h` | Application and PM-control message/result ABI, access/open flags, and service tuple |

### Portable VFS owners

| Path | Responsibility |
| --- | --- |
| `servers/vfs/vfs_console.c` | TTY submit/retry/collect/cancel, notification, pending I/O, and cleanup debt |
| `servers/vfs/vfs_core.c` | Public mount/trusted/application/control/notification facade and validation cadence |
| `servers/vfs/vfs_core.h` | Complete VFS state, records, operations, callbacks, and public portable API |
| `servers/vfs/vfs_internal.h` | Private state, ownership, transfer, namespace, I/O, process, console, and spawn contracts |
| `servers/vfs/vfs_io.c` | Close, regular read/write, getdents translation, descriptor transfer routing |
| `servers/vfs/vfs_namespace.c` | Mount, path lookup, vnode integration, open/create, mkdir, and chdir |
| `servers/vfs/vfs_ownership.c` | Process/open-file/vnode lookup, free-slot search, counts, release, and descriptor resolution |
| `servers/vfs/vfs_process.c` | Attach/share/detach/exit and process-owned cleanup |
| `servers/vfs/vfs_spawn.c` | Executable image, descriptor, hidden-publication, rollback, and release transitions |
| `servers/vfs/vfs_state.c` | Byte primitives, complete state validation, and initialization |
| `servers/vfs/vfs_transfer.c` | Application copy/validate and backend grant create/revoke boundaries |

### Service owners

| Path | Responsibility |
| --- | --- |
| `servers/vfs/vfs_service_core.c` | Configuration, wire codecs, RAMFS/TTY adapters, message classification, and portable dispatch |
| `servers/vfs/vfs_service_core.h` | Service endpoint/protocol/reply contracts |
| `servers/vfs/vfs_service.c` | Target runtime adapters, static state, mount/readiness, and reply/receive loop |

## Cross-owner source dependencies

| Path family | B11 operation | Primary/later owner |
| --- | --- | --- |
| resident mappings, operation-15 range validation, page ownership | B11-O01/O05 transfer and service image | B02/B05/B07 |
| process/thread scheduling and endpoint generation | B11-O02/O06 caller/pending ownership | B03/B04 |
| grants, checked copy, create/revoke, runtime wrappers | B11-O04-O08 both data hops | B05 |
| static manifest, release/readiness, fatal service loss | B11-O01 service lifecycle | B06 |
| PM process identity, spawn, exit, rollback | B11-O02/O07/O08 lifecycle authority | B08/B12 |
| TTY pending protocol, request IDs, cancellation, and events | B11-O06 console forwarding | B09 |
| RAMFS mount, nodes, file data, records, and `PUTNODE` | B11-O01/O03-O05 backend authority | B10 |
| complete ELF/application lifecycle | B11-O08 cross-owner integration | B12 |
| init/shell/basic-command consumers | Application-visible VFS behavior | B13 |
| native/model/QEMU and recipe evidence | All B11 operations | B14 |

## Fixed MINIX source evidence

The B11 comparison uses only local source:

- `minix/servers/vfs/main.c`
- `minix/servers/vfs/fproc.h`
- `minix/servers/vfs/const.h`
- `minix/servers/vfs/filedes.c`
- `minix/servers/vfs/file.h`
- `minix/servers/vfs/vnode.c`
- `minix/servers/vfs/vnode.h`
- `minix/servers/vfs/path.c`
- `minix/servers/vfs/path.h`
- `minix/servers/vfs/open.c`
- `minix/servers/vfs/stadir.c`
- `minix/servers/vfs/read.c`
- `minix/servers/vfs/write.c`
- `minix/servers/vfs/cdev.c`
- `minix/servers/vfs/misc.c`
- `minix/servers/vfs/exec.c`
- `minix/servers/vfs/request.c`
- `minix/servers/vfs/mount.c`
- `minix/servers/vfs/vmnt.c`
- `minix/servers/vfs/vmnt.h`
- `minix/servers/vfs/worker.c`
- `minix/servers/vfs/dmap.c`
- `common/lib/libc/arch/arm/string/memcpy.S`
- `common/lib/libc/arch/arm/string/memset.S`

## Logical variables

| Variable | Meaning |
| --- | --- |
| `P` | VFS process-record capacity, 64 |
| `D` | descriptors per process, 16 |
| `F` | global open-file-description capacity, 1,024 |
| `V` | VFS vnode capacity, 64 |
| `p` | active VFS process records |
| `o` | active open-file descriptions |
| `v` | active vnodes |
| `Zpending` | pending-record bytes checked as canonical zero in the current pending state |
| `Zspawn` | spawn transaction/buffer bytes checked as canonical zero in the current spawn state |
| `Zchild` | duplicate zero-byte check of one reserved free child process during descriptor preparation |
| `A` | public spawn descriptor actions, at most 8 |
| `R` | maximum retained spawn-request bytes, 4,096 |
| `E` | maximum retained executable bytes, 131,072 |
| `x` | one application/path/data transfer, at most 4,096 bytes |
| `Vvfs` | complete VFS-state validation |
| `Vgrant` | B05 complete grant relation/range/copy validation |
| `Vram` | B10 complete RAMFS validation inside one backend call |
| `Vtty` | B09 complete TTY service/terminal validation inside one backend call |

## Complete validation cost definition

One complete VFS validation:

1. scans all `P` process records and compares every active endpoint with later
   records;
2. scans all `F` open-file records and all active-process descriptors;
3. for every active open-file description, rescans all `P*D` descriptors to
   recompute its user count;
4. scans all `V` vnodes and compares every active backend node with later
   vnodes;
5. for every active vnode, rescans all `P` process roots/cwds and all `F`
   open-file descriptions to recompute local ownership;
6. validates the global pending operation and page owners; and
7. when spawn is idle, byte-scans the 104-byte transaction, all `R` request
   bytes, and all `E` executable bytes for canonical zero.

```text
Vvfs = O(
    P*process_bytes + p*P
    + F*open_file_bytes + p*D + o*P*D
    + V*vnode_bytes + v*V + v*(P+F)
    + R + E + P
)
```

When spawn is active, validation additionally decodes the retained request,
plans the initial stack, checks descriptor actions, and scans the unused
request/executable tails; its envelope remains capacity-wide in `R + E`.

Immediately after mount and before any process attach, one `Vvfs` byte-checks
at least:

```text
64*44 free process bytes
+ 1024*32 free open-file bytes
+ 63*40 free vnode bytes
+ 3 active root-vnode reserved bytes
+ 56 pending bytes
+ 104 idle spawn bytes
+ 4096 request bytes
+ 131072 executable bytes
= 173435 bytes
```

This excludes scalar checks, descriptor scans, root-vnode ownership scans, and
pairwise comparisons. At full open-file use, the open-file reference proof
alone performs:

```text
F*P*D = 1024*64*16 = 1048576 descriptor comparisons
```

The zero-byte volume is state-dependent:

```text
Zbytes =
    44*(P-p)
    + 32*(F-o)
    + 40*(V-v)
    + 3*v
    + Zpending
    + Zspawn
    + Zchild
```

`Zpending` is 56 for `NONE` and smaller for active/debt forms. `Zspawn` is
135,272 while idle and becomes the validated unused tails plus reserved
fields while active. `Zchild` is 44 only while a descriptor-prepared child
record remains free and is checked once by the process scan and again by the
spawn validator. After one process is attached with three console open-file
descriptions and spawn remains idle, `Zbytes = 173295`.

## Operation ledger

### B11-O01 Static VFS service, RAMFS mount, and readiness

Both systems make VFS the application-facing authority, mount one root
filesystem before ordinary use, retain one root vnode/backend reference, and
route backend requests through synchronous filesystem calls.

`micros` uses fixed generation-safe service endpoints, one RAMFS backend,
fixed messages, direct grants, and one static readiness chain; those are
required adaptations. Dynamic service/filesystem discovery and recovery are
staged under SOM-01. Persistent backends and mount-table topology are staged
under SOM-05.

`micros_vfs_mount()` executes `2*Vvfs` around one RAMFS mount call. The initial
pre-mount validation scans 173,472 canonical-zero bytes; the post-mount
validation scans the 173,435 bytes above. MINIX validates the
selected mount/vnode state rather than every process, filp, vnode, pending,
spawn, request-buffer, and executable-buffer relationship. Production service
startup performs one further explicit `Vvfs` after mount and before readiness.

### B11-O02 Caller identity and VFS process records

MINIX derives the process slot directly from the kernel-written source
endpoint and selects `fproc[slot]`, then verifies that the complete stored
endpoint generation matches.

`micros` keeps process-record slots logically separate from endpoint slots,
but every application, trusted attach/share/detach, spawn-parent, and exit
lookup calls `vfs_find_process()`, which scans all `P` records by endpoint.
Free-record allocation also restarts at record zero rather than using the
already supplied child endpoint slot or a retained cursor.

The spawn path selects a free record during `PREPARE_DESCRIPTORS`, before
`COMMIT_HIDDEN` supplies the child endpoint. Restoring direct endpoint-slot
placement therefore also requires deferring child-record publication until
that later command.

```text
MINIX caller selection: O(1)
micros caller selection: O(P)
```

Distinct semantic identity and generation validation do not require a linear
lookup. See B11-F002.

### B11-O03 Descriptors, open-file descriptions, and vnodes

Both systems preserve:

- process-local descriptor indexes;
- global shared open-file descriptions with access/flags/count/position;
- distinct VFS vnodes with local and backend reference counts;
- one mount-root pin;
- linear free filp/open-file and free vnode scans;
- linear vnode-cache identity lookup; and
- batched backend `PUTNODE`.

MINIX has 1,024 filps and 1,024 vnodes. `micros` retains 1,024 open-file
descriptions but bounds vnodes to the 64-node RAMFS backend. The smaller
capacity is a required adaptation inside the current bounded RAMFS stage, and
the final persistent/multi-filesystem capacity is staged under SOM-05. The
direct backend handle is a required wire adaptation; stale-generation
rejection is a compatible extension. The linear allocation/cache algorithm
family is baseline parity.

One blocking difference remains in backend reference cleanup. MINIX increments
`v_fs_count` on a cache hit and, during a later `put_vnode()` with remaining
local users, reduces it only when the count is greater than 256. `micros`
cleans immediately during lookup/create integration when increment reaches
exactly 256, sending `PUTNODE(255)` before the new owner is published.

Failure policy also differs, but compatibly: MINIX logs/continues after a
backend `PUTNODE` failure during threshold or final cleanup and still clears
the final vnode. `micros` treats any failed or malformed backend release as a
fatal invariant violation and does not shape it as application success.

See B11-F003.

### B11-O04 Path routing, open, mkdir, and chdir

MINIX copies the application path, preflights a descriptor and filp, reserves
a scratch vnode before backend lookup even when the target may be cached, and
uses a second free vnode while an uncached parent and new child may coexist.
It performs component traversal in VFS to handle mounts, symlinks, and policy.

`micros` copies the path before descriptor/open-file/vnode capacity preflight,
matching the top-level MINIX copy boundary. It delegates the complete
single-filesystem walk to RAMFS, passes root/cwd plus confinement root, and
preflights one scratch vnode. Missing-target create requires two simultaneously
free vnode slots, matching the MINIX worst-case parent-plus-child envelope
even though the whole-path RAMFS adaptation does not cache the temporary
parent as a VFS vnode.

Whole-path delegation to the sole backend is a required adaptation inside the
one-root stage. Persistent/multiple mount topology is staged under SOM-05,
credential traversal under SOM-11, and symlink/general namespace operations
under SOM-12.

The selected MINIX build uses historical pathname handling and strips trailing
separators before final-component resolution. `micros` preserves a trailing
separator as a directory requirement and returns `NOT_DIRECTORY` rather than
creating or opening a regular file through that spelling. This is the
required adaptation fixed by ADR-0049 and the accepted RAMFS contract.

MINIX provisionally publishes the descriptor and filp before later access and
type checks, then removes them on expected failure. `micros` retains all local
state privately until target/type/access validation succeeds and publishes
the descriptor last. The delayed publication is a compatible
failure-atomicity extension.

For `CHDIR`, MINIX `change_into()` releases the old cwd reference, duplicates
the already resolved new vnode, and publishes the pointer last. ADR-0049
records the same release-before-publication order. `micros` integrates the new
backend/local reference, writes `working_directory_vnode`, and only then
releases the old vnode. See B11-F006.

### B11-O05 Regular I/O and directory translation

MINIX resolves the selected descriptor/filp/vnode, passes one transitive magic
grant to the filesystem, and commits the shared position from the accepted
result. Getdents uses the same filp position and native directory records.

`micros` must terminate the application grant, stage through resident pages,
create a second direct RAMFS grant, and translate 128-byte backend records
into 80-byte application records. Grant-range preflight before a consuming
read hop and the resident two-hop transfer are required adaptations.
All-or-error application copies and position commit only after the second hop
are compatible safety extensions.

The extra data hop is `O(x)` and invokes B05 grant validation/copy on both
boundaries plus B10 RAMFS work. Record translation is linear in complete
records and preserves cursor publication only after the application copy.

### B11-O06 Synthetic console and asynchronous pending state

MINIX retains character-device state per process and uses workers plus
asynchronous driver replies so unrelated VFS calls can continue.

`micros` uses one synthetic console object and one global pending-operation
slot. While it is nonempty, exact TTY events and PM cleanup proceed, but every
otherwise valid application open/close/read/write/getdents/mkdir/chdir returns
`BUSY`, including unrelated processes and regular files.

The VFS-owned synthetic object is a required v0.1 adaptation replacing MINIX
character nodes and device maps. Named device/descriptor behavior remains
SOM-12, controlling-terminal ownership SOM-07, and complete terminal/device
behavior SOM-13.

The one global pending slot is a staged substitution for the absent worker/
asynchronous transport rather than baseline concurrency. SOM-23 owns the
kernel transport; SOM-12 owns application nonblocking/select/descriptor
breadth. TTY request-ID ordering and resident-output retry differences remain
B09-F004/B09-F006.

### B11-O07 Close, detach, and exit cleanup

Both systems:

1. resolve process-owned pending work;
2. clear descriptors before final open-file release;
3. decrement shared open-file references;
4. release one vnode reference at final RAMFS open-file release;
5. release cwd and root references; and
6. clear the process record last.

`micros` cleanup may leave one exact TTY notification-debt state while every
process-owned grant, reply token, descriptor, and bounce-page owner is already
released. That is a required adaptation to the accepted TTY notification
protocol. PM/VFS cross-owner exit ordering remains B12; B11 owns the local
reference/grant cleanup algorithm.

### B11-O08 VFS-local spawn transaction

The pinned VFS source owns executable buffering, descriptor preparation,
hidden process publication, rollback, release, and exit-control storage.
Complete cross-owner spawn/rollback/exit behavior belongs to B12.

B11 records the local cost that every `Vvfs`:

- scans the complete 4,096-byte request buffer and 131,072-byte executable
  buffer when idle;
- while active, decodes the retained request and replans its stack;
- recomputes descriptor/vnode ownership including reserved child state.

Public `PREPARE_DESCRIPTORS` separately processes `A` descriptor actions.
For each action it rescans all earlier actions to count how many name the same
open-file description before checking reference-count overflow:

```text
micros public descriptor preparation: O(A^2)
MINIX inherited descriptor pass:       O(D)
```

The quadratic term is local preparation work, not part of `Vvfs`. See
B11-F005.

The full executable buffering and PM/VM/VFS transaction mechanism are
classified in B12. B11-F001 owns their multiplication through ordinary VFS
validation.

### B11-O09 Complete validation cadence

Valid application dispatch, PM control, TTY notification, mount, trusted
attach/share/detach, and test-drain operations run `Vvfs` before and after the
operation whenever the handler returns normally.

| Request class | Complete validations |
| --- | ---: |
| bad/foreign application or PM-control service envelope, or malformed/foreign TTY notification | `0` |
| valid application wire rejected by core caller lookup or VFS phase | `1*Vvfs` |
| dispatched application result, valid PM control including phase rejection, valid TTY event, or trusted transition | `2*Vvfs` |

The zero-byte volume for an ordinary successful request is
`Zbytes(pre_state) + Zbytes(post_state)`, before counting table relationships,
grants, RAMFS, or TTY work. For an operation that preserves the
console-attached, idle-spawn shape it is:

```text
2*173295 = 346590 bytes (about 338.5 KiB)
```

Active process/open-file/vnode records and active spawn tails change this
byte volume according to the formula above; they do not remove the
capacity-wide scalar and relationship scans in `Vvfs`.

Production startup performs the mount facade's two validations and one
additional explicit post-mount validation before readiness:

```text
173472 + 2*173435 = 520342 canonical-zero bytes
```

The QEMU fixture additionally surrounds its trusted attach with two more
complete validations.

MINIX updates selected counters and relationships directly. Optional/debug
checks do not recompute every filp/vnode owner and scan maximum executable
storage around each ordinary call. See B11-F001.

### B11-O10 Table lookup and allocation algorithms

MINIX and `micros` both linearly scan the descriptor table, global filp/open-
file table, and vnode table for free/cached entries. Those fixed-table
algorithm families are parity, subject to staged capacity differences.

The process table is different:

```text
MINIX request/PM child record:
    direct endpoint-derived fproc slot, O(1)

micros request/attach/spawn/exit record:
    endpoint scan or lowest-free scan, O(P)
```

Complete-state reference proofs are also not ordinary allocation algorithms:
they are the B11-F001 validation divergence.

### B11-O11 Copy, clear, and buffer algorithms

MINIX uses architecture/generic `memcpy`/`memset`, direct filp/vnode updates,
and bounded request/path buffers.

`micros` uses source-level byte loops to:

- initialize the complete 184,320-byte VFS state;
- clear process/open-file/vnode/pending/spawn records;
- clear the 4,096-byte request and 131,072-byte executable buffers during
  transaction release;
- clear/build service messages and result actions; and
- translate directory names and padding record by record.

Wire codecs and name validation remain semantic byte operations. General
state, page, executable, message, and record movement is not. See B11-F004.

### B11-O12 Deferred MINIX/POSIX VFS breadth

Current VFS lacks:

- DS/RS restart, reconstruction, dynamic filesystem/driver publication;
- persistent/multiple filesystems, mount/unmount/crossing topology;
- complete credentials, permissions, ownership, and umask;
- fork and unrestricted descriptor inheritance/dup/close-on-exec;
- seek/append/truncate, links, rename, metadata APIs, devices, pipes/FIFOs,
  locks, sync, ioctl, select/poll, and nonblocking I/O;
- signals and cancellation policy;
- sessions, controlling terminals, PTYs, and general device routing;
- sockets/networking;
- dynamic/file-backed mappings and public mapping APIs;
- public POSIX VFS/libc interfaces; and
- worker-pool/asynchronous request breadth.

| Omission family | Exact owner |
| --- | --- |
| Service/driver publication, restart, reconstruction | SOM-01 |
| Persistent filesystems and mount topology | SOM-05 |
| Fork/exec descriptor inheritance and close-on-exec | SOM-06 |
| Sessions/process groups/controlling terminal | SOM-07 |
| Signals and cancellation policy | SOM-09 |
| Credentials and access policy | SOM-11 |
| Complete descriptors, namespace/metadata operations, devices, pipes/FIFOs, locks, sync, ioctl, select/poll, and nonblocking I/O | SOM-12 |
| Full terminal/PTYS/login-facing device behavior | SOM-13 |
| Dynamic/file-backed VM mechanism | SOM-04 |
| Public POSIX mapping/shared-memory interface | SOM-14 |
| Sockets/networking | SOM-16 |
| Public VFS/libc API and errno surface | SOM-17 |
| Asynchronous/nonblocking kernel transport and backpressure | SOM-23 |

No new staged-omission row is required by B11.

## Difference summary

| Area | Result |
| --- | --- |
| Process/descriptor/open-file/vnode ownership split | Baseline parity |
| Static endpoints, generation checks, fixed messages, direct grants | Required adaptation |
| Whole-path delegation to one fixed RAMFS backend | Required adaptation |
| Preserve trailing separator as a directory requirement | Required adaptation |
| Persistent/multiple mount topology | Staged substitution under SOM-05 |
| Symlink and general namespace-operation breadth | Staged substitution under SOM-12 |
| Resident two-hop transfer and grant preflight | Required adaptation |
| Failure-atomic second-hop publication | Compatible extension |
| Delayed descriptor/open-file publication after target checks | Compatible extension |
| Fatal fail-closed backend `PUTNODE` mismatch | Compatible extension |
| VFS-owned synthetic console instead of character node/device map | Required adaptation |
| One global pending console operation | Staged substitution under SOM-23 |
| Complete VFS validation cadence | Divergence requiring correction |
| Process caller/free-record lookup | Divergence requiring correction |
| Backend reference cleanup threshold/timing | Divergence requiring correction |
| Copy/clear algorithms | Divergence requiring correction |
| Public spawn descriptor preparation | Divergence requiring correction |
| Chdir cwd reference/publication order | Divergence requiring correction |
| Full POSIX/persistent/concurrent VFS breadth | Staged substitution |

## Time, stack, and auxiliary-space envelope

Persistent B11-owned target state includes:

```text
complete VFS state:               184,320 bytes
initialized service-data page:      4,096 bytes
combined state plus data page:     188,416 bytes
process table:                       2,816 bytes
open-file table:                    32,768 bytes
vnode table:                         2,560 bytes
client/backend bounce pages:         8,192 bytes
spawn request buffer:                4,096 bytes
spawn executable buffer:           131,072 bytes
```

Representative automatic storage is:

```text
decoded spawn request:                 144 bytes
application request:                    56 bytes
application result action:              48 bytes
PM control request:                     80 bytes
PM control result action:               56 bytes
service reply action:                   80 bytes
RAMFS request/response:                 72 / 40 bytes
IPC message:                            64 bytes
```

The VFS service uses one 4 KiB user stack. Namespace/create paths may nest
61-byte names, backend request/response objects, and service messages.
Spawn validation nests the decoded request and stack-layout plan around the
large resident buffers rather than copying those buffers to the stack. No
direct target stack-usage evidence covers worst request/control/cleanup
nesting.

## Staged omissions

| ID | B11 relation |
| --- | --- |
| SOM-01 | VFS/filesystem/driver publication, replacement, and recovery |
| SOM-04 | Dynamic/file-backed VM regions and fault mechanism |
| SOM-05 | Persistent filesystems and mount topology |
| SOM-06 | Fork/exec descriptor inheritance and close-on-exec |
| SOM-07 | Sessions, process groups, and controlling-terminal ownership |
| SOM-09 | Signals and cancellation policy |
| SOM-11 | Credentials, ownership, permissions, and access checks |
| SOM-12 | Complete descriptor, namespace, metadata, device, pipe/FIFO, lock, sync, ioctl, select/poll, and nonblocking behavior |
| SOM-13 | Complete terminal, PTY, login, and device-facing behavior |
| SOM-14 | Public POSIX mapping/shared-memory interface built on SOM-04 |
| SOM-16 | Sockets and networking |
| SOM-17 | Public POSIX VFS/libc interfaces and errno |
| SOM-23 | Asynchronous/nonblocking kernel transport, queues, and backpressure |

## Evidence gaps

| ID | Missing evidence | Owner |
| --- | --- | --- |
| B11-E001 | No counter records `Vvfs` calls, zero bytes, process/vnode pair comparisons, descriptor scans, or recomputed reference users per operation | B14 |
| B11-E002 | No direct-index probe compares endpoint-derived MINIX `fproc` selection with VFS process lookup, spawn-time free-record selection, deferred endpoint binding, and low-slot concentration | B14/B12 |
| B11-E003 | No backend-reference trace compares current cleanup at acquisition count 256 with MINIX cleanup after release above 256 | B14 |
| B11-E004 | No copy-work counter distinguishes byte loops from word/block state, spawn-buffer, page, and record movement | B14 |
| B11-E005 | No staged-concurrency evidence distinguishes one global `BUSY` slot from MINIX worker/per-process suspension | B14 after SOM-23 design |
| B11-E006 | No direct 4 KiB VFS user-stack evidence covers worst namespace, console, spawn-control, and cleanup nesting | B14 |
| B11-E007 | No descriptor-action counter compares quadratic same-reference rescans with one linear inheritance/preparation pass | B14/B12 |
| B11-E008 | No chdir ordering probe distinguishes old-release/new-duplicate/publication sequencing and same-vnode behavior | B14 |

## Findings register

### B11-F001: Complete VFS validation surrounds every ordinary transition

- Severity: High
- `micros` evidence:
  - `micros_vfs_state_validate`
  - `validate_processes`
  - `validate_open_files`
  - `validate_reference_counts`
  - `validate_vnodes`
  - `validate_pending`
  - `validate_spawn`
  - every public facade in `vfs_core.c`
- MINIX evidence: direct selected `fproc`, descriptor, filp, vnode, worker,
  mount, and request checks
- Difference: one operation repeatedly scans maximum-capacity process, open-
  file, vnode, pending, request, and executable storage and recomputes every
  active open-file/vnode owner relation.
- Impact: immediate post-mount validation scans 173,435 canonical-zero bytes;
  after one console-attached process with idle spawn it scans 173,295.
  Ordinary dispatched transitions scan
  `Zbytes(pre_state) + Zbytes(post_state)`. At full open-file use, one
  validation performs 1,048,576 descriptor comparisons before vnode, spawn,
  backend, grant, or device work.
- Smallest correction boundary: retain complete VFS agreement as an explicit
  startup/diagnostic operation; ordinary transitions use selected process,
  descriptor, open-file, vnode, pending, and transaction plans with
  incrementally maintained counts and exact prevalidated commits.
- Dependencies: explicit ADR-0049 complete-state-invariant supersession,
  B05-F001/F006, B08-F002, and B14 operation counters.

### B11-F002: VFS scans process records instead of using endpoint-slot identity

- Severity: High
- `micros` evidence:
  - `vfs_find_process`
  - `vfs_find_free_process`
  - application dispatch, attach/share/detach, spawn, and exit callers
- MINIX evidence:
  - `_ENDPOINT_P`
  - direct `fproc[slot]`
  - complete endpoint-generation consistency check
- Difference: exact kernel endpoint identity is compared against every active
  VFS process record, and new child/fixture records use lowest-free search
  rather than the exact endpoint slot already supplied by PM/kernel identity.
- Impact: hot dispatch and lifecycle lookup are `O(P)` instead of `O(1)`;
  repeated attach/reuse also concentrates record use at low slots.
- Smallest correction boundary: reproduce direct endpoint-slot placement,
  retain the complete endpoint generation, and keep semantic PID outside VFS
  authority. `PREPARE_DESCRIPTORS` retains only the child descriptor/root/cwd
  plan; `COMMIT_HIDDEN` validates the newly supplied endpoint slot and
  publishes the process record there. A design that instead preserves
  separate record slots must explicitly justify that adaptation and provide
  both an O(1) endpoint index and an O(1)/amortized free-record allocator.
- Dependencies: explicit ADR-0049/ADR-0050 supersession, B08-F003, B12
  spawn-transaction recertification, and B14 direct-index/free-allocation
  evidence.

### B11-F003: Backend vnode references are cleaned at a different threshold and phase

- Severity: Medium
- `micros` evidence:
  - `MICROS_VFS_BACKEND_REFERENCE_THRESHOLD`
  - `vfs_namespace_integrate_vnode`
  - validator requirement `backend_reference_count < 256`
- MINIX evidence:
  - cache-hit `v_fs_count` increment in `advance`
  - `put_vnode`
  - `vnode_clean_refs`
- Difference: `micros` sends `PUTNODE(255)` immediately when acquisition raises
  the count to exactly 256. MINIX permits the count to reach 256, and cleans
  only during a later local release when the count is greater than 256.
- Impact: backend message timing and frequency differ; `micros` can perform a
  synchronous backend release on the lookup/open acquisition path before the
  new local owner is published.
- Smallest correction boundary: reproduce MINIX acquisition and release
  timing, including the greater-than-256 condition, while retaining exact
  failure handling and the final-reference batch.
- Dependencies: explicit ADR-0049 threshold-cleanup supersession and B14
  acquisition/release traces.

### B11-F004: VFS state and large retained buffers use byte-at-a-time movement

- Severity: Medium
- `micros` evidence:
  - `vfs_zero_bytes`
  - service `clear_bytes`/`runtime_clear_bytes`
  - process/open-file/vnode/spawn cleanup
  - directory-record translation
- MINIX evidence: architecture/generic `memcpy`/`memset` and direct
  table/buffer operations
- Difference: VFS initializes a 184,320-byte state, releases up to 135,168
  spawn bytes, and moves general records/messages through source-level byte
  loops.
- Impact: startup, rollback, exit, and directory translation retain the
  project-wide byte-serial constant-factor regression; complete validation
  separately rereads the same large buffers under B11-F001.
- Smallest correction boundary: use independently written standard-defined
  RV64 word/block copy and fill primitives with alignment handling and byte
  tails; retain bytewise wire codecs and actual name validation.
- Dependencies: B05-F006, B06-F004, B07-F005, B08-F006, B09-F007, B10-F004/
  F006, and B14 transfer-work evidence.

### B11-F005: Public spawn descriptor preparation rescans prior actions

- Severity: Medium
- `micros` evidence:
  - `reserve_public_spawn_descriptors`
  - inner `prior < descriptor` same-reference count
- MINIX evidence: one linear inherited-descriptor pass in VFS fork handling
- Difference: each of `A` valid descriptor actions scans all earlier actions
  to recompute the increment count for its shared open-file description.
- Impact: descriptor preparation is `O(A^2)` rather than one `O(A)` pass.
  This cost is multiplied by surrounding B11-F001 validation and the B12
  coordinator transaction.
- Smallest correction boundary: build one linear descriptor plan that
  accumulates per-open-file increments in bounded transaction workspace,
  validates every destination/source and overflow once, then commits each
  shared reference exactly once per action.
- Dependencies: explicit ADR-0050 descriptor-preparation supersession, B12
  transaction recertification, and B14 action-comparison evidence.

### B11-F006: Chdir publishes the new cwd before releasing the old reference

- Severity: Medium
- `micros` evidence: final mutation sequence in
  `vfs_namespace_handle_chdir`
- MINIX evidence:
  - `do_chdir`
  - `change_into` in `stadir.c`
- Difference: MINIX releases the old cwd, duplicates the resolved new vnode,
  then stores the new cwd pointer. `micros` stores the new vnode reference in
  the process record before calling the potentially backend-releasing old-
  vnode path.
- Impact: mutation order does not reproduce the baseline or Accepted
  ADR-0049. An invariant failure during old-vnode release observes a
  prematurely published cwd and a different ownership boundary.
- Smallest correction boundary: retain the newly resolved reference as
  unpublished transaction state, handle the same-vnode case explicitly,
  release the old cwd, then publish the new vnode as the final non-failing
  store.
- Dependencies: ADR-0049 conformance correction and B14 mutation-order/same-
  vnode evidence.

## Unit conclusion

B11 primary-path and operation coverage is complete for the pinned tree. The
audit adds B11-F001 through B11-F006 as blocking findings, confirms B05-F001/
F006, B08-F002/F003, B09-F004/F006/F007, and B10-F004, and maps deferred VFS
breadth to existing SOM-01, SOM-04, SOM-05, SOM-06, SOM-07, SOM-09, SOM-11,
SOM-12, SOM-13, SOM-14, SOM-16, SOM-17, and SOM-23 rows.

B11 is **audited but not certified**. The earlier process/descriptor/device
study remains useful for successful-path behavior and staged scope, but its
behavior-only classifications do not certify validation cadence, direct
caller indexing, exact backend-reference cleanup timing, or copy algorithms
under ADR-0080. Complete cross-owner spawn/rollback/exit remains B12. No B11
correction may start during the read-only audit.

## References

- [ADR-0049](../adr/0049-vfs-service-and-application-io-protocol.md)
- [ADR-0050](../adr/0050-post-handoff-elf-spawn.md)
- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)
- [B05 grant/user-execution audit](minix-complete-baseline-b05-grants-user-execution.md)
- [B08 PM/spawn audit](minix-complete-baseline-b08-pm-spawn.md)
- [B09 TTY audit](minix-complete-baseline-b09-tty.md)
- [B10 RAMFS audit](minix-complete-baseline-b10-ramfs.md)
- [MINIX VFS process/descriptor/device study](minix-vfs-process-descriptor-and-device-routing.md)
- [MINIX VFS/MFS protocol study](minix-vfs-mfs-filesystem-protocol.md)
- [MINIX VFS source-organization study](minix-vfs-source-organization.md)

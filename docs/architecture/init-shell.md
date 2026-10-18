# Production Init and Shell

## Status and outcome

This is the Accepted behavioral and authority contract for
[ADR-0079](../adr/0079-production-init-shell-and-basic-commands.md). The
structural and algorithmic correction required by
[ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md) blocks
implementation through the complete B01-B14 audit and every prerequisite
blocking correction. The requirements below define the B13 contract; they do
not authorize implementation before the repository-wide gate closes.

```text
design task:         init-shell-design
implementation task: init-shell-implementation
development DAG:     node Q / Step 15
static services:     launcher, VM, PM, TTY, RAMFS, VFS
dynamic applications:
  /sbin/init
  /bin/sh
  /bin/echo
  /bin/cat
  /bin/ls
  /bin/ps
```

The measurable outcome is one clean QEMU boot that creates production init
after bootstrap sealing, runs the interactive shell over the real TTY/VFS
console, executes all four commands as foreground applications, reaps them,
reaps the shell, proves the post-init resource baseline, drains UART, and
shuts down through SBI.

## Authority map

| Owner | Authority |
| --- | --- |
| Kernel | Existing process/thread/endpoint publication, scheduler, IPC/grants, trap authentication, post-comparison test-only terminal seizure, raw pass record and final test shutdown |
| PM | PID 1 publication, parent/reaper tree, command display metadata, spawn/exit/wait, application snapshot |
| VFS | Root/cwd, console descriptors, paths, files, directory records, descriptor duplication and cleanup |
| VM | Application frame selection, loading, frozen mappings, release |
| RAMFS | Immutable userland files and directory contents |
| TTY | Canonical serial input, echo, queued output, physical drain |
| Init | One console-shell session policy and exact shell wait/restart |
| Shell | Line parsing, fixed `/bin` resolution, foreground spawn/wait, `exit` |
| Commands | Their own bounded application behavior only |

No IPC payload pointer, PID, snapshot row, path, or display name grants
authority outside its owning protocol.

## Source ownership

### Shared production owners

| Path | Responsibility |
| --- | --- |
| `include/micros/bounded_sort.h` | Project-owned comparator/context contract for the fixed MINIX/NetBSD sort family |
| `lib/algorithm/bounded_sort.c` | Independent bounded Bentley-McIlroy three-way quicksort implementation |
| `include/micros/application_client.h` | Stable typed local application-client and snapshot/directory contracts |
| `lib/spawn/spawn_request_build.c` | Sole canonical spawn-request encoder |
| `lib/application/client_common.c` | Byte codecs, reply envelope validation, exact full writes |
| `lib/application/client_pm.c` | Spawn, wait, process-list grant and result handling |
| `lib/application/client_vfs.c` | Open, close, read, write, and getdents calls |
| `lib/application/client_internal.h` | Declarations-only private client contract |

The client layer is not a libc. It owns only the exact PM/VFS operations
needed by node Q.

### PM owners

| Path | Responsibility |
| --- | --- |
| `servers/pm/pm_init.c` | Fixed production-init request and post-seal spawn policy |
| `servers/pm/pm_snapshot.c` | Command-name validation and sorted application snapshot construction |
| `servers/pm/pm_protocol.c` | Process-list decode and result encoding |
| `servers/pm/pm_service_requests.c` | Running-caller resolution, snapshot grant copy, reply selection |
| Existing lifecycle/coordinator owners | Command metadata publication/clear at their existing transition boundaries |

`pm_snapshot.c` invokes no transition and mutates no lifecycle state.
`pm_init.c` uses only the existing trusted-init coordinator.

### Userland owners

| Path | Responsibility |
| --- | --- |
| `user/init/init.c` | Application startup checks and persistent spawn/wait loop |
| `user/init/init_policy.c` | Exact shell request and status policy |
| `user/init/init_internal.h` | Narrow init core/adapter contract |
| `user/shell/shell.c` | Application frontend, console loop and client calls |
| `user/shell/shell_parse.c` | Bounded line tokenizer and command resolution |
| `user/shell/shell_internal.h` | Narrow shell parse/action contract |
| `user/commands/echo.c` | Selected echo behavior |
| `user/commands/cat.c` | Selected raw file-copy behavior |
| `user/commands/ls.c` | Directory-record validation, collection and sorting |
| `user/commands/ps.c` | PM snapshot validation and formatting |
| `user/common/text.c` | Bounded byte/string/decimal helpers shared by these applications |
| `user/common/text.h` | Declarations-only userland text contract |

Function boundaries follow tasks. No single command frontend also owns
generic PM/VFS wire code.

### Test-only owners

| Path | Responsibility |
| --- | --- |
| `tests/qemu/init_shell_init_adapter.c` | Baseline/final init checkpoints and final VFS/TTY drain |
| `tests/qemu/init_shell_protocol.h` | Exact test stages, script and pass marker |
| `kernel/tests/shell/shell_test*.c` | Trap authentication, resource capture/comparison and clean shutdown |
| Shared application-resource snapshot owner | Common exact kernel/service resource projection used by ELF-spawn and shell tests |
| `tools/generate_userland_seed.py` | Checked six-application RAMFS seed |
| Focused native tests | Builder, PM snapshot, clients, init, shell and command behavior |

Test-only sources cannot be linked into `build-userland-images`.

## Shared bounded sort

`micros_bounded_sort()` is a freestanding helper shared by PM and `ls`. Its
exact public contract is:

```c
typedef int (*micros_bounded_sort_compare)(
    const void *left,
    const void *right,
    void *context
);

enum micros_bounded_sort_result {
    MICROS_BOUNDED_SORT_OK = 0,
    MICROS_BOUNDED_SORT_ERROR_ARGUMENT = 1,
};

enum micros_bounded_sort_result micros_bounded_sort(
    void *base,
    size_t count,
    size_t width,
    micros_bounded_sort_compare compare,
    void *context
);
```

Valid arguments satisfy:

```text
base:       null is permitted only when count is zero; otherwise designates
            count * width writable bytes
count:      0..64
width:      nonzero and, when count is nonzero, <= SIZE_MAX / count
compare:    non-null deterministic total-order callback
context:    opaque caller context, possibly null
allocation: none
```

The comparator returns a value less than, equal to, or greater than zero for
left-before, equal, or left-after. It does not mutate sortable storage and its
ordering result cannot depend on call count; it may update caller-owned
instrumentation in `context`.

The implementation checks count, width, multiplication representability,
base, and comparator before any pointer arithmetic or mutation. Every
detectable argument failure returns
`MICROS_BOUNDED_SORT_ERROR_ARGUMENT` with storage unchanged. The writable
extent is a caller precondition because C cannot discover an object's size.
Once arguments are valid, sorting has no failure path. Swaps operate through
standard-defined byte copies and require no alignment. Count zero is a
successful no-op when width and comparator are valid; `base` may be null in
that case.

The comparator receives the opaque context so PM can compare slot-indexed
records and `ls` can compare name-table indexes without global sort state.

The helper sorts exactly the supplied records. Callers own candidate
construction, duplicate policy, and publication. It is not a libc or a
general unbounded container framework.

## Exact PM snapshot

The PM snapshot page is always 4,096 bytes and initially zero. At most 64
compact records are written. Records are ordered by semantic PID, not slot or
endpoint.

The selected records are represented by a contiguous lightweight entry vector
containing the process slot and resolved parent PID. The vector is ordered by
the project-owned independent implementation of the same Bentley-McIlroy
three-way quicksort family used by the fixed MINIX/NetBSD `qsort`:

1. partitions smaller than seven use insertion sort;
2. larger partitions choose the median of first, middle, and last;
3. partitions larger than 40 first take three medians of three at stride
   `count / 8`, then take their median;
4. the pivot moves to the first element;
5. four cursors perform the reference three-way partition, moving keys equal
   to the pivot to both ends before vector-swapping them beside the middle;
6. the logical left partition is completed before the right partition; and
7. the right partition uses the reference tail-iteration order.

To retain that order without placing worst-case recursive C frames on the
2 KiB application stack, the bounded implementation uses an explicit
pending-right range stack. Count is limited to 64; each range uses one-byte
start and count fields, so the maximum range storage is 128 bytes. The
algorithm has expected `O(n log n)` and worst `O(n^2)` comparison work,
expected `O(log n)` and worst `O(n)` pending ranges, and no alignment or
null-pointer arithmetic. This is an independent standard-defined
implementation, not copied source. Selection sort and whole-range insertion
sort are forbidden.

The general PM validator used on this path replaces nested identity and wait
searches with one bounded workspace:

```text
PID-ordered active slots:       64 x u16
endpoint-ordered live slots:    64 x u16
child counts by parent slot:    64 x u8
```

One table pass validates local record shape and fills the vectors. Sorting and
adjacent comparison prove PID and endpoint uniqueness. Parent handles resolve
directly by slot/generation while child counts serve wildcard waits. An exact
wait selector is found by binary search in the PID-ordered slots and then
checked against the child's parent handle. Prepared spawn endpoints use binary
search in the endpoint-ordered live slots. The validator is `O(p log p)` time
and `O(p)` bounded auxiliary storage; it performs no nested complete-table
scan.

Snapshot construction:

1. validate the complete PM table;
2. require no spawning or exiting record while servicing the request;
3. select running and zombie records;
4. resolve each non-init parent to its semantic PID;
5. validate command metadata;
6. emit one complete candidate record;
7. verify strict PID ordering and record count; and
8. leave the unused page tail zero; and
9. copy the complete 4,096-byte candidate through the caller's grant.

The complete valid path, including invoked PM invariant validation, must have a
documented bound no worse than the reference process-list collection and sort.
An inherited nested process-table scan is a blocking finding, even when the
table capacity is fixed.

Copying the complete page proves the exact declared grant range and publishes
the zero tail from PM-owned state. The result publishes count/size/capacity
only after that complete copy succeeds.

The PM service clears its resident snapshot page after every returning path.
The process table never points into that page.

## Application client state

Each application owns:

```c
struct micros_application_client {
    _Alignas(4096) uint8_t page[4096];
};
```

Only one operation may use the page at a time because v0.1 has one thread per
process. Every public client call:

1. validates scalar arguments;
2. zeroes required request/result storage;
3. prepares local bytes;
4. creates one directional grant when needed;
5. performs one synchronous IPC call;
6. revokes the grant on every returning path;
7. validates the complete reply;
8. publishes output; and
9. zeroes transient page bytes when they contained a spawn request or process
   snapshot.

Failure to revoke an accepted grant is an invariant failure, never an ordinary
command error.

## Init state machine

```text
STARTUP
  -> CHECKPOINT_BASELINE
  -> BUILD_SHELL_REQUEST
  -> SPAWN_SHELL
  -> WAIT_SHELL
  -> CHECKPOINT_REAPED
  -> BUILD_SHELL_REQUEST
  -> ...
```

The production checkpoint adapter returns immediately. The QEMU adapter
returns from the baseline checkpoint and is nonreturning after the final
drain/report checkpoint.

Init retains no shell PID after successful wait. It does not issue a wildcard
wait, so unrelated adopted children cannot satisfy shell-session collection.

## Shell state machine

```text
STARTUP
  -> WRITE_BANNER
  -> WRITE_PROMPT
  -> READ_LINE
  -> PARSE
     -> EMPTY ---------> WRITE_PROMPT
     -> EXIT ----------> RETURN 0
     -> SYNTAX_ERROR --> WRITE_ERROR -> WRITE_PROMPT
     -> COMMAND -------> RESOLVE
                         -> NAME_TOO_LONG -> WRITE_ERROR -> WRITE_PROMPT
                         -> BUILD_REQUEST
                         -> SPAWN
                            -> EXPECTED_ERROR -> WRITE_ERROR -> WRITE_PROMPT
                            -> SUCCESS -> WAIT_EXACT
                                         -> WRITE_PROMPT
```

The shell has no second child in flight. It never issues a nohang or wildcard
wait.

### Line grammar

```text
line       := horizontal-space* [word (horizontal-space+ word)*]
              horizontal-space* newline
word       := one or more printable ASCII bytes except space
horizontal-space := space | tab
```

There is no quote or escape character. A command word containing `/` is an
explicit path and passes unchanged to the existing PM/VFS spawn transaction.
The shell does not normalize, pre-open, or reinterpret absolute or relative
explicit paths; VFS remains the pathname and executable-policy authority.

## Command behavior contracts

### Common

- Startup globals and argument terminators are validated before I/O.
- Descriptor 1 is stdout; descriptor 2 is diagnostics.
- All output uses exact full-write helpers.
- Normal C return publishes the command exit code through the existing
  application runtime.
- No command calls PM except `ps`; no command calls VM, RAMFS, TTY, or kernel
  control directly.

### `echo`

Operands, separators, and the optional final newline are submitted
sequentially through the complete-write client. Echo retains no
complete-output aggregate. Sequential argument and byte work is `O(a + b)`
with `O(1)` auxiliary state, matching the reference argv traversal and bounded
stdio buffering.

### `cat`

The transfer page is reused for each read. A zero transferred count is EOF.
Every nonzero read must be fully written before another read. Close is
attempted after every successful open, including a prior read/write failure.
The copy is `O(b)` with one fixed transfer page, matching MINIX's reusable
block buffer and partial-write loop.

### `ls`

Exact `.` and `..` records are validated and filtered before storage. Other
names are copied into one fixed 64-by-61-byte name table. A separate 64-entry
order vector plays the role of FTS's pointer array, avoiding repeated
whole-name movement. The order vector uses the same project-owned
Bentley-McIlroy quicksort family as PM. Sorting compares unsigned bytes and
then length. Adjacent equal names after sorting, or more than 64
non-synthetic records, are invariant failures. There is no duplicate scan
during collection. Filtering admits the valid RAMFS maximum of 63 direct
children plus its two synthetic records.

### `ps`

The snapshot page is validated before any output. `record_count` rows must be
strictly ascending, init must be PID 1 with parent 0 and reaper flag, and every
other parent PID must be found by binary search in the earlier PID-sorted
prefix. A per-row prefix scan is forbidden. `ps` prints only `RUN` and
`ZOMBIE`.

## Userland seed and image budgets

Every application remains within ADR-0050:

```text
image pages:      3..15
stack pages:      1
resident pages:   <=16
file bytes:       <=131,072
```

The userland seed retains:

```text
entry capacity:   64
block capacity:   64
data maximum:     262,144 bytes
```

The generator rejects aggregate rounded-block or byte overflow. No capacity is
raised merely to fit the selected applications.

## Native evidence

### Spawn builder

- exact init and shell bytes;
- zero through capacity boundaries;
- every string/reference/descriptor bound;
- duplicate/overlap/unterminated rejection;
- output preservation on failure.

### PM

- bounded sort family, context comparison and deterministic work counts;
- complete-table validation with ordered slot vectors, adjacent uniqueness,
  child counts and binary exact-child lookup;
- command basename/truncation/zero-tail lifecycle;
- non-display command-byte replacement without spawn rejection;
- init, child, zombie and record release;
- snapshot ordering and parent PID;
- running/zombie selection;
- free/quarantine omission;
- malformed request, stale caller and every grant result;
- output and table preservation on rejection.

### Shell and init

- startup validation;
- whitespace and maximum line/argument boundaries;
- empty, exit, simple name, absolute slash path, relative slash path and syntax
  error;
- slash-free command-name lengths 250 and 251, with no PM call for the latter;
- fixed environment and three descriptor actions;
- exact spawn/wait traces and error recovery;
- init exact-child wait and respawn loop.

### Commands

- echo zero/max args, `-n`, sequential write traces and absence of
  complete-output aggregation;
- cat ordered multi-file/EOF/partial failure/close behavior;
- ls malformed records, 64-entry bound, sort order, adjacent duplicate
  rejection, and reference-sort comparison-count cases;
- ps malformed rows, binary parent lookup, parent/reaper invariants and exact
  formatting;
- shared sort empty/single/small, sorted, reverse, duplicate-heavy and
  adversarial inputs whose comparison-key trace matches an independent model
  of the exact pivot, partition and left-before-right order;
- ls comparator byte-work counts, with expected
  `O(d log d * name_length)` and worst `O(d^2 * name_length)` bounds.

All models use explicit events, not sleeps. No randomized test is required.

## QEMU evidence

The new workflow is:

```text
test-qemu-shell
```

It uses one 128 MiB, one-hart QEMU `virt` execution, the ordinary six-service
manifest, the generated userland seed, and five prompt-paced serial steps
whose concatenation is the 48-byte script.

`qemu_smoke_arguments.py` owns repeatable trigger/payload parsing and mutual
exclusion with the existing single-input form.
`qemu_smoke_execution.py` owns raw post-match cursors, one write per stage,
ordered arming, and the final sent-step count.
`qemu_smoke_outcome.py` requires all configured stages before success.

The runner also owns repeatable `--exact-transcript-line` records.
`qemu_smoke_outcome.py` requires the unique complete-line slice from the first
configured record through the final configured record to equal that list
exactly. Native runner cases cover extra, duplicate, reordered, missing and
unterminated session records.

The runner requires:

- ordinary boot/FDT/trap/frame/MMU/object readiness;
- exact ordered shell output patterns;
- all five prompt-paced serial writes;
- exact contiguous shell transcript;
- authenticated init baseline/final breakpoint stages;
- exact baseline resource comparison;
- aggregate TTY output drain and bounded physical `THRE|TEMT` drain;
- no bootstrap failure, panic, test failure, host timeout, or unexpected exit;
  and
- clean SBI success shutdown.

The pass marker alone is insufficient.

The shell resource comparator shares the existing exact dynamic-application
projection, with one bounded TTY normalization:

- canonicalize `input_head` only for a completely empty, zeroed input ring
  with no completed/current line; and
- canonicalize `echo_head` only for a completely empty, zeroed echo ring.

All other TTY fields and every non-TTY live resource remain exact. Native
mutations cover residual counts, bytes, flags, completed lines and pending
operations so cursor normalization cannot hide live state.

VFS `next_tty_request_id` and TTY `last_accepted_request_id` retain the
existing monotonic comparison used by the dynamic-application resource oracle.
They may increase but cannot decrease or wrap. Request IDs inside any live
pending/completion state remain exact, and both checkpoints require those live
states to be absent.

Before the breakpoint, the init test adapter uses a shell-test-only VFS/TTY
drain transaction. TTY holds one reply token until both echo and resident
write output are absent, replying from its interrupt path on the exact
transition to aggregate empty. The test state is separately validated and is
not present in production builds.

After successful resource comparison, the shell test kernel performs the
existing bounded `THRE|TEMT` physical-empty poll, then invokes the terminal
panic-seizure transition, validates panic console/route ownership, writes and
flushes the raw pass marker, and performs SBI success shutdown. This is a
test-only nonreturning completion path; production kernel output remains
forbidden while TTY owns the console.

## Implementation sequence

1. Project-owned MINIX/NetBSD sort-family Red/Green with deterministic work
   evidence.
2. Shared spawn builder and application-client native Red/Green.
3. PM command metadata, efficient complete validation, and snapshot
   protocol/model Red/Green.
4. Public VFS record and client I/O Red/Green.
5. Init/shell/command portable Red/Green, including order-vector and binary
   parent lookup evidence.
6. Checked images and production seed.
7. Expected-failing target scenario, then minimum target adapters.
8. Resource/drain/ordered-output completion.
9. Narrow checks, complete milestone gate, exact producer/reuse evidence, and
   independent review.

If implementation exposes an incomplete authority or wire contract, stop and
merge a corrective design-only PR before continuing.

## Non-goals

The complete non-goal list is authoritative in ADR-0079. In particular, this
contract adds no fork, signals, full shell grammar, login, system-wide `ps`,
production shutdown authority, or generalized userland framework.

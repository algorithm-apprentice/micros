# B13 Node-Q Accepted-Design Baseline Audit

## Status

```text
audit unit:       B13 node-Q accepted design
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
candidate commit: 33380e6973bc9b1fa8d2972d8bd0a986f5ee3500
candidate tree:   d309ec615e5978a42faa26535dd7394bd59cc425
result:           audited; seven blocking candidate findings; not certified
implementation:   forbidden during complete-audit phase
```

B13 audits Accepted ADR-0079 plus the archived, never-merged node-Q candidate.
The pinned production tree contains no node-Q production source. Only the
pinned archive commit/tree above is candidate evidence; it must never become
an implementation base.

## Exact primary path manifest

B13 has **zero primary production paths** in the pinned tree. The complete-
audit contract assigns B13 the accepted node-Q operation families and the
abandoned-candidate evidence, not runtime ownership.

Canonical design evidence is:

- `docs/adr/0079-production-init-shell-and-basic-commands.md`;
- `docs/architecture/init-shell.md`;
- `docs/research/minix-init-shell-basic-commands.md`;
- `docs/research/minix-structural-algorithm-transition.md`; and
- ADR-0080's node-Q correction requirements.

The archived candidate evidence relevant to B13 findings is:

- `user/commands/echo.c`;
- `user/commands/cat.c`;
- `user/commands/ls.c`;
- `user/commands/ps.c`;
- `lib/application/client_common.c`;
- `lib/application/client_vfs.c`;
- `servers/pm/pm_snapshot.c`;
- `servers/pm/pm_service_requests.c`; and
- `servers/pm/pm_core.c`.

Archived shell-parser, init, seed, QEMU, resource, and validation paths remain
supporting behavioral evidence but introduce no additional blocking finding.

## Closed operation manifest

| ID | Operation family |
| --- | --- |
| B13-O01 | Post-seal production init creation and PID-1/reaper publication |
| B13-O02 | One exact shell-session spawn/wait/restart loop |
| B13-O03 | Shell read/tokenize/resolve/spawn/exact-wait loop |
| B13-O04 | Shared application client and canonical spawn builder |
| B13-O05 | PM command metadata and application snapshot for `ps` |
| B13-O06 | `echo` argument/output algorithm |
| B13-O07 | `cat` reusable-buffer copy algorithm |
| B13-O08 | `ls` record ownership, duplicate policy, sort, and output |
| B13-O09 | `ps` snapshot validation, parent lookup, and formatting |
| B13-O10 | Userland seed, serial scenario, resource baseline, and drain |
| B13-O11 | Deferred shell/session/POSIX/system-inspection breadth |

## Fixed MINIX source evidence

The B13 comparison uses only local source:

| Source | Exact functions/role |
| --- | --- |
| `sbin/init/init.c` | `single_user`, `collect_child`, `start_getty`, session list/DB helpers |
| `bin/sh/main.c` | `cmdloop` |
| `bin/sh/eval.c` | `evaltree` and foreground evaluation |
| `bin/sh/exec.c` | `find_command` and command-hash/PATH lookup |
| `bin/sh/jobs.c` | `waitforjob` and job/process tracking |
| `bin/echo/echo.c` | `main` sequential argv output |
| `bin/cat/cat.c` | `raw_args`, `raw_cat` reusable-buffer/partial-write loop |
| `bin/ls/main.c` | `main` frontend |
| `bin/ls/ls.c` | `ls_main`, `traverse`, `display`, `mastercmp` |
| `bin/ls/cmp.c` | `namecmp` default byte/string ordering |
| `bin/ps/ps.c` | `main`, `pscomp`, process-array sorting |
| `bin/ps/print.c` | process row formatting |
| `lib/libc/gen/fts.c` | `fts_sort` record-list to pointer-array conversion |
| `lib/libc/stdlib/qsort.c` | Bentley-McIlroy `qsort` pivot/partition/order |
| `minix/servers/pm/mproc.h` | bounded `mp_name[PROC_NAME_LEN]` process title |
| `minix/servers/pm/exec.c` | `do_newexec` copy/truncate/NUL publication of `args.progname` |

## Logical variables

| Variable | Meaning |
| --- | --- |
| `p` | PM application records selected or validated, at most 64 |
| `d` | non-synthetic directory names, at most 64 |
| `a` | command arguments, at most 8 |
| `b` | bytes emitted or copied |
| `m` | compared name bytes, at most 60 |
| `Vpm` | complete PM-table validation inherited from B08 |

## Operation ledger

### B13-O01 Production init creation and publication

MINIX init is PID 1, persists as the reaper, owns shell/getty session policy,
collects children, and participates in broader system/session policy.

The Accepted `micros` design creates init exactly once after the authenticated
bootstrap-sealed event through the existing trusted hidden-child transaction.
It publishes semantic PID 1, no application parent, one reaper record, exact
console descriptors, `/sbin/init`, argv `init`, and
`PATH=/bin:/sbin`.

Post-seal direct spawn is a required adaptation to the existing static
launcher and spawn-before-fork architecture. One-shot fatal failure and exact
generation/token validation are compatible safety extensions.

### B13-O02 One shell session

MINIX `single_user()` retains one shell child PID and waits for that exact
child. In the normal build it then returns to `runcom`; only the small-build
variant loops directly. Ordinary automatic restart comes from the multi-user
`collect_child()`/`start_getty()` session path, backed by a session list and
PID-indexed session database.

Node Q specializes that policy to one console:

```text
spawn /bin/sh
-> retain one exact shell PID
-> wait exact PID
-> require normal exit
-> clear PID
-> repeat
```

Node Q combines the exact `single_user()` child/wait identity with the
multi-user restart policy as one fixed-console specialization. The scalar
foreground shell identity remains baseline parity. Multiple session/process-
group policy is staged under SOM-07, login credentials under SOM-11, and
getty/multi-terminal behavior under SOM-13.

MINIX retries selected single-user fork/wait failures, restarts a shell after
non-`SIGKILL` signal termination, and restarts a terminated getty session
unless shutting down. Node Q instead treats shell spawn/protocol failure or
non-normal termination as fatal. Lifecycle retry/recovery is staged under
SOM-01, restart delay/backoff under SOM-10, and signal-termination policy
under SOM-09.

### B13-O03 Shell parse, resolution, foreground spawn, and wait

MINIX shell parsing/evaluation supports a full syntax tree, PATH hash,
multiple jobs/processes, redirection, pipelines, signals, and job control.

The accepted staged shell:

- consumes one 256-byte canonical line in one forward pass;
- stores at most eight in-place argument pointers;
- owns only exact zero-operand `exit`;
- constructs the sole immutable `/bin/<name>` candidate directly;
- passes slash-bearing explicit paths unchanged;
- retains one exact foreground PID scalar; and
- waits for that PID before another command.

The simple grammar and one foreground child are staged substitutions.
Direct one-component lookup is the correct specialization: importing MINIX's
31-bucket command hash when there is one immutable candidate would be an
unrequired cache, not baseline parity.

### B13-O04 Application client and spawn-request builder

The accepted design has one pointer-free canonical builder and one
standard-library-free PM/VFS client. Each application owns one aligned 4 KiB
page, creates only exact directional grants, revokes every returning grant,
validates all reply fields, and exposes typed errors.

The complete-write helper must be alias-safe when its source is the client's
own page and must retain linear byte movement. Each external page-sized chunk
is staged at most once. Retries advance one grant-relative/in-page pointer and
remaining count over the already staged bytes; they do not recopy or compact
the complete remaining suffix on every partial result.

This is the required direct-grant/wire adaptation for the existing
application runtime. It does not authorize a heap, registry, retries, `errno`,
or hidden fallback. Broader libc and POSIX interfaces remain SOM-17.

### B13-O05 PM command metadata and process snapshot

MINIX `ps` consumes a contiguous kernel/KVM process snapshot and uses the
NetBSD three-way `qsort` family before display. MINIX PM itself has direct
slot identity and does not publish the node-Q wire page.

MINIX PM stores one bounded `mp_name[PROC_NAME_LEN]`. `do_newexec()` copies
`args.progname`, truncates to `PROC_NAME_LEN - 1`, and writes one terminal
NUL. The title is display/debug data, not lifecycle authority.

The accepted `micros` metadata:

- derives one executable-path basename from the already validated pointer-free
  request;
- retains at most 24 bytes from spawning through zombie;
- replaces non-display bytes with `?`;
- requires a zero tail; and
- clears the complete title in free/quarantined records.

```text
metadata build/validation: O(path bytes + 24), O(1) auxiliary state
```

The bounded non-authoritative title is baseline parity. The pointer-free path
source and project capacity are required adaptations; printable sanitization
and complete zero-tail/lifecycle validation are compatible safety extensions.

The accepted `micros` design:

1. validates PM state without nested complete-table scans;
2. selects running/zombie application slots into lightweight vectors;
3. resolves parent PIDs by direct slot/generation;
4. sorts selected entries by semantic PID with the shared bounded MINIX/
   NetBSD quicksort family;
5. proves PID/endpoint uniqueness by adjacent checks on ordered vectors;
6. uses binary lookup for exact-child/wait and endpoint relationships;
7. emits one zeroed 4,096-byte observational snapshot; and
8. copies the complete page through one caller grant.

```text
accepted PM snapshot/validator:
    expected O(p log p), worst O(p^2)
    O(p) bounded auxiliary vectors/ranges
```

B13-F004 and B13-F006 record the archived candidate's selection sort and
quadratic inherited validator.

### B13-O06 `echo`

MINIX `echo` walks argv sequentially through bounded stdio buffering.

The accepted design writes each operand, separator, and optional newline
sequentially through the complete-write client:

```text
time:       O(a + b)
auxiliary:  O(1) beyond the shared client page
```

The archived candidate instead accumulated the complete output in a
2,048-byte array before one write. See B13-F001.

### B13-O07 `cat`

MINIX `cat` reuses one block buffer and completes partial writes before the
next read.

The archived candidate reuses one 4 KiB client page for each read and passes
that same page to the generic complete-write helper. Its retry algorithm is
not alias-safe: after a partial result it copies `page + offset` back to
`page`, then retains the original total offset, so repeated partial writes can
overwrite bytes that have not yet been submitted. Even without corruption,
each retry recopies the complete remaining suffix; one-byte completions
therefore perform `b + (b-1) + ... + 1 = Theta(b^2)` staging work.

```text
time:       O(b)
auxiliary:  O(block)
```

The accepted design retains one reusable page and complete writes, but
requires alias-safe retries that stage each byte at most once and advance over
the existing page span. See B13-F007.

### B13-O08 `ls`

MINIX FTS owns entry records separately from a reusable pointer array. It
sorts the pointers with the imported NetBSD Bentley-McIlroy three-way
quicksort:

- insertion sort only below seven;
- the middle element directly at exactly seven;
- median-of-three only above seven, or pseudomedian-of-nine above 40;
- four-cursor equal-key partition;
- recursive left before tail-iterated right.

The accepted independent implementation uses:

- one fixed 64-by-61-byte name table;
- one 64-entry order-index vector;
- the same pivot/partition/order family;
- a bounded explicit pending-right range stack;
- adjacent duplicate rejection after sorting; and
- no whole-name movement during ordering.

```text
expected: O(d log d * m)
worst:    O(d^2 * m)
auxiliary order/ranges: O(d)
```

The archived candidate scanned every prior collected name for duplicates and
then insertion-sorted/moved complete `struct ls_name` objects. See B13-F002
and B13-F003.

### B13-O09 `ps`

The accepted snapshot is already strictly PID ordered. `ps` validates every
row and resolves each non-init parent by binary search in the earlier sorted
prefix.

```text
snapshot validation after sort:
    O(p log p)
```

The archived candidate searched the entire earlier prefix linearly for every
row, producing `O(p^2)` total parent lookup. See B13-F005.

### B13-O10 Seed, serial scenario, resource baseline, and drain

The accepted design uses one checked RAMFS seed containing init, shell, four
commands, and `/etc/motd`; every ELF remains within existing application and
RAMFS budgets.

The QEMU scenario sends five prompt-paced serial lines, requires one exact
contiguous transcript, authenticates init baseline/final checkpoints, compares
complete live resources, drains aggregate TTY echo/output, performs bounded
physical UART empty checking, seizes the terminal only in the test image, and
shuts down through SBI.

These are compatible deterministic evidence extensions. They do not grant
production drain, shutdown, inspection, or test checkpoint authority.

### B13-O11 Deferred shell, session, and inspection breadth

Current node-Q design omits:

- fork/COW/destructive exec and unrestricted descriptor inheritance;
- signals, pipelines, redirection, background jobs, job control;
- sessions/process groups/controlling-terminal policy and multiple gettys;
- users, credentials, login, runlevels, startup scripts, and accounting;
- shell quoting, escaping, expansion, variables, functions, scripts, and
  mutable/multi-component PATH;
- persistent filesystem behavior and broad file operations;
- full command options, locale, terminal formatting, and hosted libc;
- static-service/kernel-thread/system-wide process inspection; and
- service recovery or production shutdown;
- init/getty restart delay and timer policy.

| Omission family | Exact owner |
| --- | --- |
| Recovery and service/session reconstruction | SOM-01 |
| Fork/COW/exec and descriptor inheritance | SOM-06 |
| Sessions, process groups, controlling terminal, and job control | SOM-07 |
| Signals and asynchronous child/job events | SOM-09 |
| Init/session restart timers and delay policy | SOM-10 |
| Credentials, users, groups, login authorization | SOM-11 |
| Pipes, redirection-supporting descriptors, metadata/file-operation breadth | SOM-12 |
| Full terminal, PTY, getty, login, and session accounting | SOM-13 |
| Public process/file/libc headers, wrappers, errno, and hosted runtime | SOM-17 |
| POSIX shell grammar, expansion, redirection, pipelines, jobs, and mutable/PATH search semantics | SOM-18 |
| Mandatory utility breadth beyond selected bounded `echo`/`cat`/`ls`/`ps` | SOM-19 |
| Locale, environment, regex/glob, databases, and configuration queries | SOM-20 |
| Complete process/service/kernel-thread inspection | SOM-22 |
| Asynchronous/nonblocking command/job transport | SOM-23 |
| Persistent filesystem/storage behavior | SOM-05 |
| Production shutdown/reboot authority and quiesce policy | SOM-27 |

B13 adds SOM-27 for production shutdown/reboot policy. No additional staged-
omission row is required.

## Difference summary

| Area | Result |
| --- | --- |
| PID-1/reaper and exact shell-session ownership | Baseline parity |
| Post-seal trusted init creation | Required adaptation |
| One-console/simple-shell grammar and one foreground child | Staged substitution |
| One immutable `/bin` candidate without command hash | Staged substitution under SOM-18 |
| Pointer-free builder and typed direct-grant clients | Required adaptation |
| Deterministic QEMU transcript/resource/drain evidence | Compatible extension |
| No production shutdown authority/policy | Staged substitution under SOM-27 |
| No shell-session retry/recovery after spawn or protocol failure | Staged substitution under SOM-01 |
| No restart delay/backoff policy | Staged substitution under SOM-10 |
| Fatal non-normal shell termination before signal support | Staged substitution under SOM-09 |
| PM-owned bounded non-authoritative command title | Baseline parity |
| Pointer-free basename source and 24-byte title capacity | Required adaptation |
| Printable title sanitization and zero-tail/lifecycle checks | Compatible extension |
| Sequential `echo` over the complete-write client | Required adaptation |
| Reusable-page `cat` with complete-write loop | Required adaptation |
| `ls` name-table plus lightweight order vector | Required adaptation |
| `ls` and PM exact MINIX/NetBSD sort family | Required adaptation |
| PM application snapshot replacing KVM/sysctl | Required adaptation |
| Binary parent validation in sorted `ps` snapshot | Compatible extension |
| Ordered-vector complete PM snapshot validator | Compatible extension |
| Archived echo aggregate | Divergence requiring correction |
| Archived ls collection duplicate scan | Divergence requiring correction |
| Archived ls whole-range insertion sort | Divergence requiring correction |
| Archived PM selection sort | Divergence requiring correction |
| Archived ps parent prefix scans | Divergence requiring correction |
| Archived PM nested complete validation | Divergence requiring correction |
| Archived aliased partial-write retries in `cat` | Divergence requiring correction |
| Complete POSIX shell/session/command/inspection breadth | Staged substitution |

## Time, stack, and auxiliary-space envelope

Accepted bounded persistent/application storage includes:

```text
one application client page:            4,096 bytes per application
shell line:                                256 bytes
shell argv pointers:                       8 entries
PM snapshot page:                        4,096 bytes
ps snapshot storage:                     4,096 bytes
ls name table:                   64 * 61 = 3,904 bytes
ls order vector:                           64 bytes
bounded sort pending ranges:              128 bytes
```

The large client/snapshot/name objects are static BSS, not automatic storage
on the 2 KiB application runtime reserve. The sort range stack is bounded
explicitly instead of relying on worst-case recursive target frames.

Representative automatic state is one shell command vector, a few index/
length values, PM slot vectors and parent PID values whose accepted
implementation must stay within the 4 KiB PM service stack, and command
formatting buffers below 256 bytes. B14 still owns direct stack evidence.

## Evidence gaps

| ID | Missing evidence | Owner |
| --- | --- | --- |
| B13-E001 | No merged native comparator trace proves exact MINIX/NetBSD pivot, partition, equal-key, and left-before-right order | B14 after correction implementation |
| B13-E002 | No merged operation counter proves PM snapshot/validator `O(p log p)` and absence of nested table scans | B14 after B08/B13 corrections |
| B13-E003 | No merged `ls` probe proves order-vector sorting and adjacent-only duplicate detection | B14 after correction implementation |
| B13-E004 | No merged `ps` probe proves binary parent lookup rather than prefix scans | B14 after correction implementation |
| B13-E005 | No merged `echo` trace proves sequential calls and absence of a complete-output aggregate | B14 after correction implementation |
| B13-E006 | No current QEMU shell evidence exists because the archived candidate is not an implementation base | B14 after corrected implementation |
| B13-E007 | No direct application/PM stack evidence covers accepted sort/snapshot/client nesting | B14 |
| B13-E008 | No merged metadata probe covers basename selection, 24-byte truncation, printable replacement, zero tail, and spawning-to-zombie/free lifecycle | B14 after correction implementation |
| B13-E009 | No merged client/`cat` tests separately exercise repeated one-byte completions for an external immutable source (stage-once `O(b)` copied-byte count) and a client-page alias (exact content plus monotonic grant spans) | B14 after correction implementation |
| B13-E010 | No init-policy evidence covers shell spawn/wait failure, abnormal termination, retry/backoff absence, and exact fatal boundary | B14 after correction implementation |

## Findings register

### B13-F001: Archived echo retains the complete logical output

- Severity: Medium
- Candidate evidence:
  - static `echo_output[2048]`
  - `micros_text_append_*`
  - one final `micros_application_write_all`
- MINIX evidence: sequential argv traversal through bounded stdio buffering
- Difference: auxiliary output storage grows with emitted bytes.
- Impact: `O(b)` auxiliary storage replaces the baseline streaming algorithm
  and imposes an unnecessary aggregate capacity/failure point.
- Smallest correction boundary: submit each operand, separator, and final
  newline sequentially through the complete-write helper; retain only argv
  indexes and newline state.
- Dependencies: application-client complete-write behavior and B14 call-trace
  evidence.

### B13-F002: Archived ls performs quadratic duplicate checks during collection

- Severity: Medium
- Candidate evidence: `ls_store_name()` scans all previously stored names
- MINIX evidence: FTS record ownership plus sort; duplicate policy is not a
  repeated collection prefix scan
- Difference: each inserted name compares against the complete prior
  collection.
- Impact: collection performs `O(d^2*m)` byte comparisons before sorting.
- Smallest correction boundary: collect each validated non-synthetic name
  once, sort one lightweight order vector, and reject only adjacent equal
  names after ordering.
- Dependencies: B13-F003 and B14 comparison-count evidence.

### B13-F003: Archived ls uses whole-range insertion sort and moves full names

- Severity: High
- Candidate evidence:
  - `ls_sort`
  - insertion from `outer = 1` through the complete range
  - assignment/movement of full `struct ls_name`
- MINIX evidence:
  - `fts_sort`
  - Bentley-McIlroy three-way `qsort`
- Difference: the candidate uses `O(d^2*m)` comparison/movement work for
  general inputs instead of the reference expected `O(d log d*m)` sort family
  over lightweight pointers/indexes.
- Impact: directory listing has the exact asymptotic regression ADR-0080 was
  created to prevent.
- Smallest correction boundary: independently implement the fixed reference
  pivot/partition/left-before-right family over the order-index vector,
  retaining insertion sort only for reference-size small partitions.
- Dependencies: one shared bounded-sort correction used by B13-F004 and B14
  exact comparator/order evidence.

### B13-F004: Archived PM snapshot uses selection sort

- Severity: High
- Candidate evidence: `pm_snapshot_sort()` selects the minimum remaining PID
  for every output position
- MINIX evidence: process snapshot sorting through the complete NetBSD
  three-way `qsort` family
- Difference: snapshot ordering is `O(p^2)` even before inherited table
  validation.
- Impact: PM process publication regresses from expected `O(p log p)` sorting
  and repeatedly moves parallel slot/parent arrays.
- Smallest correction boundary: sort one lightweight selected-entry vector
  with the shared bounded reference-family implementation and publish records
  from that order.
- Dependencies: B13-F003, B08-F001, and B14 comparison/order evidence.

### B13-F005: Archived ps validates each parent with a prefix scan

- Severity: Medium
- Candidate evidence: `ps_parent_exists()` scans records `0..index-1` for
  every non-init row
- MINIX evidence: trusted sorted process snapshot and linear display pass
- Difference: safety validation adds `1 + 2 + ... + (p-1)` parent probes.
- Impact: complete snapshot validation is `O(p^2)` after PM has already sorted
  the records.
- Smallest correction boundary: binary-search the strictly PID-sorted earlier
  prefix for each parent PID and preserve the init/reaper invariants.
- Dependencies: sorted snapshot contract and B14 parent-probe counts.

### B13-F006: Archived snapshot publication inherits quadratic PM validation

- Severity: High
- Candidate evidence:
  - `micros_pm_process_snapshot_build`
  - `micros_pm_find_running_by_endpoint`
  - complete validation inside caller lookup
  - second `micros_pm_table_validate` inside snapshot construction
- Primary-owner evidence: B08-F001 complete PM validation and B08-F003 caller
  endpoint lookup
- Difference: the process-list path performs one `Vpm` before an `O(p)` caller
  endpoint scan, then a second `Vpm` before candidate selection/sort/parent
  work.
- Impact: correcting only selection sort would leave the complete valid path
  with `2*Vpm + O(p)` before snapshot construction begins.
- Smallest correction boundary: resolve the caller through the corrected
  endpoint index/path-local checks, then retain the Accepted snapshot-specific
  complete validator using PID/endpoint ordered slot vectors, adjacent
  uniqueness, direct parent resolution, child counts, and binary exact-child
  lookup in `O(p log p)`. The old quadratic whole-table audit remains
  diagnostic only.
- Dependencies: B08-F001/F003, B13-F004, and B14 complete-path operation
  counts.

### B13-F007: Archived cat corrupts aliased partial-write retries

- Severity: High
- Candidate evidence:
  - `cat_copy_descriptor()` passes `cat_client.page` to
    `micros_application_write_all`
  - `micros_application_write_all` advances a source offset
  - `micros_application_write` copies each retry source back to
    `client->page`
- MINIX evidence: `raw_cat` retains one read buffer and advances an
  independent output pointer/count through partial writes
- Difference: when source bytes already reside in the client page, a partial
  write followed by `bytes + offset -> client->page` overlaps and overwrites
  the still-unsent sequence before later offsets are evaluated. Each retry
  also recopies the complete remaining suffix.
- Impact: repeated partial completions can duplicate or corrupt `cat` output
  despite every individual VFS write succeeding; one-byte completions perform
  `Theta(b^2)` staging copies instead of `O(b)`.
- Smallest correction boundary: make complete-write retries explicitly
  alias-safe for the client page. Stage each external chunk once, then issue
  retries over monotonically advancing in-page/grant spans without copying the
  remaining suffix again.
- Dependencies: application-client correction and B14 multi-partial alias
  and external-source evidence: exact aliased content/monotonic grant spans
  plus stage-once copied-byte counts under repeated one-byte completions.

## Unit conclusion

B13 has no primary production paths, and its accepted-design operation
manifest is complete. The archived candidate confirms B13-F001 through
B13-F007. Accepted ADR-0079 behavior remains authoritative; ADR-0080 and the
canonical init/shell architecture already record the required structural/
algorithmic corrections.

B13 is **audited but not certified**. The seven findings remain blocking until
their later design/implementation corrections merge and node-Q is
re-certified. The pinned `archive/init-shell-pre-audit` commit/tree is
evidence only and is not a future implementation base.

## References

- [ADR-0079](../adr/0079-production-init-shell-and-basic-commands.md)
- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)
- [B08 PM/spawn audit](minix-complete-baseline-b08-pm-spawn.md)
- [Node-Q MINIX study](minix-init-shell-basic-commands.md)
- [Structural/algorithmic transition](minix-structural-algorithm-transition.md)
- [Init/shell architecture](../architecture/init-shell.md)

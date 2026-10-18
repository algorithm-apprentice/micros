# POSIX and MINIX Staged-Omission Manifest

## Purpose

This manifest closes the set of currently deferred final-system mechanisms for
Accepted ADR-0081. It prevents an entirely absent POSIX or MINIX family from
disappearing merely because the current source tree has no operation to audit.

Every staged omission has exactly one ID. B01-B14 audit rows reference these
IDs rather than creating overlapping informal "later work" entries.

## Rules

- Every mandatory POSIX.1-2024 requirement must ultimately pass.
- "Inapplicable" is allowed only when POSIX itself conditions the requirement;
  the final capability matrix cites that exact normative condition.
- Optional and XSI option groups are tracked separately from mandatory rows.
- A POSIX subprofile may be reported as subprofile conformance, but cannot
  satisfy the complete-POSIX milestone.
- A newly discovered staged family requires a reviewed manifest update before
  the discovering audit unit may merge.
- One row may contain multiple interfaces only when they share one owner,
  substitute, dependency boundary, and conformance gate.

## Permanent compatibility exclusions

These are not staged POSIX omissions:

| Exclusion | Reason |
| --- | --- |
| MINIX source, binary, syscall-number, and driver ABI compatibility | POSIX source compatibility is the portable target |
| Copying MINIX or NetBSD source text | Implementation remains independently authored |
| SMP as a prerequisite for POSIX | POSIX compatibility does not require multiple harts |
| USB, audio, graphics, or broad hardware as compatibility prerequisites | They are outside mandatory POSIX system behavior |
| Formal verification or production hardening as compatibility prerequisites | They require separate evidence and do not define POSIX conformance |

Physical hardware, SMP, additional drivers, hardening, and verification may
remain future project goals without blocking the complete POSIX claim.

## Closed staged manifest

| ID | Final mechanism or interface | Fixed MINIX baseline | Current substitute | Replacement point | Required predecessors | Removal/conformance evidence |
| --- | --- | --- | --- | --- | --- | --- |
| SOM-01 | Dynamic service/endpoint registry and lifecycle recovery | DS/RS publication, SEF initialization, restart and dependency policy | Static manifest, launcher, fixed endpoints, fatal service loss | Step 16 / Milestone 7 | Stable VM, PM, TTY, RAMFS, VFS, shell | Dynamic publication, stale endpoint invalidation, injected restart, dependency notification |
| SOM-02 | User-space scheduling policy | Kernel scheduling mechanism plus SCHED policy server | Kernel-owned round-robin policy | Step 16 / Milestone 7 | Stable scheduler mechanism, DS | Policy takeover, quantum renewal, priority balance, failure agreement |
| SOM-03 | Dynamic IRQ/driver routing and shared interrupt ownership | IRQ hook lists, privilege IRQ tables, driver registration and explicit reenable | One manifest-bound PLIC source 10 and one TTY owner | Step 16 driver/discovery design, before another device source | DS, RS, privilege profiles, PLIC/notification baseline | Multiple routes, shared-source handling, claim/drain/complete, stale-owner rejection |
| SOM-04 | Dynamic VM regions, general page faults, file-backed mappings, reclaim, and mapping policy | VM region AVL, page-fault resolution, page-table mutation and retry | Fixed user window, wired leaves, tokenized special mapping transactions | Milestone 8 file-backed VM, completed in Step 18 POSIX memory semantics | Persistent VFS, VM/PM spawn, scheduler return | `mmap`/`munmap`/`mprotect`, demand fault/retry, file-backed mapping, exact teardown |
| SOM-05 | Persistent block and filesystem service | Block drivers, MFS/VFS persistent namespace and metadata | Immutable seed plus writable RAMFS | Step 17 / Milestone 8 | VFS, VM, RS/driver lifecycle when required | Reboot durability, metadata/link/locking/fsync behavior, failure boundaries |
| SOM-06 | Complete process creation/replacement and MINIX-baseline copy-on-write | PM/VFS/VM `fork`, copy-on-write mappings/faults, `exec`, wait and exit coordination | Direct `spawn`, normal exit, exact wait | Step 18 / Milestone 9 | Dynamic VM policy, persistent VFS, PM lifecycle | POSIX System Interfaces `fork`, `_Fork`, `exec*`, `wait*`, `_exit`, `posix_spawn*`; COW sharing, write-fault split, isolation and teardown |
| SOM-07 | Process groups, sessions, controlling terminals, and job control | PM session/process-group state, TTY session ownership, shell jobs | One init-owned foreground shell and exact child wait | Step 18 system semantics and Step 19 shell | SOM-06, SOM-09, full TTY | `setpgid`, `setsid`, `tcsetpgrp`, foreground/background wait and shell job tests |
| SOM-08 | POSIX threads, synchronization, TLS, cancellation, and once semantics | MINIX/NetBSD thread and libc mechanisms over kernel scheduling | Checked one-thread-per-process policy | Step 18 / Milestone 9 | Thread-capable scheduler/IPC, VM stacks/TLS, libc entry points | Mandatory `pthread_*` headers/APIs, races, cancellation points, unchanged threaded applications |
| SOM-09 | Signals and asynchronous delivery | Kernel/PM signal routing and signal-frame return | Fatal faults only; no user signal state or delivery | Step 18 / Milestone 9 | SOM-06, SOM-08, scheduler, TTY | `sigaction`, masks, delivery/return, `kill`, pending/queued semantics, cancellation interactions |
| SOM-10 | POSIX clocks, sleeps, interval and process timers | CLOCK task timer queue, virtual/profile timers, architecture clock | Scheduler tick and bootstrap deadlines only | Step 18 / Milestone 9 | Scheduler accounting, SOM-09, libc entry points | `clock_*`, `timer_*`, `alarm`, `nanosleep`, process CPU timers, expiration ordering |
| SOM-11 | Credentials, users, groups, permissions, and access checks | PM/VFS credential state and filesystem authorization | No user credentials; fixed image/file modes | Step 18 / Milestone 9 | Persistent VFS/FS, process lifecycle, libc databases | UID/GID APIs, access/chown/chmod semantics, passwd/group interfaces, negative authorization tests |
| SOM-12 | Complete descriptors, metadata, links, pipes, FIFOs, locking, and durable file operations | VFS open-file/vnode/pipe and MFS metadata mechanisms | Bounded regular/directory/console descriptors, no pipes or mutation breadth | Step 17 persistent FS and Step 18 system semantics | SOM-05, process lifecycle, VFS | `stat*`, link/unlink/rename/truncate, `pipe`, FIFO, `dup*`, `fcntl`, locks, sync |
| SOM-13 | Complete terminal, termios, pseudo-terminal, and login-facing behavior | TTY line disciplines, termios including noncanonical `VMIN`/`VTIME` timers, PTYs, getty/login/session accounting | One NS16550A canonical console | Step 18 terminal semantics; login/shell integration in Step 19 | SOM-07, SOM-09, SOM-10, SOM-11, full TTY | termios/ioctl, controlling TTY, PTY pairs, canonical/raw modes including `VMIN`/`VTIME`, session ownership |
| SOM-14 | POSIX memory mapping and shared-memory objects | VM regions, page faults, file/shared mappings, libc mapping interfaces | Private anonymous fixed-window mappings and no POSIX API | Step 18 / Milestone 9 | SOM-04, persistent VFS, process/thread semantics, libc entry points | `mmap`, `munmap`, `mprotect`, `msync`, `shm_open`, shared lifetime and protection tests |
| SOM-15 | POSIX semaphores and message queues | MINIX IPC/PM/VFS/libc named-object mechanisms | Kernel IPC/grants only, no POSIX API | Step 18 / Milestone 9 | DS/RS, process/thread semantics, VFS namespace, libc entry points | `sem_*`, `mq_*`, named lifetime, priority/order, blocking and cross-process tests |
| SOM-16 | POSIX sockets and networking interfaces | MINIX network servers/drivers and socket layer | No networking | Step 18 / Milestone 9 | DS/RS, drivers, VFS descriptors, SOM-10, SOM-11 | POSIX.1-2024 socket interfaces, loopback/network integration, application tests |
| SOM-17 | Public POSIX headers and libc System Interfaces | NetBSD-derived MINIX libc and syscall wrappers | Freestanding project runtime and typed project-private clients | Delivered alongside each Step 18 interface family | Stable syscall/service ABI for that family | Standards-visible declarations, ABI wrappers, error/errno semantics, unchanged C applications |
| SOM-18 | POSIX shell language | MINIX/NetBSD `sh` parser, expansion, redirection, pipelines and jobs | Blank-separated foreground commands plus `exit` | Step 19 / Milestone 10 | SOM-06, SOM-07, SOM-09, SOM-12, SOM-13, SOM-17 | POSIX Shell Command Language grammar and semantic conformance suites |
| SOM-19 | Mandatory POSIX utilities and text-processing environment | MINIX/NetBSD userland utilities | `echo`, `cat`, `ls`, `ps` bounded applications | Step 19 / Milestone 10 | SOM-05, SOM-17, SOM-18 | Mandatory Shell & Utilities rows, exit/output/error semantics, scripts |
| SOM-20 | Locale, environment, regular expressions, system databases, and configuration queries | MINIX/NetBSD libc/userland facilities | Fixed ASCII environment and project-private formatting | Steps 18-19 / Milestones 9-10 | SOM-11, SOM-17, SOM-19 | Mandatory locale/regex/fnmatch/glob/pwd/grp/sysconf/uname capability rows |
| SOM-21 | Runnable-load accounting, history, and public query | CLOCK `load_update()`, ready-queue scan, `kloadinfo` history, system query | Scheduler accounting without load-history publication | Step 16 scheduler/DS diagnostics, exposed through Step 18-19 libc/utility interfaces | Scheduler policy/mechanism, DS diagnostics, SOM-17 | Ready-count sampling, history rollover, query freshness, deterministic load scenarios |
| SOM-22 | System-wide process/service inspection | PM process table plus DS/RS/kernel information paths | `ps` lists PM-owned applications only | Step 16 service registry plus Step 19 utility completion | DS/RS, scheduler, PM, SOM-17 | Coherent process/thread/service snapshot and POSIX utility-visible behavior |
| SOM-23 | MINIX asynchronous and nonblocking microkernel IPC | `SENDNB`, asynchronous-send tables, bounded delivery and backpressure | Synchronous blocking send/call/receive only | After synchronous IPC correction and before a service or POSIX interface requires asynchronous kernel transport | Stable endpoint lifecycle, scheduler, VM buffer-fault policy, DS/RS resource policy | Nonblocking result semantics, bounded queues/storage, backpressure, cancellation, endpoint close, deterministic stress |
| SOM-24 | Receiver-owned MINIX IPC blacklist/whitelist filters | Bounded filter pool/chains, source/type matching, state-control install/clear, timed RS receive | Endpoint/profile authorization only; no per-receiver message filter | After synchronous IPC correction and before RS timed receive or equivalent selective receive | Stable IPC queues, VM buffer access, RS, timers | Filter validation, chain order, blacklist/whitelist semantics, queued/immediate/notification/asynchronous matching, clear and timed receive |
| SOM-25 | Delegated, wildcard, fault-marked, and vectored grants | Indirect and magic grants, wildcard grantees, bounded indirect verification, `CPF_TRY` persistent soft-fault markers and revoke reporting, and `SYS_VSAFECOPY` batching | Exact-generation direct non-transitive grants, exact grantees, immediate scalar fault results, page-bounded copy, and service bounce buffers | Step 18 service/IPC semantics, before a protocol requires delegated, fault-history, or batched authority | Corrected direct-grant baseline, SOM-04 dynamic fault policy, stable VM/VFS/RS protocols | Indirect depth/cycle checks, trusted magic-grant authority, wildcard policy, `CPF_TRY` fault/revoke semantics, vector ordering/partial-progress semantics, endpoint teardown, deterministic stress |
| SOM-26 | Process tracing, resource usage, and core-dump lifecycle | PM tracer relationships, traced stop/exit wait states, per-process/child `rusage`, and VFS-assisted core dumps | Normal exit and wait only; no tracer state, resource-usage result, or core image | Step 18 process/signal semantics before tracing or core-dump compatibility claims | SOM-06 process lifecycle, SOM-09 signals, scheduler accounting, VFS writable files, SOM-17 libc | Trace attach/stop/resume/exit ordering, exact wait status, cumulative resource accounting, core file policy/data, teardown and negative authority tests |
| SOM-27 | Versioned full conformance claim | MINIX compatibility surface plus imported conformance history | No POSIX capability matrix | Step 20 / Milestone 11 | SOM-01 through SOM-26 | Every unconditional mandatory row and every applicable conditionally mandatory row passes; optional/XSI claims pass separately; representative applications run unchanged |

## Audit closure

The complete MINIX baseline audit is not accepted until:

- every staged statement in README, roadmap, DAG, ADRs, and B01-B14 references
  exactly one SOM ID;
- every SOM row has all five ADR-0081 fields;
- no staged statement maps to multiple SOM rows without an explicit
  cross-reference owner;
- no SOM row is orphaned from the final correction/DAG plan; and
- newly discovered omissions are added through reviewed updates before the
  discovering audit unit merges.

## References

- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete MINIX baseline audit](minix-complete-baseline-audit.md)
- [Development DAG](../architecture/development-dag.md)
- [Roadmap](../roadmap.md)

# MINIX Init, Shell, and Basic-Command Baseline

## Purpose

This study fixes the MINIX behavioral baseline for development-DAG Step 15:
creating production init after static bootstrap sealing, running one
interactive console shell, launching foreground commands, reaping them, and
providing `echo`, `cat`, `ls`, and `ps`.

It is the sole ADR-0025 classification ledger for this node-Q outcome. The
existing PM/VFS/VM spawn, application runtime, TTY, RAMFS, and exit/wait
decisions remain authoritative.

The study does not authorize copying MINIX or NetBSD source. It identifies
authority, lifecycle, observable behavior, and the smallest required
adaptations for the already accepted `micros` runtime.

## Fixed reference

The fixed local reference is MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

Files inspected locally:

- `sbin/init/init.c`
- `bin/sh/main.c`
- `bin/sh/eval.c`
- `bin/sh/exec.c`
- `bin/sh/jobs.c`
- `bin/echo/echo.c`
- `bin/cat/cat.c`
- `bin/ls/main.c`
- `bin/ls/ls.c`
- `bin/ps/ps.c`
- `bin/ps/print.c`

The fixed MINIX source includes imported NetBSD userland. That source is
baseline evidence only and is not a code donor.

## Init baseline

### PID 1 is the persistent reaper

MINIX init requires process ID 1, creates the initial session, and owns
system-state transitions. Its child collection path retains init while other
processes terminate.

For `micros`, the reusable invariant is:

- init is the first application and semantic PID 1;
- it has no application parent;
- PM marks it as the sole application reaper;
- init does not exit through the ordinary application exit path; and
- every later application remains in the init-rooted application tree.

Static launcher services are not init children. They remain outside PM's
application table under their existing manifest identities.

### Init starts and monitors a shell session

MINIX single-user init forks and executes a shell on the console, waits for
that child, and restarts the session after ordinary termination. Multi-user
init similarly starts getty sessions, collects their children, and restarts a
session unless it is explicitly shutting down.

The reusable baseline is:

- init owns shell-session policy;
- the shell receives the console as standard input, output, and error;
- init waits for the exact shell child;
- successful child collection consumes the zombie; and
- init can start a replacement session without recreating PID 1.

MINIX spacing delays, login databases, getty, controlling terminals, security
levels, runlevels, `/etc/rc`, signals, windows, users, and multiple terminal
sessions are broader than the node-Q dependency boundary.

## Shell baseline

### Interactive command loop

MINIX `sh` initializes its environment, enters `cmdloop()`, prompts when
interactive, parses one command, evaluates it, and repeats until EOF or the
`exit` built-in.

The reusable baseline is:

- display a prompt on the console;
- read one complete canonical input line;
- reject malformed input without corrupting the next iteration;
- recognize a shell-owned exit operation;
- resolve an external command;
- create one foreground child with explicit argv/env/descriptors;
- wait for that exact child; and
- continue prompting after ordinary command completion.

### Command resolution and execution

MINIX `find_command()` distinguishes built-ins, functions, explicit paths, and
PATH search. A normal foreground command is created, executed, and waited for.
Unknown commands map to a stable command-not-found result.

`micros` has direct `spawn`, not `fork` plus `exec`. The equivalent boundary is
therefore:

- a simple name resolves through the fixed `/bin` search directory;
- any explicit path containing `/`, absolute or relative, is used directly;
- the shell supplies the exact argument and environment vectors;
- descriptors 0, 1, and 2 are explicitly duplicated;
- PM coordinates one hidden spawn transaction;
- the shell waits for the returned semantic PID; and
- spawn rejection or nonzero command status does not destroy the shell.

The complete POSIX shell language is not needed to prove that authority.

## Basic-command baseline

### `echo`

MINIX `echo` writes its operands separated by one space and normally appends a
newline. Its historical `-n` first operand suppresses the newline.

Node Q retains those exact observable semantics. Locale, stdio buffering, and
hosted-libc error reporting are not required.

### `cat`

MINIX `cat` opens each selected file, reads until EOF, writes every byte to
standard output, closes the file, and records failure without stopping later
operands. Its full option set and cooked-display modes are broader than the
MVP.

Node Q retains ordered raw file copying and a nonzero result if any operand
cannot be completely copied.

### `ls`

MINIX `ls` resolves paths, enumerates directory entries, sorts them, and
prints the selected representation. Its long formats, recursion, ownership,
timestamps, columns, colors, quoting, and terminal-width policy are broader
than node Q.

Node Q retains one directory selection, complete directory-record validation,
bytewise name sorting, and one name per output line.

### `ps`

MINIX `ps` obtains process and LWP information through kernel/KVM/sysctl
interfaces, selects and sorts processes, then prints requested fields.

The reusable authority boundary is:

- process lifecycle state comes from the process authority;
- the caller receives a coherent read-only snapshot;
- records have stable identity and parent relationships;
- command display text cannot inject control characters into the listing;
- the display order is deterministic; and
- observation does not mutate lifecycle state.

`micros` PM owns only the dynamic application tree. Static services remain
manifest/kernel objects rather than PM applications. The node-Q `ps` therefore
lists init, shell, commands, and retained application zombies only. A future
DS/RS or system-inspection design may compose static services and kernel
threads; node Q does not invent that authority.

## Existing `micros` foundation

Node Q begins after these merged outcomes:

- static launcher sealing and exact six-service readiness;
- one-way VM handoff;
- PM semantic PIDs, init-reaper state, spawn, exit, zombie, and wait;
- TTY canonical input and interrupt-driven output;
- RAMFS seeded files and deterministic directory records;
- VFS descriptors, root/cwd, console routing, regular I/O, and getdents;
- direct post-handoff ELF spawn with rollback and activation;
- checked application startup, argv/env/auxv, and normal-return PM exit; and
- a six-service PID-1 integration fixture proving complete child cleanup.

The current PID-1 application and PM trigger are test-only. They prove the
mechanisms but do not implement production init or shell policy.

## Sole ADR-0025 classification ledger

| ID | `micros` node-Q choice relative to the fixed baseline | Classification | Reason or replacement point |
| --- | --- | --- | --- |
| IQ-01 | PM creates init as the first parentless application and sole reaper | Baseline parity | MINIX init is PID 1 and owns descendant collection |
| IQ-02 | PM uses the sealed-event trusted direct-spawn transaction instead of kernel-created init | Required adaptation | The accepted static launcher and spawn-before-fork architecture create applications only after bootstrap sealing |
| IQ-03 | Init owns one console shell session and waits for its exact child | Baseline parity | MINIX init owns shell/getty session creation and collection |
| IQ-04 | Normal shell exit causes init to start another shell | Baseline parity | MINIX restarts a terminated console session |
| IQ-05 | One fixed console replaces getty, login, sessions, users, runlevels, and multiple TTYs | Staged substitution | Authentication, signals, credentials, and multi-session policy are outside v0.1 |
| IQ-06 | The shell runs a prompt/read/parse/execute/wait loop | Baseline parity | This is the observable interactive-shell core |
| IQ-07 | Direct spawn/wait replaces fork/exec/job-control machinery | Required adaptation | ADR-0009 intentionally provides spawn before fork |
| IQ-08 | Simple names search only `/bin`; any slash-bearing explicit path passes unchanged to VFS | Baseline parity | Retains MINIX PATH search versus explicit-path behavior while VFS owns absolute/relative resolution |
| IQ-09 | Parsing supports only blank-separated simple commands and `exit` | Staged substitution | Quoting, expansion, scripts, redirection, pipelines, background jobs, functions, aliases, and variables require later POSIX work |
| IQ-10 | `echo` preserves joined operands and optional first `-n` | Baseline parity | Exact selected command behavior |
| IQ-11 | `cat` uses stdin for zero operands or `-`, and otherwise performs ordered raw file-to-stdout copies | Baseline parity | Retains MINIX raw input selection and copy behavior |
| IQ-12 | `ls` validates, sorts, and prints one directory's names | Baseline parity | Exact selected directory-listing behavior |
| IQ-13 | PM publishes a generation-independent application lifecycle snapshot for `ps` | Required adaptation | There is no KVM/sysctl ABI; PM is the application process authority |
| IQ-14 | `ps` lists only PM-owned applications, not static services or kernel threads | Staged substitution | System-wide discovery returns with DS/RS or a separately reviewed inspection service |
| IQ-15 | Pointer-free records, direct write grants, zero tails, exact versions, and deterministic resource snapshots | Compatible extension | Strengthens isolation and acceptance without changing valid behavior |
| IQ-16 | Test-only init instrumentation reports pre-shell and post-reap checkpoints | Compatible extension | It adds no production protocol or authority and permits clean SBI test shutdown |
| IQ-17 | Broader cat/ls/ps options, formatting, locale and terminal presentation are absent | Staged substitution | Selected default behavior is sufficient for node Q; broader command compatibility returns with POSIX expansion |

No unclassified divergence remains for the node-Q MVP.

## Derived node-Q boundary

The smallest complete outcome requires:

1. one production-init policy enabled only in the canonical user-environment
   build after the bootstrap-sealed event;
2. one checked `/sbin/init` application created through the existing trusted
   init transaction;
3. one persistent init loop that spawns, waits for, and restarts `/bin/sh`;
4. one bounded application client library for PM spawn/wait/list and VFS
   open/close/read/write/getdents;
5. one shared canonical spawn-request builder used by PM's init policy and
   applications;
6. one simple foreground shell with fixed `/bin` resolution and `exit`;
7. checked `echo`, `cat`, `ls`, and `ps` application images;
8. one PM-owned application snapshot protocol and persistent bounded command
   display name;
9. one immutable RAMFS userland seed;
10. deterministic native models for request building, process snapshots,
    shell parsing/dispatch, and command behavior; and
11. one serial QEMU scenario that proves exact command output, shell exit,
    child reaping, post-init resource equality, physical UART drain, and clean
    SBI shutdown.

## Explicitly deferred behavior

Node Q does not add:

- fork, exec replacement, copy-on-write, pipelines, redirection, background
  jobs, job control, process groups, sessions, signals, or terminal ioctls;
- shell quoting, escaping, expansion, globbing, variables, aliases, functions,
  scripts, profiles, command substitution, or PATH mutation;
- users, login, credentials, getty, multiple consoles, runlevels, startup
  scripts, service recovery, or session accounting;
- seek, stat, unlink, rename, truncate, pipes, devices beyond the existing
  console, or persistent storage;
- full command option sets or locale behavior;
- static-service/kernel-thread rows in `ps`; or
- a production shutdown syscall or application authority to reset the
  machine.

Those are later POSIX, DS/RS, persistent-storage, or platform-policy outcomes.

## References

- [Development DAG](../architecture/development-dag.md)
- [System overview](../architecture/system-overview.md)
- [Roadmap](../roadmap.md)
- [PM coordinator](../architecture/pm-spawn-coordinator.md)
- [Application runtime](../architecture/application-runtime.md)
- [ELF-spawn integration](../architecture/elf-spawn-integration.md)
- [MINIX exec/spawn baseline](minix-elf-exec-and-spawn.md)

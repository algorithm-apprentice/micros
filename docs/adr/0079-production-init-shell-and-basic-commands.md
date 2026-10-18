# ADR-0079: Production Init, Shell, and Basic Commands

- Status: Accepted
- Date: 2026-10-18
- Refines: ADR-0004, ADR-0005, ADR-0008, ADR-0009, ADR-0025, ADR-0029,
  ADR-0030, ADR-0038, ADR-0039, ADR-0041, ADR-0042, ADR-0046, ADR-0047,
  ADR-0048, ADR-0049, ADR-0050, and ADR-0072

## Context

Development-DAG node P is complete:

- all six static services start and seal bootstrap authority;
- PM creates a hidden process, coordinates VFS and VM, prepares execution, and
  atomically activates a dynamic application;
- VFS installs init's console descriptors and duplicates selected descriptors
  for children;
- applications receive checked argv, envp, PM/VFS endpoints, PID, and page
  size;
- normal return becomes PM `EXIT`;
- wait consumes an exact zombie; and
- the current integration fixture proves failed-spawn rollback, inherited I/O,
  child exit/wait, and post-init resource equality.

That PID-1 program and its PM trigger are test fixtures. The next
dependency-ready outcome is node Q: production init, an interactive shell,
`echo`, `cat`, `ls`, `ps`, and one deterministic serial end-to-end gate.

The fixed MINIX baseline is documented in
[the init/shell study](../research/minix-init-shell-basic-commands.md).
MINIX init is PID 1, owns shell/getty sessions, waits for children, and
restarts sessions. MINIX shell prompts, parses, resolves commands through PATH,
creates a foreground child, and waits. The selected commands join arguments,
stream files, sort directory entries, and display process-authority data.

The complete MINIX/NetBSD userland requires fork, exec replacement, signals,
sessions, credentials, terminal ioctls, pipes, redirections, globbing, shell
language, libc, KVM/sysctl, and persistent filesystems that are outside the
v0.1 graph. ADR-0009 requires direct spawn before fork.

## Decision

Implement the exact
[node-Q architecture contract](../architecture/init-shell.md).

### Production init publication

The canonical node-Q system build enables one explicit production-init policy
in the real PM service. Historical component images remain unchanged.

After PM receives and validates the one kernel-origin
`MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED` event:

1. PM enables its application runtime exactly once;
2. the production-init owner builds one canonical trusted request directly in
   the coordinator's resident request page;
3. the request names `/sbin/init`, argv `init`, environment
   `PATH=/bin:/sbin`, and zero descriptor actions;
4. PM invokes the existing trusted-init coordinator;
5. VFS installs the synthetic console as descriptors 0, 1, and 2;
6. activation publishes semantic PID 1 and the sole reaper record; and
7. PM enters the ordinary receive loop.

Any request-build, VFS, VM, kernel, activation, or local-commit disagreement is
fatal. Trusted init is attempted once and never retried or assigned another
PID.

The production-init policy is selected at build time for the canonical user
environment. It is not inferred from a missing test macro, service count,
RAMFS contents, or failed spawn.

### Shared canonical spawn-request builder

One portable builder becomes the sole production owner of canonical local
spawn-request encoding. It receives caller-local path/argument/environment
byte strings and descriptor actions, validates all ADR-0050 capacities and
canonical references, zeroes the complete declared request, and publishes the
encoded size only after success.

The builder:

- creates no cross-process pointer authority;
- does not perform VFS lookup or PM policy;
- accepts at most eight arguments, eight environment entries, and eight
  descriptor actions;
- requires one nonempty absolute or relative path and one through eight
  arguments;
- requires every string to contain its terminating NUL within 256 bytes;
- validates descriptor source/target bounds and duplicate operations; and
- leaves the output and size unchanged on failure.

PM's fixed init policy, init's shell request, the shell's command requests, and
existing fixture request tests use this one encoding owner.

### Application client boundary

Add one standard-library-free application client layer over the existing raw
runtime:

- PM spawn;
- PM wait;
- PM application-process snapshot;
- VFS open;
- VFS close;
- VFS read;
- VFS write; and
- VFS getdents.

Every operation:

- builds one zero-initialized 64-byte IPC message;
- uses exact endpoint globals from application startup;
- creates only the minimum directional direct grant;
- revokes every created grant after the returning call;
- validates peer generation, message type, echoed request, result range,
  command-specific outputs, and every reserved byte before publication; and
- returns explicit typed results rather than `errno` or success-shaped
  fallback.

Each application owns one aligned 4,096-byte transfer/request page in its BSS.
The client layer owns no heap, global registry, descriptor table, process
table, retry, or hidden fallback.

### Persistent command display name

PM process records gain one non-authoritative display name:

```text
maximum bytes: 24
source: executable path basename
lifetime: spawning through zombie
truncation: first 24 basename bytes
display bytes: ASCII 0x21..0x7e retained; every other byte becomes `?`
```

The name is derived during PM's local record reservation from the already
validated request snapshot. Replacing non-display bytes prevents a path from
injecting lines or columns into `ps`; it does not reject the spawn. The name
affects no lookup, authorization, lifecycle transition, executable selection,
or reply. Free and quarantined records have zero length and zero bytes.
Complete PM-table validation checks length, display bytes, zero tail, and
lifecycle consistency.

Truncation matches the baseline concept of a bounded command title and does
not reject an otherwise valid spawn path.

### PM application snapshot protocol

Add:

```text
MICROS_PM_MESSAGE_PROCESS_LIST = 0x00010005
snapshot buffer size           = 4096
snapshot record size           = 64
snapshot record capacity       = 64
command bytes                  = 24
```

The request uses the ordinary call/reply boundary:

```text
payload 0   u32 protocol version
payload 4   u32 flags = 0
payload 8   u32 caller-to-PM WRITE grant
payload 12  u32 buffer size = 4096
payload 16  u64 grant offset = 0
payload 24  24 zero bytes
```

The caller must resolve to one running PM application. PM builds a complete
zeroed candidate page before any copy. It publishes only `RUNNING` and
`ZOMBIE` application records, sorted by ascending semantic PID.

Each 64-byte record is:

```text
offset 0   u32 version = 1
offset 4   u32 size = 64
offset 8   u32 state: RUNNING = 1, ZOMBIE = 2
offset 12  u32 flags: bit 0 = reaper
offset 16  u64 pid
offset 24  u64 parent pid, zero only for init
offset 32  u32 command length, 1..24
offset 36  u32 reserved = 0
offset 40  u8 command[24], zero after command length
```

The ordinary `MICROS_PM_MESSAGE_RESULT` echoes the process-list request and
uses:

```text
payload 16  u64 record count
payload 24  u32 record size = 64
payload 28  u32 record capacity = 64
payload 32  16 zero bytes
```

On error every output field is zero. Expected caller-grant failures return
`MICROS_PM_RESULT_GRANT`; malformed request/caller/state results retain the
existing public result namespace. Unknown transport failure is fatal.

PM copies the complete zeroed 4,096-byte candidate page through the grant,
regardless of record count. This validates the declared full range and makes
the unused record tail PM-published zero data rather than caller-owned state.

The snapshot is observational data, never endpoint, process-handle, wait, or
reply authority. Static services and kernel threads are absent by design.

### Public VFS directory-record layout

The already implemented 80-byte application directory record becomes a
versioned public layout in `include/micros/vfs.h`:

```text
offset 0   u32 record size = 80
offset 4   u32 canonical mode
offset 8   u32 name length, 1..60
offset 12  u32 reserved = 0
offset 16  u8 name[60], zero after name length
offset 76  u32 reserved = 0
```

No VFS behavior or backend record changes. The application client and `ls`
validate every field before accepting a name.

### Production init application

`/sbin/init` validates:

- `argc == 1`;
- `argv[0] == "init"`;
- one exact `PATH=/bin:/sbin` environment entry;
- PID 1;
- page size 4,096; and
- distinct concrete PM and VFS endpoints.

It then loops forever:

1. invoke the test checkpoint adapter, which is a no-op in the production
   image;
2. build a request for `/bin/sh`, argv `sh`, environment `PATH=/bin`;
3. duplicate descriptors 0, 1, and 2 explicitly;
4. spawn one shell;
5. wait for that exact PID;
6. require a normal shell exit; and
7. repeat.

Protocol corruption, spawn rejection, wrong wait identity, or non-normal shell
termination is fatal. A normal shell exit code does not terminate init and
does not change PID 1.

This is the bounded one-console equivalent of MINIX session restart. No delay
or retry loop masks a failing shell image; expected operation is that a valid
shell waits for user input.

### Interactive shell

`/bin/sh` validates argv/env/runtime metadata, writes:

```text
micros shell 0.1.0
```

then repeatedly:

1. writes the raw prompt `micros$ `;
2. reads one canonical line from descriptor 0 into a 256-byte buffer;
3. requires one terminating newline and accepts only printable ASCII plus
   space and tab before it;
4. splits on nonempty runs of space or tab into at most eight arguments;
5. ignores an empty line;
6. handles exact built-in `exit` with no operands by returning zero;
7. resolves a name without `/` as `/bin/<name>`;
8. accepts at most 250 bytes for a slash-free name so `/bin/`, the name, and
   its NUL fit the 256-byte spawn path bound;
9. prints `sh: command too long` and returns to the prompt for a longer
   slash-free name without calling PM;
10. passes any command word containing `/` unchanged as an explicit path;
11. spawns the command with `PATH=/bin` and explicit duplication of
    descriptors 0, 1, and 2;
12. waits for the exact returned PID; and
13. returns to the prompt after normal completion.

There is no quoting, escaping, expansion, globbing, assignment, script,
redirection, pipe, background job, job control, alias, function, or command
substitution. A malformed line prints `sh: syntax error`. A `NOT_FOUND` spawn
prints `sh: <name>: not found`; every other expected spawn rejection prints
`sh: <name>: failed`. These outcomes do not exit the shell.

### `echo`

`/bin/echo`:

- accepts zero through seven operands after argv[0];
- treats only the first operand `-n` as newline suppression;
- writes remaining operands separated by one ASCII space;
- appends one newline unless suppressed; and
- returns 1 on incomplete output, otherwise 0.

### `cat`

`/bin/cat`:

- accepts zero through seven operands;
- treats zero operands or an operand exactly equal to `-` as descriptor 0;
- accepts no other options;
- opens every other path read-only in order;
- reads until a zero-byte EOF result;
- writes every byte to descriptor 1 before continuing;
- closes every successfully opened pathname descriptor but never closes
  descriptor 0;
- writes `cat: <path>: error` to descriptor 2 on an operand failure; and
- returns 1 if any operand fails, otherwise 0.

### `ls`

`/bin/ls`:

- accepts zero or one path operand and no options;
- uses `.` when no path is supplied;
- opens the path read-only as a directory;
- reads complete 80-byte records until EOF;
- validates every record, filters exact synthetic `.` and `..`, and stores at
  most 64 remaining names;
- sorts names by unsigned bytewise lexical order; and
- writes one name plus newline per record.

Any malformed record or I/O failure writes `ls: error` to descriptor 2 and
returns 1. It does not print permissions, owners, sizes, columns, colors, or
recursive output.

### `ps`

`/bin/ps` accepts no operands. It requests one PM snapshot, validates all
records and zero tail, and writes:

```text
PID PPID STATE COMMAND
```

followed by ascending-PID rows:

```text
<pid> <parent-pid> <RUN|ZOMBIE> <command>
```

It lists only PM-owned applications. In the deterministic node-Q scenario the
rows are init, shell, and the running `ps` command.

### Immutable userland seed

One checked seed contains:

```text
/
/bin
/etc
/sbin
/sbin/init
/bin/sh
/bin/echo
/bin/cat
/bin/ls
/bin/ps
/etc/motd
```

All six application files are validated by the existing application ELF
checker before encoding. Executables use mode `0555`; `/etc/motd` uses `0444`
and the existing exact bytes:

```text
micros ramfs\n
```

The generator reuses the RAMFS seed encoder and retains its 64-entry,
64-block, and 262,144-byte aggregate bounds. It does not read unchecked ELF
bytes after validation.

Expose:

```text
build-userland-images
test-qemu-shell
```

The build workflow checks production init, shell, four commands, and the
production seed. The QEMU workflow uses a test-instrumented init adapter but
the same init core, shell, commands, client library, seed policy, PM/VFS/VM
transactions, and six static services.

### Deterministic end-to-end scenario

The test init adapter has two test-only checkpoints:

1. after init startup and before the first shell spawn; and
2. after the exact shell wait consumes the shell zombie.

The production adapter is a no-op and init continues to respawn shells. The
test final adapter requests one node-Q-only aggregate-output drain through VFS
and TTY, then reports through one authenticated application breakpoint. It
adds no production message or shutdown authority.

The drain call is enabled only in the shell test image:

1. VFS accepts the exact init generation and forwards one test-only drain call
   to TTY;
2. TTY replies immediately only when
   `micros_tty_output_pending()` is false;
3. otherwise TTY retains the one reply token while continuing to handle UART
   interrupts;
4. the interrupt path replies exactly once when both echo and resident write
   output become empty; and
5. VFS then replies to init.

The drain is busy if another test drain or incompatible pending completion is
live. Native transition tests cover echo-only, write-only, combined output,
duplicate drain, reply failure, and exact cleanup.

The QEMU runner gains a repeatable prompt-paced serial-input interface owned by
`qemu_smoke_arguments.py` and `qemu_smoke_execution.py`:

```text
--serial-input-step-trigger <raw UTF-8 bytes>
--serial-input-step-hex <payload bytes>
```

The two options are repeated in pairs. They are mutually exclusive with the
existing single-trigger form. The runner searches raw accumulated output only
after the previous matched trigger, writes one payload exactly once, and does
not arm the next step until that payload's following trigger appears. Success
requires every configured step to have been sent.

It also gains repeatable:

```text
--exact-transcript-line <complete line>
```

The outcome owner locates the unique first and final expected records and
requires the complete terminated record slice between them to equal the
configured lines byte-for-byte. Extra, duplicate, missing, reordered, or
unterminated records inside the shell session fail. Foundation records before
the init-ready line remain outside the transcript.

For node Q the raw trigger is `micros$ ` for each of five stages:

```text
1  echo hello micros\r
2  cat /etc/motd\r
3  ls /bin\r
4  ps\r
5  exit\r
```

The concatenation remains exactly 48 bytes:

```text
6563686f2068656c6c6f206d6963726f730d636174202f6574632f6d6f74640d6c73202f62696e0d70730d657869740d
```

TTY echoes each line when that stage is sent. Because the next line is not
sent until the next prompt, the exact ordered transcript is deterministic:

```text
MICROS_INIT_TEST_READY pid=1
micros shell 0.1.0
micros$ echo hello micros
hello micros
micros$ cat /etc/motd
micros ramfs
micros$ ls /bin
cat
echo
ls
ps
sh
micros$ ps
PID PPID STATE COMMAND
1 0 RUN init
2 1 RUN sh
6 2 RUN ps
micros$ exit
MICROS_SHELL_TEST_PASS init=production shell=foreground commands=echo,cat,ls,ps resources=baseline
```

PIDs 3, 4, and 5 belong to the completed `echo`, `cat`, and `ls` commands and
must already be reaped when `ps` runs.

At the first init checkpoint, the kernel captures the complete post-init
resource state. At the final checkpoint it requires the same live kernel, VM,
PM, VFS, RAMFS, and TTY resources except for explicitly monotonic inactive
PIDs, tokens, generations, VFS `next_tty_request_id`, and TTY
`last_accepted_request_id`. Those two request-ID high-water marks must not
decrease or wrap. Every live pending-read, read-completion, write-completion,
and action request identity remains exact; baseline and final require those
states to be absent. Only init remains as a live application.
The shell test projection also canonicalizes two empty TTY ring cursors:

- `input_head` becomes zero only when `input_count`,
  `current_line_length`, and `completed_line_count` are zero and every input
  entry byte/flag is zero; and
- `echo_head` becomes zero only when `echo_count` is zero and every echo byte
  is zero.

Nonempty counts, residual bytes/flags, pending reads/completions, output,
grants, live request identities, drop counters, or any other TTY state remain
exact resource differences. Targeted mutation evidence must prove those states
still fail comparison.

The aggregate-output drain must complete before the final breakpoint. After
the kernel authenticates that breakpoint and validates the complete resource
projection, the test-only kernel path:

1. polls UART `LSR.THRE|LSR.TEMT` through the existing bounded physical-empty
   check;
2. invokes the existing terminal panic-seizure mechanism;
3. validates console phase `PANIC`, route phase `PANIC`, and released interrupt
   ownership;
4. writes the pass record directly through the seized UART;
5. flushes the UART; and
6. performs nonreturning SBI success shutdown.

This success-only test transition occurs after resource comparison and is
never linked into a production image. No routine kernel write occurs while TTY
owns the console. The emitted record is:

```text
MICROS_SHELL_TEST_PASS init=production shell=foreground commands=echo,cat,ls,ps resources=baseline
```

The host never kills QEMU to manufacture success.

## Failure and precedence

- Production-init request construction and trusted-init failure are fatal.
- PM process-list malformed version/shape precedes caller resolution; caller
  authority precedes grant copy; expected grant failure returns `GRANT`.
- Snapshot building is read-only and failure-atomic.
- Application client outputs publish only after complete reply validation and
  successful grant revocation.
- Shell syntax or expected spawn rejection is recoverable and returns to the
  prompt.
- A 251-byte slash-free command name is a recoverable local resolution error
  and does not call PM.
- Shell wait identity/protocol corruption is fatal.
- Command I/O errors return command status 1; the shell remains live.
- Init never exits through PM. Its protocol or child-identity corruption is
  fatal.
- Test-only checkpoint or resource disagreement is fatal and cannot emit the
  pass marker.

## Test-first implementation sequence

1. Add native Red for the shared spawn builder and production-init request.
2. Add PM command-name lifecycle and process-snapshot model/protocol Reds,
   then implement the minimum PM behavior and runtime grant-copy adapter.
3. Add application-client and public VFS directory-record Reds.
4. Add native init-loop, shell parser/dispatch, and command behavior Reds.
5. Add six checked application images and immutable userland seed.
6. Add the target init/shell adapters and expected-failing QEMU image.
7. Complete ordered serial input/output, process snapshot, drain, resource
   baseline, and clean shutdown.
8. Run focused native checks, then the complete milestone gate, exact producer
   and reuse evidence, and independent implementation review.

Portable parsers, builders, state machines, and snapshot logic start with
native deterministic tests. The serial QEMU scenario is the correct Red for
the integrated TTY/PM/VFS/VM/application boundary.

## Design review disposition

Initial independent review found five contract gaps:

- one-shot serial input could not preserve command/echo ordering;
- PM copied only active snapshot bytes rather than the declared full page;
- public process states had no numeric wire values;
- `ls` capacity omitted synthetic `.` and `..`; and
- zero-operand `cat` contradicted the claimed MINIX parity.

The corrected design added prompt-paced input stages, complete 4,096-byte
snapshot publication, RUNNING=1/ZOMBIE=2, validated synthetic-entry filtering,
stdin/`-` cat behavior, and a separate staged classification for broader
command options.

Correction review then found that empty TTY ring heads and monotonic VFS/TTY
request-ID high-water marks cannot compare literally after the scripted
session, and that relative slash-bearing paths remained contradictory. The
design now canonicalizes heads only under complete empty/zero proof, preserves
monotonic high-water comparison while keeping live request identities exact,
and passes every explicit slash path unchanged to VFS.

Acceptance review finally found that the old drain did not include echo or
physical UART emptiness, ordered patterns were only a subsequence, and a
251-byte simple command lacked a transition. The final contract adds a
test-only aggregate TTY drain, exact contiguous transcript matching, a
250/251-byte recoverable name boundary, and the post-comparison
`THRE|TEMT`/terminal-seizure/raw-marker/SBI sequence.

Final independent read-only review concluded:

> No significant issues found in the reviewed changes.
>
> No substantive issue remains. The staged node-Q design is
> implementation-ready.

Reviewers ran no builds, suites, models, QEMU, or documentation gates.
Acceptance records a complete design, not implemented userland or a passing
shell workflow.

## Preserved invariants and non-goals

This decision does not add:

- fork, exec replacement, signals, sessions, credentials, login, getty, or
  multiple terminals;
- pipelines, redirection, scripts, shell expansion, job control, background
  jobs, or a general POSIX parser;
- seek, stat, unlink, rename, truncate, pipes, sockets, networking, or
  persistent storage;
- a heap, libc, dynamic linker, shared objects, or writable executable text;
- static-service or kernel-thread enumeration in `ps`;
- a production drain request, test report, shutdown syscall, or application
  reset authority;
- retries, sleeps, timing-based correctness, or success-shaped fallback; or
- changes to existing static-service identities, kernel operations, frame
  ownership, page-fault policy, one-thread policy, or one-hart policy.

## Alternatives considered

### Port the complete MINIX/NetBSD init and shell

Rejected. Their dependencies include fork, signals, sessions, credentials,
getty, libc, terminal ioctls, shell grammar, pipes, redirection, and filesystem
features outside v0.1.

### Implement shell commands as privileged or static services

Rejected. Commands are ordinary applications and must exercise the reviewed
spawn, descriptor, VFS, exit, and wait paths.

### Make every command a shell built-in

Rejected. It would avoid proving foreground application spawn and cleanup.
Only `exit` is shell-owned.

### Omit zero-operand `cat`

Rejected. MINIX raw `cat` selects standard input when no pathname is supplied.
The bounded command retains that baseline behavior through inherited
descriptor 0 without adding options or new VFS operations.

### Let the shell inspect PM private memory for `ps`

Rejected. PM must publish a bounded read-only application snapshot through an
explicit grant protocol.

### Include static services in the PM snapshot

Rejected. PM does not own their lifecycle. System-wide inspection requires a
separate DS/RS or diagnostic authority.

### Exit init after the scripted shell

Rejected. PM correctly forbids reaper exit. A test-only adapter reports after
the shell is reaped; production init respawns another shell.

### Give applications a shutdown operation

Rejected. The test kernel owns clean SBI shutdown after authenticated evidence.

## References

- [MINIX baseline](../research/minix-init-shell-basic-commands.md)
- [Node-Q architecture](../architecture/init-shell.md)
- [Development DAG](../architecture/development-dag.md)
- [ELF-spawn integration](../architecture/elf-spawn-integration.md)
- [PM coordinator](../architecture/pm-spawn-coordinator.md)
- [Application runtime](../architecture/application-runtime.md)
- [Testing strategy](../testing-strategy.md)

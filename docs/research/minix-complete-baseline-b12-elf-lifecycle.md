# B12 ELF Spawn and Application-Lifecycle Baseline Audit

## Status

```text
audit unit:       B12 ELF spawn and application lifecycle
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; two blocking findings; not certified
implementation:   forbidden during complete-audit phase
```

B12 audits the complete cross-owner transaction rather than assigning another
production source family. It covers request capture, hidden reservation,
executable lookup/snapshot, VM load/freeze, descriptor preparation, kernel
execution preparation, hidden metadata, activation, mandatory postpublication
commit, reverse rollback, application startup/normal return, exit teardown,
zombie/wait, and the absent fault-exit path.

## Exact primary path manifest

B12 has **zero primary production paths**. The complete-audit contract assigns
all production paths to B01-B11 and assigns B12 only the integration operation
families. B12 does not duplicate or transfer those owners.

The cross-owner operation trace uses these already assigned source owners:

| Owner unit | Participating production sources |
| --- | --- |
| B02 | frame ownership, address-space translation, page-table, and release mechanisms consumed by operations 12/13 |
| B03/B04/B05 | kernel object/scheduler, endpoint/IPC, grant, trap, execution, and application runtime paths consumed by preparation, activation, IPC, copy, startup, and exit |
| B06 | complete bootstrap/runtime validation re-entered by operations 12/13 |
| B07 | `kernel/vm/**`, `servers/vm/**`, `lib/vm/**`, and VM public headers for load, scratch fill, freeze, abort, commit, and exit release |
| B08 | `kernel/pm/**`, `servers/pm/**`, `lib/spawn/**`, `include/micros/pm.h`, and `include/micros/spawn.h` for lifecycle coordination, request decoding, wait, and operation 13 |
| B09 | TTY service-side cancel/collect/result/event transitions invoked during conditional console cleanup |
| B10 | RAMFS lookup/read/reference paths for executable acquisition |
| B11 | `servers/vfs/vfs_spawn.c`, `vfs_process.c`, `vfs_console.c`, `vfs_namespace.c`, `vfs_ownership.c`, `vfs_state.c`, `vfs_core.c`, transfer/service adapters, and VFS public headers |

Application startup is additionally owned by B05's
`include/micros/application_runtime.h` and `lib/runtime/application*` paths.
B14 owns every native/model/QEMU/generator/checker path that proves this
operation graph.

## Closed integration-operation manifest

| ID | Operation family |
| --- | --- |
| B12-O01 | Canonical parent request copy and PM PID/process reservation |
| B12-O02 | VFS executable lookup, policy, complete buffering, and grants |
| B12-O03 | VM ELF/stack planning, frame selection, page fill, and freeze |
| B12-O04 | VFS root/cwd/descriptor preparation |
| B12-O05 | Kernel execution preparation and hidden PM/VFS commit |
| B12-O06 | Kernel activation, VM commit, VFS release, and parent result |
| B12-O07 | Reverse prepublication rollback at every completed stage |
| B12-O08 | Application startup and normal-return PM exit |
| B12-O09 | Stop, VFS cleanup, kernel detach, VM release, process release, zombie, and wait |
| B12-O10 | Fault exit and broader process-image lifecycle omissions |
| B12-O11 | End-to-end validation/copy/storage multiplication |

## Fixed MINIX source evidence

The B12 comparison uses only local source:

- `minix/servers/pm/exec.c`
- `minix/servers/pm/event.c`
- `minix/servers/pm/forkexit.c`
- `minix/servers/pm/main.c`
- `minix/servers/pm/signal.c`
- `minix/servers/pm/trace.c`
- `minix/servers/vfs/exec.c`
- `minix/servers/vfs/main.c`
- `minix/servers/vfs/misc.c`
- `minix/servers/vfs/open.c`
- `minix/servers/vfs/path.c`
- `minix/servers/vfs/filedes.c`
- `minix/servers/vfs/vnode.c`
- `minix/servers/vfs/worker.c`
- `minix/servers/vfs/coredump.c`
- `minix/servers/vm/fork.c`
- `minix/servers/vm/exit.c`
- `minix/servers/vm/main.c`
- `minix/servers/vm/pagefaults.c`
- `minix/lib/libexec/exec_elf.c`
- `minix/lib/libexec/exec_general.c`
- `minix/kernel/system/do_exec.c`
- `minix/kernel/system/do_fork.c`
- `minix/kernel/system/do_clear.c`
- `minix/kernel/system/do_runctl.c`
- `minix/kernel/system/do_privctl.c`
- `minix/kernel/system/do_schedctl.c`
- `minix/kernel/system/do_schedule.c`
- `minix/kernel/system/do_kill.c`
- `minix/kernel/system/do_getksig.c`
- `minix/kernel/system/do_trace.c`
- `minix/kernel/system/do_vmctl.c`
- `minix/kernel/system.c`
- `minix/kernel/arch/i386/exception.c`
- `minix/lib/libc/sys/stack_utils.c`
- `minix/lib/libc/sys/posix_spawn.c`
- `minix/lib/libsys/sef_signal.c`
- `lib/csu/arch/riscv/crt0.S`
- `lib/csu/common/crt0-common.c`

## Logical variables

| Variable | Meaning |
| --- | --- |
| `X` | executable bytes, at most 131,072 |
| `q` | 4 KiB executable chunks, `ceil(X/4096)`, at most 32 |
| `u` | application data pages including stack, 4 through 16 |
| `A` | descriptor actions, at most 8 |
| `S` | canonical spawn-request bytes, at most 4,096 |
| `Vpm` | B08 complete PM-table validation |
| `Vvfs` | B11 complete VFS-state validation |
| `Vsvc` | B07 complete VM-service state validation |
| `Vram` | B10 complete RAMFS-state validation |
| `Vram*` | B10 complete RAMFS validation while lookup path bytes are staged |
| `K13_common` | B08 shared operation-13 whole-system validation before command-specific work |
| `C13_success` | Non-overlapping command-specific reserve/prepare/activate work |
| `K12load(u)` | B07 complete kernel operation-12 load/scratch/freeze validation for `u` pages |
| `K12commit` | B07 operation-12 `COMMIT_LOAD` common plus command-specific kernel validation |
| `K12abort` | B07 operation-12 `ABORT_LOAD` common plus command-specific kernel validation |
| `K12exit` | B07 operation-12 `EXIT_RELEASE` common plus command-specific kernel validation |
| `Vgrant` | B05 complete grant-relation/range/copy validation |
| `Gspawn` | Spawn grant create/copy/revoke validation outside operation-13 common preflight |

`K13_common` is exactly:

```text
Vboot + Vvmrt + Vpmrt + Vgrant
```

It excludes the selected command's object, endpoint, scheduler, mapping,
ownership, output, and mutation work; successful reserve/prepare/activate
collectively contribute `C13_success`. `Gspawn` excludes every `Vgrant`
already contained in `K13_common`. `K12load(u)` retains B07's exact per-page
expansion, including at least `(8 + 2*nroot)*Vkh` per scratch map/commit pair.
`K12commit`, `K12abort`, and `K12exit` are disjoint later operation-12
commands; each includes the common whole-system operation-12 preflight and
its own command-specific `Vkh`/state work.

## Operation ledger

### B12-O01 Canonical request capture and hidden reservation

MINIX libc builds one bounded argument/environment frame. PM forwards the
exact caller and suspends completion while VFS copies the complete frame
before destructive work.

`micros` application spawn copies one pointer-free request into PM's resident
page, completely decodes it, validates stack occupancy, checks the exact
parent endpoint, consumes one monotonic PID, and reserves one hidden PM/kernel
process.

The direct hidden child is a required adaptation to the spawn-before-fork
design. Generation-safe identities, canonical request validation, and
complete prepublication rollback are compatible safety extensions. B08 owns
PM record/PID/free-slot algorithms and the 12 successful-path `Vpm` count for
public spawn. Trusted init enters the coordinator directly and bypasses public
caller/service-dispatch validation; its smaller exact PM count remains
separate B14 evidence.

`micros` admits only one global spawn transaction and one global exit
transaction across PM/VFS/VM. PM completes the entire transaction before its
next receive; another application `CALL` therefore blocks in the kernel IPC
sender queue and is processed only after the active transaction clears.
Direct owner-level VFS/VM control presented out of phase returns a state
result, but ordinary concurrent application lifecycle calls do not observe
that busy state. MINIX associates executable/lifecycle work with process
records and VFS workers, allowing unrelated processes to make progress. The
current global blocking serialization is a staged substitution under SOM-23,
not baseline concurrency.

### B12-O02 VFS executable lookup and stable snapshots

MINIX VFS copies the argument frame, resolves the executable path, checks
regular-file type and execute authority, validates ELF ranges, and then uses
loader callbacks to read/map the required executable portions. It does not
retain a second complete executable-file image in both VFS and VM.

`micros` VFS:

1. copies PM's `S`-byte request into its own 4 KiB buffer;
2. resolves the executable through RAMFS;
3. reads all `X` bytes through `q` RAMFS calls into a 131,072-byte resident
   executable buffer;
4. retains the executable vnode/reference;
5. creates complete executable and request grants to VM; and
6. keeps both snapshots until mandatory release after activation or rollback.

The pointer-free request and non-transitive grants require stable direct
participants. They do not require retaining a complete executable snapshot in
both VFS and VM. See B12-F002.

Executable vnode integration may exercise B11-F003's eager acquisition-time
threshold cleanup; later image release may exercise its final-reference
cleanup path.

One successful image preparation invokes at least:

```text
1 RAMFS lookup + q RAMFS reads
= 2*(1+q)*Vram + Vram*
```

before any final `PUTNODE`, plus one VFS control facade's `2*Vvfs` and B05
grant creation/copy/revocation work.

### B12-O03 VM validation, page construction, and freeze

MINIX validates the ELF header/program ranges before its destructive boundary,
then loads segment spans through reusable callbacks, zero-fills anonymous
bytes, builds a separate stack, and returns entry/SP only after completion.

`micros` VM independently copies the full VFS executable/request grants into
its own resident buffers, validates the fixed RISC-V ELF64 subset and
canonical request, selects four table frames plus `u` data frames, binds one
kernel load token, fills every page through the VM-only scratch alias, and
freezes one mapping generation.

ELF/stack planning and page contents preserve the baseline. RISC-V,
non-transitive grants, eager resident pages, exact scratch authority, and
hidden freeze are required adaptations. Frame selection and validation
cadence remain B07-F001 through B07-F005.

One successful VM load performs:

```text
(8 + 7*u)*Vsvc
+ K12load(u)
+ q executable grant-copy operations
+ 1 request grant-copy operation
```

before the separate postactivation VM commit.

### B12-O04 Descriptor/root/cwd preparation

MINIX fork/exec and `posix_spawn` apply descriptor inheritance/file actions in
one process-owned descriptor table before successful image publication.

`micros` asks VFS to retain root/cwd and apply up to `A` explicit duplicate
actions into hidden child metadata. Trusted init receives three independently
owned synthetic console descriptions.

The bounded pointer-free duplicate-action representation is a required
adaptation. Starting every public child with an empty descriptor table and
providing no implicit inheritance or close-on-exec semantics is a staged
substitution under SOM-06. Additional action types and general descriptor
operations remain SOM-12. B11-F002 owns child-record placement timing, and
B11-F005 owns the current `O(A^2)` same-reference rescan versus a linear
descriptor plan.

### B12-O05 Execution preparation and hidden commit

MINIX creates/updates kernel process state, privilege/scheduler policy, and
final execution context before releasing the new image.

`micros` operation 13 creates one held thread, reserved endpoint, immutable
application profile, scheduler assignment, and token-bound PC/SP. VFS then
commits hidden process/descriptor state, and PM commits its hidden metadata.
No application endpoint is externally usable yet.

Exact generations, profiles, tokens, and non-failing local commit plans are
compatible safety adaptations. Kernel/PM validation costs remain B08-F005.

### B12-O06 Activation and mandatory postpublication commit

Activation is the sole publication point:

1. PM preflights its local activation plan;
2. kernel operation 13 atomically publishes the endpoint and runnable thread;
3. PM performs its non-failing `RUNNING` commit;
4. VM commits the frozen mapping into persistent active state;
5. VFS releases executable/request grants, bytes, and temporary vnode state;
6. only then may PM reply with the child PID.

This hidden-child publication is the required safe replacement for MINIX's
destructive `PARTIAL_EXEC` boundary. Failure after activation is fatal and
cannot become ordinary spawn rejection.

The postactivation VM command performs three `Vsvc` validations: one service
instance validation and VM-state pre/post validation. VFS release performs
`2*Vvfs`. Kernel mapping commit separately pays `K12commit`.

### B12-O07 Reverse rollback

Every expected preactivation failure releases the highest completed stage in
strict reverse order:

```text
PM/VFS hidden commit
-> kernel prepared execution
-> VFS descriptors
-> VM frozen mappings
-> VFS image/grants/reference
-> kernel reserved process
-> PM record
```

Rollback failure is fatal. Parent loss suppresses the final reply but does not
skip any release. This is a compatible safety extension over MINIX's
destructive partial-exec kill behavior.

VFS image release may take the executable vnode through B11-F003's optional
final backend-reference cleanup path. Threshold cleanup, when reached, was
already paid during O02 integration.

Rollback from the latest preactivation stage adds up to:

```text
7*Vpm                         (B08)
6*Vvfs                        (REVERT_HIDDEN, ABORT_DESCRIPTORS, RELEASE_IMAGE)
3*Vsvc                        (VM ABORT_LOAD)
K12abort                      (kernel frozen-load rollback)
2 operation-13 abort commands
RAMFS PUTNODE work when the executable vnode reaches final release
grant revoke validation
```

to the validations already paid by the failed forward prefix.

### B12-O08 Application startup and normal return

MINIX architecture startup binds arguments/environment, performs hosted
initialization, calls `main`, and converts normal return to process exit.

`micros` starts at a fixed RV64 assembly entry, clears unused `gp`/`tp`,
validates the complete bounded initial stack page, publishes PM/VFS/PID/page-
size globals, calls `micros_application_main`, and turns its signed 32-bit
return into one no-reply PM `EXIT` call.

The RV64 stack/auxv format and freestanding PM exit are required adaptations.
Complete pointer/string/auxv validation is a compatible linear safety
extension. TLS and thread-runtime behavior remain SOM-08; hosted libc,
constructors, destructors, finalizers, and public startup interfaces remain
SOM-17.

### B12-O09 Normal exit, zombie, and wait

MINIX:

1. cancels a pending process timer and captures accounting;
2. stops the process and sends `VM_WILLEXIT`;
3. asks VFS to exit or create a core file;
4. marks the PM record exiting;
5. for a normal exit, calls `zombify()` immediately, so a waiting parent may
   receive/collect status before kernel/VM teardown;
6. reparents children and sends session-leader `SIGHUP`;
7. after VFS replies, serially publishes the process-exit event to every
   subscriber;
8. only then stops scheduler policy, clears kernel state, releases VM state,
   completes tracer exit, and frees a parent-collected record.

Core-dump exit delays `zombify()` until the VFS/core result is available, but
still before kernel clear and VM release.

`micros` performs:

```text
kernel EXIT_BEGIN
-> PM STOPPED/reparent/exit-transaction commit
-> VFS EXIT_PROCESS
-> kernel EXIT_DETACH
-> VM EXIT_RELEASE
-> kernel RELEASE_EXITED_PROCESS
-> PM zombie publication
-> staged wait replies
```

VFS releases pending console work, descriptors, root/cwd, open files, vnodes,
backend references, and grants before kernel detach. VM releases every data/
table frame before process-slot release.

The PM commit immediately after kernel `EXIT_BEGIN` clears the exiting
process's wait state, reparents its children, records the exit token, and
publishes `EXITING/STOPPED` before VFS cleanup. B08's `9*Vpm` count includes
that prevalidated local mutation and its later acknowledgments.

MINIX's pre-VFS `VM_WILLEXIT` has no separate `micros` peer command. The
token-bound kernel `EXIT_BEGIN` permanently stops the sole thread while the
fixed no-fault VM mapping remains stable through VFS cleanup; that is the
required current adaptation. General VM exiting/fault coordination remains
staged under SOM-04.

MINIX later invokes the user scheduling server's `sched_stop()`. `micros`
removes the fixed kernel-owned scheduler assignment during operation-13
detach. The kernel mechanism is the required v0.1 substitute; external
user-space scheduler policy and its teardown are staged under SOM-02.

A complete ordinary exit with two wait outcomes reaches B08's `9*Vpm`, plus:

```text
2*Vvfs                         (one VFS EXIT_PROCESS control)
5*Vsvc                         (service + VM exit begin/complete pre/post)
K12exit                        (kernel mapping/frame exit release)
3 operation-13 commands        (EXIT_BEGIN, EXIT_DETACH, RELEASE)
RAMFS/TTY cleanup calls selected by owned resources
```

Resource-complete zombie publication is a compatible extension. B08-F004 owns
MINIX table-order/original-parent-first wait differences.

MINIX timer cancellation is SOM-10 breadth. Session/process-group ownership is
SOM-07 and `SIGHUP` delivery is SOM-09. Serialized process-event subscriber
publication is staged under SOM-23. Tracing/rusage/core behavior remains
SOM-26.

### B12-O10 Fault exit and broader lifecycle

MINIX routes architecture exceptions and VM page faults through kernel signal
delivery into PM's signal/exit path. It also supports destructive `exec`,
fork/COW, ptrace stop/resume/wait, rusage, core dumps, close-on-exec,
credentials, interpreters, dynamic linking, and broader image formats.

`micros` implements normal return/explicit exit only. An ordinary application
fault remains fatal to the current system rather than entering PM/VFS/VM
cleanup. Fault-generated exit is owned by SOM-09; fork/COW/exec by SOM-06;
descriptor inheritance and close-on-exec also belong to SOM-06. Ordinary
POSIX signal stop/continue remains SOM-09. Tracing, traced stop/resume/wait
states, rusage, and core dumps belong to SOM-26. Credentials belong to
SOM-11; additional descriptor/file actions to SOM-12; broader runtime
threads/TLS/cancellation to SOM-08; and hosted/public process/runtime
interfaces to SOM-17. SOM-04 owns the file-backed/demand-mapping mechanism;
SOM-14 owns its dependent public mapping interface.

The pinned MINIX fault path includes architecture exception classification,
VM's authenticated `VM_PAGEFAULT` dispatch, kernel signal generation,
`SYS_GETKSIG` selection/transfer of one pending signal set, SEF delivery to
PM's `process_ksig`, and eventual `exit_proc` cleanup. `SYS_GETKSIG` may scan
the process table for one signaled process and changes blocked/signaled state
as it transfers the pending set. Current `micros` has no corresponding
ordinary application transition.

### B12-O11 End-to-end validation multiplication

Combining the audited primary owners, one successful **public** spawn has the
non-overlapping lower bound:

```text
12*Vpm                         B08 PM/coordinator lifecycle
+ 8*Vvfs                       four VFS control commands
+ (11 + 7*u)*Vsvc              VM LOAD plus VM COMMIT_LOAD
+ 2*(1 + q)*Vram + Vram*       successful lookup and q reads
+ 3*K13_common                 shared reserve/prepare/activate preflight
+ C13_success                  command-specific operation-13 work
+ K12load(u)                   load begin, scratch pages, freeze
+ K12commit                    postactivation mapping commit
+ Gspawn                       non-operation-13 grant work
+ remaining non-overlapping PM/VFS/VM selected-record work
```

The expression excludes the parent application's original PM call envelope,
the optional final executable `PUTNODE`, and target scheduling/trap costs.
Trusted init bypasses the public frontend/service cadence and requires its own
smaller PM count.

MINIX checks each selected process, vnode, segment, region, and kernel
transition. It does not repeatedly prove every PM/VFS/VM/kernel object and
inactive tail at each transaction stage and page substep.

See B12-F001.

## Difference summary

| Area | Result |
| --- | --- |
| PM/VFS/VM/kernel authority split | Baseline parity |
| Capture request before mutation | Baseline parity |
| Pointer-free direct-grant request transport | Required adaptation |
| ELF range validation, segment fill, BSS zero, separate stack | Baseline parity |
| RISC-V ELF64 and startup ABI | Required adaptation |
| Eager fixed resident pages instead of demand/file-backed mappings | Staged substitution under SOM-04 |
| No fork/COW/destructive exec | Staged substitution under SOM-06 |
| No interpreter/dynamic linker/hosted process interface | Staged substitution under SOM-17 |
| Bounded pointer-free descriptor duplicate actions | Required adaptation |
| Empty-by-default child descriptors and no implicit inheritance/close-on-exec | Staged substitution under SOM-06 |
| Additional descriptor action/file-operation breadth | Staged substitution under SOM-12 |
| Hidden child instead of destructive caller replacement | Required adaptation |
| Complete reverse rollback before activation | Compatible extension |
| One global spawn and exit transaction | Staged substitution under SOM-23 |
| Activation as sole publication point | Baseline parity |
| Exact token/generation preflight and non-failing activation commit | Compatible extension |
| Stop process and retain identity through VFS cleanup before kernel/VM release | Baseline parity |
| Tokenized kernel stop instead of separate pre-VFS `VM_WILLEXIT` | Required adaptation |
| Fixed kernel scheduler detach mechanism | Required adaptation |
| User SCHED policy teardown | Staged substitution under SOM-02 |
| Delay zombie/wait publication until complete resource release | Compatible extension |
| Exit-time process timer cancellation | Staged substitution under SOM-10 |
| Session/process-group exit policy | Staged substitution under SOM-07 |
| Exit-time signal delivery including `SIGHUP` | Staged substitution under SOM-09 |
| Serialized process-event subscriber publication | Staged substitution under SOM-23 |
| End-to-end complete validation cadence | Divergence requiring correction |
| Complete executable/request snapshot buffering | Divergence requiring correction |
| Fault-generated exit and signals | Staged substitution under SOM-09 |
| Threads/TLS/cancellation | Staged substitution under SOM-08 |

## Time, stack, and auxiliary-space envelope

Maximum simultaneous transaction snapshots include:

```text
PM request page:                 4,096 bytes
VFS request page:                4,096 bytes
VM request page:                 4,096 bytes
VFS executable buffer:         131,072 bytes
VM executable buffer:          131,072 bytes
total request/executable bytes: 274,432 bytes
```

This excludes the RAMFS file bytes and final child pages. The snapshots remain
resident in PM/VFS/VM service images across the transaction; VFS/VM buffers
are cleared on release/commit/abort.

Application runtime uses one 4 KiB stack page. Initial stack occupancy is at
most 2,048 bytes and leaves 2,048 bytes for downward runtime growth. PM, VM,
VFS, and RAMFS each use one 4 KiB service stack; exit cleanup may additionally
enter TTY's 4 KiB service stack. Kernel operation-12/13 work runs on the
16 KiB supervisor stack.

Known automatic storage is already multi-kilobyte on audited B12 paths:

```text
VM load-preparation locals:            about 1.5 KiB
kernel freeze-plan aggregate:           1,224 bytes
endpoint-close plan:                    about 3 KiB
nested scheduler transition candidates: 5,120 bytes
kernel scheduler/return plan objects:   about 4 KiB in selected paths
```

PM/VFS request and peer-message plans are generally hundreds of bytes, but
they nest with their owner validators and IPC/grant wrappers. Endpoint detach
during exit can nest the close plan and scheduler candidates on the 16 KiB
supervisor stack. No direct target stack-usage evidence covers the complete
forward, latest-stage rollback, postactivation commit, or exit teardown peaks.

## Staged omissions

| ID | B12 relation |
| --- | --- |
| SOM-01 | Service/application recovery and transaction reconstruction |
| SOM-02 | User-space scheduler policy and exit-time scheduler teardown |
| SOM-04 | Dynamic regions, demand faults, and file-backed mapping mechanism |
| SOM-06 | Fork, copy-on-write, destructive exec, descriptor inheritance, and close-on-exec |
| SOM-07 | Sessions, process groups, and exit-time session-leader policy |
| SOM-08 | Threads, TLS, cancellation, and hosted runtime prerequisites |
| SOM-09 | Signals, ordinary stop/continue, application-fault delivery, and fault-generated exit |
| SOM-10 | Process timers and exit-time timer cancellation |
| SOM-11 | Credentials, setuid/setgid, and executable permission transitions |
| SOM-12 | Additional descriptor actions and complete file-operation breadth |
| SOM-14 | Public POSIX mapping/shared-memory interfaces built on SOM-04 |
| SOM-17 | Public process/exec/spawn/wait/libc and hosted startup interfaces |
| SOM-23 | Concurrent/asynchronous collaborator transport and backpressure |
| SOM-26 | Tracing, traced stop/resume/wait states, rusage, and core-dump lifecycle |

## Evidence gaps

| ID | Missing evidence | Owner |
| --- | --- | --- |
| B12-E001 | No end-to-end counter composes `Vpm`, `Vvfs`, `Vsvc`, `Vram`, `Vram*`, `K13_common`, `C13_success`, `K12load`, `K12commit`, `Gspawn`, and page-stage work for public success | B14 |
| B12-E002 | No rollback counter records every completed forward stage, `K12abort`, and exact reverse validation/call count | B14 |
| B12-E003 | No transfer counter distinguishes streamed MINIX segment reads from PM/VFS/VM complete request/executable snapshots | B14 |
| B12-E004 | No activation trace proves publication, PM local commit, VM commit, VFS release, and parent reply with exact operation costs | B14 |
| B12-E005 | No exit trace composes MINIX `VM_WILLEXIT`, timer/accounting, early zombie/wait, reparent/session signal, subscriber events, SCHED teardown, or `micros` tokenized VFS/VM/kernel cleanup plus `K12exit` | B14 |
| B12-E006 | No fault-injection path enters PM/VFS/VM cleanup because SOM-09 is absent | B14 after SOM-09 design |
| B12-E007 | No complete 4 KiB PM/VM/VFS/RAMFS and conditional TTY or 16 KiB supervisor-stack evidence covers worst transaction nesting | B14 |
| B12-E008 | No exact trusted-init PM validation count distinguishes direct coordinator entry from public spawn | B14 |
| B12-E009 | No multi-process lifecycle probe compares PM sender-queue blocking/global spawn-exit serialization with MINIX per-process worker progress | B14 after SOM-23 design |

## Findings register

### B12-F001: Cross-owner lifecycle stages multiply complete validators

- Severity: High
- `micros` evidence:
  - `pm_coordinator_run_spawn`
  - `pm_coordinator_rollback_spawn`
  - `micros_pm_coordinator_exit_request`
  - `run_stopped_exit`
  - VFS control facade
  - VM service load/release facade
  - kernel operation-12/13 adapters
- MINIX evidence: selected PM/VFS/VM/kernel exec/exit handlers and loader
  callbacks
- Difference: each forward, reverse, postpublication, and exit stage re-enters
  complete PM, VFS, VM, bootstrap, grant, ownership, endpoint, and mapping
  validation rather than consuming one retained cross-owner transaction plan.
- Impact: successful public spawn pays at least
  `12*Vpm + 8*Vvfs + (11+7*u)*Vsvc + 2*(1+q)*Vram + Vram*
  + 3*K13_common + C13_success + K12load(u) + K12commit + Gspawn` before
  remaining selected-record work. Latest-stage rollback adds up to
  `7*Vpm + 6*Vvfs + 3*Vsvc + K12abort` plus two operation-13 aborts and
  resource-specific release work. Exit adds
  `9*Vpm + 2*Vvfs + 5*Vsvc + K12exit` plus three operation-13 commands.
- Smallest correction boundary: define one generation-bound cross-owner spawn/
  exit plan whose owner-specific subplans are fully validated once before
  their irreversible boundary. Each stage/ack checks only exact phase, token,
  selected records, and consumed outputs. Complete validators remain only
  explicit diagnostic/acceptance operations, never an ordinary transaction
  creation or irreversible-boundary precondition.
- Dependencies: B05-F001, B06-F002, B07-F001/F002, B08-F001/F002/F005,
  B10-F001, B11-F001, the explicit B07-F002 boundary narrowing recorded in
  this audit, and B14 composed operation counts.

### B12-F002: VFS and VM retain complete duplicate executable snapshots

- Severity: High
- `micros` evidence:
  - VFS `spawn_executable_bytes`
  - `read_spawn_executable`
  - VFS-to-VM executable grant
  - VM `executable_bytes`
  - `service_copy_inputs`
- MINIX evidence:
  - VFS/libexec header/program-range validation
  - segment/file-mapping loader callbacks
  - direct segment copy into target memory
- Difference: VFS reads and retains the complete executable, then VM copies
  and retains a second complete executable before page construction. PM, VFS,
  and VM also retain three complete request snapshots.
- Impact: one transaction reserves 274,432 bytes of duplicate request/
  executable snapshots and copies `X` through RAMFS into VFS and again from
  VFS into VM before copying segment intersections into child pages.
- Smallest correction boundary: reproduce a reusable bounded loader data path:
  retain one stable canonical request, validate ELF header/program ranges
  before allocation, and stream/map exact executable spans through a reusable
  page/block buffer during child-page construction instead of retaining two
  complete executable images. Preserve non-transitive grant authority and
  hidden rollback.
- Dependencies: explicit ADR-0050 complete-buffering supersession, B05-F005/
  F006, B07-F005, B11-F004, and B14 transfer/storage evidence.

## Unit conclusion

B12 has no primary production paths, and its complete integration-operation
manifest is closed. The audit adds B12-F001 and B12-F002 as blocking findings,
confirms B05-F001/F005/F006, B07-F001/F002/F005, B08-F001/F002/F004/F005,
B10-F001, and B11-F001/F002/F003/F004/F005, and maps deferred lifecycle
breadth to existing SOM-01, SOM-02, SOM-04, SOM-06, SOM-07, SOM-08, SOM-09,
SOM-10, SOM-11, SOM-12, SOM-14, SOM-17, SOM-23, and SOM-26 rows.

B12 is **audited but not certified**. Hidden-child rollback, exact authority,
activation publication, normal exit, and resource-complete zombie behavior
remain valid successful-path contracts. Their whole-system validation cadence
and complete executable snapshot topology are not structural certification
under ADR-0080. No B12 correction may start during the read-only audit.

## References

- [ADR-0050](../adr/0050-post-handoff-elf-spawn.md)
- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)
- [B03 object/scheduler audit](minix-complete-baseline-b03-objects-scheduler.md)
- [B07 VM audit](minix-complete-baseline-b07-vm.md)
- [B08 PM/spawn audit](minix-complete-baseline-b08-pm-spawn.md)
- [B10 RAMFS audit](minix-complete-baseline-b10-ramfs.md)
- [B11 VFS audit](minix-complete-baseline-b11-vfs.md)
- [MINIX ELF execution/spawn study](minix-elf-exec-and-spawn.md)
- [ELF spawn integration map](../architecture/elf-spawn-integration.md)
- [PM spawn coordinator](../architecture/pm-spawn-coordinator.md)
- [Application runtime](../architecture/application-runtime.md)

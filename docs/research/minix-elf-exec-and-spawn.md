# MINIX ELF Execution and Spawn Baseline

## Scope

This note records the fixed behavioral baseline for the `micros` ELF spawn
path. The reference is the local MINIX checkout at:

```text
/Users/alephling/Repo/minix
commit 4db99f4012570a577414fe2a43697b2f239b699e
```

The study covers:

- PM, VFS, VM, and kernel authority during executable replacement;
- executable lookup and permission checks;
- argument and environment capture;
- ELF validation, segment allocation, copying, zero fill, and stack setup;
- the destructive partial-exec boundary;
- final instruction-pointer and stack publication;
- process-exit cleanup ordering; and
- MINIX `posix_spawn` behavior where it differs from the `micros`
  spawn-before-fork decision.

This is behavioral research, not permission to copy MINIX source. Accepted
`micros` ADRs remain authoritative where the systems differ.

## PM forwards execution policy and suspends the caller

MINIX PM receives `exec`, forwards the caller endpoint, path, argument frame,
and frame length to VFS, and deliberately does not reply to the original call
(`minix/servers/pm/exec.c:35-55`).

The reusable behavior is:

- PM remains the owner of process lifecycle and user-visible completion;
- VFS receives the exact kernel-known process endpoint rather than a
  payload-selected authority;
- the calling thread cannot mutate the supplied path or argument frame while
  execution is being coordinated; and
- successful execution does not return through the old call path.

`micros` adapts this from destructive replacement to creation of a new hidden
child. The parent remains blocked in its `SPAWN` call until PM either returns
one failure after complete rollback or publishes the child and returns its
semantic PID.

## VFS copies arguments before destructive memory changes

MINIX libc first builds one bounded stack image containing `argc`, `argv`,
`envp`, pointer fixups, auxiliary-vector space, strings, and process-string
metadata (`minix/lib/libc/sys/stack_utils.c:19-67,74-112,117-171`).

VFS copies that complete frame from the caller before clearing or replacing
the old address space (`minix/servers/vfs/exec.c:225-240`). It then performs
path resolution and executable lookup from the caller's VFS context
(`minix/servers/vfs/exec.c:242-254`).

The reusable behavior is:

- all caller-owned request bytes are copied into trusted resident storage
  before any lifecycle mutation;
- argument and environment data is pointer-free after capture;
- executable path resolution remains VFS policy; and
- the loader consumes a stable snapshot rather than repeatedly reading a
  mutable caller buffer.

`micros` keeps these properties with one canonical, at-most-one-page spawn
request. PM first copies the application grant. VFS then copies the PM-owned
snapshot, retains argument metadata for the transaction, and gives VM separate
read-only grants to VFS-owned executable and request buffers. No grant is
forwarded.

## VFS owns executable lookup, type, and execute permission

MINIX VFS resolves the path, requires a regular vnode, applies execute
permission policy, retrieves file metadata, and reads the executable header
before invoking the loader (`minix/servers/vfs/exec.c:91-153`).

The reusable behavior is:

- executable pathname traversal belongs to VFS;
- a directory or non-regular object is not executable;
- execute authorization is checked before image construction;
- file size and bytes come from the filesystem owner through VFS; and
- the VM server does not resolve paths or open files.

The first `micros` version has no credentials. VFS therefore requires one
regular RAMFS file with at least one execute bit set and applies the parent
process's root and working-directory routing. The trusted parentless init
transaction requires an absolute path from the mounted root.

## The ELF loader validates before crossing its destructive boundary

MINIX `libexec` checks the ELF header and complete program-header file ranges
before invoking `clearproc` (`minix/lib/libexec/exec_elf.c:127-169`). It then
iterates `PT_LOAD` segments, allocates or maps memory, copies file-backed
bytes, clears leading and trailing bytes, allocates the stack, and returns the
entry point (`minix/lib/libexec/exec_elf.c:171-314`).

The loader permits both `ET_EXEC` and `ET_DYN`, can use file-backed mappings,
and accepts a broader segment shape than `micros`
(`minix/lib/libexec/exec_elf.c:29-47,127-148,178-304`).

The reusable behavior is:

- validate every file range before publishing executable state;
- load only declared loadable segments;
- allocate memory under VM ownership;
- copy exactly `p_filesz` bytes;
- zero every remaining byte through `p_memsz` and every page fragment that
  does not come from the file;
- create a separate writable stack; and
- return an explicit entry point only after the image is complete.

Required `micros` adaptations are:

- ELF64 little-endian RISC-V instead of MINIX's selected 32-bit target;
- fixed non-PIE `ET_EXEC` images instead of `ET_DYN`;
- exactly three page-closed `PT_LOAD` segments with RX, R, and RW/NX
  permissions;
- eager anonymous resident pages instead of file-backed mappings or demand
  allocation;
- a one-page bounded initial stack;
- no interpreter, dynamic linker, relocation, TLS, or script handling; and
- a safer hidden-child freeze and activation boundary instead of clearing a
  live process.

## VM owns address-space memory policy

MINIX's loader callbacks request memory through VM operations. Anonymous
preallocated or on-demand mappings create segment and stack storage, while
VFS may ask VM to map executable file ranges directly
(`minix/lib/libexec/exec_general.c:21-64`;
`minix/servers/vfs/exec.c:161-184,316-343`).

The reusable behavior is:

- VFS does not allocate page frames or edit page tables;
- VM decides how executable and stack memory is backed;
- the kernel supplies page-table and mapping mechanisms rather than
  executable-path policy; and
- address-space teardown returns all mapped memory through VM coordination.

`micros` preserves that authority after the one-way handoff. VM chooses exact
free physical frames, uses one kernel-controlled VM-only scratch alias to fill
them, and asks the kernel to atomically bind the completed pages and exact
page-table frames to the reserved child generation. PM and VFS never choose
physical frames or write child PTEs.

## VFS copies segments and the final stack

When direct file mapping is not used, MINIX VFS reads each executable segment
directly into the target process (`minix/servers/vfs/exec.c:685-713`). After
loading and PM metadata coordination, VFS copies the prepared stack into the
new image and closes close-on-exec descriptors
(`minix/servers/vfs/exec.c:357-374,721-730`).

The reusable behavior is:

- executable bytes originate from the VFS-authorized file snapshot;
- the final stack is built from the previously captured argument snapshot;
- segment and stack copies complete before execution starts; and
- descriptor state is committed as part of the execution transaction.

`micros` changes the data path because grants are non-transitive. VFS buffers
the complete executable and request, grants VM direct read-only access to
those VFS buffers, and VM fills child frames through its own scratch mapping.
New children begin with an empty descriptor table. VFS applies only explicit
bounded duplicate actions, or the trusted init console action, so
close-on-exec replacement semantics do not apply.

## PM marks the destructive partial-exec boundary

After VFS has loaded the new address space, MINIX asks PM to update process
metadata. PM sets `PARTIAL_EXEC`, meaning later failure kills the process
instead of replying as if the original image still existed
(`minix/servers/pm/exec.c:60-123`).

If completion later fails while that flag is set, PM sends `SIGKILL`;
otherwise PM clears the flag and asks the kernel to install the new execution
state (`minix/servers/pm/exec.c:155-198`).

The reusable behavior is the existence of an explicit publication boundary:
before it, failure may be reported to the caller; after it, the old execution
state cannot safely resume.

`micros` deliberately rejects MINIX's destructive failure behavior. A spawned
child remains hidden and rollbackable until:

- VM has frozen its exact mapping generation;
- the kernel has prepared a held thread and reserved endpoint;
- VFS and PM have committed hidden state; and
- PM invokes one final kernel activation.

Only activation publishes the endpoint and runnable thread. Every expected
earlier failure releases resources in reverse order and returns one spawn
error to the parent.

## The kernel installs execution state and suppresses the old reply

MINIX's kernel `SYS_EXEC` path initializes the architecture context with the
new PC and SP, clears pending message-delivery state, and removes receive
blocking without replying to the original exec call
(`minix/kernel/system/do_exec.c:24-55`).

The reusable behavior is:

- final execution-state installation is a kernel mechanism;
- PC and SP are validated and installed together;
- stale IPC receive state cannot leak into the new execution context; and
- successful publication does not resume the prior call frame.

`micros` uses a PM-only preparation operation to create the first thread,
reserved endpoint, immutable `APPLICATION` profile, scheduler assignment, and
token-bound initial context. A separate activation operation revalidates the
frozen mapping generation, publishes the endpoint, and makes the held thread
runnable atomically.

## MINIX exit ordering preserves cleanup authority

MINIX PM first stops the process and informs VM that exit is beginning. It
then asks VFS to clean process-owned state before destroying the kernel
process and releasing VM mappings
(`minix/servers/pm/forkexit.c:325-359,430-457`).

VFS closes every descriptor, releases root and working-directory vnode
references, cancels blocked work, and only then frees the process slot
(`minix/servers/vfs/misc.c:635-708`).

The reusable behavior is:

- an exiting process is made permanently non-runnable first;
- VFS cleanup occurs while the process identity still exists;
- endpoint and kernel execution state are removed before address-space
  release;
- VM releases mappings and page tables before the lifecycle slot is reusable;
  and
- zombie/wait state is retained independently from execution resources.

`micros` binds each post-stop step to one opaque exit transaction. Expected
failure is permitted only before kernel exit-begin commits. After that point,
VFS, kernel-detach, VM-release, or identity failure is fatal rather than a
rollback to running state.

## MINIX `posix_spawn` is not the selected creation baseline

The selected MINIX libc implements `posix_spawn` as `pipe`, `fork`, child-side
file actions, and `execve`; the pipe communicates pre-exec failure to the
parent (`minix/lib/libc/sys/posix_spawn.c:210-269`).

That implementation is useful evidence for:

- reporting spawn failure only when the child never reached the new image;
- applying descriptor actions before successful publication; and
- collecting a failed child before returning an error.

It is not the `micros` mechanism. ADR-0009 requires spawn before fork, and
`micros` has no copy-on-write or general `fork`. The first implementation
therefore uses one PM-directed hidden-child transaction with explicit
descriptor duplication and complete prepublication rollback.

## Classification ledger

| MINIX behavior | Classification | `micros` first-version decision |
| --- | --- | --- |
| PM owns lifecycle completion | Baseline parity | PM owns PID, parent, spawn, exit, zombie, and wait |
| VFS copies argv/env before image mutation | Baseline parity | PM and then VFS copy one canonical request before resource acquisition |
| VFS resolves path and checks executable object | Baseline parity | VFS resolves from parent root/cwd or the trusted init root |
| VM owns executable memory policy | Baseline parity | VM selects frames and directs post-handoff mapping |
| Loader validates file bounds before mutation | Baseline parity | VM validates the complete resident ELF before `LOAD_BEGIN` |
| `PT_LOAD` copy plus BSS zero fill | Baseline parity | VM fills zeroed scratch pages from exact segment intersections |
| Separate writable initial stack | Baseline parity | One RW/NX page ending at `0x80000000` |
| Kernel installs final execution state | Baseline parity | PM-only prepare plus final activation |
| Stop, VFS cleanup, kernel detach, VM release | Baseline parity | Exact token-bound exit transaction |
| Destructive `exec` of the caller | Required adaptation | New hidden child; no old image is destroyed |
| `PARTIAL_EXEC` failure kills caller | Required adaptation | Complete rollback before activation; fatal only after publication |
| Transitive direct segment copy | Required adaptation | VFS-to-VM grants plus VM-only scratch mapping |
| Native pointer-rich stack frame | Required adaptation | Pointer-free request; VM creates child-local pointers |
| `ET_DYN`, interpreter, and dynamic linker | Deferred | Fixed static `ET_EXEC` only |
| Script interpreter handling | Deferred | No `#!` support |
| File-backed and demand mappings | Deferred | Eager anonymous resident pages |
| setuid/setgid and credentials | Deferred | No credential transition in v0.1 |
| close-on-exec descriptor replacement | Deferred | New child starts empty and receives explicit duplicate actions |
| `fork`-based `posix_spawn` | Rejected for first version | Direct spawn-before-fork transaction |
| Signals and fault-to-exit conversion | Deferred | Normal exit only; fault delivery remains later work |

## Design obligations

The resulting `micros` design must therefore:

1. keep path, descriptor, lifecycle, memory-policy, and publication authority
   in VFS, PM, VM, and the kernel respectively;
2. copy all caller request bytes before consuming a PID or creating a child;
3. use only direct grants whose grantor and grantee are the actual copy
   participants;
4. validate a bounded static RISC-V ELF before allocating child resources;
5. preserve one exact hidden shape at every spawn stage;
6. bind VM freeze, PM preparation, activation, rollback, and exit to
   nonzero nonreused tokens;
7. perform local instruction-fetch synchronization before the child becomes
   runnable;
8. make activation the sole publication point;
9. release all expected prepublication failures in reverse ownership order;
   and
10. leave init, shell commands, dynamic linking, destructive `exec`, `fork`,
    page faults, and signals to later dependency-ready work.

## Application-startup implementation trace

The Step 14 application runtime additionally rechecked the fixed local
`lib/csu/arch/riscv/crt0.S:36-44` and
`lib/csu/common/crt0-common.c:145-191`. Architecture entry delegates to
common startup, which binds the environment from `ps_strings`, performs
hosted initialization, and converts normal `main` return to `exit`.
The existing [runtime study](minix-user-service-runtime.md) remains the
source for the full hosted and service-specific trace.

| Application choice | ADR-0025 classification | Existing authority |
| --- | --- | --- |
| Application code receives argument/environment vectors and normal return becomes process exit | Baseline parity | ADR-0050 replaces the service-only staged entry at the dependency-ready application boundary. |
| RV64 direct initial-stack decoding, fixed entry and explicit unused `gp`/`tp` | Required adaptation | Accepted RISC-V/toolchain and standalone image contracts replace NetBSD `ps_strings` and machine startup. |
| Complete bounded pointer/string/auxv validation before calling application code | Compatible extension | ADR-0050's canonical stack, exact endpoint generations and publication boundary remain authoritative. |
| No hosted libc, TLS, constructors or finalizers | Staged substitution | The runtime remains freestanding; those facilities require later reviewed requirements, not application startup. |
| PM IPC exit rather than hosted `exit`, with fatal unexpected return | Required adaptation | Accepted PM authority and no-reply exit semantics replace the hosted termination path. |

These are implementation classifications of already Accepted behavior, not
permission to copy runtime source or import deferred hosted facilities.

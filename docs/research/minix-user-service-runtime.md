# MINIX User-Service Runtime Study

## Purpose

This study traces the fixed MINIX process-entry, IPC-wrapper, grant-wrapper,
safe-copy-wrapper, and service-startup paths before `micros` defines its first
freestanding user-service runtime.

The reference is MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

MINIX source is behavioral evidence only. `micros` remains an independent C17
and RISC-V implementation.

This document classifies only runtime, startup, and standalone-image choices
introduced by the proposed runtime design. Core endpoint, message, reply-token,
grant, checked-copy, and syscall differences remain classified by:

- [the endpoint and blocking IPC study](minix-endpoint-and-ipc.md);
- [the direct grant and safe-copy study](minix-direct-grants-and-safecopy.md);
- [the user grant and safe-copy syscall study](minix-user-grant-syscall-boundary.md).

Those earlier classifications are not duplicated here.

## Process entry and libc initialization

### Architecture entry

The MINIX checkout uses the NetBSD C runtime entry model. Its RISC-V `crt0`
exports `_start` as an alias of `__start` and transfers directly to the common
`___start` routine
(`lib/csu/arch/riscv/crt0.S:36-44`).

The architecture assembly does not parse the initial stack itself. It relies
on the loader-provided C arguments and delegates hosted initialization to the
common C runtime.

### Hosted C runtime

The common startup routine:

1. requires a loader-provided `ps_strings`;
2. publishes `environ` and derives `__progname`;
3. validates dynamic-loader metadata when present;
4. registers loader cleanup;
5. calls `_libc_init()`;
6. runs preinitializers and constructors;
7. registers finalizers; and
8. calls `exit(main(argc, argv, environ))`.

See `lib/csu/common/crt0-common.c:145-191`.

MINIX-specific libc initialization is itself a constructor. It starts with
architecture interrupt wrappers in a global IPC vector table, asks the kernel
for the current kernel-info page, and replaces that table when kernel-provided
vectors are available
(`minix/lib/libc/sys/init.c:5-31`).

This is a hosted process runtime. It assumes arguments, environment, libc
state, constructors, finalizers, `atexit`, and process exit.

### ELF loading, BSS, and stack ownership

The MINIX ELF loader accepts executable or dynamic ELF types, rejects a second
interpreter while loading the selected object, walks `PT_LOAD` program
headers, installs segment permissions, copies file bytes, and clears the
remaining memory through the end of each segment
(`minix/lib/libexec/exec_elf.c:29-47,127-169,200-299`).

The same loader allocates the stack separately from the ELF load segments and
publishes the ELF entry point only after the image and stack are ready
(`minix/lib/libexec/exec_elf.c:302-316`).

For ordinary exec, VFS first obtains a complete argument/environment stack
image, selects an executable loader, loads the image, copies the prepared stack
into the new address space, and returns the new PC and stack pointer
(`minix/servers/vfs/exec.c:188-239,347-373`). Dynamic executables additionally
receive auxiliary-vector data
(`minix/servers/vfs/exec.c:413-480`).

The resulting boundary is clear:

- the image owner installs file-backed bytes;
- the image owner zeroes non-file-backed memory, including BSS;
- the image owner supplies the initial stack and stack pointer;
- `crt0` begins only after those conditions hold.

## IPC vectors and wrappers

MINIX declares architecture trap entry points, a process-global IPC vector
table, and inline libc-facing wrappers. The inline wrappers dispatch through
the currently installed vector table rather than naming one fixed trap
implementation directly
(`minix/include/minix/ipc.h:2760-2830`).

The default i386 wrappers move the endpoint, message pointer, and operation
into the required registers, enter `IPCVEC_INTR`, preserve the architecture's
callee-saved register used by the trap ABI, and return the integer result
(`minix/lib/libc/arch/i386/sys/_ipc.S:16-83`).

The ARM wrappers perform the equivalent register shuffle, select
`IPCVEC_INTR`, execute `svc`, preserve the procedure-call ABI, and return the
integer result
(`minix/lib/libc/arch/arm/sys/_ipc.S:7-69`).

The relevant baseline behavior is not the mutable vector implementation
itself. It is:

- one C-callable architecture boundary;
- explicit register marshalling;
- preservation of the ordinary C procedure-call ABI;
- direct integer success or failure results;
- no hidden user-memory result for ordinary IPC.

## Grant and safe-copy wrappers

### Direct grants

MINIX `cpf_grant_direct()` obtains or grows a user-owned table slot, writes the
grantee, virtual base, length, and access bits, publishes validity after an
instruction barrier, and returns the slot/sequence grant identifier
(`minix/lib/libsys/safecopies.c:90-174`).

Table growth uses `malloc`; capacity failure returns `-1` with `errno` set.
Revocation clears validity, advances the sequence, returns the slot to the
free list, and may report a recorded `CPF_TRY` fault
(`minix/lib/libsys/safecopies.c:222-273`).

The kernel learns the user table's address and size through `SYS_SETGRANT`
(`minix/lib/libsys/sys_setgrant.c:5-14`).

The already accepted `micros` grant ADRs replace that mutable table with one
kernel-managed registry. This study does not reopen that authority decision.

### Safe copy

MINIX exposes distinct `sys_safecopyfrom()` and `sys_safecopyto()` wrappers.
Each packages:

- the other endpoint;
- the grant identifier;
- the grant-relative offset;
- the caller-local address; and
- the byte count.

The wrappers then invoke separate kernel-call numbers
(`minix/lib/libsys/sys_safecopy.c:5-43`).

`_kernel_call()` stores the call number in the message, enters the kernel-call
path, and retries `ENOTREADY` with increasing tick delays
(`minix/lib/libsys/kernel_call.c:7-18`).

The i386 and ARM kernel-call assembly wrappers use a distinct trap selection
from ordinary IPC
(`minix/lib/libc/arch/i386/sys/_do_kernel_call_intr.S:4-8`,
`minix/lib/libc/arch/arm/sys/_do_kernel_call_intr.S:4-8`).

ADR-0041 already replaces that second message/trap ABI with operations 7
through 10 in the unified RISC-V `ecall` namespace. The runtime must preserve
the accepted participant, direction, offset, local-address, length, and
integer-result behavior without recreating the MINIX kernel-call message
layer.

## Service startup boundary

MINIX services do not treat `crt0` as their readiness protocol. A service
enters its own `main`, installs service-specific SEF callbacks, and invokes
`sef_startup()` before entering its request loop. PM and SCHED both use this
shape with a no-argument `main`
(`minix/servers/pm/main.c:42-131`,
`minix/servers/sched/main.c:14-126`).

`sef_startup()`:

1. obtains the service's endpoint, name, privilege flags, and init flags;
2. handles special RS/VM cases;
3. otherwise waits for the correct RS initialization request;
4. dispatches the registered initialization callback; and
5. returns to the service only after initialization processing succeeds.

See `minix/lib/libsys/sef.c:68-142`.

Initialization processing invokes the service callback, sends an `RS_INIT`
response, reloads grant and asynchronous-send tables, and then returns
(`minix/lib/libsys/sef_init.c:43-138`).

Thus the baseline separates three layers:

1. generic process entry and libc initialization;
2. service-framework startup and readiness exchange;
3. the service's long-running request loop.

The first `micros` runtime task defines only layer 1 plus syscall wrappers.
The static launcher, manifest, readiness exchange, service-specific
initialization, and later recovery framework remain separate DAG successors.

## RISC-V ABI constraints

The selected RISC-V integer procedure-call ABI passes eight scalar arguments
in `a0` through `a7`, returns scalar results in `a0`, requires 16-byte stack
alignment at procedure entry, and preserves `s0` through `s11` across calls.
The ABI also reserves `gp` and `tp` from ordinary allocation. See the
[RISC-V ELF psABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/).

ADR-0002 already selects:

- `riscv64-unknown-elf`;
- the LP64 ABI;
- freestanding C17;
- no host libc;
- Clang and LLD.

The current target flags also disable small-data generation, linker
relaxation, save/restore helpers, stack protectors, and unwind tables. The
runtime design can therefore make `gp` and `tp` policy explicit without
requiring a global-offset table, thread-local storage, or compiler runtime.

Clang may lower large aggregate copies and zero initialization to `memcpy` and
`memset` even in a freestanding build. Those two symbols are therefore the
smallest compiler-support surface. No evidence requires a general string,
allocation, formatting, or POSIX library.

## Sole ADR-0025 classification ledger

This table is the sole classification ledger for runtime and startup choices
introduced by this study. Each difference appears once. No unclassified
divergence remains.

| ID | `micros` choice relative to the fixed MINIX baseline | Classification | Reason or replacement point |
| --- | --- | --- | --- |
| UR-01 | A C-callable assembly boundary enters the kernel and returns one integer result | Baseline parity | The raw RISC-V stub preserves the same wrapper/kernel boundary |
| UR-02 | One RV64 stub consumes `a0` through `a7` and executes `ecall` instead of using i386 or ARM register/trap conventions | Required adaptation | The accepted target ISA and unified syscall ABI require the RISC-V mechanism |
| UR-03 | C wrappers call the fixed raw stub directly instead of dispatching through constructor-selected IPC vectors | Required adaptation | The accepted unified RISC-V ABI has one trap path and no alternate kernel-vector provider |
| UR-04 | The image owner copies ELF bytes, zeroes non-file-backed memory, maps an external stack, and only then enters `_start` | Baseline parity | It preserves the MINIX loader/crt ownership boundary |
| UR-05 | Initial services use fixed-address `ET_EXEC` images with no interpreter, dynamic relocation, or runtime loader | Staged substitution | Static embedded services precede the later executable and POSIX runtime work |
| UR-06 | Exact RX, R, and RW/NX segment closure plus fail-closed ELF checks are mandatory | Compatible extension | Stronger static W^X evidence preserves valid service behavior |
| UR-07 | `gp` is zero and small-data sections and relaxation are rejected | Compatible extension | A deterministic unused register plus static rejection preserves code that has no global-pointer references |
| UR-08 | `tp` is zero and no TLS image or initialization exists | Staged substitution | TLS remains deferred until a reviewed multithreaded or libc requirement |
| UR-09 | Startup performs no libc initialization, constructors, finalizers, or `atexit` registration | Staged substitution | The dependency-ready outcome is a service runtime; hosted libc startup remains later work |
| UR-10 | The C entry is `void micros_service_main(void)` and receives no `argc`, `argv`, environment, or auxiliary vector | Staged substitution | Static services need no process arguments; the application entry contract is deferred |
| UR-11 | Service-specific startup is invoked by service code after C entry, not by `crt0` | Baseline parity | MINIX service `main` owns the SEF boundary above generic process entry |
| UR-12 | The minimal runtime contains no SEF-equivalent manifest, readiness, restart, or live-update exchange | Staged substitution | The static launcher is the next DAG node; RS recovery remains later |
| UR-13 | A returned service entry executes a deterministic user breakpoint instead of calling hosted `exit` | Staged substitution | PM/process-exit semantics are not dependency-ready; that later owner replaces the fatal fallback |
| UR-14 | The grant-create C wrapper stores its token output only after syscall success and normalizes that success to `OK`; other wrappers expose the accepted result unchanged | Compatible extension | It adds failure-preserving typed output without changing the stable kernel result convention |
| UR-15 | The runtime supplies only compiler-required `memcpy` and `memset` symbols | Required adaptation | Freestanding Clang may emit those calls; every broader libc facility remains outside the outcome |

## Derived `micros` runtime boundary

The smallest dependency-ready runtime therefore has these properties:

- one fixed standalone user ELF at the existing user virtual window;
- an external, zero-filled, writable, non-executable user stack;
- loader-owned BSS zeroing and instruction synchronization;
- a nonreturning `_start` that establishes only `gp` and `tp` policy;
- one ordinary no-argument C service entry;
- one raw eight-register RISC-V `ecall` function;
- stateless C wrappers for operations 1 through 10;
- exact stable signed results with no `errno`;
- success-only grant-token output publication;
- `memcpy` and `memset`, but no general libc;
- no launcher, readiness, service protocol, process exit, dynamic mapping,
  application, heap, TLS, or constructor behavior.

## Source map

- `lib/csu/arch/riscv/crt0.S`
- `lib/csu/common/crt0-common.c`
- `minix/lib/libexec/exec_elf.c`
- `minix/servers/vfs/exec.c`
- `minix/lib/libc/sys/init.c`
- `minix/include/minix/ipc.h`
- `minix/lib/libc/arch/i386/sys/_ipc.S`
- `minix/lib/libc/arch/arm/sys/_ipc.S`
- `minix/lib/libsys/safecopies.c`
- `minix/lib/libsys/sys_setgrant.c`
- `minix/lib/libsys/sys_safecopy.c`
- `minix/lib/libsys/kernel_call.c`
- `minix/lib/libc/arch/i386/sys/_do_kernel_call_intr.S`
- `minix/lib/libc/arch/arm/sys/_do_kernel_call_intr.S`
- `minix/lib/libsys/sef.c`
- `minix/lib/libsys/sef_init.c`
- `minix/servers/pm/main.c`
- `minix/servers/sched/main.c`

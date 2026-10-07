# MINIX User Grant and Safe-Copy Syscall Boundary

## Purpose

This study traces how MINIX user services create direct grants and invoke
checked copies, then classifies the smallest unified RISC-V syscall boundary
for `micros`.

The reference is MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

MINIX source is behavioral evidence only. `micros` remains an independent C17
and RISC-V implementation.

## IPC trap boundary

MINIX synchronous IPC wrappers expose send, receive, send/receive, notify, and
other operations through architecture assembly. The wrappers place the
operation, endpoint, and message pointer in architecture-specific registers
and enter the kernel directly
(`minix/lib/libc/arch/i386/sys/_ipc.S:16-83`,
`minix/lib/libc/arch/arm/sys/_ipc.S:7-69`).

The libc-facing inline functions dispatch through an initialized IPC-vector
table
(`minix/include/minix/ipc.h:2760-2830`,
`minix/lib/libc/sys/init.c:5-31`).

Thus ordinary IPC has a direct user-to-kernel trap boundary.

## Grant creation and revocation

MINIX grant records live in a process-owned user table. `cpf_grant_direct()`:

1. obtains or grows one free table slot;
2. writes grantee, virtual base, length, and access bits;
3. publishes valid flags after an instruction barrier;
4. returns an index/sequence grant identifier
   (`minix/lib/libsys/safecopies.c:90-171,153-174`).

`cpf_revoke()` clears validity, advances the sequence, and returns the slot to
the free list
(`minix/lib/libsys/safecopies.c:222-273`).

Individual create and revoke operations do not enter the kernel because the
table is user-managed. The process informs the kernel only when the table
location or size changes through `SYS_SETGRANT`
(`minix/lib/libsys/sys_setgrant.c:5-14`).

`micros` deliberately uses a kernel-managed fixed grant registry. User-visible
create and revoke therefore require explicit kernel operations rather than
mutable table publication.

## Safe-copy kernel calls

`sys_safecopyfrom()` and `sys_safecopyto()` package:

- grantor endpoint;
- grant identifier;
- grant-relative offset;
- local virtual address;
- byte count.

They issue distinct `SYS_SAFECOPYFROM` and `SYS_SAFECOPYTO` kernel calls
through the libsys kernel-call path
(`minix/lib/libsys/sys_safecopy.c:5-43`,
`minix/include/minix/com.h:239-242`).

`_kernel_call()` installs the call number in the message, invokes
`do_kernel_call()`, and retries an `ENOTREADY` result with increasing tick
delays
(`minix/lib/libsys/kernel_call.c:7-18`).

The architecture wrapper enters a distinct kernel-call trap rather than the
ordinary IPC trap:

- i386 passes the message pointer in `eax` and executes `int $KERVEC_INTR`;
- ARM passes the pointer in `r0`, selects `KERVEC_INTR`, and executes `svc`.

See
`minix/lib/libc/arch/i386/sys/_do_kernel_call_intr.S:4-8` and
`minix/lib/libc/arch/arm/sys/_do_kernel_call_intr.S:4-8`.

The kernel verifies grant identity, grantee, direction, and bounds before
mapping access. Runtime mapping faults may suspend the kernel call and involve
VM, as documented in
[the runtime safe-copy study](minix-post-handoff-wired-address-resolution.md).

## Result conventions

MINIX IPC and kernel-call wrappers return integer status values. Grant creation
uses `-1` plus `errno` for user-table allocation failure, while safe-copy
wrappers return the kernel-call result directly.

`micros` already exposes stable signed RV64 IPC results in `a0`. It does not
provide hosted libc, `errno`, or a separate libsys kernel-call message layer.

## `micros` classification

| Topic | MINIX behavior | `micros` classification |
| --- | --- | --- |
| Direct IPC trap | Architecture wrapper enters the kernel with operation registers | Baseline parity through the existing RISC-V `ecall` boundary |
| Grant create authority | User library publishes one direct grant entry | Required adaptation: kernel-managed grants require a create syscall |
| Grant revoke authority | User library invalidates and advances one table sequence | Required adaptation: kernel-managed grants require a revoke syscall while preserving revoke-time generation advance |
| Safe-copy fields | Kernel calls carry endpoint, grant, offset, local address, and length | Baseline parity |
| Direction | Distinct safe-copy-from and safe-copy-to calls | Baseline parity |
| Table registration | `SYS_SETGRANT` publishes mutable table location and size | Compatible extension: omitted because the authoritative registry is kernel-managed and fixed-capacity |
| Kernel-call trap and message | Safe copy uses a 64-byte message and distinct `KERVEC_INTR` entry | Required adaptation: one RISC-V `ecall` namespace and scalar registers replace the second trap/message ABI |
| Result convention | Integer status; grant allocation may use `errno` | Compatible extension: stable negative project errors and a nonnegative returned token, with no hosted `errno` |
| Inspect operation | User library already owns the mutable entry | Compatible least-authority extension: no user inspect syscall; the opaque token is sufficient |
| Runtime wrappers | libc/libsys provide hosted wrappers and constructor-selected IPC vectors | Required adaptation: the selected freestanding toolchain requires direct wrappers without hosted constructors; their design remains the next task |
| VM retry | Ordinary safe copy may suspend and retry through VM | Staged substitution: current resident-only checked copy returns an explicit fault |

## Required unified namespace

`micros` already assigns operation numbers 1 through 6 to IPC. The smallest
stable extension reserves:

```text
7  GRANT_CREATE
8  GRANT_REVOKE
9  GRANT_COPY_FROM
10 GRANT_COPY_TO
```

The namespace remains one `ecall` ABI selected by `a7`. This avoids:

- a second trap vector;
- overlapping operation numbers interpreted by different handlers;
- a kernel-call message protocol before the user runtime exists;
- future wrapper-specific dispatch conventions.

## Required user-visible behavior

The grantor creates and revokes its own grants. The exact grantee invokes
copy-from or copy-to with the grantor endpoint and opaque token.

Create returns the opaque 32-bit token as a nonnegative RV64 `a0` value.
Every failure returns one stable negative result. The token never aliases a
negative result, and `MICROS_GRANT_NONE` remains unallocated.

All unused argument registers are zero. Endpoints, grants, and permission masks
must fit their public 32-bit representations. Address, offset, and length
arguments use full RV64 values.

## Deferred runtime work

The later freestanding runtime design consumes this fixed ABI and separately
defines:

- C wrapper names and output-preservation behavior;
- one raw RISC-V `ecall` stub;
- service image startup and linker contracts;
- compiler support routines;
- accidental service-return behavior.

Those choices cannot change operation numbers, register layouts, stable
results, grant direction, bounds, identity, or lifetime.

## Source map

- `minix/lib/libc/arch/i386/sys/_ipc.S`
- `minix/lib/libc/arch/arm/sys/_ipc.S`
- `minix/include/minix/ipc.h`
- `minix/lib/libc/sys/init.c`
- `minix/lib/libsys/safecopies.c`
- `minix/lib/libsys/sys_setgrant.c`
- `minix/lib/libsys/sys_safecopy.c`
- `minix/lib/libsys/kernel_call.c`
- `minix/lib/libc/arch/i386/sys/_do_kernel_call_intr.S`
- `minix/lib/libc/arch/arm/sys/_do_kernel_call_intr.S`
- `minix/include/minix/com.h`

# MINIX Trap and Syscall Source-Organization Study

## Purpose and reference

This is the behavioral and responsibility baseline for the source-only trap,
syscall, and existing user-runtime evidence correction. It does not introduce
a trap, syscall, fault, runtime, or scheduling feature.

The fixed MINIX reference is
`4db99f4012570a577414fe2a43697b2f239b699e`. The following sources were inspected
at that exact commit:

- [ARM entry and restore](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/arch/earm/mpx.S);
- [ARM exceptions](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/arch/earm/exception.c);
- [common process/IPC return](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/proc.c); and
- [kernel-call dispatch](https://github.com/Stichting-MINIX-Research-Foundation/minix/blob/4db99f4012570a577414fe2a43697b2f239b699e/minix/kernel/system.c).

MINIX is evidence, not a source donor. No MINIX source, assembly, comments,
structure layout, or test is copied into this correction.

## Entry, authority, failure, and return trace

ARM entry separates architecture save/restore from C exception and call
handling. Its exception path identifies user versus nested origin, saves the
user register context, stops user accounting, calls the C handler, and reaches
`switch_to_user`; a nested kernel exception instead retains the interrupted
kernel continuation (`mpx.S:53-126`).

IRQ routing preserves the same distinction. A user-origin IRQ saves context,
stops user accounting, handles the interrupt, and selects through
`switch_to_user`. A kernel-origin IRQ saves the interrupted kernel registers,
accounts idle, handles the source, and restores that same kernel continuation
with interrupt masking preserved (`mpx.S:135-174`).

The SVC boundary saves user registers before interpreting the selector.
Kernel calls and IPC have separate selectors and argument conventions, but
both stop user accounting before dispatch and converge on the common user
return. An invalid selector returns an explicit result rather than inventing
another entry path (`mpx.S:177-238`). Architecture restore consumes the selected
saved process context only after that common selection (`mpx.S:243-260`).

`kernel_call` copies one request into kernel-owned storage and replaces its
source with the exact caller endpoint before dispatch. Dispatch checks the
operation range and the caller's kernel-call mask before invoking its handler.
Finish owns result delivery or retained VM-suspended work
(`system.c:59-162`). Synchronous IPC separately validates the operation,
endpoint, destination mask, and call privileges before its operation-specific
transition (`proc.c:479-585`).

`switch_to_user` is the scheduling safe point: repair preemption, choose or
idle, select the address space, complete deferred work, recheck runnable state,
prepare architecture/accounting state, then restore the selected context
(`proc.c:299-458`). A trap/router does not own another scheduler.

The exception owner distinguishes guarded copy failures, kernel failure,
VM-origin page faults, and ordinary user faults. VM cannot recursively handle
its own page fault; that path is fatal. Ordinary page faults inhibit the
process and notify VM (`exception.c:38-109`). Other user exceptions may become
signals, while kernel exceptions remain fatal (`exception.c:169-244`).

These are responsibility boundaries, not a requirement to import MINIX's
process-as-thread representation, ARM vectors, kernel-call message ABI,
signal delivery, VM retry, or recovery.

## Existing `micros` authority

Accepted ADRs remain authoritative:

- ADR-0015 and its object/context refinements own the RISC-V vector, complete
  frame, hart stack anchor, nested sentinel, and restore ABI.
- ADR-0016, ADR-0026, and ADR-0027 own timer/accounting and common selected-user
  return, including direct S-origin return.
- ADR-0035 and ADR-0041 own captured syscall arguments, one `sepc + 4`, stable
  results, delegated operation handlers, and normal versus captured return.
- ADR-0043/0044/0045/0047 and their later lifecycle consumers own exact
  bootstrap, VM, TTY, and fatal-diagnostic ordering.
- ADR-0042 and its existing
  [runtime study](minix-user-service-runtime.md) own static-service startup,
  image installation, stateless wrappers, compiler support, and accidental
  return. Application argv/env and normal exit remain separate work.

The [scheduler study](minix-scheduler-and-context-switch.md), runtime study,
and existing IPC/grant/VM studies retain their original behavioral
classification ledgers. This correction does not reclassify those decisions.

## Classification of this correction

| ID | Choice | Classification | Boundary |
| --- | --- | --- | --- |
| TS-01 | Separate architecture context/entry validation from C user, supervisor-interrupt, syscall, and fault routing | Baseline parity | MINIX separates architecture entry/restore, exception, IPC, and kernel-call handling. |
| TS-02 | Preserve one common selected-user-return owner and direct interrupted S-context return | Baseline parity | No router-local scheduler, queue, return planner, or duplicated completion owner is introduced. |
| TS-03 | Retain the existing RV64/OpenSBI/Sv39 frame, CSR, `ecall`, and stack-anchor mechanisms | Required adaptation | These are the already Accepted target mechanisms, not ARM or x86 source structures. |
| TS-04 | Separate exact-profile/context preflight from operation-specific authority while preserving the existing unified namespace | Compatible extension | Existing generation checks, typed objects, immutable profiles, and captured result policy remain unchanged. |
| TS-05 | Leave ordinary user fault delivery and retry at the existing deferred boundary | Staged substitution | The existing Accepted VM/fault contracts retain their later replacement boundary; this source correction adds no signals, paging, or recovery. |
| TS-06 | Give existing target and native evidence explicit storage, automatic frame, and protocol owners | Compatible extension | This preserves the exact executions and makes register custody and failure evidence inspectable. |
| TS-07 | Preserve standalone image installation before startup and keep wrappers stateless | Baseline parity | No service protocol, PM exit, or application runtime is moved into generic startup. |
| TS-08 | Preserve fatal VM-origin page-fault handling without recursively asking VM to recover itself | Baseline parity | The existing phase-specific diagnostics retain the already Accepted observability contract. |

No new behavioral divergence is authorized. If extraction exposes a material
design defect, implementation stops for a separate corrective design rather
than repairing behavior inside a move.

## Derived source contract

[ADR-0061](../adr/0061-trap-and-syscall-source-decomposition.md) and the
[exact source contract](../architecture/trap-syscall-source-organization.md)
define the planned owners, fixed contexts, nine semantic long-function
splits, fourteen symbol moves, stable surfaces, and unchanged evidence.

The decomposition deliberately follows authority and protocol phase rather
than reproducing MINIX's file names or introducing its broader facilities.

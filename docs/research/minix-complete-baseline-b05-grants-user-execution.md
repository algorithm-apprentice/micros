# B05 Grant and User-Execution Baseline Audit

## Status

```text
audit unit:       B05 grants and user execution
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; six blocking findings; not certified
implementation:   forbidden during complete-audit phase
```

B05 audits grant identity and lifecycle, checked copy, the unified syscall
boundary, user context preparation/capture/return, trap routing, the
freestanding service runtime, the application startup adapter, and runtime
copy/fill primitives.

## Exact primary path manifest

B05 has 52 primary paths in the pinned tree.

### Public contracts

| Path | Responsibility |
| --- | --- |
| `include/micros/application_runtime.h` | Application entry, startup publications, and nonreturning exit |
| `include/micros/grant.h` | Grant token, record, registry, cancellation, and lifecycle ABI |
| `include/micros/grant_copy.h` | Copy authority, retained chunks, range validation, and directional copy |
| `include/micros/grant_runtime.h` | Authoritative target grant runtime |
| `include/micros/grant_syscall_core.h` | Captured grant-syscall request and decoder |
| `include/micros/runtime.h` | Freestanding service IPC/grant wrapper ABI |
| `include/micros/syscall_abi.h` | Unified operation numbers, captured arguments, and stable results |
| `include/micros/user_execution.h` | Context preparation, capture, validation, storage, and return API |

### Grant owners

| Path | Responsibility |
| --- | --- |
| `kernel/grant/grant.c` | Registry storage, token lifecycle, create/inspect/revoke/cancel, and copy authority |
| `kernel/grant/grant_abi.c` | Portable grant-error to syscall-result mapping |
| `kernel/grant/grant_copy.c` | Copy/remote-validation orchestration, phase, device exclusion, and IRQ scope |
| `kernel/grant/grant_copy_core.c` | Retained-plan validation, overlap rejection, and byte commit |
| `kernel/grant/grant_copy_internal.h` | Private range-planning contract |
| `kernel/grant/grant_copy_range.c` | At-most-two-chunk address translation and device-span exclusion |
| `kernel/grant/grant_internal.h` | Private failure-atomic create plan |
| `kernel/grant/grant_runtime.c` | Static authoritative registry and initialization scratch |
| `kernel/grant/grant_runtime_internal.h` | Mutable authoritative-registry accessor |
| `kernel/grant/grant_syscall.c` | Captured grant operation dispatch and result publication |
| `kernel/grant/grant_syscall.h` | Private captured grant-syscall adapter |
| `kernel/grant/grant_syscall_core.c` | Operations 7-10/15 shape and width decoding |

### User-execution owners

| Path | Responsibility |
| --- | --- |
| `kernel/execution/user_execution.c` | Stack derivation/clearing, bootstrap and post-handoff preparation, inspect/detach |
| `kernel/execution/user_execution_internal.h` | Private resolution and context-validation phases |
| `kernel/execution/user_execution_trap.c` | Trap-frame capture, stored-context update, and return-frame installation |
| `kernel/execution/user_execution_validation.c` | Canonical status, mapping, current-context, trap, and return validation |

### Trap and syscall owners

| Path | Responsibility |
| --- | --- |
| `kernel/trap/syscall.c` | Scalar capture, one `sepc + 4`, context resolution, and operation dispatch |
| `kernel/trap/syscall.h` | Captured syscall context and normal/captured return type |
| `kernel/trap/syscall_context.c` | Exact current thread/process resolution and fatal boundary |
| `kernel/trap/syscall_decode.c` | Early bootstrap/VM/PM/TTY control-shape decoding |
| `kernel/trap/syscall_internal.h` | Private top-level syscall phases |
| `kernel/trap/trap.c` | Trap installation, top-level origin routing, and nested fatal entry |
| `kernel/trap/trap_context.c` | CSR access, frame-stack validation, and hart-context resolution |
| `kernel/trap/trap_fault.c` | Bootstrap-service, TTY-owner, and VM self-fault classification |
| `kernel/trap/trap_internal.h` | Private causes, routing, hooks, and phase contracts |
| `kernel/trap/trap_interrupt.c` | User/supervisor timer and external-interrupt routing |
| `kernel/trap/trap_route_core.c` | Pure interrupt-origin/cause classifier |
| `kernel/trap/trap_route_core.h` | Interrupt-route result contract |
| `kernel/trap/trap_syscall.c` | User `ecall` bridge and common scheduler-return selection |
| `kernel/trap/trap_test_hooks.c` | Compile-time target-evidence bridges at retained route phases |
| `kernel/trap/trap_user.c` | Ordered user capture, fault, interrupt, syscall, fixture, and final-return routing |

### User-runtime owners

| Path | Responsibility |
| --- | --- |
| `lib/runtime/application.c` | Application startup publication, C entry, PM exit message, and fatal return |
| `lib/runtime/application.ld` | Fixed application image and fifteen-page bound |
| `lib/runtime/application_internal.h` | Private bounded startup decoder/result contract |
| `lib/runtime/application_stack.c` | Canonical one-page `argc`/`argv`/`envp`/auxv decoder |
| `lib/runtime/application_start.S` | Application `_start`, original-SP transfer, and fatal labels |
| `lib/runtime/memory.c` | Freestanding copy and fill implementations |
| `lib/runtime/memory.h` | Private copy/fill declarations |
| `lib/runtime/memory_symbols.c` | Compiler-visible `memcpy` and `memset` symbols |
| `lib/runtime/raw_syscall.S` | One eight-register RV64 `ecall` stub |
| `lib/runtime/raw_syscall.h` | Private raw syscall declaration |
| `lib/runtime/runtime.c` | Stateless IPC/grant wrappers |
| `lib/runtime/start.S` | Service `_start`, `gp`/`tp` policy, and fatal return |
| `lib/runtime/user.ld` | Fixed service ELF segment/linker contract |

## Cross-owner source dependencies

| Path family | B05 operation | Primary/later owner |
| --- | --- | --- |
| `arch/riscv64/trap.S`, `trap_context.h`, `user_entry.S`, `interrupt.h` | B05-O01/O02 architecture save/restore and serialization | B01 primary |
| `kernel/memory/**`, frame-ownership runtime | B05-O02/O06 context/copy translation and ownership | B02 |
| `kernel/objects/**`, `kernel/scheduler/**` | B05-O01-O03 context ownership and selected return | B03 |
| `kernel/endpoint/**`, `kernel/ipc/**` | B05-O03-O08 endpoint authority, IPC wrappers, and result return | B04 |
| `kernel/boot/**` | B05-O01/O03 bootstrap fault and control diagnostics | B06 |
| `kernel/vm/**`, `kernel/pm/**`, `kernel/tty/**` | B05-O01/O03/O06/O07 delegated controls, fault state, and device exclusion | B07-B09 |
| `include/micros/spawn.h`, application initial-stack construction | B05-O09 stack format and bounds | B08/B12 |
| B05 native and target evidence paths | All B05 operations | B14 |

## Fixed MINIX source evidence

The B05 comparison uses only local source:

- `minix/include/minix/safecopies.h`
- `minix/include/minix/ipc.h`
- `minix/include/minix/com.h`
- `minix/lib/libsys/safecopies.c`
- `minix/lib/libsys/sys_setgrant.c`
- `minix/lib/libsys/sys_safecopy.c`
- `minix/lib/libsys/kernel_call.c`
- `minix/lib/libc/sys/stack_utils.c`
- `minix/lib/libc/sys/init.c`
- `minix/lib/libc/arch/arm/sys/_ipc.S`
- `minix/lib/libc/arch/arm/sys/_do_kernel_call_intr.S`
- `minix/kernel/priv.h`
- `minix/kernel/proc.h`
- `minix/kernel/proc.c`
- `minix/kernel/cpulocals.h`
- `minix/kernel/system.c`
- `minix/kernel/system/do_privctl.c`
- `minix/kernel/system/do_setgrant.c`
- `minix/kernel/system/do_safecopy.c`
- `minix/kernel/arch/earm/mpx.S`
- `minix/kernel/arch/earm/exception.c`
- `minix/kernel/arch/earm/arch_system.c`
- `minix/kernel/arch/earm/memory.c`
- `minix/kernel/arch/earm/phys_copy.S`
- `common/lib/libc/arch/arm/string/memcpy.S`
- `common/lib/libc/arch/arm/string/memset.S`
- `common/lib/libc/string/memcpy.c`
- `common/lib/libc/string/memset.c`
- `lib/csu/arch/riscv/crt0.S`
- `lib/csu/common/crt0-common.c`
- `minix/lib/libexec/exec_elf.c`
- `minix/servers/vfs/exec.c`
- `minix/lib/libsys/sef.c`
- `minix/lib/libsys/sef_init.c`
- `minix/servers/pm/main.c`
- `minix/servers/sched/main.c`

## Logical variables

| Variable | Meaning |
| --- | --- |
| `gcap` | grant-record capacity |
| `ga` | active grant records |
| `b` | bytes in one checked copy or memory primitive |
| `s` | bytes occupied/validated in one initial application stack page |
| `id` | MINIX indirect-grant chain depth, bounded by five |
| `Vep` | B04 complete endpoint/object/IPC validation work |
| `Vgrant` | complete grant/endpoint/object relation validation work |
| `Rmem` | B02 complete address-space translation/lookup validation work |
| `Dtty` | B09 TTY device-authority validation work |

The pinned grant capacity is 64. One checked copy accepts at most 4,096 bytes
and produces at most two chunks for each participant.

## Validation cost definition

One complete grant validation:

1. scans all `gcap` records and their authority bytes;
2. invokes `Vep` once for the endpoint/object registry;
3. for every active record, resolves one grantor endpoint and one grantee
   endpoint through public endpoint resolution, each invoking `Vep` again.

```text
Vgrant = O(gcap + Vep + ga * (2*Vep + 1))
       = O(gcap * Vep) worst case
```

Because B04 established that `Vep` includes a `pcap^3` term, `Vgrant`
includes `gcap * pcap^3`.

## Operation ledger

### B05-O01 Trap entry, origin routing, and selected return

MINIX architecture entry writes the user register frame directly into the
current `struct proc`, reached through CPU-local current-process state.
User-origin exceptions and interrupts converge on `switch_to_user`; a
kernel-origin interrupt restores its interrupted kernel continuation.

`micros` uses the accepted RV64 trap anchor and a 288-byte supervisor frame.
It resolves the hart from the stored context pointer, accounts and captures
the selected thread, routes the event, then delegates every reschedulable
return to the common scheduler selector.

```text
architecture save/restore: fixed register count
MINIX current CPU/process: O(1)
micros trap hart resolution: O(hcap), confirmed by B03-F004
selected return: B03 common-return work
```

| Difference | Classification |
| --- | --- |
| One architecture save/restore boundary and one common selected-user return | Baseline parity |
| RV64 CSRs, `sscratch` anchor, direct vector, and emergency stack | Required adaptation |
| Distinct thread context rather than process-as-thread storage | Compatible extension |
| Capacity-wide hart ownership lookup | Divergence already recorded by B03-F004 |
| Fatal ordinary user faults before VM/signal delivery | Staged substitution; SOM-04 and SOM-09 |

### B05-O02 User context preparation, capture, validation, and detach

MINIX stores the architecture frame in the process table and uses a per-CPU
kernel stack. Context setup installs the new PC/SP/status before publication;
ordinary return consumes the selected stored frame.

`micros` owns a 264-byte saved RV64 context per thread, derives canonical user
status, validates executable PC and writable stack mappings, clears one
slot-derived 16 KiB supervisor stack before attachment, captures a U-origin
frame, and validates the selected frame before return.

```text
prepare:
    complete address-space validation
    + executable-page lookup
    + stack-page lookup
    + complete object/stack validation
    + O(16 KiB) stack clear
trap capture:
    O(hcap) current-thread proof + fixed 264-byte context copies
return validation:
    two address-space lookups + fixed context work
```

The per-thread stack and distinct thread context are the B03 accepted required
adaptation. The repeated complete address-space and object validation costs
confirm B02-F005, B03-F003, B03-F004, and B03-F005 rather than creating a
duplicate B05 finding.

### B05-O03 Unified user syscall capture, decode, and dispatch

MINIX saves the user frame, distinguishes IPC from kernel-call selectors, and
dispatches a bounded operation after caller privilege checks. Both paths
return through the common user-selection boundary.

`micros` captures `a0` through `a7`, advances `sepc` exactly once, decodes
known control-operation shape before current-context resolution, resolves the
exact current thread/process, and dispatches the dense operation namespace.

```text
local capture/decode/dispatch: O(1)
current-context ownership: B03 object/hart terms
delegated operation: operation-specific cost below
```

| Difference | Classification |
| --- | --- |
| One C-callable architecture boundary and integer result | Baseline parity |
| One RV64 `ecall` namespace instead of separate IPC/kernel-call traps | Required adaptation |
| Exact generation/current-process resolution and stable signed results | Compatible extension |
| No VM-suspended kernel-call retry | Staged substitution; SOM-04 and SOM-01 |

### B05-O04 Grant representation, identity, validation, and lifecycle

MINIX stores a dynamically grown grant table in each grantor's user address
space. Each slot embeds sequence state and a free-list next index. The kernel
stores only table address, extent, and the endpoint generation that owns it.
Direct verification indexes one selected entry and validates it after copying
that fixed record into kernel storage.

`micros` stores one global kernel-owned fixed array. Each record holds exact
grantor/grantee endpoint generations, range, direction, and a quarantining
generation. A separate `active_count` summarizes live records.

MINIX direct, indirect, and magic entries may name `ANY` as the grantee.
`CPF_TRY` additionally records a soft-copy fault in the mutable grant entry;
later `cpf_revoke()` returns `GRANT_FAULTED` to the grantor. `micros` requires
one exact grantee and returns the immediate copy error only; it has no
persistent grant fault-history bit or revoke result.

```text
MINIX direct verification: O(1)
MINIX indirect verification: O(id), id <= 5
micros direct record access after validation: O(1)
micros complete relation preflight: Vgrant
```

| Difference | Classification |
| --- | --- |
| Index/generation opaque token and revoke-time generation advance | Baseline parity |
| Kernel-managed records instead of mutable user table authority | Required adaptation |
| Exact participant generations and terminal quarantine | Compatible extension |
| Fixed `O(gcap)` registry storage | Compatible extension |
| Complete grant/endpoint/object audit on each ordinary operation | Divergence requiring correction |
| Indirect/magic grant delegation absent | Staged substitution; SOM-25 |
| Wildcard grantee authority absent | Staged substitution; SOM-25 |
| `CPF_TRY` fault marker and grantor-visible revoke result absent | Staged substitution; SOM-25 |

See B05-F001.

### B05-O05 Grant allocation, inspect, revoke, and cancellation

MINIX maintains one free-list head. Allocation pops in `O(1)` when a slot is
available. Table growth doubles capacity, copies/initializes `O(g)` records,
and is amortized over subsequent allocations. Revoke invalidates one indexed
slot, advances its sequence, and pushes it to the free-list head in `O(1)`.
Endpoint replacement invalidates the registered grant table through its stored
endpoint generation without scanning all grants.

`micros` scans from slot zero for every create, directly indexes inspect and
revoke only after `Vgrant`, and scans all records for endpoint-cancellation
preflight and commit.

```text
MINIX allocate: O(1) common/amortized, O(g) growth
MINIX revoke:   O(1)
micros create:  Vgrant + O(gcap) + endpoint resolves
micros inspect/revoke:
                Vgrant + endpoint resolve + O(1)
micros cancel prepare:
                Vgrant + endpoint resolve + O(gcap)
micros cancel commit:
                O(gcap)
```

The failure-atomic create/cancel plans and exact participant teardown are
compatible safety extensions. The missing free list and capacity-wide
unrelated-grant scan are not structural parity.

See B05-F003 and B05-F004.

### B05-O06 Checked range planning and byte transfer

MINIX verifies direct/indirect/magic authority, maps source and destination in
bounded address-space chunks, and uses the architecture `phys_copy` routine.
The ARM routine aligns addresses, moves multiword blocks, and uses byte copies
only for alignment/tails. Ordinary faults may suspend through VM; vectored
safe copy processes entries in order.

`micros` validates exact direct authority, restricts one operation to one
page, prepares at most two physical chunks per participant, excludes the TTY
device leaf, validates all source/destination intervals, and commits only
after complete failure-atomic preflight.

```text
authority: Vgrant + endpoint resolves
planning:  at most 4*Rmem + Dtty
overlap:   at most 2 * 2 interval comparisons
commit:    O(b), source-level byte iteration without an explicit block path
```

| Difference | Classification |
| --- | --- |
| Participant, direction, offset, and complete range before transfer | Baseline parity |
| Retained physical plans and all-or-error commit | Compatible extension |
| Exact `PROCESS_USER`/`VM_WIRED` ownership and device exclusion | Required adaptation |
| One-page scalar limit | Compatible bounded extension |
| VM fault suspension/retry absent | Staged substitution; SOM-04 |
| Indirect/magic/vectored operation breadth absent | Staged substitution; SOM-25 |
| Wildcard authority and persistent `CPF_TRY` fault history absent | Staged substitution; SOM-25 |
| Byte-at-a-time commit instead of alignment/block copy | Divergence requiring correction |

The repeated complete translation cost confirms B02-F005. See B05-F005.

### B05-O07 Grant syscall admission and result publication

MINIX direct grant create/revoke is user-table work. Safe copy packages one
kernel-call message and enters the kernel-call dispatcher. `micros` operations
7-10 and 15 use scalar registers in the unified `ecall` namespace.

Before decoding a selected grant operation, the target adapter validates the
complete grant runtime and the complete endpoint runtime. The selected
portable create/revoke/copy/validate operation then reruns `Vgrant`.
After a successful create preflight, operation 7 also obtains B09 TTY device
authority and rejects any grant range intersecting the exact user-mapped UART
leaf before publishing the token.

```text
one ordinary grant syscall:
    top-level current-context resolution
    + Vgrant
    + Vep
    + selected operation containing another Vgrant
grant create additionally:
    conditional Dtty + O(1) UART interval check
```

Operation 15's remote-only non-copying range validation is a compatible
extension for consuming protocols. Grant-time MMIO exclusion is a compatible
safety extension over the TTY mapping adaptation. Stable error mapping and
success-only token publication reproduce the required wrapper boundary.

See B05-F002.

### B05-O08 Freestanding service entry and typed wrappers

MINIX libc startup establishes hosted process state and installs IPC vector
wrappers before a service invokes its own SEF startup. The wrappers marshal
register/message fields and preserve the C ABI.

`micros` service `_start` clears unused `gp` and `tp`, calls one no-argument C
entry, and traps fatally if it returns. Typed wrappers marshal operations
1-10/15 through one fixed raw `ecall` stub.

```text
startup excluding service code: O(1)
one wrapper excluding kernel operation: O(1)
```

| Difference | Classification |
| --- | --- |
| C-callable assembly boundary, ABI preservation, direct integer result | Baseline parity |
| RV64 fixed stub and no mutable vector table | Required adaptation |
| Loader-owned image zeroing and external stack | Baseline parity |
| Static service image, no hosted libc/TLS/constructors | Staged substitution; SOM-08 and SOM-17 |
| No SEF readiness/restart layer in generic startup | Staged substitution; SOM-01 |
| Fatal service return before service-exit policy | Staged substitution; SOM-01 |

### B05-O09 Application startup, initial stack, and normal return

MINIX constructs `argc`, `argv`, `envp`, auxiliary data, strings, and
`ps_strings` in one bounded stack image. Hosted `crt0` publishes environment
state, initializes libc/constructors, calls `main`, then calls `exit`.

`micros` receives the same logical vectors in one canonical page. It validates
one through eight arguments, up to eight environment strings, fixed ordered
auxiliary entries, dense string placement, NUL bounds, alignment zeros, and
the zero tail before publishing process globals and calling
`micros_application_main`.

```text
decode: O(argument count + environment count + s)
normal return: O(1) message construction + one PM call
auxiliary storage: fixed candidate/layout/value arrays
```

| Difference | Classification |
| --- | --- |
| Argument/environment vectors and normal return to process exit | Baseline parity |
| Direct RV64 stack decoding and fixed auxiliary entries | Required adaptation |
| Complete bounded canonical validation before publication | Compatible extension |
| PM IPC exit and fatal unexpected reply | Required adaptation |
| No hosted libc, TLS, constructors, finalizers, or `errno` | Staged substitution; SOM-08 and SOM-17 |

### B05-O10 Runtime and kernel copy/fill primitives

MINIX's kernel `phys_copy` and libc `memcpy`/`memset` implementations align
addresses and move words or larger blocks, falling back to bytes only for
short or unaligned tails.

`micros_grant_copy_commit()` copies each retained chunk byte by byte.
`micros_runtime_memcpy()` and `micros_runtime_memset()` use volatile
byte-at-a-time loops for every length.

All are `O(b)`, but the logical algorithm family and memory-operation count
are not baseline parity. The volatile runtime loops also prevent the compiler
from widening the accesses.

See B05-F005 and B05-F006.

## Difference summary

| Area | Result |
| --- | --- |
| Trap origin, capture, and common return | Baseline shape with required RV64/distinct-thread adaptation; B03 hot-lookup findings confirmed |
| User context preparation/return | Accepted mapping/status/stack behavior; B02/B03 validation findings confirmed |
| Syscall namespace and wrappers | Required unified-RV64 adaptation with baseline marshalling/result shape |
| Grant identity and authority | Baseline token/direction semantics with required kernel-managed exact-generation adaptation |
| Grant validation cadence | Blocking divergence |
| Grant allocation/free structure | Blocking divergence |
| Grant endpoint cancellation index | Blocking divergence |
| Checked-copy preflight/failure atomicity | Compatible extension |
| Checked-copy transfer algorithm | Blocking divergence |
| Service/application startup | Baseline ownership with staged libc/TLS/SEF substitutions |
| Runtime `memcpy`/`memset` algorithms | Blocking divergence |

## Time, stack, and auxiliary-space envelope

Persistent B05-owned grant storage is:

```text
authoritative grant registry: 3,088 bytes
initialization scratch:        3,088 bytes
total:                         6,176 bytes
```

Representative automatic storage is:

```text
grant cancel plan:             288 bytes
grant create plan:              72 bytes
grant syscall work:            184 bytes
grant copy work:               184 bytes
grant remote-validation work:  120 bytes
user-execution binding:        296 bytes
saved user context:            264 bytes
architecture trap frame:       288 bytes
```

A grant create or copy syscall can nest the 184-byte syscall work, and copy
adds 184-byte copy work, with B04 endpoint/object validator frames, B02
translation validation, B03 selected-return planning, and B09
device-authority validation. User execution preparation can hold a 296-byte
candidate plus a 264-byte prepared context while invoking complete
object/address-space validation. No current evidence proves the complete
nested envelope against the 16 KiB supervisor stack.

B03 owns the persistent `tcap * 16 KiB` supervisor-stack pool. The application
runtime consumes one existing 4,096-byte user stack page, with at most 2,048
bytes occupied by the canonical initial frame; it does not allocate another
runtime stack.

## Staged omissions

| ID | B05 relation |
| --- | --- |
| SOM-01 | SEF/RS startup, retry, service lifecycle, and recovery |
| SOM-04 | VM-assisted page faults, suspended safe copy, and dynamic mappings |
| SOM-08 | TLS and multithreaded runtime state |
| SOM-09 | User-fault signal delivery and signal-frame return |
| SOM-17 | Hosted/POSIX libc, public wrappers, `errno`, constructors, and finalizers |
| SOM-25 | Indirect/magic/wildcard grants, persistent `CPF_TRY` fault history, and vectored safe-copy breadth |

## Evidence gaps

| ID | Missing evidence | Owner |
| --- | --- | --- |
| B05-E001 | No operation counter records complete `Vgrant`, nested `Vep`, and conditional `Dtty` calls per create/revoke/copy/validate syscall | B14 |
| B05-E002 | No allocation probe distinguishes free-list pop/growth from capacity-wide lowest-free scans | B14 |
| B05-E003 | No cancellation counter records unrelated grant slots visited for one endpoint | B14 |
| B05-E004 | No checked-copy work counter distinguishes byte, word, and block transfers across aligned/unaligned sizes | B14 |
| B05-E005 | No runtime copy/fill work counter covers short, aligned, unaligned, and page-sized inputs | B14 |
| B05-E006 | No complete nested grant-syscall/copy/context/validator stack evidence exists for the 16 KiB supervisor stack | B14 |
| B05-E007 | No executable evidence exists for SOM-25 delegated/wildcard grants, `CPF_TRY` history, or vectored copy | B14 after SOM-25 design |

## Findings register

### B05-F001: Complete grant relation validation precedes ordinary operations

- Severity: High
- `micros` evidence:
  - `micros_grant_registry_validate`
  - `micros_grant_prepare_create`
  - `resolve_owned_record`
  - `micros_grant_prepare_copy_authority`
- MINIX evidence:
  - direct slot/index validation in `cpf_grant_direct`/`cpf_revoke`
  - selected-entry `verify_grant`
- Difference: one local indexed grant operation first scans all grant records,
  validates the complete endpoint/object registry, then reruns complete
  endpoint validation twice for every active grant.
- Impact: ordinary grant work pays
  `Vgrant = O(gcap * Vep)`, including `gcap * pcap^3`, before local token,
  participant, permission, and range checks.
- Smallest correction boundary: retain complete relation validation as an
  explicit diagnostic operation; ordinary paths validate only runtime
  readiness, the indexed record, exact participants, and path-local
  generation/range state.
- Dependencies: B03-F001, B04-F001, and B04-F002.

### B05-F002: Grant syscalls validate the complete runtime twice

- Severity: High
- `micros` evidence:
  - `micros_grant_handle_captured_user_ecall`
  - `micros_grant_runtime_validate`
  - selected create/revoke/copy/validate operations
- MINIX evidence: one kernel-call dispatch followed by selected grant
  verification/copy
- Difference: the captured grant adapter runs complete grant and endpoint
  validation before decode, then every selected portable operation reruns
  complete grant validation. Create also conditionally enters complete B09 TTY
  validation before token publication.
- Impact: one valid user grant syscall pays at least `2*Vgrant` before its
  range translations or byte transfer, plus `Dtty` for active-phase create.
- Smallest correction boundary: one local runtime-readiness/current-context
  guard followed by exactly one selected path-local authority preflight;
  complete validators remain explicit diagnostics.
- Dependencies: B05-F001 and B04-F002.

### B05-F003: Grant allocation replaces the MINIX free list with a full scan

- Severity: Medium
- `micros` evidence: lowest-free loop in `micros_grant_prepare_create`
- MINIX evidence:
  - embedded free-list next index
  - `freelist` head
  - `cpf_new_grantslot`
  - LIFO push in `cpf_revoke`
- Difference: MINIX common allocation pops one free-list head in `O(1)` and
  grows amortized; `micros` scans `gcap` records on every create.
- Impact: grant-heavy I/O adds capacity-linear allocation work even after
  B05-F001 is removed.
- Smallest correction boundary: reproduce an embedded indexed free list and
  revoke-time push while retaining exact generations, quarantine, and
  failure-atomic publication.
- Dependencies: B05-F001 correction and B14 allocation counts.

### B05-F004: Endpoint grant cancellation visits every grant slot

- Severity: Medium
- `micros` evidence:
  - `micros_grant_prepare_endpoint_cancel`
  - `micros_grant_commit_endpoint_cancel`
  - capacity-sized generation plan
- MINIX evidence: grant-table authority is invalidated by endpoint-generation
  replacement; unrelated tables/entries are not scanned
- Difference: exact kernel-managed cleanup scans and records all `gcap` slots
  twice even when the endpoint has no or few grants.
- Impact: endpoint teardown gains `O(gcap)` unrelated work and a 288-byte
  capacity-sized plan.
- Smallest correction boundary: retain exact two-party cleanup but index
  records by participant so work is proportional to related grants, or supply
  a separately Accepted measured justification for the capacity scan.
- Dependencies: B04 endpoint-close correction and B05-F003.

### B05-F005: Checked copy commits one byte at a time

- Severity: High
- `micros` evidence: inner byte loop in `micros_grant_copy_commit`
- MINIX evidence:
  - `lin_lin_copy`
  - ARM `phys_copy` alignment and 32-/16-/12-/4-byte transfer loops
- Difference: both mechanisms are linear in `b`, but `micros` performs one
  source-level byte iteration instead of the reference alignment plus
  multiword block algorithm; no structural or generated-code evidence proves
  an equivalent widened transfer path.
- Impact: every TTY, RAMFS, VFS, executable, and future POSIX data path using
  checked grants can incur a large constant-factor regression.
- Smallest correction boundary: add an independently written RISC-V
  alignment/block copy primitive over the already validated nonoverlapping
  physical plans, with byte tails and unchanged failure atomicity.
- Dependencies: B02 translation correction and B14 transfer-work evidence.

### B05-F006: Runtime copy and fill are volatile byte loops

- Severity: High
- Current correction design: COPY01 follows merged EV04 #189.
  [Accepted ADR-0090](../adr/0090-minix-word-based-runtime-memory.md)
  defines the generic MINIX word family and exact authority transition.
  This finding and B05-E005 remain open until separate implementation,
  actual evidence and independent review complete.
- `micros` evidence:
  - `micros_runtime_memcpy`
  - `micros_runtime_memset`
- MINIX evidence:
  - architecture `memcpy`/`memset` alignment and block loops
  - generic word-at-a-time fallbacks
- Difference: every runtime byte is accessed through volatile unsigned-char
  loops, preventing compiler widening and omitting the reference word/block
  algorithm family.
- Impact: compiler-emitted aggregate copies/zeroing and explicit service
  memory operations can execute one load/store per byte across all user
  services and applications.
- Smallest correction boundary: independently implement standard-defined
  RV64 alignment-aware word/block copy and fill with short/unaligned tails,
  preserving `memcpy`'s nonoverlap contract and exact exported symbols.
- Dependencies: B14 copy/fill work evidence.

## Unit conclusion

B05 primary-path and operation coverage is complete for the pinned tree. The
audit adds B05-F001 through B05-F006 as blocking findings,
confirms B02-F005, B03-F001/F003/F004/F005, and B04-F001/F002, and records
SOM-01, SOM-04, SOM-08, SOM-09, SOM-17, and SOM-25.

B05 is **audited but not certified**. Independent review corrected wildcard
and `CPF_TRY` grant coverage plus grant-create TTY validation and stack/cost
accounting. Final re-review reported B05 complete. No B05 correction may start
during the read-only audit.

## References

- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)
- [B02 memory audit](minix-complete-baseline-b02-memory.md)
- [B03 object/scheduler audit](minix-complete-baseline-b03-objects-scheduler.md)
- [B04 endpoint/IPC audit](minix-complete-baseline-b04-endpoint-ipc.md)
- [MINIX direct grant and safe-copy study](minix-direct-grants-and-safecopy.md)
- [MINIX user grant syscall study](minix-user-grant-syscall-boundary.md)
- [MINIX user-service runtime study](minix-user-service-runtime.md)
- [MINIX scheduler/context study](minix-scheduler-and-context-switch.md)
- [MINIX ELF execution/spawn study](minix-elf-exec-and-spawn.md)

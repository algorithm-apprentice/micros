# ADR-0042: Freestanding User-Service Runtime

- Status: Accepted
- Date: 2026-10-08
- Refines: ADR-0002, ADR-0004, ADR-0022, ADR-0035, and ADR-0041

## Context

The kernel/user boundary now has stable operations 1 through 10:

- IPC send, receive, call, reply, reply/receive, and notify;
- direct-grant create and revoke;
- checked grant copy-from and copy-to;
- stable signed results;
- exact current-process authority;
- bootstrap and handed-off wired address resolution.

The next dependency-DAG outcome is the smallest user-side layer that lets a
freestanding C service enter at a reviewed address, call those operations, and
remain independent from the host libc.

The existing target tests execute relocation-free instruction payloads copied
out of the kernel ELF. Those payloads prove privilege and syscall mechanisms,
but they are not standalone user executables and do not define:

- a user ELF and linker contract;
- `_start`;
- an external stack contract;
- `gp` or `tp` initialization;
- a C service entry;
- user-side syscall wrapper names and types;
- compiler-emitted memory support;
- accidental service-return behavior.

The fixed MINIX baseline uses hosted `crt0`, libc initialization,
constructor-selected IPC vectors, user-managed grant helpers, libsys safe-copy
messages, and SEF service startup. The canonical trace and the sole
ADR-0025 classification ledger for this outcome are in
[the user-service runtime study](../research/minix-user-service-runtime.md).

This design must not pull the static launcher or any service protocol ahead of
the runtime in the development DAG.

## Decision

### Single outcome

This outcome defines:

- one fixed non-PIE standalone user ELF contract;
- one minimal RISC-V `_start`;
- one raw eight-register `ecall` stub;
- stateless C wrappers for operations 1 through 10;
- exact result and output-preservation behavior;
- the minimum compiler support required by freestanding C;
- native, static, and one real QEMU runtime acceptance boundary;
- validation ownership and implementation commit boundaries.

The runtime owns no endpoint, grant, process, mapping, readiness, or service
protocol state. It marshals one call at a time to the accepted kernel ABI.

### Non-goals

This outcome does not define or implement:

- the bootstrap launcher;
- an embedded-service manifest;
- service readiness or dependency protocols;
- VM, PM, TTY, RAMFS, VFS, init, shell, or application behavior;
- PM exit, process exit, wait, or return-status semantics;
- dynamic VM allocation, mapping, unmapping, faults, retry, or paging;
- an application ELF-loading transaction;
- libc, POSIX, `errno`, signals, or file descriptors;
- a heap, allocator, `sbrk`, or `malloc`;
- thread-local storage;
- constructors, finalizers, `atexit`, or C++ runtime support;
- `argc`, `argv`, environment strings, or auxiliary vectors;
- PIE, dynamic linking, a runtime loader, or relocations;
- asynchronous IPC, vectored safe copy, or additional syscall operations.

The first launcher and each service remain separate reviewed
design-implementation pairs after this runtime merges.

### Dependency position

This is the second half of development-DAG Step 7:

1. stable IPC, direct grants, checked copies, wired handed-off reads, and
   operations 1 through 10;
2. this freestanding runtime;
3. only then the static bootstrap launcher.

The implementation phase may begin only after this ADR is reviewed, marked
Accepted, and merged.

## Standalone user ELF contract

### ELF identity

Every initial service image is:

- ELF64;
- little-endian;
- `EM_RISCV`;
- `ET_EXEC`;
- LP64 soft-float ABI;
- linked for `rv64imac_zicsr_zifencei`;
- statically linked without a program interpreter or dynamic section;
- non-PIE and relocation-free after final link.

The RISC-V ELF flags may contain the compressed-instruction flag required by
the accepted ISA. They must not claim an unsupported floating-point ABI, RVE,
TSO, or another unreviewed extension.

The ELF entry is the global `_start` symbol. Both are exactly:

```text
0x0000000040000000
```

This is `MICROS_USER_VIRTUAL_BASE`, the start of the existing private user
window. Distinct processes may use the same fixed virtual layout because each
owns a separate Sv39 root.

### Load segments and permissions

The linker script defines three page-separated `PT_LOAD` permission classes:

| Segment | Required flags | Contents |
| --- | --- | --- |
| Text | `R|X` | `_start`, raw syscall assembly, runtime text, service text |
| Read-only | `R` | constants and other immutable allocatable data |
| Writable | `R|W` | initialized data followed by zero-fill BSS |

Every load segment:

- has `p_align == 4096`;
- begins on a 4096-byte virtual-address boundary;
- has congruent file offset and virtual address modulo 4096;
- lies completely inside
  `[MICROS_USER_VIRTUAL_BASE, MICROS_USER_VIRTUAL_END)`;
- has `p_filesz <= p_memsz`;
- does not overlap another load segment;
- has `p_paddr == p_vaddr` to avoid a second address interpretation.

No load segment is both writable and executable. The read-only and writable
segments begin on new pages so permissions never share one leaf.

The text segment begins with a kept `.text.start` section containing `_start`.
The linker then collects:

```text
.text .text.*
.rodata .rodata.*
.data .data.*
.bss .bss.* COMMON
```

The final script exports:

```text
__micros_user_image_start
__micros_user_text_start
__micros_user_text_end
__micros_user_rodata_start
__micros_user_rodata_end
__micros_user_data_start
__micros_user_data_end
__micros_user_bss_start
__micros_user_bss_end
__micros_user_image_end
```

Each permission boundary is page aligned. BSS is `SHT_NOBITS` and lies only
inside the writable segment. At least one 4 KiB page must remain available
below `MICROS_USER_VIRTUAL_END` for the external stack, so the linker rejects:

```text
__micros_user_image_end > 0x000000007ffff000
```

The actual stack may be larger and therefore imposes a lower per-image overlap
limit at load time.

### Forbidden ELF state

The final user ELF contains no:

- `PT_INTERP`, `PT_DYNAMIC`, or `PT_TLS`;
- dynamic symbol, hash, PLT, GOT, or versioning section;
- relocation section or remaining relocation entry;
- `.preinit_array`, `.init_array`, `.fini_array`, constructor, or destructor
  section;
- `.tdata` or `.tbss`;
- `.sdata` or `.sbss`;
- unwind, exception, stack-protector, sanitizer-runtime, or profiling section;
- undefined symbol;
- host-libc dependency.

Non-allocatable metadata such as the RISC-V attributes section and debug
information may remain in the ELF file. It is not mapped into the process.

The target is compiled with the accepted freestanding warning policy plus:

```text
-fno-pic
-fno-pie
-fno-common
-fno-builtin
-fno-stack-protector
-fno-unwind-tables
-fno-asynchronous-unwind-tables
-mcmodel=medany
-msmall-data-limit=0
-mno-save-restore
```

The link uses `-nostdlib`, `-static`, the dedicated user linker script,
garbage collection, no build ID, no relaxation, and fatal linker warnings.
The implementation must use discovered LLVM/LLD tool paths and must not
hard-code a host installation prefix.

### Image installation and BSS

The runtime does not load or relocate itself.

Before `_start`, the external image owner must:

1. validate the complete ELF contract;
2. allocate every page touched by each load segment;
3. zero every allocated page completely;
4. copy exactly each segment's `p_filesz` bytes from the ELF file;
5. leave every other byte through `p_memsz` zero;
6. install the segment's final user permissions;
7. create the external stack described below;
8. perform the required local instruction-fetch synchronization; and
9. prepare the user context with `sepc == e_entry` and the exact initial
   stack pointer.

Zeroing whole pages before copying file bytes defines section padding and BSS
without requiring startup code to find linker symbols or write its own image.
It also preserves the accepted full-frame zeroing discipline.

The future launcher may install embedded service images through this contract.
The later PM/VFS/VM spawn transaction may consume the same ELF shape, but that
transaction is outside this outcome.

## External stack contract

The user stack is not an ELF segment.

The external image owner maps one or more complete writable, user-accessible,
non-executable pages ending at:

```text
MICROS_USER_STACK_TOP = MICROS_USER_VIRTUAL_END
                      = 0x0000000080000000
```

The stack region:

- has a page-aligned bottom;
- is at least 4096 bytes;
- does not overlap any load segment;
- is completely zero before first entry;
- remains resident for every blocking runtime operation in this bootstrap
  phase.

The initial `sp` equals `MICROS_USER_STACK_TOP`, exactly one byte past the
mapped region. It is 16-byte aligned, and `sp - 1` resolves to the writable
stack mapping as required by ADR-0022.

The initial stack contains no return address, argument count, argument
pointers, environment, auxiliary vector, or runtime control block. `_start`
does not realign, probe, clear, or move `sp`.

The runtime does not know the stack bottom or own stack growth. Stack size,
guard pages, overflow policy, and later dynamic growth belong to the image
owner and future service/VM designs.

## Startup contract

### Register policy

At `_start`:

- `sp` is the exact external stack pointer;
- `sepc` names `_start`;
- user status is the canonical ADR-0022/ADR-0023 value;
- all other general registers are unspecified input.

Startup performs only:

1. set `gp` to zero;
2. set `tp` to zero;
3. call `micros_service_main`;
4. if it returns, execute the accidental-return path.

`gp == 0` is valid because small-data sections, global-pointer relaxation, and
GOT use are forbidden. `tp == 0` is the explicit no-TLS state.

Startup does not:

- clear BSS or the stack;
- initialize another register;
- initialize libc or `errno`;
- call constructors;
- create grants or perform IPC;
- publish readiness;
- discover the process endpoint;
- enable a heap;
- install a process-exit callback.

### C service entry

The public service entry is:

```c
void micros_service_main(void);
```

It is deliberately not declared `_Noreturn`. Returning from the function is
therefore defined C behavior that reaches a deterministic runtime fallback
rather than creating undefined behavior through a false function annotation.

The implementation does not expose hosted `main`.

### Accidental return

`_start` itself never returns. Immediately after
`micros_service_main()` returns, it reaches a global diagnostic symbol:

```text
micros_runtime_service_returned
```

At that symbol it executes one uncompressed `ebreak`. If a test-specific or
future handler resumes execution, startup enters a local infinite branch and
does not re-enter the service or consume an uninitialized return address.

Under the current production trap policy, an accidental service return is a
fatal unexpected U-mode breakpoint with the exact user PC in panic
diagnostics. The later PM/process-exit design must explicitly replace this
fallback if services gain ordinary exit semantics.

## Raw RISC-V syscall boundary

### Private function

The runtime has one internal function:

```c
int64_t micros_runtime_raw_syscall(
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4,
    uint64_t a5,
    uint64_t a6,
    uint64_t a7
);
```

The RV64 C ABI places those eight values directly in `a0` through `a7`.
The complete target implementation is an uncompressed:

```asm
ecall
ret
```

sequence. It has no prologue, stack access, global access, or hidden
translation. It consumes the operation and arguments already supplied by its
caller and returns the signed `a0` bits.

The stub does not touch `sp`, `gp`, `tp`, `s0` through `s11`, or memory.
`a1` through `a7` and the temporary registers remain subject to the ordinary
C caller-saved rule, although the accepted kernel ABI changes only `a0` and
`sepc`. Target assembly evidence checks that stronger kernel/stub property.

The symbol has external linkage only so the same production C wrappers can be
linked natively against a host capture stub. It is not part of the public
service API.

## Public C wrapper interface

### Result type

The public runtime header defines:

```c
typedef int64_t micros_runtime_result_t;
```

Every wrapper returns:

- `MICROS_SYSCALL_ABI_OK` on ordinary success;
- one exact negative `MICROS_SYSCALL_ABI_*` value on failure.

There is no `errno`, thread-local result, global last-error value, automatic
retry, or silent result translation.

Compile-time assertions require:

- eight-bit bytes;
- `sizeof(uintptr_t) == 8`;
- `sizeof(size_t) == 8`;
- `sizeof(micros_runtime_result_t) == 8`;
- `sizeof(micros_endpoint_t) == 4`;
- `sizeof(micros_grant_t) == 4`;
- the accepted 64-byte, eight-byte-aligned IPC message layout.

### IPC wrappers

```c
micros_runtime_result_t micros_runtime_send(
    micros_endpoint_t destination,
    const struct micros_ipc_message *message
);

micros_runtime_result_t micros_runtime_receive(
    micros_endpoint_t source,
    struct micros_ipc_message *message
);

micros_runtime_result_t micros_runtime_call(
    micros_endpoint_t destination,
    struct micros_ipc_message *message
);

micros_runtime_result_t micros_runtime_reply(
    uint64_t reply_token,
    const struct micros_ipc_message *message
);

micros_runtime_result_t micros_runtime_reply_receive(
    uint64_t reply_token,
    const struct micros_ipc_message *reply_message,
    micros_endpoint_t source,
    struct micros_ipc_message *receive_message
);

micros_runtime_result_t micros_runtime_notify(
    micros_endpoint_t destination,
    uint64_t event_mask
);
```

Their data behavior is:

- `send` and `reply` treat the supplied message as input;
- `receive` publishes a canonical message only on success;
- `call` snapshots the request and later replaces the same buffer with the
  canonical reply only on success;
- `reply_receive` snapshots the reply and publishes the next canonical
  message in `receive_message` only on success;
- `notify` transfers no user memory.

The runtime does not hide or translate IPC reply tokens. A server reads the
nonzero token from the canonical request message and passes the complete
`uint64_t` value to `reply` or `reply_receive`. Delivered replies retain the
accepted token-zero message shape.

The accepted kernel buffer rules remain authoritative. A negative result
leaves every message output unchanged. A blocking call may safely retain a
stack-local message because the stack is resident and the kernel retains and
revalidates the user virtual address rather than a physical pointer.

### Grant wrappers

```c
micros_runtime_result_t micros_runtime_grant_create(
    micros_endpoint_t grantee,
    uintptr_t base,
    size_t length,
    uint32_t permissions,
    micros_grant_t *grant_out
);

micros_runtime_result_t micros_runtime_grant_revoke(
    micros_grant_t grant
);

micros_runtime_result_t micros_runtime_grant_copy_from(
    micros_endpoint_t grantor,
    micros_grant_t grant,
    size_t grant_offset,
    uintptr_t local_address,
    size_t length
);

micros_runtime_result_t micros_runtime_grant_copy_to(
    micros_endpoint_t grantor,
    micros_grant_t grant,
    size_t grant_offset,
    uintptr_t local_address,
    size_t length
);
```

`micros_runtime_grant_create()` requires a valid `grant_out`. A null output
returns `MICROS_SYSCALL_ABI_ARGUMENT` without executing `ecall`.

The raw create operation returns either:

- one nonnegative opaque 32-bit token; or
- one stable negative result.

The C wrapper keeps `*grant_out` unchanged for every negative result. Only
after a nonnegative result does it cast the accepted token to
`micros_grant_t`, store it once, and return `MICROS_SYSCALL_ABI_OK`.
The wrapper does not decode the opaque token's internal packing.

Revoke and both copy wrappers return the raw stable result directly. A copy
failure transfers zero bytes by the accepted kernel contract.

### Exact raw register mapping

All values are explicitly converted to `uint64_t` before the raw call.
Endpoints, grant tokens, and permission masks are therefore zero-extended
despite RV64's procedure-call rules for narrower integer arguments. Pointers
pass through `uintptr_t`.

| Wrapper | `a0` | `a1` | `a2` | `a3` | `a4` | `a5` | `a6` | `a7` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `send` | destination | message | 0 | 0 | 0 | 0 | 0 | 1 |
| `receive` | source | message | 0 | 0 | 0 | 0 | 0 | 2 |
| `call` | destination | in/out message | 0 | 0 | 0 | 0 | 0 | 3 |
| `reply` | token | message | 0 | 0 | 0 | 0 | 0 | 4 |
| `reply_receive` | token | reply message | source | receive message | 0 | 0 | 0 | 5 |
| `notify` | destination | event mask | 0 | 0 | 0 | 0 | 0 | 6 |
| `grant_create` | grantee | base | length | permissions | 0 | 0 | 0 | 7 |
| `grant_revoke` | grant | 0 | 0 | 0 | 0 | 0 | 0 | 8 |
| `grant_copy_from` | grantor | grant | offset | local address | length | 0 | 0 | 9 |
| `grant_copy_to` | grantor | grant | offset | local address | length | 0 | 0 | 10 |

The wrappers do not perform endpoint, grant, range, permission, mapping, or
message validation locally. Duplicating kernel policy would risk different
error precedence. The only runtime-owned argument is the grant-create output
pointer.

## Minimal compiler support

The runtime defines exactly these standard compiler-support symbols:

```c
void *memcpy(void *restrict destination, const void *restrict source,
    size_t length);
void *memset(void *destination, int byte, size_t length);
```

Their byte-loop bodies use private, runtime-prefixed C functions. The target
standard symbols are thin aliases or forwarding definitions to those bodies.
Native sanitizer tests link the prefixed bodies without interposing the host
libc's `memcpy` or `memset`; the target ELF check proves every retained
compiler-generated reference resolves internally. Linker garbage collection
may omit an unreferenced helper from a particular service image.

They:

- operate byte for byte;
- accept unaligned addresses;
- return the original destination;
- allocate no memory;
- use no syscall;
- have deterministic bounded loops;
- compile without recursively calling themselves.

`memcpy` requires nonoverlapping objects, as in C17. The runtime does not
provide `memmove`, `memcmp`, string functions, formatting, allocation,
division helpers, atomics, unwind support, or another compiler-runtime
facility.

These symbols exist so Clang may lower aggregate copy and zero initialization.
They are not a general libc API. Any additional undefined target symbol makes
the final ELF check fail and requires a new reviewed need rather than an
implicit host library.

## Runtime invariants and failure boundary

- The runtime contains no writable global control state.
- A wrapper does not retain an endpoint, token, pointer, result, or message
  after the raw call returns.
- There is no runtime lock or interrupt manipulation; serialization remains a
  kernel property.
- No wrapper changes kernel error precedence.
- No wrapper converts a negative result into success.
- `grant_out` is the only wrapper-owned output and changes only on successful
  grant creation.
- Startup never writes text or read-only data.
- Startup depends on loader-zeroed BSS and stack and does not mask a broken
  image-install contract.
- `gp` and `tp` stay zero unless a later accepted runtime explicitly adds
  small-data or TLS support.
- The only normal control transfer out of `_start` is into
  `micros_service_main`.
- Returning from the service cannot fall through into arbitrary bytes.

## Test-first evidence

### Native wrapper tests

Production wrapper C is linked natively against a host implementation of
`micros_runtime_raw_syscall()` that:

- records all eight arguments;
- returns a configured signed value;
- optionally writes through a configured message pointer to model successful
  kernel output;
- counts invocations.

Table-driven tests cover every wrapper and require:

- exact `a0` through `a7` values;
- zero in every unused register selected by this runtime;
- full 64-bit token, event-mask, address, offset, and length preservation;
- zero-extension of endpoint, grant, and permission types;
- exact propagation of every stable negative result;
- no `errno` change;
- no message mutation by wrapper code;
- receive, call, and reply/receive output publication only when the host stub
  models a successful kernel write;
- grant-create null-output rejection without a raw call;
- grant-create token publication and normalized `OK` return on success;
- grant-create output preservation on every negative result.

The same native suite tests the production byte-loop bodies for zero,
one-byte, unaligned, boundary, and representative larger lengths under ASan
and UBSan. It checks destination return values, exact copied/set bytes, and
surrounding canaries without replacing the host sanitizer runtime's standard
memory symbols.

No state-machine model is required because the runtime is stateless and every
kernel transition is already modeled by the accepted IPC and grant suites.

### ELF, linker, and static checks

The implementation adds a repository-owned user-ELF checker and native
regressions for its parser. It verifies:

- ELF class, endianness, machine, type, flags, and exact entry;
- exact `_start` and linker-boundary symbols;
- the three program-header permission classes;
- page alignment, offset congruence, range, nonoverlap, and
  `p_filesz <= p_memsz`;
- allocatable-section closure inside a compatible load segment;
- BSS `SHT_NOBITS` placement in the writable segment;
- no W+X page;
- no forbidden dynamic, relocation, TLS, constructor, small-data, unwind, or
  sanitizer state;
- no undefined symbol;
- no host-libc or dynamic dependency;
- an exact two-instruction raw stub with no stack or global access;
- no instruction that writes `gp` or `tp` outside the two startup
  initialization instructions;
- an accidental-return breakpoint at
  `micros_runtime_service_returned`.

Malformed-fixture regressions exercise each rejection class. The checker does
not infer success from section names alone; it validates ELF headers, program
headers, section flags, symbols, and disassembly metadata.

### QEMU freestanding runtime acceptance

The implementation adds one isolated workflow, intended to be named:

```text
test-qemu-user-runtime
```

This workflow does not exist at design time.

Its kernel test image embeds one standalone user ELF linked through the
production runtime. It creates three exact process generations, active
endpoints, private address spaces, and external stacks. Each user instance
uses the same fixed virtual layout in its distinct root.

The test maps one production-linked service ELF into all three roots. That ELF
contains one versioned test-only configuration object in writable initialized
data. After validating and copying the ELF but before first entry, the kernel
test writes the exact client, server, and peer roles and endpoint values into
that object through the inactive root's physical mapping. A separate
initialized-data sentinel remains untouched and must retain its file value.
This patch is QEMU-fixture setup, not an argument, environment, runtime API, or
cross-process pointer channel.

A host-side test fixture generator consumes only an ELF that has passed the
standalone checker and emits bounded immutable segment descriptors and file
bytes for the kernel test image. The target harness applies those descriptors;
it does not add a reusable production ELF parser or the later PM/VFS/VM load
transaction.

The test must:

1. dirty candidate frames, install each ELF through its program headers, and
   prove complete zero-fill outside file bytes;
2. map final RX, R, and RW/NX permissions, create zeroed external stacks, and
   execute the required `fence.i`;
3. enter through `_start`, require `gp == 0`, `tp == 0`, and a valid aligned
   stack, and verify initialized data, zero BSS, and readable rodata;
4. attempt one labeled write to rodata, take the exact expected user store
   page fault, and resume after proving the writable segments were unchanged;
5. retain stack-local canaries across at least one blocking syscall and
   scheduler switch;
6. use an assembly probe around the production raw stub to prove `sp`, `gp`,
   `tp`, `s0` through `s11`, and the kernel-preserved non-result argument
   registers survive a real syscall return;
7. execute production operations 1 through 6 through the runtime boundary,
   including blocked receive, send, two call rounds, standalone reply,
   atomic reply/receive, and notification wakeup;
8. execute production operations 7 through 10, including read and write grant
   creation, copy-from, copy-to, revoke, and stale-token rejection;
9. verify canonical messages, reply tokens, copied bytes, stable results, and
   surrounding canaries in user code rather than replacing wrapper behavior
   with kernel-owned test messages;
10. intentionally return from one `micros_service_main`, trap at the exact
    `micros_runtime_service_returned` breakpoint, and prove that no arbitrary
    fallthrough occurred;
11. stop the remaining test threads, revoke remaining grants, close endpoints,
    destroy bootstrap roots, release objects and frames, and restore the
    pre-test baseline.

The operation choreography is fixed:

1. the server blocks in `receive(ANY)`;
2. the client performs one assembly-observed raw `SEND`, waking the server;
3. the client performs two `CALL` operations;
4. the server completes the first with `REPLY` and the second with
   `REPLY_RECEIVE(ANY)`;
5. the peer performs `NOTIFY`, satisfying that combined receive;
6. the client creates one read grant and one write grant for the server and
   sends both tokens through an ordinary runtime `send`;
7. the server performs `grant_copy_from` and `grant_copy_to`, verifies the
   bytes, and sends completion;
8. the client revokes both grants and tells the server to retry one old token;
9. the server requires `STALE_GRANT`, reports completion, and returns from its
   service entry.

Other runtime sends and receives provide the synchronization messages for
steps 6 through 9. No shared user mapping or kernel-written trusted message
substitutes for those wrapper calls.

Only the complete sequence may emit:

```text
MICROS_USER_RUNTIME_TEST_PASS elf=freestanding startup=validated syscalls=1-10 registers=preserved stack=external data=initialized bss=zero rodata=protected return=trapped cleanup=complete
```

The image proves the runtime in the bootstrap phase. Existing ADR-0040 and
ADR-0041 handed-off syscall evidence remains the authority that the same
kernel ABI operates on exact `VM_WIRED` mappings; this test does not duplicate
the irreversible handoff.

## Validation ownership

The implementation must extend the fail-closed planner when the new files and
workflow exist.

Changes to the public runtime header or wrapper C select:

- complete native tests containing the host raw-stub suite;
- the user-ELF/static checks;
- `test-qemu-user-runtime`;
- documentation and all three diff checks.

Changes to user startup assembly, the raw syscall stub, linker script, target
runtime flags, or user-image build helper select:

- complete native tests;
- the complete user-ELF/static checks;
- `test-qemu-user-runtime`;
- every additional target gate selected by existing shared
  toolchain/linker/entry ownership;
- documentation and all three diff checks.

Changes to the QEMU runtime component, its embedded image, or its host runner
select that workflow and the runner's native parser regressions.

Any change to the accepted syscall ABI or kernel dispatcher is outside the
intended implementation scope and retains the broader ADR-0041 validation
ownership.

The implementation pull request records exact author-run commands only after
the new commands exist. This design does not claim those commands or targets
are currently available.

## Implementation commit plan

The later implementation uses three green commits:

1. **Add the stateless C runtime surface.**
   - Red: native wrapper tests fail to compile or link because the public
     runtime API and raw-stub symbol are absent.
   - Green: add the public header, wrapper C, host capture stub, and exact
     wrapper/output tests.
   - Include native `memcpy`/`memset` tests and the minimum implementations
     when required to keep the commit complete.

2. **Add the standalone RISC-V user image.**
   - Red: the user-ELF checker rejects the missing startup, linker layout, raw
     target stub, and section contract.
   - Green: add `_start`, the raw `ecall` assembly, the user linker script,
     freestanding target flags, one standalone fixture ELF, and the complete
     static checker regressions.

3. **Prove the runtime under QEMU.**
   - Red: the isolated runtime image cannot produce the required acceptance
     marker.
   - Green: add the three-process acceptance scenario, host runner,
     intent-based workflow, fail-closed validation ownership, and directly
     related documentation.

Each commit remains buildable and green for every gate that exists at that
commit. No launcher, manifest, service implementation, PM exit, dynamic VM,
libc, or application code is included.

## Consequences

- Initial services receive one small and reviewable C boundary over the stable
  kernel ABI.
- User images become independently inspectable ELFs instead of copied slices
  of the kernel executable.
- Fixed virtual addresses are safe across isolated roots and avoid a runtime
  relocator before one is needed.
- Loader-owned BSS and stack setup keep startup assembly minimal.
- Direct stable results avoid hidden `errno` and TLS state.
- The runtime is ready for the launcher without defining launcher policy.
- Service return remains fatal until PM owns process-exit semantics.
- The lack of libc, heap, TLS, constructors, and arguments is explicit rather
  than accidental.
- Later application or POSIX work may add a separate runtime without changing
  operations 1 through 10.

## Alternatives considered

### Reuse kernel-linked user payload fragments

Rejected. They do not define an independent ELF, section permissions, symbol
closure, BSS, stack, or host-libc exclusion and cannot become service images.

### Add launcher and readiness in the same outcome

Rejected. The launcher is the next DAG node and has separate manifest,
privilege, release, timeout, and readiness authority.

### Use inline assembly in every wrapper

Rejected. One raw function centralizes the architecture boundary, preserves a
single test seam, and keeps register marshalling table-driven in C.

### Expose raw token-or-error from grant create

Rejected. A typed output plus stable result makes error handling uniform with
the other wrappers and guarantees output preservation on failure.

### Add `errno`

Rejected. It requires hidden mutable state and later TLS semantics without
improving the already stable kernel results.

### Clear BSS in `_start`

Rejected. It duplicates the ELF loader contract, writes the image before C
entry, and can hide a broken segment installer.

### Put the stack in the ELF

Rejected. Stack size and mapping ownership belong to the process/image owner,
not to file-backed load segments.

### Declare the service entry `_Noreturn`

Rejected. An accidental return would then be undefined behavior and could be
optimized past the required deterministic failure boundary.

### Add a general libc subset

Rejected. Only `memcpy` and `memset` are currently required compiler support.
Every additional API needs a dependency-ready consumer and reviewed contract.

### Use PIE or dynamic relocation

Rejected. Distinct process roots already permit one fixed layout, while a
runtime relocator, GOT, interpreter, and symbol-resolution policy would pull
later executable and libc work into this prerequisite.

## Specification basis

- [MINIX user-service runtime study](../research/minix-user-service-runtime.md)
- [System overview](../architecture/system-overview.md)
- [Development dependency DAG](../architecture/development-dag.md)
- [Testing strategy](../testing-strategy.md)
- [ADR-0002: Language and Toolchain](0002-language-and-toolchain.md)
- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0004: IPC and Endpoint ABI](0004-ipc-and-endpoint-abi.md)
- [ADR-0005: Direct Memory Grants](0005-direct-memory-grants.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0021: Generation-Safe User Address Spaces](0021-generation-safe-user-address-spaces.md)
- [ADR-0022: User Execution Contexts and U-Mode Entry](0022-user-execution-contexts-and-u-mode-entry.md)
- [ADR-0023: Canonical User Status Summary Bits](0023-canonical-user-status-summary-bits.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0029: Endpoint and Privilege Substrate](0029-endpoint-and-privilege-substrate.md)
- [ADR-0030: MINIX-Baseline Blocking IPC](0030-minix-baseline-blocking-ipc.md)
- [ADR-0033: Reviewed Design Before Implementation](0033-reviewed-design-before-implementation.md)
- [ADR-0035: RISC-V IPC Syscall and Bootstrap Buffers](0035-riscv-ipc-syscall-and-bootstrap-buffers.md)
- [ADR-0036: Reply Authority Begins at Request Return](0036-reply-authority-begins-at-request-return.md)
- [ADR-0038: Kernel-Managed Direct Grant Registry](0038-kernel-managed-direct-grant-registry.md)
- [ADR-0039: Page-Bounded Checked Grant Copy](0039-page-bounded-checked-grant-copy.md)
- [ADR-0040: Post-Handoff Wired Address-Space Resolution](0040-post-handoff-wired-address-space-resolution.md)
- [ADR-0041: Unified RISC-V Grant Syscalls](0041-unified-risc-v-grant-syscalls.md)

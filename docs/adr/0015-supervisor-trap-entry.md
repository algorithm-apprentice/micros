# ADR-0015: Supervisor Trap Entry

- Status: Accepted
- Date: 2026-10-05

## Context

Milestone 2 needs a supervisor trap path before timer delivery, page tables,
user mode, scheduling, or IPC can be implemented. The path must preserve enough
state to return from an expected exception today and to become the saved thread
context later without replacing its assembly/C ABI.

RISC-V does not switch stacks or save general registers when a trap enters
supervisor mode. Hardware writes `sepc`, `scause`, and `stval`, updates the
SPP/SPIE/SIE fields in `sstatus`, and transfers control through `stvec`. The
kernel must therefore establish a trusted stack before calling C and must save
every integer register before using it.

The current kernel has one boot stack and one hart but must not make a user
stack, process, or global current-thread pointer part of the trap ABI. The
first target proof must execute the real entry and `sret` paths in QEMU; a host
mock cannot validate CSR transitions or register restoration.

## Decision

### Installation and stack

The kernel installs one direct-mode supervisor trap vector before emitting the
boot marker or parsing the FDT:

1. clear `sstatus.SIE`;
2. record the explicit OpenSBI boot hart ID for fatal diagnostics;
3. write the top of a dedicated 16 KiB, 16-byte-aligned boot-hart trap stack to
   `sscratch`;
4. write the aligned assembly entry address to `stvec` with mode bits zero.

The one-hart MVP has one statically reserved primary trap stack and one 4 KiB
emergency nested-trap stack. The future hart object will own the same per-hart
primary stack and `sscratch` role; adding harts does not change the trap-frame
ABI.

On entry, assembly atomically swaps `sp` and `sscratch`. `sp` then names the
trusted trap stack and `sscratch` temporarily holds the interrupted stack
pointer. The primary and emergency stacks are statically reserved, supervisor
read-write, and remain wired after paging is enabled.

The first-stage prologue is exact:

1. reject a zero `sp` from the swap as the nested-trap sentinel before any
   stack access;
2. allocate the fixed frame on the primary stack;
3. store the interrupted `t0` in its frame slot;
4. read the interrupted `sp` from `sscratch` into `t0` and store it in the
   frame;
5. write zero to `sscratch`;
6. save the remaining registers and CSRs.

The two initial stores are a required non-faulting invariant of the wired
primary trap stack. No architecture-visible state exists to retain both the
interrupted `sp` and every original GPR while arming a sentinel before the
first store. If that invariant is broken by kernel mapping corruption,
deterministic nested-fault reporting is not guaranteed.

After the sentinel is armed, a second trap swaps zero into `sp`. The entry
detects it before any stack access, loads the emergency stack, and enters a
non-returning `nested-trap` panic path. It does not attempt to resume the outer
handler.

The normal exit uses this exact `t0` permutation:

1. restore the validated `sstatus`, `sepc`, and every GPR except `t0` and `sp`;
2. load the selected return `sp` from the frame into `t0`;
3. write that value to `sscratch`;
4. reload the selected return `t0` from the wired frame;
5. deallocate the frame so `sp` is the primary trap-stack top;
6. atomically swap `sp` and `sscratch`;
7. execute `sret`.

The final `t0` frame load is also a required non-faulting wired-stack access.
After it, only register/CSR operations occur before `sret`. The swap restores
the primary trap-stack top to `sscratch` while installing the selected return
stack in `sp`.

Supervisor interrupts remain disabled throughout dispatch and restore. Timer,
external-interrupt, and user-origin handling are added only by later
dependency-ready tasks.

### Trap-frame ABI

The assembly/C boundary uses this exact 288-byte frame:

```c
struct micros_trap_frame {
    uint64_t ra;
    uint64_t sp;
    uint64_t gp;
    uint64_t tp;
    uint64_t t0;
    uint64_t t1;
    uint64_t t2;
    uint64_t s0;
    uint64_t s1;
    uint64_t a0;
    uint64_t a1;
    uint64_t a2;
    uint64_t a3;
    uint64_t a4;
    uint64_t a5;
    uint64_t a6;
    uint64_t a7;
    uint64_t s2;
    uint64_t s3;
    uint64_t s4;
    uint64_t s5;
    uint64_t s6;
    uint64_t s7;
    uint64_t s8;
    uint64_t s9;
    uint64_t s10;
    uint64_t s11;
    uint64_t t3;
    uint64_t t4;
    uint64_t t5;
    uint64_t t6;
    uint64_t sstatus;
    uint64_t sepc;
    uint64_t scause;
    uint64_t stval;
    uint64_t reserved;
};
```

Fields are consecutive eight-byte values in the shown order. Their offsets are
therefore `0` through `280`, and the total size is `288`, preserving the
RISC-V ABI's 16-byte stack alignment. The reserved field is written as zero
and must remain zero until a later ADR assigns it.

A preprocessed assembly/C layout header defines every offset and the total
size. C verifies every field with `_Static_assert`, `offsetof`, and `sizeof`.
Floating-point and vector state are absent because ADR-0001 excludes those
extensions from the v0.1 context.

The target remains compiled with `-msmall-data-limit=0` and without TLS, so C
dispatch does not consume the interrupted `gp` or `tp` values. Enabling kernel
small-data or TLS access requires entry-time installation of kernel values
before any user-origin trap can call C.

The frame is a mutable return context:

- assembly restores all x1-x31 integer registers from it;
- assembly writes its `sepc` value before `sret`;
- assembly writes a validated `sstatus` value with SIE forcibly clear;
- `sp` is the interrupted or selected return stack, not the trap-frame address;
- `scause` and `stval` are captured inputs and are not written back to CSRs.

The desired interrupt state after `sret` is represented by SPIE, never by
setting SIE during restore. The current production dispatcher does not change
the captured status. The component-test hook may toggle the harmless SUM bit
to prove that the exit consumes the frame's status, while the assembly mask
still clears SIE. A later user-entry ADR must define the complete writable
status mask, including SPP, SPIE, SUM, MXR, and extension-state fields, before
returning to U-mode.

Later user-entry, scheduling, and fault tasks may select a different validated
return context without replacing the entry ABI.

### Dispatch and failure

The C dispatcher receives only a pointer to the frame. It derives:

- interrupt versus exception from the top bit of `scause`;
- the cause code from the remaining bits;
- the interrupted privilege from `sstatus.SPP`.

Production code has no recoverable exception in this slice. Any interrupt or
exception that is not the armed component-test case invokes a trap-aware panic
entry:

```c
MICROS_TRAP_PANIC(boot_hart_id, "unexpected-interrupt", frame)
```

or:

```c
MICROS_TRAP_PANIC(boot_hart_id, "unexpected-exception", frame)
```

This ADR extends ADR-0014 without changing its public direct-panic macro or its
five core records. The shared private panic assembly entry accepts an optional
trap-frame pointer, captures the direct panic-call context, emits the unchanged
five core records, and, when a trap frame is present, appends exactly one
newline-terminated record:

```text
MICROS_TRAP_CONTEXT origin=<S|U> sstatus=0x<16 hex> scause=0x<16 hex> stval=0x<16 hex> sepc=0x<16 hex> ra=0x<16 hex> sp=0x<16 hex>
```

The appended values come from the saved frame, not from the panic call inside
the dispatcher. The direct panic image emits no trap-context record. There is
no silent skip, retry, or success-shaped fallback.

### QEMU component test

A separate `MICROS_BUILD_TRAP_TEST` image installs the production vector after
BSS initialization and runs one assembly self-test after FDT readiness.

The self-test uses two disjoint register patterns and two aligned test stacks:

1. save the C caller state in test-only static storage;
2. load entry pattern A into every x1-x31 register, including `gp`, `tp`, `ra`,
   and a first test `sp`;
3. execute the labeled exact 32-bit illegal encoding `.4byte 0xc0001073`;
4. require the dispatcher to observe an S-mode exception with cause code `2`
   at that exact label and to validate every captured pattern-A register;
5. overwrite every return register in the frame with distinct pattern B,
   select the second test `sp`, toggle `sstatus.SUM`, and set `sepc` to the
   labeled continuation while leaving SIE clear;
6. snapshot every restored x1-x31 register and `sstatus` before calling C or
   restoring the saved caller state;
7. verify pattern B, the second `sp`, and the toggled SUM value, then restore
   the caller state.

The test does not require a particular `stval` value because the architectural
illegal-instruction payload is not needed for recovery. It does require the
exact `sepc` fault label and S-mode origin.

Only after the exception was handled exactly once and every restored register
matches does the kernel emit:

```text
MICROS_TRAP_TEST_PASS origin=S cause=illegal-instruction registers=preserved
```

The isolated recovery gate requires the normal boot and FDT evidence, exactly
one `MICROS_TRAP_READY` marker followed later by exactly one pass record, clean
SBI shutdown, no panic or explicit failure, and no timeout. A host regression
rejects missing, duplicated, or out-of-order trap-test records.

A second isolated image executes the same illegal instruction without arming
recovery. Its expected-panic gate requires:

- reason `unexpected-exception`;
- the unchanged five panic core records;
- exactly one following `MICROS_TRAP_CONTEXT` record;
- origin S, cause code `2`, and the exact fault-label `sepc`;
- no trap-test pass record;
- clean SBI system-failure shutdown without timeout or explicit failure.

The normal and direct intentional-panic images also require exactly one
`MICROS_TRAP_READY` marker but do not arm recovery.

## Consequences

- Faults during early FDT and boot work enter a deterministic fatal path rather
  than an unconfigured firmware path.
- The full integer frame can become a thread execution context without an ABI
  rewrite.
- A dedicated stack keeps future user `sp` values out of supervisor memory
  accesses before they are validated.
- Direct mode keeps one audited save path for exceptions and later interrupts.
- The target test proves real CSR entry, exception decoding, `sepc` recovery,
  mutable return-context consumption, `sret`, and integer-register
  preservation.
- Unexpected traps retain both the direct panic-call context and the complete
  interrupted trap context.
- Twenty KiB of static memory is reserved until bootstrap allocation and hart
  objects provide their later storage model.

## Alternatives considered

### Save only caller-saved registers

This is enough for a C call but not for asynchronous traps or future thread
contexts. It would hide corruption in callee-saved, `gp`, or `tp` state and
force a later ABI replacement.

### Use the interrupted stack directly

This works for current S-mode boot code but fails as soon as a user trap
arrives with an untrusted user stack pointer. A dedicated trap stack establishes
the correct privilege boundary now.

### Use vectored `stvec` mode

Vectored mode can reduce interrupt branch latency, but exceptions still share
the base entry and every vector needs a compatible save discipline. Direct
mode is smaller and sufficient for the educational MVP.

### Implement timer handling in the same pull request

Timer delivery adds SBI TIME calls, `sie.STIE`, tick accounting, rearming, and
interrupt-specific acceptance criteria. Keeping it as the next sequential
slice makes trap return independently reviewable and testable.

## Specification basis

- [RISC-V Privileged Architecture, Supervisor-Level ISA](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html)
- [ADR-0001: Target Platform](0001-target-platform.md)
- [ADR-0010: Testing and Observability](0010-testing-and-observability.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0014: Panic Diagnostics](0014-panic-diagnostics.md)

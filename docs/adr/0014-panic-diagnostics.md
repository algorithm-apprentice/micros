# ADR-0014: Panic Diagnostics

- Status: Accepted
- Date: 2026-10-05

## Context

Fatal kernel failures must remain diagnosable before traps, processes, IPC, or
TTY exist and after the early UART has been handed to TTY. A panic path cannot
depend on allocation, libc, scheduler progress, interrupts, locks, or a
function that might return a success-shaped result.

The current QEMU harness recognizes panic output, but the kernel has no panic
API or independently tested panic image. The report also needs a stable
machine-readable contract without freezing future process, thread, or trap
objects before their dependency-DAG tasks.

## Decision

### Entry and capture

The target exposes a non-returning macro:

```c
MICROS_PANIC(hart_id, reason)
```

`hart_id` is explicit until the hart-local object exists. `reason` is a static
lowercase kebab-case token, not a runtime format string.

A narrowly scoped RISC-V assembly entry:

1. atomically captures the prior `sstatus` and clears its SIE bit;
2. records `scause`, `stval`, and `sepc`;
3. records the value of `ra` on entry;
4. records the pre-entry stack pointer as `sp`;
5. passes the fixed context to the C reporter.

The private assembly entry has this C declaration:

```c
_Noreturn void micros_panic_entry(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line
);
```

It allocates this exact 48-byte context while preserving 16-byte stack
alignment:

```c
struct micros_panic_machine_context {
    uint64_t sstatus;
    uint64_t scause;
    uint64_t stval;
    uint64_t sepc;
    uint64_t ra;
    uint64_t sp;
};
```

A preprocessed assembly/C layout header defines every field offset and the
total size. C uses `_Static_assert` with `sizeof` and `offsetof` to verify that
the structure matches those constants. The entry keeps the original four
arguments in `a0` through `a3`, passes the context pointer in `a4`, and calls:

```c
_Noreturn void micros_panic_report(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_panic_machine_context *context
);
```

Both declarations are `_Noreturn`; the reporter owns the SBI-return fallback
loop. It does not allocate, lock, or use variadic formatting.

The source file and line come from the macro. Target compilation uses a
trailing-separator mapping equivalent to
`-ffile-prefix-map=<repository-root>/=`. The emitted path is root-relative,
has no leading slash or `..` component, and does not expose a host checkout
path.

For a direct panic, `scause`, `stval`, and `sepc` are raw CSR snapshots and may
be stale or unrelated to the panic. `ra` is the address after the call
instruction, not the call instruction's own PC. The source record identifies
the exact call site. A later trap entry must supply separately captured
trap-time values and define their validity with the trap-frame design; this
ADR does not define that layout.

### Report contract

The v0.1 core report is exactly five newline-terminated records, each occurring
once and in this order:

```text
MICROS_PANIC reason=<reason>
MICROS_PANIC_BUILD version=<version>
MICROS_PANIC_SOURCE file=<repository-path> line=0x<16 lowercase hex digits>
MICROS_PANIC_HART mode=S id=0x<16 lowercase hex digits>
MICROS_PANIC_MACHINE sstatus=0x<16 hex> scause=0x<16 hex> stval=0x<16 hex> sepc=0x<16 hex> ra=0x<16 hex> sp=0x<16 hex>
```

The first record remains the panic classifier marker. The other records make
build identity, source location, hart/privilege identity, and machine state
independently verifiable. `<repository-path>` uses only root-relative path
components. All hexadecimal fields include `0x` and exactly 16 lowercase
digits.

Process, thread, endpoint, IPC, or trap-frame context is not represented by
invented sentinel values. A later Accepted ADR may add separately named records
after these five core records without changing their names, order, or meaning.

### Console and termination

Panic disables supervisor interrupts and seizes the UART in polled mode under
the exceptional ownership rule in ADR-0012. Seizure is a small destructive
`uart_panic_seize` operation for the QEMU 16550: it writes line control to a
known 8-N-1 state with DLAB clear, then disables UART interrupt generation.
No hardware state is restored because normal execution never resumes.

After writing all core records, panic flushes the transmitter and requests SBI
shutdown with the system-failure reason. If SBI returns, the kernel emits the
exact line:

```text
MICROS_TEST_FAILURE sbi-system-reset-returned
```

It then flushes again and remains in a `wfi` loop.

### Verification image

The normal debug image does not panic. A separate CMake preset builds the same
source graph with one test-only compile definition that invokes
`MICROS_PANIC` after FDT readiness.

The QEMU harness gains an expected-outcome mode and full-line regular-expression
requirements. The intentional-panic gate passes only when:

- the observed outcome is `panic`;
- the five core records are each present exactly once, contiguous, ordered, and
  match their exact or full-line regular-expression contracts;
- no `MICROS_TEST_FAILURE` record is present;
- FDT event/count and nonempty-reservation invariants pass independently of
  panic outcome classification;
- `timed_out` is false and QEMU's return code is zero after SBI system-failure
  shutdown.

The existing normal smoke gate retains `pass` as its default expected outcome.

## Consequences

- Fatal early failures have deterministic source and RISC-V machine snapshots.
- Panic remains usable after normal console ownership moves to TTY.
- The assembly surface is limited to state capture and interrupt disable.
- Repository-relative file paths make output reproducible across hosts.
- A dedicated image tests real target capture and shutdown without weakening
  the normal boot acceptance path.
- Later trap and process work extends the report through explicit context
  rather than replacing the panic API.

## Alternatives considered

### C-only capture

Inline C can read CSRs, but a function prologue changes `sp` and hides the
entry value of `ra`. A small assembly entry preserves both.

### `printf`-style panic formatting

Variadic formatting would enlarge the trusted failure path and risk hidden
runtime dependencies. Fixed records and the existing hexadecimal UART writer
are sufficient.

### Make the normal smoke image panic

This would stop testing successful FDT readiness and shutdown. A separate build
preset keeps success and fatal-path acceptance independent.

### Accept a timeout after panic output

A timeout proves that diagnostics began, not that the documented shutdown path
worked. The intentional-panic gate therefore requires clean QEMU termination.

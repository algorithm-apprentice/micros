# ADR-0002: Language and Toolchain

- Status: Accepted
- Date: 2026-10-05

## Context

The project needs a freestanding target toolchain, native host tests, explicit
linker control, and reproducible commands on the current macOS development
host and future Linux CI.

## Decision

- Kernel, runtime, servers, and user programs use C17.
- Architecture entry, trap, and context-switch paths may use RISC-V `.S`
  assembly where C cannot express the required behavior.
- Target code is compiled with Homebrew LLVM/Clang for
  `riscv64-unknown-elf`.
- Target ELF images are linked with Homebrew LLD.
- CMake defines host and target build graphs; Ninja executes them.
- CTest may register native and QEMU test commands.
- Host automation may use Python 3 from the standard library when process,
  timeout, or serial-stream control would be fragile in shell.
- Target code is freestanding and does not link the host libc.
- Compiler warnings selected by the project are treated as errors.
- Tool paths are discovered or supplied through a toolchain file; source files
  do not hard-code Homebrew prefixes.

All source, identifiers, comments, diagnostics, test names, documentation, and
commit messages use English.

## Consequences

- The same C implementation can be compiled natively for tests when it has no
  hardware dependency.
- Linker scripts and freestanding runtime code remain explicit and reviewable.
- C undefined behavior requires strict warnings, sanitizers in host tests, and
  narrow unsafe boundaries.
- Homebrew installs `llvm` and `lld` as separate formulae, so setup
  documentation must check both.
- A C++ runtime, Rust runtime, and hosted standard library are not initial
  dependencies.

## Alternatives considered

### GCC cross-toolchain

GCC is viable, but LLVM is already installed and supports both native host
tests and the RISC-V target with a consistent diagnostic toolchain.

### Rust

Rust provides strong memory-safety benefits, but the initial objective includes
learning low-level C ABI, linker, and ownership discipline. Rust may be
evaluated for isolated user services later.

### Makefiles only

Handwritten Makefiles would reduce one dependency but make separate host and
target graphs, generated images, and test registration harder to maintain.

# micros

`micros` is a from-scratch educational microkernel operating system inspired by
the architecture of MINIX 3. It is intended to make kernel mechanisms,
user-space services, and their dependencies small enough to study directly.

Implementation now follows the accepted dependency DAG. The current boot
foundation builds a freestanding RISC-V64 ELF, enters through OpenSBI, emits a
versioned marker through the QEMU `virt` UART, validates the firmware-provided
FDT memory map, provides structured panic diagnostics, and shuts QEMU down
through SBI. A direct-mode supervisor trap vector preserves the complete
integer context and returns safely from an isolated expected exception.

## Goals

- Boot a RISC-V 64-bit kernel under QEMU.
- Keep the privileged kernel limited to mechanisms that require supervisor
  mode.
- Run memory, process, terminal, and filesystem policy in isolated user-space
  services.
- Reach an interactive shell through a dependency-driven sequence of small,
  reviewable changes.
- Keep process, thread, endpoint, and hart responsibilities separate even
  though v0.1 runs one thread per process on one hart.
- Make correctness observable through fast host tests, QEMU integration tests,
  assertions, and deterministic diagnostics.

## Initial non-goals

- Source or binary compatibility with MINIX.
- Importing the NetBSD userland.
- Multithreaded user processes in v0.1.
- Symmetric multiprocessing.
- Networking, USB, audio, graphics, or broad hardware support.
- Persistent storage before the RAM-based system is stable.
- Full POSIX process semantics before `spawn` works end to end.
- Production hardening or formal verification of the complete system.

## Documentation

- [Documentation index](docs/README.md)
- [Build and smoke-test guide](docs/development/building.md)
- [System overview](docs/architecture/system-overview.md)
- [Development dependency DAG](docs/architecture/development-dag.md)
- [MINIX dependency analysis](docs/research/minix-dependency-analysis.md)
- [Roadmap](docs/roadmap.md)
- [Testing strategy](docs/testing-strategy.md)
- [AI-native development workflow](docs/development/ai-native-workflow.md)
- [Architecture decision records](docs/adr/README.md)
- [Contributing guide](CONTRIBUTING.md)

## Quick start

Install:

- CMake 3.25 or newer;
- Ninja;
- Python 3.8 or newer;
- Clang with the `riscv64-unknown-elf` target;
- LLD;
- QEMU 7.0 or newer with `qemu-system-riscv64`, `virt,aia=none`, and default
  OpenSBI firmware providing SBI System Reset.

Then run:

```bash
cmake --workflow --preset test-unit
cmake --workflow --preset test-qemu-smoke
cmake --workflow --preset test-qemu-panic
cmake --workflow --preset test-qemu-trap
cmake --workflow --preset test-qemu-trap-panic
```

These commands run the native suite, verify normal boot through the
QEMU-bundled OpenSBI firmware, and verify an intentional structured panic with
clean failure shutdown. They also prove complete register-preserving trap
return and captured-context diagnostics for an unexpected exception. See the
[build guide](docs/development/building.md) for tool discovery and separate
build/test commands.

## Development workflow

Architecture decisions begin as **Proposed** ADRs. They must be reviewed before
being marked **Accepted** or used as the basis for an implementation commit.
Development proceeds through one pull request at a time, in dependency order.
Humans and AI agents follow the same documentation-first, test-first,
independently reviewed workflow.

MINIX source is used as an architectural reference. `micros` is an independent
implementation: source is not copied from MINIX or NetBSD without an explicit
dependency and license decision.

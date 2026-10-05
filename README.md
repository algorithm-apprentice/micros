# micros

`micros` is a from-scratch educational microkernel operating system inspired by
the architecture of MINIX 3. It is intended to make kernel mechanisms,
user-space services, and their dependencies small enough to study directly.

The project is currently in the design phase. No implementation has been
accepted yet.

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
- [System overview](docs/architecture/system-overview.md)
- [Development dependency DAG](docs/architecture/development-dag.md)
- [MINIX dependency analysis](docs/research/minix-dependency-analysis.md)
- [Roadmap](docs/roadmap.md)
- [Testing strategy](docs/testing-strategy.md)
- [Architecture decision records](docs/adr/README.md)

## Development workflow

Architecture decisions begin as **Proposed** ADRs. They must be reviewed before
being marked **Accepted** or used as the basis for an implementation commit.
Development proceeds through one pull request at a time, in dependency order.

MINIX source is used as an architectural reference. `micros` is an independent
implementation: source is not copied from MINIX or NetBSD without an explicit
dependency and license decision.

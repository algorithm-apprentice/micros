# micros

`micros` is an educational RISC-V64 microkernel operating system. It
independently reproduces MINIX 3's logical structures, algorithms and
behavior, with reviewed RISC-V, isolation and safety adaptations. MINIX is
a reference, not a source donor or binary-compatibility target.

The first milestone is an interactive shell on QEMU `virt` through OpenSBI.
The final goal is a complete POSIX-compatible system, including libc, shell
and mandatory utilities. v0.1 is not yet complete; see the
[current status and remaining gates](docs/roadmap.md#current-status).

## Architecture

A small supervisor-mode kernel owns traps, scheduling mechanisms, page-table
activation, IPC, endpoint privileges, direct grants and bootstrap mechanisms.
Separate VM, PM, TTY, RAMFS and VFS services own user-space policy. Process,
thread, endpoint and hart identities remain distinct even though v0.1 checks
a one-thread-per-process, one-hart policy.

The [system overview](docs/architecture/system-overview.md) explains ownership,
boot and runtime interactions. The
[development DAG](docs/architecture/development-dag.md) orders implementation;
it is deliberately separate from the cyclic runtime architecture.

## Quick start

Install CMake 3.25+, Ninja, GIL-enabled final CPython 3.15.0+ within the 3.15
family, RISC-V-capable Clang with `llvm-nm`/`llvm-readelf`, LLD, and QEMU 7+
with `virt,aia=none` and OpenSBI TIME/System Reset support.
The [build guide](docs/development/building.md) owns tool discovery,
supported environment details and workflow execution.

```bash
export MICROS_PYTHON=/absolute/path/to/python3.15
export OPENSSL_CONF=/dev/null
export SSL_CERT_FILE=/dev/null
export SSL_CERT_DIR=/dev/null
export CLANG_NO_DEFAULT_CONFIG=1
export MICROS_CLANG=/absolute/path/to/clang
export MICROS_LD_LLD=/absolute/path/to/ld.lld

cmake --workflow --preset test-unit-fast
cmake --workflow --preset test-qemu-smoke
```

These are development checks, not the complete PR gate or a production-shell
demo. Use the documented change-aware validation planner for final acceptance.

## Start reading

| Question | Source |
| --- | --- |
| What works, what remains, and what is next? | [Roadmap](docs/roadmap.md) |
| Which component owns a mechanism? | [System overview](docs/architecture/system-overview.md) |
| What must merge before the next task? | [Development DAG](docs/architecture/development-dag.md) and [correction register](docs/research/minix-complete-baseline-findings-and-correction-dag.md) |
| How do I build and run checks? | [Build guide](docs/development/building.md) |
| Which evidence is required? | [Testing strategy](docs/testing-strategy.md) |
| How do I contribute? | [Contributing](CONTRIBUTING.md) and [agent contract](AGENTS.md) |
| Where are decisions and detailed studies? | [Documentation index](docs/README.md) and [ADR index](docs/adr/README.md) |

## Scope

Networking, persistent storage, full process/thread/signal semantics and the
complete POSIX userland are staged outcomes, not permanent omissions.
The [staged-omission manifest](docs/research/posix-staged-omission-manifest.md)
owns their replacement points; [ADR-0081](docs/adr/0081-complete-posix-compatible-system-goal.md)
defines final compatibility.

Permanent non-goals are MINIX source/binary/syscall-number compatibility,
copying MINIX or NetBSD source, and claiming production hardening or formal
verification without separate evidence. SMP and physical hardware are
optional later platform work, not prerequisites for POSIX source compatibility.

Development is documentation-first, test-first and independently reviewed,
one dependency-ready outcome at a time. Accepted decisions and their history
remain in ADRs; exact task validation and review records remain in their PRs.

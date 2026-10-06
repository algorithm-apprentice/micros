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
OpenSBI TIME drives a one-hart supervisor timer that rejects stale pending
delivery, rearms from the current counter, and preserves caller interrupt
state. A bounded bootstrap frame allocator canonicalizes every FDT memory and
reservation range, excludes all memory through the linker-defined kernel end,
and tracks availability with a fixed bitmap. A typed ownership ledger binds
that allocator geometry to exact kernel or process-generation owners and
stages the later one-way VM handoff. An allocator-backed Sv39 root requests
every table frame through that ledger, identity-maps the kernel with RX, R,
and RW/NX linker permissions,
maps managed RAM and UART as supervisor-only RW/NX, and is activated through
an ordered `sfence.vma`/`satp` transition.
The kernel now allocates process, thread, and hart identities from bounded
generation-checked tables. The one-thread and one-hart MVP limits are checked
policies over structurally independent objects. `sscratch` routes traps through
the registered hart's stack anchor, the trap frame carries that hart context,
and timer mechanism state is owned by the same hart rather than standalone
globals. Each live process generation can now own a private Sv39 root whose
user subtree occupies `[0x40000000, 0x80000000)`, while immutable
supervisor-only kernel subtrees remain shared. Typed page-table and user-frame
owners, full ASID-zero activation fences, inactive-root mutation, complete-page
zeroing, and teardown-before-process-release are enforced and tested.

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
- Clang with the `riscv64-unknown-elf` target plus `llvm-nm` and
  `llvm-readelf`;
- LLD;
- QEMU 7.0 or newer with `qemu-system-riscv64`, `virt,aia=none`, and default
  OpenSBI firmware providing SBI TIME and System Reset.

Then run:

```bash
cmake --workflow --preset test-unit
cmake --workflow --preset test-qemu-smoke
cmake --workflow --preset test-qemu-panic
cmake --workflow --preset test-qemu-trap
cmake --workflow --preset test-qemu-timer
cmake --workflow --preset test-qemu-frame-allocator
cmake --workflow --preset test-qemu-trap-panic
cmake --workflow --preset test-qemu-mmu
cmake --workflow --preset test-qemu-object-model
cmake --workflow --preset test-qemu-nested-trap
cmake --workflow --preset test-qemu-frame-ownership
cmake --workflow --preset test-qemu-user-address-space
```

These commands run the native suite, verify normal boot through the
QEMU-bundled OpenSBI firmware, and verify an intentional structured panic with
clean failure shutdown. They also prove complete register-preserving trap
return, three accepted supervisor timer expirations with two rearms and a final
disarm, bootstrap allocation and release against the real FDT at two RAM
sizes, and captured-context diagnostics for an unexpected exception. The MMU
gate additionally recovers from a hardware store page fault against text and
an instruction page fault from writable memory. The object-model gate proves
generation-safe reuse, stale-handle rejection, the checked one-thread policy,
and hart-local current-thread state. The frame-ownership gate proves exact
process-generation authority, blocked process release, failure-atomic staged
handoff, and irreversible sealing. The nested
trap gate injects a second exception at the first instruction after arming the
`sscratch` sentinel and proves routing to a separately configured per-hart
emergency stack. The user-address-space gate proves same-VA isolation across
two roots, exact typed ownership, zeroed reuse, active-root rejection, stale
generation rejection, atomic teardown, and live SUM clearing on trap entry.
See the
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

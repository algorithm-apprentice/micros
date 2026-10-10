# micros

`micros` is a from-scratch educational RISC-V64 reimplementation of the
behavioral architecture of MINIX 3. MINIX provides the subsystem baseline;
`micros` independently reproduces that behavior with documented target
adaptations and safety extensions. The system is intended to make kernel
mechanisms, user-space services, and their dependencies small enough to study
directly.

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
zeroing, and teardown-before-process-release are enforced and tested. Threads
now own exact 264-byte user contexts and slot-derived 16 KiB supervisor stacks.
The kernel can enter U-mode, capture and validate user traps, resume a modified
user context, and restore the hart's idle trap stack.

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
- [MINIX baseline parity audit](docs/research/minix-baseline-parity-audit.md)
- [MINIX scheduler and context-switch study](docs/research/minix-scheduler-and-context-switch.md)
- [MINIX endpoint and blocking IPC study](docs/research/minix-endpoint-and-ipc.md)
- [MINIX RS and SEF bootstrap study](docs/research/minix-rs-sef-bootstrap.md)
- [MINIX VM bootstrap and handoff study](docs/research/minix-vm-bootstrap-and-handoff.md)
- [MINIX PM process-lifecycle study](docs/research/minix-pm-process-lifecycle.md)
- [MINIX VFS/MFS filesystem protocol study](docs/research/minix-vfs-mfs-filesystem-protocol.md)
- [MINIX VFS process, descriptor, and device-routing study](docs/research/minix-vfs-process-descriptor-and-device-routing.md)
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
cmake --workflow --preset test-unit-fast
cmake --workflow --preset test-ipc-model
cmake --workflow --preset test-tty-model
cmake --workflow --preset test-ramfs-model
cmake --workflow --preset test-unit
cmake --workflow --preset test-qemu-smoke
cmake --workflow --preset test-qemu-panic
cmake --workflow --preset test-qemu-trap
cmake --workflow --preset test-qemu-timer
cmake --workflow --preset test-qemu-frame-allocator
cmake --workflow --preset test-qemu-trap-panic
cmake --workflow --preset test-qemu-mmu
cmake --workflow --preset test-qemu-object-model
cmake --workflow --preset test-qemu-endpoint
cmake --workflow --preset test-qemu-grant
cmake --workflow --preset test-qemu-grant-syscall
cmake --workflow --preset test-qemu-user-runtime
cmake --workflow --preset test-qemu-vm-handoff
cmake --workflow --preset test-qemu-pm-service
cmake --workflow --preset test-qemu-tty
cmake --workflow --preset test-qemu-ramfs
cmake --workflow --preset test-qemu-vm-ready-early
cmake --workflow --preset test-qemu-vm-self-fault
cmake --workflow --preset test-qemu-vm-self-fault-sealed
cmake --workflow --preset test-qemu-ipc
cmake --workflow --preset test-qemu-ipc-ecall-core
cmake --workflow --preset test-qemu-ipc-syscall
cmake --workflow --preset test-qemu-ipc-syscall-panic
cmake --workflow --preset test-qemu-nested-trap
cmake --workflow --preset test-qemu-frame-ownership
cmake --workflow --preset test-qemu-user-address-space
cmake --workflow --preset test-qemu-address-space-handoff
cmake --workflow --preset test-qemu-user-execution
cmake --workflow --preset test-qemu-scheduler
cmake --workflow --preset test-qemu-scheduler-invalid-outgoing
cmake --workflow --preset test-qemu-scheduler-invalid-next
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
and hart-local current-thread state. The endpoint gate proves generation-safe
resolution, staged publication, immutable profiles, asymmetric authorization,
generation-safe direct-grant lifecycle, and complete teardown back to the
object baseline. The grant gate uses three exact address spaces to prove
page-local and cross-page checked copy in both directions, failure-atomic
mapping/permission denial, stale authority rejection, and complete cleanup.
The unified grant-syscall gate executes real U-mode operations 7 through 10
through the production dispatcher. It proves read/write creation, revoke,
page-local and cross-page copy in both directions, zero-length validation,
stable shape/authority/range/mapping failures, endpoint-generation reuse,
non-result-register preservation, representative IPC compatibility, and
complete bootstrap cleanup.
The freestanding user-runtime gate loads one independently linked fixed-address
service ELF into three isolated roots. It proves loader-owned zero fill,
external stacks, startup `gp`/`tp` policy, protected read-only data, initialized
and zero-initialized writable state, the raw `ecall` boundary, typed wrappers
for operations 1 through 10, blocking stack-local IPC, register preservation,
success-only grant-token publication, deterministic service-return trapping,
and complete bootstrap cleanup.
The PM service gate starts the real launcher, VM, PM, and a test-only probe.
It proves VM handoff before PM readiness, stable malformed and unmanaged-caller
protocol results without lifecycle-table mutation, one exact bootstrap-sealed
kernel event, and a real PM-only reserve/abort transaction with no leaked
process resources and consumed generation and transaction identities.
The TTY service gate starts the real launcher, VM, PM, and TTY with an exact
test VFS peer. It proves the one-shot UART mapping and two-phase console
handoff, retained source-10 claim through TTY completion, marker-triggered
canonical input, checked-grant reads and writes, interrupt-driven output,
physical UART drain, and clean SBI shutdown.
The RAMFS service gate starts the real launcher, VM, PM, TTY, and RAMFS with
an exact test VFS peer. It proves deterministic seed validation, one mount,
absolute lookup, short EOF, runtime directory/file creation, sparse
grant-backed I/O, complete-record cursor continuation, balanced references,
TTY-routed output, and zero live grants before shutdown.
The early-VM-readiness gate proves that a VM readiness call before operation
12 emits the exact `ready-role-gate` bootstrap failure for the bound VM
endpoint rather than a generic launcher transition failure.
The address-space handoff gate validates every live bootstrap leaf's wired
target before the irreversible transition, then proves exact `VM_WIRED`
validation, activation, IPC-buffer access, checked grants, revoked mutation,
bootstrap-only context preparation, real handed-off grant ecalls, and an
ordinary scheduler return.
The IPC gate uses trusted
kernel-owned messages before the syscall ABI exists and proves immediate and
blocked send/receive, tokenized call/reply, atomic `reply_receive`,
notification coalescing and reply-wait exclusion, deadlock rejection, close
cancellation, generation reuse, kernel-origin source-`NONE` event injection,
and scheduler/object baseline restoration.
The IPC ecall core gate executes real U-mode `ecall` instructions through the
production trap route and proves guard commit, rollback, immediate completion,
stable negative results, upper-endpoint rejection, register preservation, and
complete baseline restoration. The full six-operation syscall acceptance
matrix now runs in three real address spaces and proves blocking transitions,
tokenized calls/replies, atomic `reply_receive`, notification coalescing and
reply-wait exclusion, stable negative results, priority wakeups, deferred
completion across a timer-selected peer, register preservation, and teardown.
An isolated panic gate revokes an accepted receive-buffer mapping before
return and requires `invalid-bootstrap-ipc-buffer`.
The frame-ownership gate
proves exact process-generation authority, blocked process release,
failure-atomic staged handoff, and irreversible sealing. The nested trap gate
injects a second exception at the first instruction after arming the
`sscratch` sentinel and proves routing to a separately configured per-hart
emergency stack. The user-address-space gate proves same-VA isolation across
two roots, exact typed ownership, zeroed reuse, active-root rejection, stale
generation rejection, atomic teardown, and live SUM clearing on trap entry.
The user-execution gate performs a real U-mode round trip, enforces kernel-page
isolation, preserves every integer register, proves thread-stack ownership and
generation reuse, and validates both user and test-only supervisor `sret`
returns.
The scheduler gate runs two isolated address spaces under real supervisor
timer delivery, preserves every integer register, keeps current
queue-reachable, charges thread, kernel, and idle time separately, proves
repeated equal-priority alternation, rejects a spurious idle iteration, and
resumes a held thread after a real timer wake.
The isolated fatal-context gates prove that invalid outgoing or selected user
contexts panic before return-plan ownership changes.
See the
[build guide](docs/development/building.md) for tool discovery and separate
build/test commands.

## Development workflow

Architecture decisions begin as **Proposed** ADRs. They must be reviewed before
being marked **Accepted** or used as the basis for an implementation commit.
Development proceeds through one pull request at a time, in dependency order.
Humans and AI agents follow the same documentation-first, test-first,
independently reviewed workflow.

Each dependency-ready subsystem first traces the fixed MINIX reference
baseline, then independently implements its behavior and authority boundaries.
Required RISC-V adaptations and compatible safety extensions are documented;
optional optimization follows only after the baseline works. Source is not
copied from MINIX or NetBSD without an explicit dependency and license
decision.

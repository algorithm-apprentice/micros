# Roadmap

## Delivery model

Work proceeds through one dependency-ready outcome at a time under the
[AI-native workflow](development/ai-native-workflow.md). Implementation-bearing
outcomes have separately reviewed design and implementation PRs; pure
documentation corrections need one PR. Accepted ADRs control behavior,
the [development DAG](architecture/development-dag.md) controls ordering,
and the milestone exit criteria below control completion.

## Current status

The implemented system reaches the six-service ELF-spawn integration: real
launcher, VM, PM, TTY, RAMFS and VFS compose dynamic test-parent/child spawn,
rollback, I/O, exit/wait and resource release. This is not a production
interactive shell or a complete MINIX/POSIX certification.

| Area | Current boundary | Detailed owner |
| --- | --- | --- |
| Platform, memory, objects and protected communication | Implemented and exercised by native/target gates; remaining structural/algorithmic findings are not waived by behavioral success | [System overview](architecture/system-overview.md) |
| Static services and executable lifecycle | Implemented through the dynamic test PID-1 integration | [ELF-spawn evidence](architecture/elf-spawn-integration.md) |
| MINIX baseline corrections | Evidence foundations and preferred runtime outcomes through OBJ02 are merged; IPC01 is the next preferred runtime design | [Current correction progress](research/minix-complete-baseline-findings-and-correction-dag.md#current-correction-progress) |
| Validation simplification | Current-contract implementation is merged; historical catalogs/journals and duplicate recertification execution are retired | [Current validation and acceptance](architecture/current-contract-validation.md) |
| Production init, shell and commands | Accepted scope, not implemented on main; blocked through `BASE01`, with B13 stack admission later at `STACK13` | [Node-Q contract](architecture/init-shell.md) |
| Hosted GitHub Actions | Designs accepted; implementation remains unaccepted after hosted observed-guest timeouts. No operational workflow on main | [Hosted-CI record](research/evidence-foundations-recertification.md) |
| Complete POSIX compatibility | Future milestones; no complete-compatibility claim | [Staged omissions](research/posix-staged-omission-manifest.md) |

The correction register owns exact finding counts, completed outcome PRs,
prerequisites and preferred remaining order. Do not copy its changing status
or append restart histories here. `BASE01` still gates production node Q;
`RECERT01` still gates later ordinary feature work.

Milestone checklists below are acceptance requirements, not claims that a
milestone is complete because code or a design exists. Exact historical
commands, trees and reviews remain in the original PRs and subsystem records.

## v0.1 completion goal

Current validation policy is defined by the
[testing strategy](testing-strategy.md), not by earlier task-specific
producer or retirement ceremonies.

`micros` v0.1 is complete when a clean checkout builds with the documented
LLVM/LLD/CMake/Ninja toolchain, boots under RISC-V64 QEMU `virt` through
OpenSBI, and reaches a shell that runs `echo`, `cat`, `ls`, and `ps`.

The system must enforce user isolation and preemption and must include
generation-aware IPC, one-shot reply rights, privilege profiles, direct grants,
the static launcher, VM, PM, TTY, RAMFS, VFS, ELF spawn, init, and automated
native/QEMU/end-to-end tests.

Although v0.1 runs one user thread per process on one hart, process, thread,
endpoint, and hart state must remain distinct. No v0.1 implementation may make
multithreading or SMP require replacing the process identity, scheduler object
model, IPC reply model, or global current-execution representation.

## Long-term completion goal

Accepted
[ADR-0081](adr/0081-complete-posix-compatible-system-goal.md) defines the final
product as a complete POSIX-compatible operating system comparable in breadth
to MINIX. v0.1 remains the first integrated checkpoint, not the final feature
boundary.

Every v0.1 omission of process, thread, signal, credential, terminal, storage,
memory, IPC, networking, libc, shell-language, or utility behavior is a staged
future outcome. A mandatory requirement is conditionally inapplicable only
when POSIX itself says so and the exact condition does not hold. MINIX remains
the internal mechanism baseline; POSIX.1-2024 defines the final portable
interface and behavior.

The closed
[staged-omission manifest](research/posix-staged-omission-manifest.md) owns
every current deferral exactly once.

## Milestone 0: design baseline

### Deliverables

- architecture overview;
- MINIX dependency analysis;
- development DAG;
- testing strategy;
- initial ADR set;
- repository documentation index.

### Exit criteria

- every initial ADR is reviewed and either Accepted, revised, or Rejected;
- the shell MVP boundary is unambiguous;
- the implementation order contains no dependency cycle;
- no implementation files are committed before this gate.

## Milestone 1: reproducible boot

### Deliverables

- RISC-V cross-compilation configuration;
- linker script and boot image;
- OpenSBI/QEMU launch command;
- bounded FDT parser for memory and reserved ranges;
- UART output;
- panic path;
- deterministic test shutdown.

### Exit criteria

- a clean checkout builds with one documented command;
- QEMU prints a versioned boot marker;
- an intentional panic prints location and machine state;
- the host gates distinguish success, explicit failure, panic, unexpected exit,
  and timeout.

## Milestone 2: privileged kernel mechanisms

### Deliverables

- trap entry and exception decoding;
- timer interrupts;
- bootstrap frame allocator;
- typed bootstrap frame ownership and atomic handoff classification;
- kernel and user page tables;
- user-mode entry;
- separate process, thread, endpoint, and hart objects;
- thread execution contexts;
- preemptive round-robin scheduler.

### Exit criteria

- allocator and typed-ownership invariants pass randomized host tests;
- expected exceptions return safely;
- U-mode cannot access kernel-only pages;
- two user threads can be preempted repeatedly without register corruption.

## Milestone 3: protected communication

### Deliverables

- process slots and generation-aware endpoints;
- immutable privilege profiles;
- one-shot reply tokens bound to caller threads;
- blocking send, receive, call, reply/receive, and notification;
- deadlock-chain detection;
- direct grants and safe-copy operations;
- unified user grant syscalls;
- wired post-handoff address resolution for initial services;
- one fixed standalone service ELF and freestanding user-service runtime.

### Exit criteria

- stale endpoints and unauthorized IPC are rejected;
- every blocking transition has a tested wakeup path;
- deadlock tests terminate deterministically;
- grant direction, bounds, overflow, endpoint, and lifetime checks pass;
- real U-mode grant create, revoke, and both copy directions preserve stable
  results and non-result registers;
- wired service roots retain activation, IPC-buffer, and checked-copy access
  after the irreversible ownership handoff;
- one independent user ELF has exact RX, R, and RW/NX load closure, no
  relocation or hosted-libc dependency, loader-zeroed BSS, and an external
  aligned stack;
- startup establishes the reviewed `gp`/`tp` policy, enters the no-argument C
  service function, and traps deterministically if that function returns;
- host-stub tests cover typed wrappers for operations 1 through 10, and one
  real QEMU image proves those production operations through the standalone
  runtime while preserving registers, stack state, initialized data, BSS, and
  read-only data;
- no protocol relies on raw pointers crossing an address space.

## Milestone 4: bootstrap and memory service

### Deliverables

- pointer-free, versioned embedded service manifest with seven-entry storage
  and an exact six-entry production chain;
- generated immutable references to already checked service images;
- deterministic explicit-prerequisite topology and cycle rejection;
- kernel preparation of every static root, image, stack, context, reserved
  endpoint, and held thread before launcher entry;
- bootstrap launcher with manifest-bound release authority only;
- atomic exact-profile installation, endpoint publication, and thread release;
- one exact-generation readiness call and token-bound acknowledgment;
- guest-owned readiness deadlines and stable fatal diagnostics;
- exact manifest privilege, UART-page, and IRQ-source assignments;
- irreversible launcher authority revocation after the last static service;
- VM server;
- one-way ownership handoff from bootstrap memory to VM.

### Exit criteria

- manifest validation rejects unknown versions, malformed entries, missing
  profiles or images, invalid resource fields, missing prerequisites, and
  cycles before manifest-directed service mutation;
- capacity-seven consumers agree on the exact 128-byte service configuration
  and 364,704-byte VM boot-information offsets, active counts, zero tails, and
  digest extent;
- launcher order is deterministic and independent from source array order;
- unreleased endpoints remain hidden and held services remain off ready queues;
- release installs only the exact named immutable profile and publishes one
  endpoint generation atomically with exact scheduler admission;
- malformed, foreign, early, duplicate, and stale readiness cannot release a
  dependent;
- a missing readiness response fails inside the guest with the exact service
  identity and no host-timeout dependence;
- failure before publication preserves or rolls back hidden state, while
  failure after publication is fatal with no recovery fallback;
- VM's generic ready transition follows the irreversible ownership handoff;
- TTY release and readiness retain the ADR-0012 begin/commit gates;
- final completion holds the launcher, clears the exact controller binding,
  and makes bootstrap authority impossible to reintroduce while its retained
  source-only endpoint rejects new destinations until later teardown;
- every usable physical frame has exactly one owner;
- user mapping changes after handoff require VM authority;
- VM's complete fault-handling working set remains wired;
- VM failure while launcher bootstrap is running is a fatal bootstrap error;
  after launcher sealing, VM self-fault uses its exact non-bootstrap fatal
  diagnostic and never recurses into VM.

## Milestone 5: core user-space services

### Deliverables

- PM with application identity, hidden process reservation, spawn metadata,
  exit, zombie, wait, and rollback;
- interrupt-driven TTY with early-console and PLIC handoff;
- RAMFS;
- VFS with descriptors, a synthetic console object, pathname routing, and a
  root mount.

### Exit criteria

- PM starts through the static launcher after VM handoff and receives one
  exact bootstrap-sealed runtime event;
- semantic PIDs, parent/child relationships, init reparenting, zombie
  retention, exact-child and any-child wait, and one-shot wait replies pass a
  deterministic native model;
- PM's initial kernel authority can reserve and abort only one empty hidden
  process, releases every live reservation resource without changing
  endpoint/scheduler/ownership state, and leaves exactly one consumed process
  generation and transaction stale;
- no Milestone 5 PM outcome claims a successful spawn or allocates post-handoff
  user memory before the reviewed VM mapping protocol;
- TTY release follows console begin and one exact VM-installed UART device
  leaf, while TTY readiness follows ownership commit;
- one guest-owned manifest deadline covers the complete console begin through
  readiness sequence without a pre-release timeout gap;
- PLIC source 10 remains in service from kernel claim through exact TTY
  acknowledgment after UART draining;
- deterministic QEMU input and output prove canonical CR/LF and erase
  processing, empty-to-nonempty transmit start, interrupt-driven
  receive/transmit, and no routine kernel UART output after begin;
- service protocols reject malformed types and payload lengths;
- client termination releases server-owned state;
- terminal data and file data use exact-direction grants;
- non-transitive data paths use bounded resident bounce buffers;
- init receives working descriptors 0, 1, and 2 without RAMFS device nodes;
- RAMFS validates a reproducible seed before readiness and accepts one exact
  VFS mount;
- RAMFS generation, parent/name, link/reference, block ownership, sparse-file,
  capacity, and failure-atomic invariants pass deterministic native tests;
- a replayable RAMFS model covers at least 8,192 mixed mount, namespace, I/O,
  directory-cursor, release, malformed, stale, capacity, and grant-failure
  transitions;
- one QEMU integration proves the real RAMFS service's seed lookup, writable
  sparse file, complete-record directory cursor, exact grants, balanced
  references, VFS readiness, launcher sealing, and TTY-routed pass marker;
- VFS resolves absolute and relative paths and enforces exact process-local
  descriptor ownership;
- shared open-file descriptions own regular-file offsets and directory
  cursors, while vnode-local and RAMFS reference counts remain balanced;
- application read destinations are completely prevalidated before a
  consuming TTY hop;
- non-copying grant validation preserves checked-copy error precedence, and
  global second-hop grant exhaustion returns `NO_SPACE` without backend
  submission;
- file and terminal data cross application-to-VFS and VFS-to-backend grants
  through resident pages without transitive authority;
- complete RAMFS directory records become complete fixed application records
  with no exposed backend handle;
- pending console reads, completions, cancellation, and writable retry leave
  the VFS receive loop available, including exact
  `CANCEL/REQUEST -> COLLECT/OK` cleanup;
- a replayable VFS model covers at least 8,192 mixed lifecycle, descriptor,
  path, I/O, directory, console, malformed, capacity, and injected-failure
  transitions; and
- one isolated QEMU application probe proves both grant hops, descriptor
  access, absolute/relative routing, translated directories, canonical console
  input, TTY output, a no-authority resident-output drain barrier, zero live
  grants, and clean launcher sealing without claiming production spawn.

## Milestone 6: shell MVP

Milestone 6 is internally gated. The ELF spawn path is implemented and
reviewed first with an isolated dynamic PID-1 fixture and child application.
Production init, shell policy, and commands begin only after that fixture
proves failed-spawn rollback, successful activation, normal exit, wait, and
complete child-resource release.

### Deliverables

- ELF-spawn gate:
  - bounded static RISC-V ELF loading;
  - post-handoff VM allocation, scratch initialization, mapping, freeze,
    rollback, and release for one reserved child;
  - target-proven VM scratch preemption, inactive-root validation,
    reactivation, and stale-translation removal;
  - PM/VFS/VM spawn transaction with explicit descriptor duplication and
    complete preactivation rollback;
  - PM-only thread, endpoint, application-profile, context, and activation
    transitions;
  - separate argv/env application startup and normal-return exit;
  - RISC-V instruction-fetch synchronization before a child runs; and
  - deterministic dynamic-parent/child QEMU resource-baseline proof;
- shell gate:
  - production init;
  - interactive shell;
  - `echo`, `cat`, `ls`, and `ps`; and
  - automated serial end-to-end scenario.

Accepted
[ADR-0079](adr/0079-production-init-shell-and-basic-commands.md) and the
[node-Q architecture](architecture/init-shell.md) define this shell gate.
The bounded outcome uses the existing direct spawn/wait path: PM creates PID 1
after bootstrap sealing, init restarts one console shell, the shell executes
foreground application commands, and PM publishes an application-only process
snapshot for `ps`. Fork, signals, getty/login, full shell grammar, pipes,
redirection, job control, and static-service enumeration remain later work.

### Exit criteria

- the ELF-spawn gate proves one post-freeze failed spawn leaves no resource,
  one valid child receives exact argv/env and descriptors, and child
  exit/wait returns to the post-init baseline;
- a QEMU component preempts VM with the scratch leaf present, activates a
  distinct Sv39 root, reactivates VM through the ordinary scheduler path, and
  proves `SCRATCH_COMMIT` removes both the PTE and usable translation;
- the complete system boots from a clean build;
- the required command scenario succeeds without manual timing assumptions;
- process exit and wait leave no process, frame, descriptor, or inode leak;
- repeated scenario runs produce the same externally visible result.

## Milestone 7: resilient control plane

### Deliverables

- DS endpoint publication;
- user-space scheduler policy;
- RS lifecycle management;
- service crash injection and restart.

### Exit criteria

- service identity changes invalidate stale endpoints;
- dependencies observe a controlled restart notification;
- injected restart tests preserve or explicitly discard state according to the
  service contract;
- bootstrap-only authority is not reintroduced.

## Milestone 8: persistent storage

### Deliverables

- VirtIO block layer;
- persistent filesystem with POSIX metadata and link semantics;
- durable mount and recovery boundaries;
- file-backed VM interface.

### Exit criteria

- file data and metadata survive deterministic reboot scenarios;
- pathname, permission, link, timestamp, locking, and descriptor behavior map
  to the POSIX capability matrix;
- VFS/VM file-backed cycles use an explicit asynchronous protocol.

## Milestone 9: POSIX system semantics

### Deliverables

- `fork`, complete `exec`, process groups, sessions, and job control;
- MINIX-baseline copy-on-write fork mappings and write-fault separation;
- POSIX threads and synchronization;
- signals, clocks, timers, credentials, users, and groups;
- pipes, FIFOs, shared memory, memory mapping, and required IPC;
- complete terminal and pseudo-terminal behavior; and
- sockets and required networking interfaces;
- public POSIX headers, libc entry points, error/`errno` behavior, and ABI
  wrappers delivered with each interface family.

### Exit criteria

- every unconditional mandatory System Interfaces row has deterministic
  evidence, and every conditionally mandatory row passes when its
  standard-defined condition holds;
- deferred v0.1 substitutions are removed without weakening the MINIX
  structural/algorithmic baseline;
- fork copy-on-write sharing, write-fault isolation, and release pass native
  and target evidence;
- representative multithreaded and multiprocess applications run unchanged.

## Milestone 10: POSIX shell and utilities

### Deliverables

- complete POSIX shell language;
- mandatory utilities;
- remaining shell/utility-facing libc and userland support;
- explicitly reviewed userland provenance and maintenance policy.

### Exit criteria

- shell grammar, expansion, redirection, pipelines, jobs, and scripts pass
  conformance evidence;
- mandatory utilities pass their capability-matrix rows;
- representative third-party POSIX source builds without project-private
  source changes.

## Milestone 11: complete POSIX conformance

### Deliverables

- versioned POSIX.1-2024 capability matrix;
- claimed option-group profile;
- complete native, QEMU, integration, shell/utility, and application evidence.

### Exit criteria

- every unconditional mandatory row, every applicable conditionally mandatory
  row, and every claimed optional/XSI group passes;
- no required behavior exists only in prose;
- independent review finds no unsupported complete-compatibility claim.

## Further platform milestones

These require separate ADRs and are not part of the shell MVP:

- SMP;
- physical RISC-V hardware.

## Major risks

| Risk | Consequence | Mitigation |
| --- | --- | --- |
| Hidden bootstrap cycle | A service cannot start without one of its successors | Validate explicit manifest prerequisites, reject cycles, and release in one deterministic topological order |
| ABI churn | Every service changes at once | Accept endpoint, message, and grant ADRs before service code |
| Assembly bugs | Corruption appears far from the cause | Dedicated QEMU context and trap tests with known register patterns |
| Memory ownership ambiguity | Leaks or double allocation | Central ownership ledger and assertions before/after VM handoff |
| Slow feedback | Kernel bugs become expensive to isolate | Keep pure logic host-testable and QEMU smoke tests short |
| Unbounded or accidental baseline scope | One task imports unrelated architecture or later POSIX breadth | Reproduce complete structures/algorithms through the sequential DAG; treat milestone omissions as staged, not as permission for one broad change |
| Future concurrency requires redesign | Thread or SMP work replaces process and IPC foundations | Separate process/thread/hart objects and reply rights in v0.1 |
| Silent service failure | Boot hangs without a diagnosis | Use one guest-owned counter deadline, exact service identity, and a stable fatal bootstrap record |
| Host-only assumptions | macOS build works but CI or target behavior differs | Separate host and target toolchains and test on Linux CI |

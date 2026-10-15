# Documentation

This directory contains the design baseline for `micros`.

## Architecture

- [System overview](architecture/system-overview.md) defines the target system,
  trust boundary, component responsibilities, boot phases, and invariants.
- [Responsibility-based source organization](architecture/source-organization.md)
  defines cohesive module ownership, review signals, and staged decomposition.
- [Repository source-module audit](architecture/source-module-audit.md)
  classifies every in-scope tracked implementation/test source review signal
  at the audited base and records dependency-ordered correction units and
  cohesive exceptions.
- [Kernel address-space source organization](architecture/kernel-address-space-source-organization.md)
  defines exact kernel-root, user-root, VM-transaction, user-execution, and
  native/QEMU evidence ownership for the next audit correction.
- [Kernel PM-control source organization](architecture/kernel-pm-control-source-organization.md)
  defines exact operation-13 ABI, validation, reservation, execution,
  activation, exit, syscall-adapter, and native-evidence ownership.
- [VM service source organization](architecture/vm-service-source-organization.md)
  defines exact boot/image planning, portable state, frame, load/exit,
  service-adapter, host/QEMU evidence, and fixture-generator ownership.
- [PM service source organization](architecture/pm-service-source-organization.md)
  defines exact lifecycle, codec, validation, request/layout, service,
  native/model, kernel-report, and generator ownership.
- [PM spawn and exit coordinator](architecture/pm-spawn-coordinator.md)
  maps the Accepted post-handoff transaction to lifecycle, peer-adapter,
  commit, rollback, and native-model implementation owners.
- [Application runtime implementation map](architecture/application-runtime.md)
  maps the Accepted initial stack, separate startup, PM return-exit,
  checked images, and component evidence without claiming PID-1 integration.
- [ELF spawn integration implementation map](architecture/elf-spawn-integration.md)
  maps the final six-production-service dynamic PID-1/child rollback,
  inherited I/O, exit/wait, physical drain, and complete resource proof.
- [Kernel IPC source organization](architecture/kernel-ipc-source-organization.md)
  defines exact queue, completion, token, event, cancellation, syscall,
  native/reference-model, and target-script ownership.
- [Kernel endpoint source organization](architecture/kernel-endpoint-source-organization.md)
  proposes exact representation, profile, complete-validation, publication,
  close, native/model, and target ownership without changing authority.
- [Kernel bootstrap source organization](architecture/kernel-bootstrap-source-organization.md)
  defines exact manifest, preparation, runtime, syscall, boot-entry,
  launcher, native-reference, and generator ownership before implementation.
- [Trap and syscall source organization](architecture/trap-syscall-source-organization.md)
  defines routed context, interrupts, syscall authority, return, bounded
  hooks, and retained runtime-evidence ownership.
- [Kernel scheduler source organization](architecture/kernel-scheduler-source-organization.md)
  defines queue, IPC transaction, validation, planning, accounting, target
  return, fixture, and independent-model ownership before implementation.
- [User ELF checker source organization](architecture/user-elf-checker-source-organization.md)
  defines exact decoding, structural validation, service policy, instruction,
  CLI, fixture, and discovery ownership before application-checker reuse.
- [VFS test source organization](architecture/vfs-test-source-organization.md)
  defines the exact behavior, fixture, model, target, and tooling boundaries
  required before VFS exit-cleanup coverage is added.
- [Development dependency DAG](architecture/development-dag.md) converts the
  cyclic runtime architecture into an acyclic implementation sequence.
- [Validation evidence and owned build changes](architecture/validation-evidence.md)
  records the Accepted exact-candidate command-reuse and bounded CMake
  classification contract; existing fresh execution remains authoritative.

## Research

- [MINIX dependency analysis](research/minix-dependency-analysis.md) records the
  source baseline, component relationships, strongly connected component, and
  bootstrap lessons used to derive the `micros` design.
- [MINIX baseline parity audit](research/minix-baseline-parity-audit.md)
  classifies every merged kernel foundation as baseline behavior, required
  target adaptation, compatible extension, staged substitution, or correction.
- [MINIX scheduler and context-switch study](research/minix-scheduler-and-context-switch.md)
  traces runnable queues, accounting, context selection, restore, and
  user-scheduler authority before scheduler implementation.
- [MINIX kernel scheduler source-organization baseline](research/minix-kernel-scheduler-source-organization.md)
  traces those fixed-reference responsibilities and classifies the existing
  adaptations before the source-only scheduler correction.
- [MINIX endpoint and blocking IPC study](research/minix-endpoint-and-ipc.md)
  traces endpoint validation, privilege checks, send/receive queues,
  request/reply blocking, notifications, deadlock detection, and exit cleanup.
- [MINIX kernel endpoint source-organization baseline](research/minix-kernel-endpoint-source-organization.md)
  traces fixed representation, privilege installation, inhibited publication,
  and teardown owners before the behavior-preserving endpoint correction.
- [MINIX kernel IPC source-organization baseline](research/minix-kernel-ipc-source-organization.md)
  traces those responsibilities against the fixed local reference before the
  behavior-preserving shared IPC correction.
- [MINIX IRQ notification study](research/minix-irq-notification.md) traces
  interrupt ownership, kernel pseudo-source delivery, event coalescing,
  explicit driver acknowledgment, and endpoint cleanup.
- [MINIX direct grant and safe-copy study](research/minix-direct-grants-and-safecopy.md)
  traces grant identity, table authority, direction and bounds checks, fault
  behavior, revocation, and checked cross-address-space copy.
- [MINIX runtime safe-copy and post-handoff resolution study](research/minix-post-handoff-wired-address-resolution.md)
  traces safe-copy mapping authority after VM startup and derives the
  wired-only `micros` handoff replacement.
- [MINIX user grant and safe-copy syscall study](research/minix-user-grant-syscall-boundary.md)
  traces direct IPC traps, user-managed grant publication, safe-copy kernel
  calls, and the required unified `micros` register ABI.
- [MINIX user-service runtime study](research/minix-user-service-runtime.md)
  traces process entry, libc initialization, IPC vectors, grant and safe-copy
  wrappers, and the service-startup boundary used by the freestanding runtime
  design.
- [MINIX user ELF checker source-organization baseline](research/minix-user-elf-checker-source-organization.md)
  classifies the existing host parsing, executable-shape, startup-policy,
  and deterministic fixture checks before their source-only correction.
- [MINIX RS and SEF bootstrap study](research/minix-rs-sef-bootstrap.md)
  traces static service descriptors, inhibited privilege setup, endpoint
  visibility, initialization callbacks, readiness replies, boot ordering,
  timeouts, and the deferred RS recovery breadth.
- [MINIX bootstrap source-organization baseline](research/minix-bootstrap-source-organization.md)
  applies that fixed trace to the behavior-preserving bootstrap correction
  without importing RS recovery or changing accepted authority.
- [MINIX VM bootstrap and handoff study](research/minix-vm-bootstrap-and-handoff.md)
  traces boot-memory import, VM's frame database, wired self-hosting,
  privileged mapping authority, boot-service adoption, and fatal VM faults.
- [MINIX PM process-lifecycle study](research/minix-pm-process-lifecycle.md)
  traces PM-owned process identity, creation coordination, exit teardown,
  zombie retention, wait completion, reparenting, and rollback boundaries.
- [MINIX VFS/MFS filesystem protocol study](research/minix-vfs-mfs-filesystem-protocol.md)
  traces VFS-only filesystem authority, mount and lookup, node references,
  file and directory mutation, directional grants, sparse I/O, and directory
  cursors.
- [MINIX VFS process, descriptor, and device-routing study](research/minix-vfs-process-descriptor-and-device-routing.md)
  traces caller binding, descriptor/open-file/vnode ownership, root and
  working-directory routing, character-device suspension, and process cleanup.
- [MINIX VFS source-organization baseline](research/minix-vfs-source-organization.md)
  maps MINIX VFS responsibilities to the required `micros` adaptations before
  the portable VFS core is decomposed.
- [MINIX VFS test-organization baseline](research/minix-vfs-test-organization.md)
  traces the shared harness, behavior-oriented test programs, helper
  executables, and required `micros` evidence extensions before the VFS test
  sources are decomposed.
- [MINIX kernel address-space source-organization baseline](research/minix-kernel-address-space-source-organization.md)
  traces privileged root binding, page-table mechanism, region policy,
  lifecycle orchestration, and the required `micros` adaptations.
- [MINIX kernel PM-control source-organization baseline](research/minix-kernel-pm-control-source-organization.md)
  traces common kernel-call dispatch, process reservation, execution
  installation, stop/detach/release ordering, and the required `micros`
  adaptations.
- [MINIX VM service source-organization baseline](research/minix-vm-service-source-organization.md)
  traces service dispatch, physical allocation, regions/page tables,
  executable planning, exit release, faults, and required `micros`
  adaptations.
- [MINIX PM service source-organization baseline](research/minix-pm-service-source-organization.md)
  traces semantic identity, dispatch, lifecycle/wait completion, argument
  capture, and the current source-only correction.

## Planning and verification

- [MINIX validation-driver baseline](research/minix-validation-driver-baseline.md)
  traces fixed test identity/selection/outcomes and classifies the proposed
  development-only evidence extension without importing the hosted runner.

- [Roadmap](roadmap.md) defines milestones, exit criteria, deferred scope, and
  major risks.
- [Testing strategy](testing-strategy.md) defines the host, QEMU, integration,
  stress, and observability layers.

## Development

- [Build and smoke-test guide](development/building.md) defines prerequisites,
  tool discovery, build artifacts, and the deterministic QEMU command.
- [AI-native development workflow](development/ai-native-workflow.md) defines
  the shared human/agent task, TDD, review, and merge contract.
- [Contributing guide](../CONTRIBUTING.md) is the contributor entry point.
- [Agent contract](../AGENTS.md) is the tool-neutral AI agent entry point.

## Decisions

- [ADR index](adr/README.md) lists all architecture decisions and their current
  status.

All project documentation, source code, identifiers, comments, tests, and
commit messages are written in English.

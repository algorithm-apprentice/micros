# Architecture Decision Records

## Status meanings

- **Proposed**: drafted for review; implementation must not depend on it
  irreversibly.
- **Accepted**: reviewed and approved as the current project decision.
- **Rejected**: reviewed but not selected.
- **Superseded**: replaced by a later ADR.

Changing an Accepted decision requires a new ADR that explains the new evidence
and explicitly supersedes the old record. Existing ADR history is not edited
to make the project appear more consistent than it was.

## Initial decision set

| ADR | Title | Status |
| --- | --- | --- |
| [0001](0001-target-platform.md) | Target platform | Accepted |
| [0002](0002-language-and-toolchain.md) | Language and toolchain | Accepted |
| [0003](0003-kernel-responsibility-boundary.md) | Kernel responsibility boundary | Accepted |
| [0004](0004-ipc-and-endpoint-abi.md) | IPC and endpoint ABI | Accepted |
| [0005](0005-direct-memory-grants.md) | Direct memory grants | Accepted |
| [0006](0006-vm-bootstrap-and-handoff.md) | VM bootstrap and handoff | Accepted |
| [0007](0007-static-launcher-before-rs.md) | Static launcher before recovery services | Accepted |
| [0008](0008-ramfs-first-filesystem.md) | RAMFS-first filesystem | Accepted |
| [0009](0009-spawn-before-fork.md) | Spawn before fork | Accepted |
| [0010](0010-testing-and-observability.md) | Testing and observability | Superseded |
| [0011](0011-process-thread-and-hart-model.md) | Process, thread, and hart model | Accepted |
| [0012](0012-console-and-irq-handoff.md) | Console and IRQ handoff | Accepted |
| [0013](0013-ai-native-test-first-development.md) | AI-native test-first development | Accepted |
| [0014](0014-panic-diagnostics.md) | Panic diagnostics | Accepted |
| [0015](0015-supervisor-trap-entry.md) | Supervisor trap entry | Accepted |
| [0016](0016-supervisor-timer-interrupts.md) | Supervisor timer interrupts | Accepted |
| [0017](0017-bootstrap-physical-frame-allocator.md) | Bootstrap physical frame allocator | Accepted |
| [0018](0018-sv39-kernel-address-space.md) | Sv39 kernel address space | Accepted |
| [0019](0019-kernel-object-identity-and-ownership.md) | Kernel object identity and ownership | Accepted |
| [0020](0020-typed-bootstrap-frame-ownership.md) | Typed bootstrap frame ownership | Accepted |
| [0021](0021-generation-safe-user-address-spaces.md) | Generation-safe user address spaces | Accepted |
| [0022](0022-user-execution-contexts-and-u-mode-entry.md) | User execution contexts and U-mode entry | Accepted |
| [0023](0023-canonical-user-status-summary-bits.md) | Canonical user status summary bits | Accepted |
| [0024](0024-preemptive-round-robin-scheduler.md) | Preemptive round-robin scheduler | Superseded |
| [0025](0025-minix-behavioral-baseline-before-optimization.md) | MINIX behavioral baseline before optimization | Superseded |
| [0026](0026-minix-baseline-kernel-scheduler.md) | MINIX-baseline kernel scheduler | Accepted |
| [0027](0027-return-boundary-timer-rearming.md) | Return-boundary timer rearming | Accepted |
| [0028](0028-squash-merge-task-pull-requests.md) | Squash-merge task pull requests | Accepted |
| [0029](0029-endpoint-and-privilege-substrate.md) | Endpoint and privilege substrate | Accepted |
| [0030](0030-minix-baseline-blocking-ipc.md) | MINIX-baseline blocking IPC | Accepted |
| [0031](0031-tiered-native-validation.md) | Tiered native validation | Accepted |
| [0032](0032-fail-closed-change-aware-validation.md) | Fail-closed change-aware validation | Accepted |
| [0033](0033-reviewed-design-before-implementation.md) | Reviewed design before implementation | Accepted |
| [0034](0034-author-validation-before-review.md) | Author validation before review | Accepted |
| [0035](0035-riscv-ipc-syscall-and-bootstrap-buffers.md) | RISC-V IPC syscall and bootstrap buffers | Accepted |
| [0036](0036-reply-authority-begins-at-request-return.md) | Reply authority begins at request return | Accepted |
| [0037](0037-kernel-origin-ipc-notification-injection.md) | Kernel-origin IPC notification injection | Accepted |
| [0038](0038-kernel-managed-direct-grant-registry.md) | Kernel-managed direct grant registry | Accepted |
| [0039](0039-page-bounded-checked-grant-copy.md) | Page-bounded checked grant copy | Accepted |
| [0040](0040-post-handoff-wired-address-space-resolution.md) | Post-handoff wired address-space resolution | Accepted |
| [0041](0041-unified-risc-v-grant-syscalls.md) | Unified RISC-V grant syscalls | Accepted |
| [0042](0042-freestanding-user-service-runtime.md) | Freestanding user-service runtime | Accepted |
| [0043](0043-static-bootstrap-launcher.md) | Static bootstrap launcher and embedded manifest | Accepted |
| [0044](0044-bootstrap-failure-detail-encoding.md) | Bootstrap failure detail encoding | Accepted |
| [0045](0045-static-vm-bootstrap-and-handoff.md) | Static VM bootstrap and one-way handoff | Accepted |
| [0046](0046-pm-process-lifecycle-and-spawn-metadata.md) | PM process lifecycle and spawn metadata | Accepted |
| [0047](0047-tty-console-handoff-and-serial-protocol.md) | TTY console handoff and serial protocol | Accepted |
| [0048](0048-ramfs-service-and-vfs-filesystem-protocol.md) | RAMFS service and VFS filesystem protocol | Accepted |
| [0049](0049-vfs-service-and-application-io-protocol.md) | VFS service and application I/O protocol | Accepted |
| [0050](0050-post-handoff-elf-spawn.md) | Post-handoff ELF spawn and normal exit | Accepted |
| [0051](0051-responsibility-based-source-modules.md) | Responsibility-based source modules | Accepted |
| [0052](0052-vfs-test-source-decomposition.md) | VFS test source decomposition | Accepted |
| [0053](0053-kernel-address-space-source-decomposition.md) | Kernel address-space source decomposition | Accepted |
| [0054](0054-frame-ownership-source-decomposition.md) | Frame-ownership source decomposition | Accepted |
| [0055](0055-kernel-vm-handoff-source-decomposition.md) | Kernel VM-handoff source decomposition | Accepted |
| [0056](0056-kernel-pm-control-source-decomposition.md) | Kernel PM-control source decomposition | Accepted |
| [0057](0057-vm-service-source-decomposition.md) | VM service source decomposition | Accepted |
| [0058](0058-pm-service-source-decomposition.md) | PM service source decomposition | Accepted |
| [0059](0059-kernel-ipc-source-decomposition.md) | Kernel IPC source decomposition | Accepted |
| [0060](0060-kernel-bootstrap-source-decomposition.md) | Kernel bootstrap source decomposition | Accepted |
| [0061](0061-trap-and-syscall-source-decomposition.md) | Trap and syscall source decomposition | Accepted |
| [0062](0062-kernel-scheduler-source-decomposition.md) | Kernel scheduler source decomposition | Accepted |
| [0063](0063-user-elf-checker-source-decomposition.md) | User ELF checker source decomposition | Accepted |
| [0064](0064-fail-closed-validation-evidence-reuse.md) | Fail-closed validation evidence reuse | Accepted |
| [0065](0065-closed-tool-default-configuration-for-validation.md) | Closed tool default configuration for validation | Accepted |
| [0066](0066-ramfs-source-decomposition.md) | RAMFS source decomposition | Accepted |
| [0067](0067-kernel-endpoint-source-decomposition.md) | Kernel endpoint source decomposition | Accepted |
| [0068](0068-kernel-object-source-decomposition.md) | Kernel object source decomposition | Accepted |
| [0069](0069-grant-data-plane-source-decomposition.md) | Grant data-plane source decomposition | Accepted |
| [0070](0070-tty-function-and-evidence-decomposition.md) | TTY function and evidence decomposition | Accepted |
| [0071](0071-tty-model-validation-reference-dependency.md) | TTY model validation reference dependency | Accepted |
| [0072](0072-qemu-runner-source-decomposition.md) | QEMU runner source decomposition | Accepted |
| [0073](0073-kernel-directory-layout.md) | Kernel directory layout | Accepted |
| [0074](0074-fail-fast-validation-producer-readiness.md) | Fail-fast validation producer readiness | Accepted |
| [0075](0075-cpython-3-15-host-automation-floor.md) | CPython 3.15 host automation floor | Accepted |
| [0076](0076-bounded-complete-frame-ownership-validation.md) | Bounded complete frame-ownership validation | Accepted |
| [0077](0077-bounded-complete-vm-boot-database-validation.md) | Bounded complete VM boot-database validation | Accepted |
| [0078](0078-valid-vm-boot-database-boundary-evidence.md) | Valid VM boot-database boundary evidence | Accepted |
| [0079](0079-production-init-shell-and-basic-commands.md) | Production init, shell, and basic commands | Accepted |
| [0080](0080-minix-structural-and-algorithmic-baseline.md) | MINIX structural and algorithmic baseline | Accepted |
| [0081](0081-complete-posix-compatible-system-goal.md) | Complete POSIX-compatible system goal | Accepted |
| [0082](0082-evidence-authority-transition.md) | Evidence authority transition | Accepted |
| [0083](0083-test-only-operation-count-evidence.md) | Test-only operation-count evidence | Accepted |
| [0084](0084-checked-stack-bound-evidence.md) | Checked stack-bound evidence | Accepted |
| [0085](0085-stack-metadata-artifact-boundary.md) | Stack metadata artifact boundary | Accepted |
| [0086](0086-finite-fatal-stack-context.md) | Finite fatal-stack context | Accepted |
| [0087](0087-phase-bounded-vm-load-temporaries.md) | Phase-bounded VM load temporaries | Accepted |
| [0088](0088-github-actions-validation.md) | GitHub Actions validation | Accepted |
| [0089](0089-parallel-github-actions-validation.md) | Parallel GitHub Actions validation | Accepted |
| [0090](0090-minix-word-based-runtime-memory.md) | MINIX word-based runtime copy and fill | Accepted |
| [0091](0091-packed-evidence-identity-coverage.md) | Packed evidence identity coverage | Accepted |
| [0092](0092-relocated-evidence-payload-bindings.md) | Relocated evidence payload bindings | Accepted |
| [0093](0093-minix-diagnostic-histories.md) | MINIX diagnostic histories | Accepted |
| [0094](0094-retrigger-safe-diagnostic-probe.md) | Retrigger-safe diagnostic probe | Accepted |
| [0095](0095-bounded-uart-and-terminal-panic.md) | Bounded UART and terminal panic | Accepted |
| [0096](0096-bounded-kernel-panic-stack-trace.md) | Bounded kernel panic stack trace | Accepted |
| [0097](0097-panic-trace-link-rejection-precedence.md) | Panic-trace link rejection precedence | Accepted |
| [0098](0098-standalone-frame-validator-evidence-binding.md) | Standalone frame-validator evidence binding | Accepted |
| [0099](0099-panic-trace-execution-scopes.md) | Panic-trace author and current execution scopes | Accepted |
| [0100](0100-minix-bootstrap-allocation.md) | MINIX bootstrap allocation and range validation | Accepted |
| [0101](0101-minix-kernel-mapping-topology.md) | MINIX kernel mapping topology and direct table identity | Accepted |
| [0102](0102-granular-memory-native-source-authority.md) | Granular memory-native source authority | Accepted |
| [0103](0103-selected-frame-ownership-validation.md) | Selected frame-ownership validation | Accepted |
| [0104](0104-precise-complete-validator-token-authority.md) | Precise complete-validator token authority | Accepted |
| [0105](0105-bounded-validation-feedback.md) | Bounded validation feedback | Accepted |
| [0106](0106-granular-selected-ownership-native-source-authority.md) | Granular selected-ownership native source authority | Accepted |
| [0107](0107-selected-user-address-space-validation.md) | Selected user address-space validation | Accepted |
| [0108](0108-qemu-accepted-transcript-reporting.md) | QEMU accepted transcript reporting | Accepted |
| [0109](0109-handoff-preflight-error-ownership.md) | Handoff preflight error ownership | Accepted |
| [0110](0110-kernel-visitor-extension-admission.md) | Kernel visitor extension admission | Accepted |
| [0111](0111-fatal-tty-header-extension.md) | Fatal TTY header extension | Accepted |
| [0112](0112-granular-selected-address-space-native-source-authority.md) | Granular selected address-space native source authority | Accepted |
| [0113](0113-granular-selected-ownership-observation-authority.md) | Granular selected-ownership observation authority | Accepted |
| [0114](0114-selected-release-and-wired-handoff-plans.md) | Selected release and wired-handoff plans | Accepted |
| [0115](0115-selected-count-generation-evidence-scope.md) | Selected-count generation evidence scope | Accepted |
| [0116](0116-handed-off-leaf-fixture-coherence.md) | Handed-off leaf fixture coherence | Accepted |
| [0117](0117-granular-selected-release-handoff-native-source-authority.md) | Granular selected release/handoff native source authority | Accepted |
| [0118](0118-precise-object-and-panic-source-authority.md) | Precise object and panic source authority | Accepted |
| [0119](0119-selected-object-and-hart-validation.md) | Selected object and hart validation | Accepted |
| [0120](0120-obj01-complete-source-authority.md) | Complete OBJ01 source-authority closure | Accepted |
| [0121](0121-bounded-common-scheduler-return.md) | Bounded common scheduler return | Accepted |
| [0122](0122-obj02-evidence-consumer-scope.md) | Complete OBJ02 evidence consumer scope | Accepted |
| [0123](0123-current-contract-validation.md) | Current-contract validation | Accepted |
| [0124](0124-selected-ipc-validation.md) | Selected IPC validation | Accepted |
| [0125](0125-linear-ipc-deadlock.md) | Linear IPC deadlock projection | Accepted |
| [0126](0126-direct-reply-token-binding.md) | Direct reply-token binding | Accepted |
| [0127](0127-head-only-ipc-sender-queues.md) | Head-only IPC sender queues | Accepted |
| [0128](0128-selected-grant-validation.md) | Selected grant validation | Accepted |
| [0129](0129-grant-free-lists-and-targeted-cancellation.md) | Grant free lists and targeted cancellation | Accepted |
| [0130](0130-quiescent-grant-resource-equivalence.md) | Quiescent grant resource equivalence | Accepted |

# Documentation

Start with the guide for the question you need to answer. General guides
describe the current system and workflow; subsystem contracts provide the
exact details. Historical research and accepted decision records are not
repeated as status narratives in every guide.

## Reading map and ownership

| Question | Canonical document | Keep elsewhere |
| --- | --- | --- |
| What is the project? | [Repository README](../README.md) | Detailed architecture, complete command catalogs |
| What is delivered and what remains? | [Roadmap](roadmap.md#current-status) | Individual PR restart stories |
| Which audit findings are closed and which correction is next? | [Correction register](research/minix-complete-baseline-findings-and-correction-dag.md#current-correction-progress) | Copied finding counts in several guides |
| How do components and authorities interact? | [System overview](architecture/system-overview.md) | Implementation history and CLI instructions |
| Which dependencies gate implementation? | [Development DAG](architecture/development-dag.md) | A second runtime/state-machine specification |
| How do I build and execute checks? | [Build guide](development/building.md) | Repeated architecture and test transcripts |
| What evidence and QEMU/model workflows are required? | [Testing strategy](testing-strategy.md) | Per-PR run IDs, durations and reviews |
| How are tasks designed, validated and reviewed? | [AI-native workflow](development/ai-native-workflow.md) | Duplicated rules in prompts or new instruction files |
| Why was a decision made? | [ADR index](adr/README.md) | Silent edits to an Accepted decision |
| What proved one outcome? | Its original PR and linked subsystem acceptance record | Growing chronological appendices in general guides |

[AGENTS.md](../AGENTS.md) remains the agent entry point and defines the
required context order. [CONTRIBUTING.md](../CONTRIBUTING.md) is the human
entry point. All committed project content is English.

## Architecture

### Runtime and subsystem contracts

| Area | Contracts |
| --- | --- |
| Platform and failure diagnostics | [Diagnostic histories](architecture/diagnostic-history.md), [terminal failure](architecture/terminal-failure.md), [panic stack trace](architecture/panic-stack-trace.md) |
| Physical memory and kernel mappings | [Bootstrap allocation](architecture/bootstrap-allocation.md), [mapping topology](architecture/kernel-mapping-topology.md), [selected ownership](architecture/selected-frame-ownership.md) |
| User roots and ownership lifecycle | [Selected address spaces](architecture/selected-user-address-space.md), [release and wired handoff](architecture/selected-release-and-wired-handoff.md) |
| Objects and scheduling | [Selected object/hart validation](architecture/selected-object-and-hart-validation.md), [common scheduler return](architecture/common-scheduler-return.md) |
| IPC and grants | [Blocking IPC](adr/0030-minix-baseline-blocking-ipc.md), [selected IPC validation](architecture/selected-ipc-validation.md), [linear deadlock](architecture/linear-ipc-deadlock.md), [grant registry](adr/0038-kernel-managed-direct-grant-registry.md), [checked copies](adr/0039-page-bounded-checked-grant-copy.md), [unified syscall ABI](adr/0041-unified-risc-v-grant-syscalls.md) |
| Static startup and VM | [Service runtime](adr/0042-freestanding-user-service-runtime.md), [launcher](adr/0043-static-bootstrap-launcher.md), [VM handoff](adr/0045-static-vm-bootstrap-and-handoff.md) |
| PM and executable lifecycle | [PM model](adr/0046-pm-process-lifecycle-and-spawn-metadata.md), [post-handoff spawn](adr/0050-post-handoff-elf-spawn.md), [coordinator](architecture/pm-spawn-coordinator.md), [application runtime](architecture/application-runtime.md), [ELF-spawn integration](architecture/elf-spawn-integration.md) |
| TTY, RAMFS and VFS | [TTY protocol](adr/0047-tty-console-handoff-and-serial-protocol.md), [RAMFS protocol](adr/0048-ramfs-service-and-vfs-filesystem-protocol.md), [VFS protocol](adr/0049-vfs-service-and-application-io-protocol.md) |
| Future production user environment | [Init and shell](architecture/init-shell.md), gated by the correction register rather than the presence of its Accepted design |
| Runtime compiler primitives | [Memory primitives](architecture/runtime-memory-primitives.md) |

### Source ownership

[Responsibility-based organization](architecture/source-organization.md)
defines module boundaries and review signals.
[The source-module audit](architecture/source-module-audit.md) records
the decomposition outcomes and before-next-touch obligations.

| Family | Current ownership maps |
| --- | --- |
| Memory and VM boundary | [Address spaces](architecture/kernel-address-space-source-organization.md), [frame ownership](architecture/frame-ownership-source-organization.md), [kernel VM handoff](architecture/kernel-vm-handoff-source-organization.md), [PM control](architecture/kernel-pm-control-source-organization.md) |
| Kernel control | [Bootstrap](architecture/kernel-bootstrap-source-organization.md), [trap/syscall](architecture/trap-syscall-source-organization.md), [scheduler](architecture/kernel-scheduler-source-organization.md), [objects](architecture/kernel-object-source-organization.md) |
| Communication | [Endpoints](architecture/kernel-endpoint-source-organization.md), [IPC](architecture/kernel-ipc-source-organization.md), [grants](architecture/grant-data-plane-source-organization.md) |
| Services and evidence | [VM](architecture/vm-service-source-organization.md), [PM](architecture/pm-service-source-organization.md), [TTY](architecture/tty-source-organization.md), [RAMFS](architecture/ramfs-source-organization.md), [VFS tests](architecture/vfs-test-source-organization.md) |
| Host tools and physical layout | [ELF checker](architecture/user-elf-checker-source-organization.md), [QEMU runner](architecture/qemu-runner-source-organization.md), [kernel directories](architecture/kernel-directory-layout.md) |

Completed decomposition records retain their original migration evidence.
Current private dependencies, state/ABI ownership and independent models
remain authoritative; obsolete whole-body transport follows ADR-0123's
supersession boundary, not the original historical freeze.

## Validation and development

| Intent | Contract |
| --- | --- |
| Current validation owners, retained cases and failure boundaries | [Current-contract validation](architecture/current-contract-validation.md) |
| Primitive counters and observation integrity | [Operation-count evidence](architecture/operation-count-evidence.md) |
| Static/observed stack closure and future B13 admission | [Stack-bound evidence](architecture/stack-bound-evidence.md) |
| Optional exact-context receipts and build classification | [Validation evidence](architecture/validation-evidence.md) |
| Supported interpreter and closed host runtime | [Python runtime](architecture/python-host-runtime.md) |
| Hosted execution design, not an operational-CI claim | [GitHub Actions](development/github-actions-ci.md), [parallel validation](development/parallel-ci-validation.md) |
| Measurements and diagnosed cost | [Current overhead](research/current-validation-overhead.md), [earlier feedback cost](research/validation-feedback-cost.md) |

## Research

### Baseline, audit and remaining scope

- [MINIX dependency analysis](research/minix-dependency-analysis.md):
  fixed reference, cyclic runtime dependencies and bootstrap adaptations.
- [Structural/algorithmic baseline](adr/0080-minix-structural-and-algorithmic-baseline.md)
  and [transition study](research/minix-structural-algorithm-transition.md).
- [Complete audit contract](research/minix-complete-baseline-audit.md),
  [findings and correction DAG](research/minix-complete-baseline-findings-and-correction-dag.md):
  fixed audit rows, links to all B01-B14 studies and current closure.
- [Staged-omission manifest](research/posix-staged-omission-manifest.md):
  exactly-once replacement points for deferred MINIX/POSIX mechanisms.
- [Complete POSIX goal](adr/0081-complete-posix-compatible-system-goal.md):
  final external behavior, distinct from the first shell milestone.
- [Evidence-foundation acceptance](research/evidence-foundations-recertification.md):
  EV04 and the separately unaccepted hosted-CI outcome.

### Mechanism studies

| Family | Fixed-reference research |
| --- | --- |
| Platform | [Diagnostic history](research/minix-diagnostic-history.md), [UART/failure](research/minix-terminal-failure-baseline.md), [trace](research/minix-panic-stack-trace.md) |
| Memory | [Allocation](research/minix-bootstrap-allocation.md), [kernel mapping](research/minix-kernel-mapping-topology.md), [ownership](research/minix-selected-frame-ownership.md), [address spaces](research/minix-selected-user-address-space.md), [release/handoff](research/minix-selected-release-and-wired-handoff.md) |
| Objects/scheduler | [Original scheduler](research/minix-scheduler-and-context-switch.md), [selected object/hart](research/minix-selected-object-and-hart-validation.md), [common return](research/minix-common-scheduler-return.md) |
| IPC/grants | [Endpoints/IPC](research/minix-endpoint-and-ipc.md), [selected IPC validation](research/minix-selected-ipc-validation.md), [linear deadlock](research/minix-linear-ipc-deadlock.md), [IRQ notification](research/minix-irq-notification.md), [grants/safe copy](research/minix-direct-grants-and-safecopy.md), [wired resolution](research/minix-post-handoff-wired-address-resolution.md), [user syscalls](research/minix-user-grant-syscall-boundary.md) |
| Startup/services | [User runtime](research/minix-user-service-runtime.md), [RS/SEF](research/minix-rs-sef-bootstrap.md), [VM](research/minix-vm-bootstrap-and-handoff.md), [PM](research/minix-pm-process-lifecycle.md), [filesystem protocol](research/minix-vfs-mfs-filesystem-protocol.md), [VFS routing](research/minix-vfs-process-descriptor-and-device-routing.md) |
| Evidence and primitives | [Driver](research/minix-validation-driver-baseline.md), [operation counts](research/minix-operation-count-evidence.md), [stack bounds](research/minix-stack-bound-evidence.md), [copy/fill](research/minix-runtime-memory-primitives.md) |

Source-organization studies are linked from their corresponding ownership
maps above. The full [research directory](research/) preserves audit manifests,
measured reproductions and historical correction investigations.

## Historical records and decisions

Do not delete or silently rewrite Accepted ADRs to hide superseded decisions.
The [ADR index](adr/README.md) records status; later ADRs define exact
supersession. An ADR's design-time statement that implementation has not
started is a historical phase record, not today's project status.

ADR-0123 retires the operational catalog/binding/journal protocols in
[EV01](architecture/evidence-authority-transition.md),
[packed identities](architecture/packed-evidence-identities.md),
[relocated payloads](architecture/relocated-evidence-payloads.md),
[standalone validator binding](architecture/standalone-frame-validator-binding.md),
[memory-native bindings](architecture/memory-native-source-authority.md),
[selected-ownership bindings](architecture/selected-ownership-native-source-authority.md),
[address-space bindings](architecture/selected-address-space-native-source-authority.md),
[ownership observations](architecture/selected-ownership-observation-authority.md),
[release/handoff bindings](architecture/selected-release-handoff-native-source-authority.md),
[object/panic bindings](architecture/object-and-panic-source-authority.md),
[old source-view providers](architecture/validation-feedback.md) and
[historical trace execution scopes](architecture/panic-trace-execution-scopes.md).
Those records remain available for provenance, not as prerequisites to replay.
Their independent runtime, observation and machine obligations remain where
the current validation contract explicitly retains them.

When updating documentation, change the owner of the fact and link to it.
Keep current status concise, exact contracts in their subsystem document,
and per-attempt commands/trees/reviews in the PR. Git history retains removed
duplicate prose; do not create another archive copy or status registry just
to preserve repeated narration.

# ADR-0062: Kernel Scheduler Source Decomposition

- Status: Proposed
- Date: 2026-10-14
- Related:
  [ADR-0025](0025-minix-behavioral-baseline-before-optimization.md),
  [ADR-0026](0026-minix-baseline-kernel-scheduler.md),
  [ADR-0027](0027-return-boundary-timer-rearming.md),
  [ADR-0032](0032-fail-closed-change-aware-validation.md),
  [ADR-0033](0033-reviewed-design-before-implementation.md),
  [ADR-0034](0034-author-validation-before-review.md),
  [ADR-0035](0035-riscv-ipc-syscall-and-bootstrap-buffers.md),
  [ADR-0051](0051-responsibility-based-source-modules.md),
  [source-module audit](../architecture/source-module-audit.md),
  [exact source contract](../architecture/kernel-scheduler-source-organization.md),
  [fixed MINIX trace](../research/minix-kernel-scheduler-source-organization.md)

## Context

The trap/syscall correction is reviewed and merged as PR #122. The scheduler
is the next selected high-risk dependency-ready source-audit unit, not a new
runtime DAG node.

At base `e5b4da6e2fd33f752cfdadc7beb77f0ebf9b025b`, its six audited sources
contain 7,901 physical lines, 163 function definitions, and twelve functions
above the 120-line review signal. Portable queue, policy, IPC transaction,
whole-state validation, planning, commit, and accounting responsibilities
share one source. Target adaptation mixes initialization, completion,
interrupts, normal/captured return, idle, and test-only entry. Its native and
target evidence also mixes fixtures, transitions, projections, and scripts.

The fixed MINIX baseline confirms queue-reachable current ownership,
independent run-time flags, head restoration after early preemption, separate
thread/kernel/idle accounting, and one reschedulable user-return boundary.
It does not prescribe copying its large `proc.c`, architecture-specific
restore, VM suspension, SCHED policy, or SMP machinery.

## Decision

Apply the exact ownership, storage, phase, build, migration, and acceptance
contract in
[Kernel Scheduler Source Organization](../architecture/kernel-scheduler-source-organization.md).
Implementation starts only after independent design review, Acceptance, and
the separate design merge.

The paired tasks remain sequential:

```text
kernel-scheduler-decomposition-implementation
    -> kernel-scheduler-decomposition-design
```

Only design is active now. This pull request contains no code, executable
tests/models/harnesses, build scaffolding, or reusable prototypes.

### Expose existing mechanism owners

Use nine portable owners, seven target-runtime owners, seven normal target
evidence owners, four invalid-context evidence owners, five native-case
owners, and six independent-model owners. The complete 38-source inventory,
six private headers, and six ordered source sets are normative in the source
contract.

Supported headers, object layouts, architecture sources, and delegated
mechanisms remain byte-exact. Existing entry points move once to their
responsibility owner; no new public forwarding API or scheduling abstraction
is introduced.

Preserve validation/error precedence, queue and RTS transitions, current IPC
guard commit/rollback, input-order batch commit, complete return-plan
publication, checked-plan recomputation, and the bounded non-failing apply
boundary. User/idle trap accounting remains a minimal route, not a full
registry or mapping walk.

### Preserve return custody and failure boundaries

Normal return validates and stores the outgoing frame; captured IPC return
requires an exactly null current handle and does not store it again.
Selected completion retains its prevalidated bounded copy plan and patches
the same context image that is stored and installed.

Keep complete context/completion preflight, expired-only timer preparation,
fresh accounting, completion commit, plan apply, root activation, and context
installation in their existing order. Preserve the two accounting samples at
first start and their different recoverable/fatal boundaries.

An S-origin interrupt resumes its interrupted kernel frame. The idle assembly
pivot, `sscratch`/stack custody, `wfi`/SIE window, and IDLE-to-KERNEL selector
gate remain unchanged.

### Account for every storage lifetime

Keep the one existing planning workspace in its planner owner. Consolidate
only the existing seven runtime statics and the 48/11/4/4 private evidence
statics into five separately owned contexts. Preserve all fields, qualifiers,
zero initialization, and the normal target's `last_running = 2`.

The two C idle-bypass ABI globals and three assembly saved-state ABI objects
remain distinct. Both target entry invocations retain their IRQ-save local
and assembly entry call; normal supervisor resume returns to the same live
invocation. Automatic transition candidates, validation witnesses, plans,
completion/accounting records, full snapshots, model/reference state, and PRNG
state are not promoted to globals.

### Bounded enforcement, unchanged executions

Implementation starts with one structural Red gate recording the complete
inventory, 138 original function-owner moves, and twelve semantic long-function
migrations. Both active collections must be empty before final review.
Moving, renaming, or compressing a long body does not discharge it.

The gate checks concrete ownership, storage, dependencies, phase and
control-exit obligations. It is not a general C interpreter or a replacement
for native/target behavior tests and complete independent review.

Preserve the eleven native cases, 4,096-step two-hart model, exact LCG draw
schedule, eighteen coverage counters, required prelude, failure trace,
three scheduler QEMU workflows, existing downstream consumers, markers,
instruction symbols, and eight-second timeouts.

Every split path inherits its original owner's four validation dimensions.
In particular, the two native paths retain full-tier classification even
though their separate QEMU, slow-model, and image metadata are empty.
Validation deduplication or execution-policy changes are out of scope.

## Non-goals

No scheduling policy, quantum, priority, timer, object/layout, IPC, buffer,
mapping, privilege, fault delivery, retry, recovery, allocation, reset, cache,
concurrency, migration, SCHED server, application runtime, PID-1 integration,
directory layout, or new test execution is added.

Grant, endpoint, object, RAMFS, TTY, tooling, validation-plan, and directory
corrections remain separate documented outcomes.

## Alternatives considered

- Keep the mixed scheduler until shell integration: rejected because current
  ownership and return boundaries are already difficult to inspect.
- Copy MINIX's file layout: rejected; its broader process, VM, SCHED, and SMP
  responsibilities are not the current `micros` contract.
- Move long bodies unchanged: rejected; that hides rather than exposes their
  preflight, commit, model, and evidence phases.
- Add a second registry, per-hart runtime singleton, or reusable operation
  framework: rejected; existing state and concrete calls are sufficient.
- Optimize repeated validation or timer preparation during extraction:
  rejected; source movement does not authorize behavior or timing changes.

## Consequences

Queue, transaction, validation, accounting, and selected-return authority become
explicit without changing scheduling behavior. Cross-file visibility,
temporary-plan lifetime, context consolidation, and macro/exit-scope mistakes
are the main risks. Exact inventories, narrow green slices, the existing
native/QEMU evidence, one complete current author plan, and independent
read-only review bound those risks.

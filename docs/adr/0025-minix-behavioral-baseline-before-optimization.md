# ADR-0025: MINIX Behavioral Baseline Before Optimization

- Status: Accepted
- Date: 2026-10-06
- Supersedes:
  - ADR-0016's statement that accepted timer expirations are scheduling quanta
  - ADR-0024 in full

## Context

`micros` was initially described as MINIX-inspired but independently designed.
That protected the project from accidental source copying and from importing
the complete MINIX/NetBSD scope, but it also allowed dependency-ready kernel
mechanisms to be designed before the corresponding MINIX path had been traced
end to end.

The preemptive-scheduler design exposed the cost. ADR-0024 selected a
single-FIFO, timer-expiration-driven model before research established that
MINIX instead combines:

- independent run-time blocking flags;
- one ready queue per priority and CPU;
- a current process that remains ready-queue reachable;
- process-time charging when process execution stops, with kernel execution
  accounted separately;
- one common reschedulable user-return path;
- distinct early-preemption and quantum-expiration transitions;
- kernel scheduling mechanism with optional user-space policy.

The project needs one stable rule for learning from MINIX without copying its
source, architecture-specific accidents, known defects, or deferred breadth.

## Decision

### Fixed reference baseline

The initial behavioral reference is MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

Each dependency-ready subsystem begins by tracing the corresponding MINIX
path from entry through state transition, failure handling, authority change,
and return. The result is recorded in an English research or parity document
before a new or superseding ADR is accepted.

### Meaning of baseline

The baseline includes:

- mechanism and policy boundaries;
- authoritative state owners;
- lifecycle and blocking transitions;
- queue and wakeup semantics;
- bootstrap and handoff ordering;
- externally observable success and failure behavior;
- context-save, selection, and restore ordering;
- invariants required by cooperating MINIX components.

The baseline does not require:

- copying source, comments, tests, identifiers, structure layouts, or assembly;
- source, binary, syscall, driver, or NetBSD compatibility;
- retaining x86 or ARM mechanisms on RISC-V;
- implementing a component before its predecessors in the development DAG;
- reproducing a known memory-safety, failure-atomicity, or validation defect.

All implementation remains an independent C17 and RISC-V reimplementation.
MINIX source is evidence, not a code donor.

### Required classification

Every parity analysis classifies each difference as exactly one of:

1. **Baseline parity**: the MINIX behavior and authority boundary are
   reproduced.
2. **Required adaptation**: RISC-V, OpenSBI, Sv39, QEMU `virt`, the selected
   toolchain, or another Accepted non-MINIX platform constraint requires a
   different mechanism while preserving baseline behavior.
3. **Compatible extension**: an Accepted safety, structural, or development
   extension preserves baseline behavior. Its subtype may be recorded, but it
   remains this one classification.
4. **Staged substitution**: the development DAG temporarily places policy in
   the kernel or launcher until the dependency-ready MINIX owner exists; the
   replacement point is explicit and tested.
5. **Divergence requiring correction**: the behavior, authority, or state
   transition differs without a required adaptation or compatible extension.

A classification is not justified merely because the alternative is simpler
or more extensible.

### Baseline-first implementation rule

For each subsystem:

1. trace and document the MINIX baseline;
2. identify required target adaptations and already accepted project
   constraints;
3. specify the smallest independent implementation that reproduces the
   baseline;
4. derive tests from that behavior;
5. implement and integrate the baseline;
6. defer optional optimization or semantic redesign until the baseline is
   working end to end.

An optimization or semantic change after baseline completion requires measured
motivation and an ADR when it changes authority, ABI, lifecycle, or observable
behavior.

### Existing Accepted ADRs and implementation

Accepted ADRs remain authoritative until a later ADR explicitly supersedes
them. This decision does not silently rewrite historical records.

Already merged mechanisms are not removed merely to make structures resemble
MINIX. A compatible extension or required adaptation stays in place when it
preserves the baseline and has passing evidence. A material
behavioral divergence receives a superseding ADR and regression tests before
dependent implementation continues.

The current parity audit finds:

- no merged production mechanism requires rollback;
- the OpenSBI/FDT/Sv39 paths are required adaptations;
- structured panic diagnostics, generation quarantine, typed frame ownership,
  exact root validation, separate process/thread/hart objects, per-thread
  stacks, and deterministic tests are compatible extensions;
- kernel bootstrap memory and mapping policy is a staged substitution for VM;
- the superseded ADR-0016 scheduling-quantum interpretation must not be
  consumed by scheduler code;
- ADR-0024 is superseded because its runnable representation, single FIFO,
  tick-driven quantum, timer-specific switch path, and exact-next-thread
  external-policy option do not reproduce the selected MINIX baseline;
- MINIX's off-by-one priority validation is a known defect, not baseline
  behavior: `priority == queue_count` must be rejected.
- MINIX SCHED's non-atomic takeover, no-quantum demotion, and periodic
  promotion failure paths are known defects: `micros` must preserve agreement
  between local policy state and kernel-installed policy on every failure.

Scheduler design and Red-gate work is blocked until a new Accepted ADR defines
the replacement. Uncommitted ADR-0024-derived scaffolding is not authoritative
and may not be implemented.

The canonical evidence is
[the merged-foundation parity audit](../research/minix-baseline-parity-audit.md)
and
[the scheduler/context-switch study](../research/minix-scheduler-and-context-switch.md).

### Development DAG remains authoritative

Baseline-first does not pull VM, IPC, SCHED, RS, VFS, drivers, or NetBSD
userland ahead of their dependency-ready position. Temporary bootstrap
substitutions remain permitted only when their handoff is explicit.

When a later MINIX component becomes dependency-ready, its `micros`
implementation must converge on the documented MINIX authority boundary before
optional redesign begins.

## Consequences

- MINIX research becomes a required design input rather than optional
  inspiration.
- Behavioral divergence is explicit and reviewable.
- Existing RISC-V and safety work remains usable when it preserves baseline
  semantics.
- Some proposed designs and Red tests will be discarded before production
  implementation; this is cheaper than correcting incompatible mechanisms
  later.
- The development DAG still prevents the full MINIX runtime cycle from
  becoming a build-order cycle.
- Future optimization has a working reference behavior and regression suite
  against which it can be measured.

## Alternatives considered

### Continue treating MINIX as loose inspiration

This leaves too much room to invent incompatible state transitions before the
reference mechanism is understood.

### Copy MINIX source and structures directly

This would conflict with the RISC-V target, independent implementation goal,
project object model, licensing/provenance discipline, and reduced MVP scope.
It would also copy architecture-specific constraints and defects rather than
the intended behavior.

### Remove every existing extension before continuing

Generation-safe handles, typed frame ownership, strict page-table validation,
and deterministic diagnostics do not prevent MINIX behavior. Removing tested
protections would create risk and rework without improving baseline parity.

### Implement the complete MINIX component graph before continuing

The stable MINIX runtime contains dependency cycles. The existing development
DAG and explicit bootstrap substitutions remain necessary to reach the same
authority boundaries sequentially.

## Specification basis

- [MINIX dependency analysis](../research/minix-dependency-analysis.md)
- [MINIX baseline parity audit](../research/minix-baseline-parity-audit.md)
- [MINIX scheduler and context-switch study](../research/minix-scheduler-and-context-switch.md)
- [System overview](../architecture/system-overview.md)
- [Development dependency DAG](../architecture/development-dag.md)
- [AI-native development workflow](../development/ai-native-workflow.md)

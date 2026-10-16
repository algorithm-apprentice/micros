# ADR-0070: TTY Function and Evidence Decomposition

- Status: Accepted
- Date: 2026-10-16
- Refines: ADR-0012, ADR-0025, ADR-0047, ADR-0051, ADR-0064,
  ADR-0065, and ADR-0069

## Context

The independently reviewed grant correction #140 is merged as
`ac94a21141b86c2e9b661cf90f500cca82e0c0c4`. Its actual squash tree equals the
validated/reviewed tree, and its obsolete local and remote branches are
deleted. TTY is the next single dependency-ready maintenance task.

The eight TTY audit signals contain seven C files with 5,803 physical lines,
156 definitions and seven functions above 120 lines, plus the 256-line
fixture generator whose `render_fixture` spans 195 lines. Runtime identity,
candidate transitions, service validation and request publication, independent
model observations, native mock authority, and generated topology are mixed
in several of these owners.

Accepted ADR-0047 already defines the terminal, device, grant, PLIC and
bootstrap behavior. The fixed
[MINIX source study](../research/minix-tty-source-organization.md) traces the
corresponding responsibility boundaries without importing MINIX's terminal
breadth or changing those Accepted adaptations.

## Decision

Only after this proposal is independently reviewed, Accepted and separately
merged, implement the exact
[TTY source contract](../architecture/tty-source-organization.md).

The outcome has 26 C owners, five private headers, five ordered source sets
and eight consumer operands. Exactly 116 original C definitions move. Every
one of the 156 complete original C bodies and signatures remains accounted
for, including originals that do not move. All seven long C functions and
the Python renderer acquire their required ordered domain phases. Other
bounded cohesive bodies remain complete under necessary typed context,
visibility and namespace transport.

### Preserved authority and failure ordering

Do not split the cohesive portable terminal state, UART backend, handoff
state machine, captured control policy or their public contracts. Split the
selected service, runtime and evidence responsibilities around them.

The kernel retains the sole exact handoff binding and bootstrap-state
reference. Identity, live object/profile/scheduler checks, console and route
phases, original deadline and original candidate-before-publication rules
remain unchanged. The UART component retains real MMIO observations,
state-before-delivery publication, retained claim, explicit completion and
terminal panic seizure without new IRQ scope, retry or cleanup.

Service decode, caller, ownership, request identity, slot, grant and invariant
precedence remain exact. An unaccepted busy write does not consume its ID;
only resident-output backpressure arms the original retry hint. Copy
rejection cannot publish a write or consume terminal input. Collection
releases its completion only through the original accepted-reply boundary.

### State and evidence ownership

Keep the existing runtime's two mutable objects, handoff mock's five objects
and control-syscall mock's twenty objects as three separate original
families. Ordinary cross-owner helpers receive an explicit typed context;
pure helpers gain none. Non-resetting private getters do not introduce
runtime lookup, reset or authority. Existing externally named test doubles
retain their original names and complete signatures.

The mixed TTY model remains one automatic root object with its actual
self-referential bus/copy pointers and callback bindings. Its reference
input/echo/output/request state remains independent of production private
helpers. Preserve the seed, xorshift draw count, twenty scripted steps,
8,192-step loop, per-write additional draws, final output drain, trace
capacity, hash, counters and full comparison order.

Both native `setjmp` sites remain lexically in their actual case roots;
the static jump buffer remains in its original fixture family. No returned
helper may own a jump target used by a later callback. Preserve all stub
operation-stage checks, fatal paths, flag observations and reset sites.

### Generator and retained acceptance

The generator keeps its three original entry signatures, CLI/error behavior,
descriptor-validation order and exact output bytes. Shared ELF descriptor,
segment and image renderers remain byte-exact. Its five manifest entries
retain their actual non-topological output order, tuples, quotas and report
symbol; no generic topology builder or second metadata source is added.

The exact TTY recipe delta must invert the complete current CMake recipe
`4e370ba5578b0b114a51d2d5ae738320ce7503a3d51bd7c2e6fb2623284981b3`
before updating the sole admitted current recipe digest. Compose that finite
delta before retained Grant/Object/Endpoint/Scheduler/Trap/RAMFS checks;
never rebaseline their source, recipe or planner expected values.

Five exact family aliases and one host-only structural input preserve all
four original validation dimensions. No execution, closure, profile,
receipt, timeout, environment or cross-HEAD equivalence algorithm changes
are authorized. Canonical configure/build steps may close ordinary generated
inputs before recording; collectors never build or install dependencies.
Complete current fresh author acceptance and additional actual three-profile
reuse must refer to one current candidate and one original full producer.

## Non-goals

No UART, PLIC, mapping, ownership, readiness/deadline, interrupt, service,
request, result, grant, reply, buffering, canonical input, echo, retry,
collection, model, generated topology or test-scenario behavior changes.
No new device, terminal policy, worker, allocation, concurrency, current
process, public API, recovery, rollback, generalized framework, directory
layout, QEMU tooling or production init/shell work is included.

## Evidence and phase barrier

This design contains documentation only. Its paired implementation remains
pending until independent design review, acceptance and separate merge.

Implementation starts with structural Red and immutable inventories from
the reviewed base: all 156 C bodies/signatures, 116 moves, seven long C
dispositions, original Python function/data/CLI facts, exact storage and
initializers, real consumers and independent validation dimensions. Finite
actual typed/scope/AST/data expansion and independent positive/negative
fixtures precede source outlines.

Before implementation review, all 26 owners are real, both migration
collections are literal empty tuples, every current function is bounded,
and the complete current author and actual original-producer reuse gates
pass. Review is complete and read-only, without routine gate reruns.
Exact reviewed-head squash/tree equality, main fast-forward and protected
obsolete local/remote branch cleanup close the outcome.

## Design review disposition

Independent complete read-only design review inspected the eight-document
proposal, relevant Accepted authority, actual TTY/source/consumer contracts,
fixed MINIX evidence and exact current four-command author evidence.
It reported: "No significant issues found in the reviewed changes."
No substantive correction or targeted reproduction was reported.

Acceptance records that clean design result. It does not start or complete
the paired implementation; separate design merge remains its prerequisite.

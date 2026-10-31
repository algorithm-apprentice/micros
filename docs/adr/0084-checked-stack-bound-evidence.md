# ADR-0084: Checked Stack-Bound Evidence

- Status: Accepted
- Date: 2026-10-31
- Refines: ADR-0002, ADR-0033, ADR-0034, ADR-0080, and ADR-0082
- Evidence-only exception to: ADR-0042's exclusion of profiling support,
  solely for the explicitly instrumented test images below

## Context

EV01 and EV02 are separately reviewed and merged. Historical source transport
is no longer confused with baseline authority, and primitive-work observations
are available. B14-F003 still blocks runtime correction: a resident stack page
and a passing scenario do not prove a bound on simultaneous automatic frames.

The ten current stack gaps are B03-E006, B04-E006, B05-E006, B06-E005,
B07-E005, B08-E006, B09-E009, B10-E008, B11-E006, and B12-E007.
B01/B02 platform and memory callees are included in those paths and the
boot/fatal roots. B13-E007 cannot be certified before production node Q exists.

The fixed local MINIX trace is
[the stack-evidence study](../research/minix-stack-bound-evidence.md).
MINIX's architecture stacks and task guard word are not a compiler-derived
proof for the distinct RV64 stacks accepted by `micros`.

## Decision

Implement the bounded
[stack-evidence contract](../architecture/stack-bound-evidence.md).
The outcome has two independent evidence dimensions:

1. compiler-produced fixed frame sizes composed over a closed target call
   graph, including explicit indirect and assembly edges; and
2. deterministic RV64 observations of the lowest stack pointer reached in
   the existing executable scenarios.

A dynamic observation is a lower bound on observed use, not an upper-bound
oracle. It cannot waive an unknown edge, recursion, dynamic allocation, or
static budget excess.

### Exact artifacts, not source estimates

Static evidence uses Clang's `-fstack-usage` and `-fstack-size-section` on
the actual target translation units. It joins `.su`, object metadata,
machine calls/relocations, linked symbols, and the exact build manifest.
Missing, duplicate, stale, inconsistent, or unowned artifacts fail.
Expected budgets and assembly/indirect contracts come from Accepted design,
not measured candidate output.

All ordinary frames are fixed and nonrecursive in the admitted scope.
Unknown or dynamically sized frames and unbounded recursion are rejected.
Function identity includes the executable and translation unit; two local
functions with the same spelling are not one node.

### Stack ownership and epochs

Retain the existing capacities:

- boot, hart-primary/idle, and each thread supervisor stack: 16,384 bytes;
- hart emergency stack: 4,096 bytes;
- static service and application user stack: 4,096 bytes;
- application startup occupancy: at most 2,048 bytes, leaving at least
  2,048 bytes for the runtime call chain.

Include the 288-byte trap frame and 48-byte panic context on the stack that
actually receives them. A user syscall does not add supervisor frames to
the user's stack. An emergency pivot does not add the abandoned primary
stack to the emergency stack. Conversely, direct panic adds to its live
caller chain and is not treated as an independent zero-cost root.

Only the exact audited stack-switch assembly may create a new stack epoch.
Ordinary calls, arbitrary indirect branches, and `_Noreturn` annotations do
not imply stack release.

### Passive production, explicit test instrumentation

Metadata-only compilation changes no allocatable image bytes. The ordinary
build receives neither metadata flags nor observation support.

Explicit observation images use
`-finstrument-functions-after-inlining` and a test-only RV64 leaf observer.
The observer allocates no stack, preserves `sp`, `gp`, `tp`, and callee-saved
registers, and changes no interrupt, scheduling, mapping, or protocol policy.
It records a maximum unsigned `top - sp` in test-owned BSS for the actual
stack owner. It never writes patterns into live stacks or changes loader and
thread-stack zeroing.

Both metadata-only and observation-image frame graphs must pass. Observation
overhead is not subtracted to manufacture a production bound.
Observation support is permitted only in the explicit evidence images; it
remains forbidden in option-free production artifacts. No syscall, runtime
switch, telemetry protocol, or cross-process pointer authority is introduced.

### Current closure and future admission

The current gate checks every current QEMU and standalone-image build
variant and its declared ELF outputs statically, and the ten named existing
QEMU scenarios dynamically. A fixture variant cannot substitute for its
option-free production service image. It reports every current gap
individually; aggregate-only success is insufficient.

It emits a separate blocked B13 admission record until corrected production
init, shell, and commands exist. The future node-Q gate must cover their
actual sort, snapshot, client, startup, and supervisor chains before
`STACK13` and `RECERT01`. That blocked future record is not a current
failure, a B13 pass, or permission to skip future admission.

Budget excess in existing code is a real blocker. EV03 does not enlarge a
stack, suppress a path, promote automatic objects to globals, or silently
change runtime algorithms. Any necessary correction requires its own
reviewed design; EV04 remains closed until the current proof is complete.

## Acceptance

The separate implementation must prove:

- exact frame/artifact ownership and complete machine-call closure;
- conservative simultaneous-frame composition and stack-epoch accounting;
- rejection of dynamic frames, recursion, unknown calls, incomplete indirect
  bindings, stale artifacts, and budget excess;
- source-anchored, complete finite indirect-target sets and assembly contracts;
- test-only observer ABI, atomic high-water publication, and no production
  observer state;
- nonzero authenticated observations for every required live producer,
  including child records captured before generation-bound teardown;
- static and observed bounds for all ten current path families;
- explicit blocked B13 admission with a mandatory future completion point;
- unchanged production allocatable bytes for metadata-only variants;
- retained native/model/QEMU/recipe/source identities and mandatory coverage;
- complete current author validation and clean independent review.

## Phase barrier

This Accepted ADR and its design PR contain documentation only. No executable
prototype, implementation, test, model, harness, dependency, or build scaffold
belongs to this phase.

`ev03-stack-evidence-implementation` remains pending until the separately
reviewed design is Accepted and merged. This design does not claim the future
commands, compiler artifacts, observer, target reports, or stack certificates
already exist.

## Design review disposition

Independent GPT-6.1 Sol review found one substantive issue: mandatory static
coverage omitted the separate standalone production-image variants.
The correction includes every current QEMU and standalone-image
configuration and all declared ELF outputs; production VFS cannot borrow
its fixture variant's certificate. The ten dynamic scenarios are unchanged.

Corrected complete design review reported no substantive issue. Acceptance
records this design result, not an implementation or stack certificate.

## Alternatives considered

### Treat the ELF stack-page count as sufficient

Rejected. Allocation size does not measure nested automatic frames.

### Accept a high-water sample without a static bound

Rejected. An unselected branch or input can use more stack.

### Infer callees from source names or function-pointer types

Rejected. The actual machine call and complete bound target set are required.

### Instrument production or add a telemetry syscall

Rejected. Stack evidence is test-owned and confers no runtime authority.

### Increase stack budgets when evidence fails

Rejected. That conceals the original resource defect and changes Accepted
ownership and image contracts.

### Use a general whole-program verifier

Rejected. The current fixed RV64, non-LTO, nonrecursive build has a smaller
auditable artifact and edge closure.

## References

- [Stack-bound contract](../architecture/stack-bound-evidence.md)
- [Fixed MINIX study](../research/minix-stack-bound-evidence.md)
- [B14 evidence audit](../research/minix-complete-baseline-b14-evidence.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)
- [Testing strategy](../testing-strategy.md)

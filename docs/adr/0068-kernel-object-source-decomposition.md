# ADR-0068: Kernel Object Source Decomposition

- Status: Accepted
- Date: 2026-10-15
- Refines: ADR-0011, ADR-0019, ADR-0021, ADR-0022, ADR-0023, ADR-0025,
  ADR-0026, ADR-0029, ADR-0030, ADR-0033, ADR-0034, ADR-0051, ADR-0062,
  ADR-0064, ADR-0065, and ADR-0067

## Context

The source-module audit assigns three kernel-object production and evidence
files to one maintenance outcome. At reviewed base
`4eb1c4d68de3830b888a0d239a2874bd79792244`, they contain 4,426 physical
lines, 76 definitions, and eight functions above 120 lines.

The portable source combines representation, generation and resolution,
process and thread lifetime, execution-context ownership, hart/stack
installation, and complete base validation. Its public combined validator
delegates to the scheduler; the scheduler then calls the separate object
base validator. That direction must not become recursive during extraction.

The native source combines fifteen deterministic cases, one authoritative
test registry, pure fixture helpers, independent expected-state mathematics,
eight model operations, and complete comparison. The target combines one
IRQ lifetime, its original static stack/context, live runtime observations,
scheduler transitions, generation reuse, and cleanup.

The [fixed MINIX study](../research/minix-kernel-object-source-organization.md)
traces the corresponding identity, lifecycle, CPU-local, and execution-state
owners. Accepted `micros` object and scheduler ADRs remain authoritative.
This task does not make `struct proc` the architecture of `micros`.

## Decision

After independent design review and a separate design merge, apply the exact
[kernel-object source contract](../architecture/kernel-object-source-organization.md).

The implementation exposes nine portable, twelve native/model, and two
target sources: exactly 23 C sources, three new private headers, and three
ordered source sets. Seventy-two original definitions move; the initializer,
combined validator, native `main`, and target entry remain at their roots.
All eight original long functions and four specified shorter mixed tasks
receive their required semantic outlines.

Exactly 22 original file-local portable helpers need cross-owner visibility.
Their types remain unchanged; they receive the `kernel_object_` prefix and
one private declaration. The two unshared byte primitives remain static.
New production phases remain file-local. Public object/context/runtime/
scheduler interfaces and the existing scheduler private base-validator
declaration remain byte-exact.

### Validation direction

Identity and stack geometry are leaves. Representation may query the
existing null-handle predicates. Process/thread/context/hart operations use
those primitives without calling whole-state validation.

The existing direction remains:

```text
public object validation
  -> scheduler validation
    -> object base validation
```

Base validation calls no scheduler, endpoint, IPC, grant, or operation
handler. It retains its two original caller-owned automatic witnesses and
three observed counts. Scheduler closure remains in the scheduler rather
than being duplicated in the base validator.

### Storage and exact execution

Portable state remains caller-owned. The cohesive target runtime and its
three existing static objects remain untouched.

Native cases and the seeded model continue to share their one original
static registry. A getter returns that registry without resetting it;
ordinary helpers receive it explicitly. A new one-field fixture wrapper or
separate model registry is not needed. Whole-registry reset and snapshot
operations remain allowed only at their original sites.

The fifteen-case table in `main` is an automatic `const` object, not a local
static table. It stays automatic in the same owner. The model retains its
local 4,096-step enum, seed `0x7a31d4c9`, one xorshift draw per iteration,
eight cases, failure formats, and comparison after every completed attempt.
It gains no trace ring, scripts, coverage counters, or teardown.

Target entry retains its original static aligned stack and static mutable
context, automatic work, saved IRQ status, `passed = false`, and one common
`done` restoration. Helpers receive actual stack bounds computed from the
root-owned array, not a decayed parameter's `sizeof`. Cleanup remains the
original success path; no failed-test rollback is added.

### Retained source oracles and shared inspection

This ADR refines only the physical-file freeze for `kernel/kernel_objects.c`
in ADR-0062/0067 source contracts. Their original expected SHA-256 remains:

```text
453c3cfbfb02d1c014226a315106a157676a7b195be06f561bb3abbda042db4a
```

A finite inverse built from the actual current owners, signatures, private
constant, and outlined bodies must restore the complete original bytes
before those existing hash checks. Replacing the old hash, substituting an
old Git blob, or weakening the guard to a token-only check is not allowed.
Other stable artifacts remain unchanged.

The five existing scheduler inspection functions and their `Function`
dataclass move unchanged into one shared host inspection owner. Whole
reviewed-base ASTs are frozen before movement. This avoids a scheduler/
object-oracle import cycle without duplicating a parser or introducing a
general C interpreter.

### Build and evidence coupling

All aliases independently retain their original full-tier, QEMU, separate
model, and image dimensions. The new structural gate and shared host
inspector are explicitly host-unit-only.

The implementation may maintain the one admitted CMake digest only after
inverting exactly three source-set additions and ten consumer operands to
the complete reviewed recipe. Prior scheduler, endpoint, RAMFS, trap, and
planner oracles remain enforced under only that additional finite delta.
No selection, closure, execution, tool-admission, reusable-workflow, or
old-HEAD receipt-equivalence algorithm changes.

## Non-goals

No identity, capacity, public layout, generation, quarantine, policy, stack,
context, scheduler, IPC, endpoint, grant, address-space, or runtime behavior
change. No new validation, cleanup, repair, recovery, caching, heap,
concurrency, multithreading/SMP ABI, directory layout, clock, deadline,
QEMU flag, test budget, or validation optimization is added.

Grant, TTY, QEMU-tooling, and directory-layout corrections remain separate,
as do production init/shell/commands and later runtime-DAG work.

## Evidence and phase barrier

This design contains documentation only. The paired implementation remains
pending until independent review is clean, this ADR is Accepted, and the
separate design PR merges.

Implementation begins with a structural Red and immutable inventories from
the reviewed Git base: 76 complete bodies, 72 moves, eight original long
functions, four shorter task dispositions, exact storage/data, eighteen
stable neighbors, all real consumers, and complete recipe/classification
facts. Finite typed and scope-aware reconstruction plus independent
positive/negative fixtures verifies the permitted transformations.

Small green slices use narrow existing native or real target evidence.
Before implementation review, both owner and semantic-task migrations are
literal empty tuples, the complete current staged candidate passes the
authoritative PR plan, and bounded actual native/smoke/TTY-image reuse
acceptance succeeds. Shared CMake ownership currently selects all 47 author
commands. Review inspects exact author evidence rather than routinely
rerunning it. Exact reviewed-head squash/tree equality and obsolete branch
cleanup follow the agent contract.

## Design review disposition

Independent read-only review inspected the complete ten-document proposal,
relevant Accepted authority, current 76-definition source family, fixed
MINIX evidence, storage and exit contracts, retained-source inverse and
shared-inspection feasibility, and exact current author evidence.
It found no substantive issue remaining; no targeted reproduction was
reported.

Acceptance records that design result, not implementation completion.
The paired implementation remains pending until this separate design PR
merges.

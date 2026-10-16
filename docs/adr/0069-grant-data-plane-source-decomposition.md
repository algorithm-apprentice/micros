# ADR-0069: Grant Data-Plane Source Decomposition

- Status: Proposed
- Date: 2026-10-16
- Refines: ADR-0005, ADR-0025, ADR-0038, ADR-0039, ADR-0040, ADR-0041,
  ADR-0047, ADR-0049, ADR-0050, ADR-0051, ADR-0062, ADR-0064, ADR-0065,
  ADR-0067, and ADR-0068

## Context

After the independently reviewed kernel-object correction #138, the seven
grant data-plane signal files contain 8,018 physical lines, 95 definitions
and fourteen functions above 120 lines at
`6269d9247de6beafed2448aa0ffd6904de5e8833`.

The remaining concerns are range translation and retained-plan copy,
captured syscall adaptation, native lifecycle/reference evidence, one
mechanism scenario, and the real U-mode grant-syscall script. The grant
registry itself is an explicit cohesive exception in the source audit.
Splitting it again would add scope rather than clarify the data plane.

The fixed [MINIX source study](../research/minix-grant-data-plane-source-organization.md)
traces grant publication, verification, direction, transfer and failure
owners. Existing Accepted `micros` grant, mapping, device and syscall ADRs
remain authoritative; this is not another grant implementation.

## Decision

Only after this proposal is independently reviewed, Accepted and separately
merged, implement the exact
[grant data-plane source contract](../architecture/grant-data-plane-source-organization.md).

The outcome has exactly 22 C owners, three new private headers, three ordered
source sets and four consumer operands. Sixty-five original definitions
move. Every one of the original 95 complete bodies and signatures remains
accounted for, including original roots that do not move. All fourteen long
and five specified shorter mixed tasks acquire their ordered domain outlines;
no long function receives an algorithmic exception.

### Production boundaries

Keep `kernel/grant.c`, its private create/cancel contracts, portable authority
and chunk primitives, ABI codec, runtime and public interfaces byte-exact.
Only the existing target range planner needs cross-owner production
visibility. New production phases are file-local.

The copy root retains its early argument/local-range rejection, saved IRQ
status and one common restoration. Authority and phase checks precede
zero-length success. A device leaf cannot become a physical copy chunk.
Both remote and local plans are obtained before invariant, phase and fault
classification. Direction selects the retained source/destination plans;
complete physical validation precedes the original non-failing commit.

The captured syscall adapter retains runtime invariants before decode,
shape failure before operation, and create preparation before device denial
and publication. It adds no IRQ scope, current-process lookup, scheduler
guard, result fallback or user authority.

### Evidence ownership

Preserve the four existing mutable evidence families separately: native
registry/model, native copy, target mechanism, and target syscall. A new
fixture may group only each family's actual original fields. Ordinary
helpers receive an explicit typed context; pure helpers gain none.

The target syscall family keeps its three named assembly save objects in
the root. One fixture owns its other 38 original mutable objects, with only
the original dispatch-actor initializer. Actual page-table/user-byte
snapshots, normalization masks, script flags and runtime observations remain
unchanged. Raw arming/control are fixture leaves, so command dispatch,
copy-command helpers and transitions cannot form an import cycle.

The 46 non-`NONE` command identities retain the 38 original raw-arming
templates and exact transition order. Data-driven dispatch is admitted only
under a finite inverse of actual rows, actual typed helpers and all original
scalar argument expressions. The 24 copy/control-error arming bodies remain
separate from the fourteen non-copy arming bodies. No generic script engine,
synthetic replay or replacement target evidence is added.

Native evidence retains the static eight-case table, static nine-row ABI
mapping table, original sentinels and snapshots, separate model boundary
case, seed `0x38a110c5`, LCG, 4,096 steps, forced thirteen-operation prefix,
thirteen coverage counters and 32-entry trace. Expected authorization and
reference updates stay independent of production private helpers.

### Retained oracles and real eligibility

The three source-set additions and four consumer replacements must invert
to the complete current CMake recipe
`44d8a8615e5465ad199ed916b30e72af05643ef00ae5ac9a7df00a0dc4cad2c1`
before maintaining the one admitted current recipe digest. Earlier
source/recipe/planner expected values remain unchanged under only that
additional finite delta. This proposal refines their physical recipe freeze,
not their behavioral or fail-closed contracts.

Exactly three family aliases and the new structural gate's host-only
declaration preserve all four original classification dimensions. No
selection, execution, closure, profile admission, receipt, tool or
cross-HEAD equivalence algorithm changes are allowed.

## Non-goals

No grant token, registry, generation, cancellation, endpoint, copy size,
direction, bounds, device/scratch exclusion, phase, permission, byte,
mapping, result, scheduler, serialization, runtime or teardown change.
No VM callback, partial copy, retry, recovery, new command, script step,
syscall, policy, supported API or production authority expansion, allocation,
concurrency, directory move, deadline or validation optimization is added.

TTY, runner tooling, directory layout and production init/shell/commands
remain later tasks.

## Evidence and phase barrier

This design contains documentation only. Implementation remains pending
until the separate design review and merge are complete.

Implementation starts with structural Red and immutable inventories from
the reviewed source base: all 95 bodies/signatures, 65 moves, fourteen long
and five shorter task dispositions, exact static/automatic storage and data,
real consumers and all validation dimensions. Finite typed, scope-aware
expansion and independent positive/negative fixtures verify the permitted
changes rather than rebaselining rewritten output.

Small green slices use the narrow existing native, model or real target
gate. Before implementation review, both active migrations are literal
empty tuples, all owners are real, the complete current authoritative PR
plan passes, and bounded actual native/smoke/TTY-image reuse matches the
original full producer. Review inspects exact evidence without routine
suite or QEMU reruns. Exact reviewed-head squash/tree equality and obsolete
local/remote branch cleanup finish the outcome.

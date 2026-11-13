# ADR-0134: Shared Bootstrap Memory Transfers

- Status: Accepted
- Date: 2026-11-13
- Refines: ADR-0043, ADR-0045, ADR-0060, ADR-0080, ADR-0083, ADR-0084,
  ADR-0090, ADR-0123, ADR-0132 and ADR-0133
- Supersedes after Acceptance and separate design merge: the seven
  B06-owned byte-copy/zero-fill helper implementations identified below

## Context

BOOT02 #268 is independently reviewed and merged. BOOT03 owns B06-F004
and is next in the preferred sequence. Its actual DAG prerequisite is
COPY01 #191, not an additional BOOT02 or GRANT03 edge.

The [fixed study](../research/minix-bootstrap-memory-transfers.md) finds
three copy and four clear helpers in four bootstrap owners, reached by
46 direct calls. They still move one byte per loop iteration. COPY01
already supplies the Accepted MINIX-compatible word family; another copy
algorithm is neither necessary nor authorized.

These callers also own important behavior: complete-page zeroing before
file-byte installation, independent readback, translated page boundaries,
local candidates, protected publication and reverse cleanup. Faster
movement must not change those responsibilities.

## Decision

After independent review, Acceptance and separate merge, implement the
[bootstrap transfer contract](../architecture/bootstrap-memory-transfers.md).
This phase contains documentation only.

### Delegate at the existing seven helper boundaries

Keep the existing helper names, signatures, visibility and caller order.
Replace their loops with one call to the existing prefixed provider:

- manifest, portable-control and target-runtime copy helpers use
  `micros_runtime_memcpy`;
- their three clear helpers and the image-catalog clear helper use
  `micros_runtime_memset` with zero.

The shared provider remains in `lib/runtime/memory.c`. Its algorithm,
symbols, eight-byte copy, four-byte fill, short-fill threshold, differing-
residue byte fallback, exact access footprint and compiler adaptations
remain unchanged. Do not copy its implementation into bootstrap.

The kernel already links the provider. Keep exactly one provider in each
affected linked target and add it to directly dependent native products
where necessary. Do not import `memory_symbols.c`, host libc, new
forwarders, target assembly, ISA extensions, optimization flags or a
generic transfer interface.

### Preserve caller authority and effects

All inventoried nonzero copies operate on disjoint RAM regions. Retain
that caller precondition; do not introduce `memmove`, overlap detection,
recovery or cross-process pointer authority.
Zero length keeps the existing no-access behavior, including null
arguments to these private movement helpers.

Keep every original requested size, full-capacity zero, untouched tail,
page/segment traversal, permission check and publication position.
Do not force new alignment onto embedded image bytes or public objects.
The existing provider handles each admitted alignment.

Image and stack verification, byte-equality/zero predicates, names,
digests, scalar/structure assignments and topology are not movement loops
to rewrite in this outcome. Mapping, ownership, snapshot allocation and
cleanup stay with their existing owners.

Streaming user-range helpers still translate each chunk before access.
A later translation failure may leave an already transferred prefix,
as it does now; existing preparation rollback owns cleanup. Do not
silently replace this with a new whole-range transaction.

### Measure the actual delegated primitives

Use the existing BOOT ordinary/observed native pair and shared collector.
Preserve its 48 cases, the original 4,096-step model and complete
diagnostic/selected-corruption evidence.

Actual unchanged-production Red must expose the old byte units while
independent bytes/state agree. Green derives exact units from the fixed
COPY01 rules and fixture input geometry, not candidate output.
Measure primitives at their executing owner: B06 byte operations in Red,
RUNTIME provider operations in Green. Do not double-count a wrapper or
turn old BOOT-only byte bounds into vacuous zeros after delegation.

Add focused coverage for the image-catalog and runtime helpers not
currently executed by that pair, actual object-size classes, admitted
unaligned inputs, canaries and zero-length access. Reuse the provider's
existing exhaustive residue tests instead of multiplying every bootstrap
case by the same Cartesian matrix.

Bind all seven actual helper/provider edges and both wide provider
transfers in the existing bootstrap-launcher kernel's compiled and linked
artifacts. Reuse the current proof engines and accepted guest, not another
guest or compiler model. BOOT02's command collector remains independently
scoped; do not enable provider counters globally in that target.

### Keep all existing budgets and phase barriers

No persistent storage or capacity-shaped temporary is added.
BOOT01/BOOT02 storage and plan caps, every image/stack/deadline budget,
all native and target/composed scenarios, all 44 static configurations,
113 ELFs and ten observed stack scopes remain.
B13 and complete POSIX admission remain later work.

Current observation/source/recipe maintenance belongs in this outcome
under ADR-0123. One sufficient complete author plan and fresh independent
implementation review precede the first permanent commit.

## Phase and non-goals

Independent design review is clean and this decision is Accepted.
`boot03-implementation` stays pending the separate design merge. No code, tests,
models, harnesses, build scaffolding or executable prototypes are authored
in this phase. Design Acceptance does not close B06-F004.

No new memory algorithm or public ABI; no verification-loop optimization,
layout/alignment change, mapping batch, authority cache, validation-cadence
change, snapshot-lifetime correction, service/VM/PM/TTY byte-loop migration,
hosted-CI acceptance or node-Q implementation.

## Alternatives

- Duplicate word loops in bootstrap: unnecessary providers and compiler
  assumptions instead of reusing the accepted implementation.
- Replace every scalar/structure assignment or byte predicate: expands
  the finding beyond the seven actual movement helpers.
- Assume every image/configuration source is eight-byte aligned: not
  guaranteed by the existing callers or embedding declarations.
- Remove readback or zeroing to reduce measured work: changes safety and
  observable initialized contents.
- Add a whole-range translation/rollback framework: changes the existing
  prefix-failure boundary instead of correcting its movement primitive.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
ten-document proposal, relevant Accepted authority, fixed local
MINIX/current source and exact four-command author evidence.
It reported no substantive BOOT03/B06-F004 design issue remaining,
with High confidence. Targeted reproduction: None.

Acceptance records that design result, not an implemented transfer change
or finding closure. The separate design merge still precedes implementation.

## References

- [Bootstrap transfer contract](../architecture/bootstrap-memory-transfers.md)
- [Fixed MINIX/current study](../research/minix-bootstrap-memory-transfers.md)
- [Shared memory provider](../architecture/runtime-memory-primitives.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)

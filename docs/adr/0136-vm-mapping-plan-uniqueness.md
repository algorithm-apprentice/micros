# ADR-0136: VM Mapping-Plan Uniqueness

- Status: Accepted
- Date: 2026-11-14
- Refines: ADR-0020, ADR-0021, ADR-0045, ADR-0050, ADR-0053, ADR-0054,
  ADR-0055, ADR-0057, ADR-0077, ADR-0078, ADR-0080, ADR-0083, ADR-0084,
  ADR-0087, ADR-0103, ADR-0107, ADR-0114, ADR-0123 and ADR-0135
- Supersedes after Acceptance and separate design merge: all-process
  VM mapping preflights, their unvisited-corruption precedence, pairwise
  VM/frame-plan uniqueness mechanisms and load-token scan-based selection,
  only within the boundary below

## Context

VM01 implementation #272 is independently reviewed and merged. It removes
ordinary complete VM/BOOT/PM audits while retaining the real authority and
memory checks. VM02 now has its actual VM01 and MEM04 predecessors and
owns B02-F008, B02-F009 and B07-F004.

The [fixed study](../research/minix-vm-mapping-plan-uniqueness.md) traces
MINIX's endpoint-slot selection, selected page-table updates, indexed
physical ownership and separate diagnostic used-page map. Current
`micros` still visits every process root on VM mutations and performs
pairwise physical/token comparisons. The existing composed guest retains
199 complete memory-root walks.

A second dense bitmap is not an acceptable default: the limiting observed
VM image has six pages of total headroom and 352 bytes of stack margin.
The fixed reference also supplies the MSD radix family, which permits a
bounded ordered-key representation without weakening exact uniqueness or
importing a new allocator, record layout or snapshot-lifetime change.

## Decision

After independent review, Acceptance and separate merge, implement the
[VM mapping-plan contract](../architecture/vm-mapping-plan-uniqueness.md).
This phase contains documentation only.

### Reuse selected authority

Use the actual MEM04 selected walk and MEM05 owner/index mechanisms.
Scratch map/removal retain exact VM/root/load/PTE authority without a
full scratch clear or all-process root audit.

Freeze and teardown prove the exact selected four-table application root,
all planned frame owners and the complete selected-generation owner set.
Compare table slots with an ordered plan cursor, not a leaf search per
slot. Keep full public diagnostics separate.

Preserve the construction/owner/root/FROZEN and detach/clear/fence/free
orders. The memory owner certifies constructed tables before root
attachment and verifies the resulting binding in the same protected epoch,
without reauthorizing PM/VM during the valid pre-FROZEN transient.

Use actual frame-owner identity to select a possible physical-collision
record. This is not a successful-authority Boolean, a replacement for the
real ownership preflight or permission to expose portable code to target
globals.

### Bound uniqueness without changing semantic storage

Replace preceding-input and cross-record searches with fixed-width
unsigned MSD radix proofs and exact adjacent/group comparison. Specialize
the reference family to two bins, bounded explicit partition storage and
no insertion fallback, recursion, allocation or comparator callbacks.

One temporary workspace per participating image is at most 16 KiB plus
256 bytes. It is exclusively borrowed, touches only populated keys, is
released on every path and is never retained as validation authority.
Semantic records, executable buffers, boot transport and digests are not
rewritten to make room.

Keep original per-item error order by deriving the earliest duplicate
position and testing it at the original boundary. Use checked binary
geometry resolution and actual selected owner predicates, not repeated
range-capacity or metadata validation inside a batch.

The service's original allocator consumes a temporary membership index
instead of searching every record per candidate. Lowest-frame choice,
ignored-record behavior and allocator lifetime remain unchanged; this is
not VM04's bitmap/cache correction.

### Route the existing load identity

The low six bits of a kernel load token select its existing process slot.
The issuer chooses the smallest representable token above its prior value
with that slot, checking block overflow and never reusing or wrapping.
Only successful publication advances the existing counter.

VM may decode this routing hint, but must compare the complete token and
all existing generation, phase, PID and exit identities. PM/VFS remain
opaque consumers. Direct exit selection has no fallback scan.

This explicitly refines only ADR-0050's load-token representation and its
opacity to VM's record selector. It does not merge token namespaces,
change other issuers, encode a semantic PID or change a wire layout/version.
No compatibility or migration path is required for the unshipped system.

### Keep evidence complete

Require actual unchanged-production native and same-guest work Red, then
independent semantic/algorithm Green. Retain every existing case/model,
complete corruption check, original resource witness, scratch-preemption
boundary and all current image/stack/deadline limits.

Current work evidence must distinguish zero all-process root enumeration
from the five complete selected four-table certificates in the existing
three-freeze/two-teardown guest. The full VM/BOOT/PM zero-audit rule
remains. Do not hide either retained work or private workspace cleanup.

Use actual current portable owners and linked target artifacts, not
validation-success stubs or candidate-derived expected results.
Shared helper/source/recipe updates belong to this outcome under ADR-0123.
One sufficient final author plan and fresh independent implementation review
precede the first permanent implementation commit.

## Supersession boundary

ADR-0020/0021/0045/0050 retain generation, owner, permission, anti-alias,
failure and irreversible publication invariants. ADR-0103/0107/0114 retain
their complete diagnostics and index completeness boundaries; only the
named VM callers adopt the selected/indexed mechanisms.

ADR-0077/0078 retain canonical digest, active/inactive semantics and their
valid minimum-six/corruption matrix. Their complete semantics remain,
while the separate mapping uniqueness algorithm changes.

ADR-0135's deliberately retained ordinary collision/root and local/global
uniqueness algorithms are replaced only here. Its real before-backend
authority, complete diagnostics and protected freeze transient remain.
ADR-0087's phase-bounded temporary lifetimes and all compiler/linked/target
stack requirements remain.

## Phase and non-goals

Independent design review is clean and this decision is Accepted. Its
implementation task remains pending the separate design merge. No code, tests, models, harnesses,
build scaffolding or reusable executable prototypes are part of this phase.
Design alone closes none of the three findings.

No retained whole-load plan, allocator-policy change, movement rewrite,
snapshot reclamation, generalized index/cache/transaction framework,
process/page/input-capacity reduction, larger budget, new guest/transport, hosted-CI acceptance,
node-Q implementation or POSIX-conformance claim.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
seven-document proposal, relevant Accepted authority, fixed local
MINIX/current source and exact four-command author evidence at Proposed
staged tree `90e8e630cbb22356541c473e6b28dd47bae9c3b5`.
It reported no substantive design issue remaining, with High confidence.
Targeted reproduction: None.

Acceptance records that design result, not an implemented correction or
finding closure. The separate design merge still precedes implementation.

## References

- [Canonical mapping-plan contract](../architecture/vm-mapping-plan-uniqueness.md)
- [Fixed MINIX/current study](../research/minix-vm-mapping-plan-uniqueness.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)
- [Selected VM validation](../architecture/selected-vm-validation.md)
- [Selected address spaces](../architecture/selected-user-address-space.md)
- [Maintained owner indexes](../architecture/selected-release-and-wired-handoff.md)

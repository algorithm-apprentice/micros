# ADR-0137: Retained VM Load Plan

- Status: Accepted
- Date: 2026-11-14
- Refines: ADR-0050, ADR-0055, ADR-0057, ADR-0080, ADR-0083, ADR-0084,
  ADR-0087, ADR-0123, ADR-0135 and ADR-0136
- Supersedes only after Acceptance and separate design merge: repeated
  source-plan reconstruction and complete loading-prefix admission on the
  named ordinary page paths, including their unconsumed-corruption precedence

## Context

VM02 #274 is independently reviewed and merged. VM03 has its actual VM01
and VM02 predecessors and owns B07-F002.

The [fixed study](../research/minix-retained-vm-load-plan.md) traces the
reference's retained exec context, segment callbacks and separate stack
construction. It also identifies what remains after VM01/VM02: ordinary
global audits are already zero, but each page step rebuilds ELF/stack plans,
revisits the service record and re-proves the kernel's committed prefix.

The existing service transaction already stores useful plan data and exact
identities. A second plan, validation cache or larger buffer is unnecessary.
The limiting observed VM stack remains 3,744 of 4,096 bytes, so storage and
temporary lifetime cannot be treated as unbounded.

## Decision

After independent design review, Acceptance and separate merge, implement
[the retained load-plan contract](../architecture/retained-vm-load-plan.md).
This phase authors documentation only.

### Publish data once, consume the selected page

Use the current transaction's prepared ELF/page/stack/frame description.
Preparation retains its original complete input checks and failure-atomic
candidate publication. Bind the actual process generation and kernel load
token at the existing boundary.

The prepared description is immutable through page processing. Its phase
and exact identities describe an owned lifetime; they are not a Boolean
certificate that can replace privileged authority.

Ordinary image steps check fixed metadata, the admitted three-segment
description, current page/record/frame and necessary immediate progress
neighbor. They do not reparse source headers, decode the request, walk all
page/frame arrays or invoke a whole-state validator.

The kernel uses its existing exact-generation append-only mapping record
as the retained prefix. Validate the current header/scratch and immediate
predecessor rather than re-proving every completed page. Keep first-page,
VA/permission progression, final-stack, token and capacity rules, and actual
FREE/owner/PTE preflight. No new kernel whole-load wire object is introduced.

### Preserve every effect boundary

Keep all current caller, generation, token, phase, privilege, grant,
reservation, root, frame and fence requirements. VM02's selected root and
owner/index proofs remain authoritative; descriptive data is not authority.

Bind, freeze, local frozen publication and destructive cleanup still
preflight their complete selected sets before backend effects or stores.
Those linear operation-specific proofs are not complete VM/boot audits
at a coarser boundary. Complete global validation remains diagnostic-only.

Keep the existing pre-write stack-plan comparison and public stack builder.
Together with preparation they permit three constant stack decode/plan
passes per whole load, not one pass per image-page substep. Removing the
pre-write comparison merely to reduce that constant would permit output
mutation before detecting a retained-plan mismatch.

### Make corruption scope explicit

Selected header/page/segment/frame and scratch disagreements fail before
effects. Every field needed by a whole-set operation is checked there.
Unconsumed prepared pages, old prefix entries, inactive tails and encoded
input metadata do not become recurring ordinary validation authority.

Their corruption is not legalized or repaired. The original complete
diagnostics still reconstruct source plans and reject global/description
disagreement. Tests must distinguish those cases from malformed consumed
metadata and prove that legitimate progress never rewrites prepared data.

### Keep representation, bounds and evidence

No wire, public C API, state/record layout, token representation or capacity
changes. No new persistent backing storage, generic validation-mode flag,
ready Boolean, checksum cache, heap, VLA or capacity-sized automatic plan.
Keep ADR-0087's preparation helper lifetime and every current image,
stack, deadline and B13 admission boundary.

Require actual native and same-guest work Red, independent selected/whole-load
Green, complete corruption/output controls, original models and real hardware/
cross-service evidence. Preserve VM02's zero ordinary global audits/root
enumerations and its five selected root certificates.

Current source, callback, recipe and primitive qualification belongs in this
outcome under ADR-0123. One sufficient final author plan and fresh independent
implementation review precede its first permanent commit.

## Supersession boundary

ADR-0135 deliberately retained active ELF/stack reconstruction and local
loading-record checks. ADR-0136 corrected their uniqueness mechanism but
left repeated per-page plan consumption to VM03. Only that named cadence
and unconsumed-corruption precedence change here.

ADR-0050 retains its input semantics, ownership, scratch/commit/freeze/abort/
exit order, stack ABI and external identities. ADR-0087 retains its real
temporary-lifetime and stack constraints. Every complete diagnostic remains
complete; no allocator, movement, snapshot-lifetime or cross-owner lifecycle
finding is closed incidentally.

## Phase and non-goals

Independent design review is clean and this decision is Accepted. Its
implementation task remains pending the separate design merge. No executable code, tests, models,
harnesses, build scaffolding or reusable prototypes belong to this phase.
Design alone closes no finding.

No VM04 allocation-policy change, VM05 copy/fill rewrite, VM06 snapshot
reclamation, LIFE01 transaction framework, public protocol extension, paging,
COW, recovery, concurrency, larger budget, hosted-CI acceptance, node Q or
POSIX-completion claim.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
eight-document proposal, canonical context and relevant Accepted contracts,
current VM/service/kernel/PM/memory code, fixed local MINIX source and exact
four-command author evidence at Proposed staged tree
`60bb55e3d73f611690107db4977a224867c46300`.
It reported no substantive design issue remaining, with High confidence.
Targeted reproduction: None.

Acceptance records that design result, not an implemented correction or
B07-F002 closure. The separate design merge still precedes implementation.

## References

- [Retained load-plan contract](../architecture/retained-vm-load-plan.md)
- [Fixed MINIX/current study](../research/minix-retained-vm-load-plan.md)
- [Selected VM validation](../architecture/selected-vm-validation.md)
- [VM mapping-plan uniqueness](../architecture/vm-mapping-plan-uniqueness.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)

# ADR-0138: MINIX VM Frame Allocation

- Status: Accepted
- Date: 2026-11-14
- Refines: ADR-0045, ADR-0050, ADR-0057, ADR-0077, ADR-0080, ADR-0083,
  ADR-0084, ADR-0087, ADR-0100, ADR-0123, ADR-0135, ADR-0136 and ADR-0137
- Supersedes only after Acceptance and separate design merge: VM's
  restart-at-zero/lowest-frame selection, immutable initial-free bitmap
  representation, table-only local reservation and diagnostic allocation
  replay, within the boundary below

## Context

MEM01 #210 and VM03 #276 are merged. VM04 owns B07-F003; kernel bootstrap
allocation remains MEM01's distinct lowest-address policy.

The [fixed study](../research/minix-vm-frame-allocation.md) traces MINIX's
current-free bitmap, bounded LIFO page cache, retained descending scan
origin, empty-word skipping and release. Current VM still restarts from
frame zero and constructs a global temporary membership view for selection.

A second dense bitmap is not a viable default. The current observed
ELF-spawn VM uses 159 of its permitted 160 resident pages. Its existing
32-KiB initial-free bitmap can instead hold current availability, while
the existing local frame byte preserves the initial eligibility of
dynamically reserved/owned frames. No snapshot-lifetime change is needed.

## Decision

After independent review, Acceptance and separate merge, implement
[the VM allocation contract](../architecture/vm-frame-allocation.md).
This phase authors no executable artifacts.

### Reproduce the allocator mechanism

Use one current-free bitmap indexed by the existing compact managed-frame
coordinate, a twenty-entry single-page LIFO cache and one retained origin.
Cache entries are hints: validate their index, actual bitmap bit and local
frame class before consumption. Discard legitimate stale allocated entries;
reject malformed consumed metadata.

On cache exhaustion, scan downward from the retained origin, skipping empty
64-bit words, then retry from the managed upper bound if necessary. A
successful allocation records its selected index as the origin. Release
sets the bit and appends the released index when the cache has room; it does
not lower the origin. No address restriction or multi-page-contiguous API
is added.

The complete application request is still four table frames plus its data
pages. Allocate those single pages in reference order, then canonicalize
that selected set into the existing ascending table/data representation
using VM02's shared ordered-key workspace. This changes the chosen set from
globally lowest frames, not the existing role/VA/PTE publication contracts.

### Reuse storage without discarding provenance

Reuse the existing bitmap backing for current availability. Keep the
original wire frame values unchanged.

VM-local `FREE` remains the original untagged value. The four existing
dynamic local classes become tagged with bit `0x80`: transferable,
load-reserved, process-wired and process-page-table. Those classes establish
that the frame originated in the imported free pool. Initial eligibility
is therefore untagged FREE or one of those exact tagged dynamic values.
Static kernel/self/service classes remain untagged and ineligible.

Cold initialization first validates the unmodified boot object and builds
the availability bitmap/cache without changing its frame bytes.
Reservation publishes the tagged local class; release returns to untagged
FREE. Complete diagnostics reconstruct the original FREE byte for tagged
dynamic classes and retain the original boot digest, tails, counts and
global ownership proofs. The mutable bitmap is never substituted for that
original-class reconstruction.

This explicitly changes a private representation, not the boot transport,
kernel ledger, public wire protocol or snapshot lifetime. A coherent
alteration of private class/index data is not made detectable by inventing
a second immutable bitmap; actual kernel authority and complete diagnostics
remain independent boundaries.

### Reserve the complete local set before publication

Preparation reserves all selected table and data frames locally, before
publishing `INPUT_COPIED` and before `LOAD_BEGIN`. Kernel ownership remains
FREE until the existing scratch/freeze transitions.

Allocation may tentatively change only its local bitmap, frame bytes,
cache count and origin while its private preparation helper runs. Cache
pop does not erase array slots. Save the old count/origin as scalars and
retain the selected frames in the existing preparation arrays.
Every failing batch restores those scalars and every selected bit/class
before returning; no full bitmap/cache snapshot is added.

Bind checks the existing selected set, actual returned generation/token and
record membership before local record publication. Scratch requires a
locally reserved data frame, then mirrors transferable ownership. Freeze
and release retain all existing kernel and local preflight/effect order.
An `INPUT_COPIED` abort now releases its local reservations too.

Successful release may change cache contents/origin history without leaking
resources. Failed batch allocation and rejected lifecycle operations remain
state/output preserving. A cache/origin hint is not an application resource,
generation, owner, token or exemption from checking actual availability.

### Diagnose current invariants, not historical selection

Complete validation proves bitmap/class equivalence, original imported
class/digest, active cache bounds, transaction/record set equality,
uniqueness and every existing lifecycle invariant. It does not allocate
again to guess the historical choice from a now-advanced origin/cache.

Ordinary allocation consumes selected bits/classes and cache/search
prefixes, not every mapping record. Bind and complete diagnostics retain
their assigned membership/set boundaries. Preserve VM01-VM03's selected
per-page work, zero ordinary global audits, real before-backend authority,
five selected root certificates and separate complete diagnostics.

## Resources and evidence

Add at most 128 bytes of production allocator metadata, including alignment:
twenty 32-bit compact cache indexes, count and origin need 88 bytes before
any layout padding. Reuse the existing 32-KiB bitmap and 16-KiB-plus-256
key workspace. No second frame plane, capacity-sized automatic rollback
copy, heap, VLA, allocation framework or budget increase is allowed.

The [contract](../architecture/vm-frame-allocation.md) fixes native
cache/scan/rollback and independent model evidence, actual same-guest
allocation work, current qualification and the complete existing
image/stack/hardware gates. New physical choices must come from the fixed
reference model, not edited-candidate output.

Actual compiler/linked evidence must establish fit within the remaining
image space and unchanged four-KiB VM stack. A failure is a blocker, not
permission to enlarge a limit or start VM06 early.

## Phase and non-goals

Independent design review is clean and this decision is Accepted.
Implementation remains pending the separate design merge; design closes
no finding.

No kernel bootstrap allocator change, page movement rewrite, snapshot
reclamation, LIFE01 framework, physical contiguity/placement flags, paging,
COW, recovery, concurrency, new guest/transport, hosted-CI acceptance,
node-Q or POSIX-completion claim.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
eight-document proposal and its exact author evidence at staged tree
`199c7af2a0b936aa1ca4a77e1138b283ca30927f`, together with relevant Accepted
contracts, current VM code and fixed local MINIX evidence.
It reported no substantive design issue remaining, with High confidence.
Targeted reproduction: None.

Acceptance records the design result, not an implemented allocator or
B07-F003 closure. Separate design merge still precedes implementation.

## References

- [VM allocation contract](../architecture/vm-frame-allocation.md)
- [Fixed MINIX/current study](../research/minix-vm-frame-allocation.md)
- [Retained load plan](../architecture/retained-vm-load-plan.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)

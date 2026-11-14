# Retained VM Load Plan

## Status and boundary

This is the Accepted VM03 contract under
[ADR-0137](../adr/0137-retained-vm-load-plan.md). VM01 #272 and VM02 #274
are merged. The [fixed study](../research/minix-retained-vm-load-plan.md)
owns current/reference provenance and resource measurements.
Independent design review is clean and design #275 is separately merged.
Implementation is active; B07-F002 remains open until complete acceptance,
independent implementation review and merge.

Close only B07-F002: publish one useful load description and consume its
selected page/progress relations without reparsing the complete ELF/stack
or re-proving every previously completed mapping at each page step.
This is not another complete-validation cadence change: VM01/VM02's zero
ordinary global audits and selected root/owner mechanisms remain mandatory.

Use the existing service transaction and kernel mapping record. Preserve
their layouts, public C APIs, wire versions, process/page capacities,
frame-choice policy, input snapshots, token namespaces and all publication
orders. No second plan, generic validation-mode flag, ready Boolean,
checksum-based cache, new persistent storage or new scratch mapping is added.

VM04 retains allocator bitmap/cache/origin policy; VM05 retains movement;
VM06 retains snapshot lifetime; LIFE01 retains cross-owner lifecycle plans.
No paging, fork/COW, recovery, concurrency, hosted-CI, node-Q or POSIX
completion is included.

## A retained description is not cached authority

The service's reservation/PID, validated ELF segments/pages, stack layout,
input extents, stack values and selected frame arrays describe one load.
Preparation constructs them in the existing unpublished candidate and
publishes them with `INPUT_COPIED` only after every existing preparation
check succeeds.

Binding adds the actual kernel-issued load token and process generation.
VM02's six-bit routing and full-token comparison remain. The mapping
generation is added only after successful freeze. Page operations mutate
only the existing progress/scratch fields and their local frame-state
mirror; they never rewrite the prepared description.

Commit/abort clear the same transaction and input buffers in the same order.
No description survives its transaction as a reusable validation result.
An old token, process generation or page action cannot authorize a new load.
The one-thread/one-hart limits remain checked policy, not object equivalence.

After publication, this VM-owned description is the mapping/content-policy
input for the load. Ordinary image steps do not compare it again with the
encoded ELF header or unconsumed request bytes. Complete diagnostics retain
that independent source/description comparison.

The kernel mapping record is a separate, incrementally constructed mirror.
Load begin starts an empty exact-generation record; each accepted scratch
commit appends one checked mapping. No new whole-load wire object is sent
to the kernel and no VM pointer becomes kernel authority.

The actual VM/PM caller, endpoint, root, reservation, owner, PTE, token and
phase checks still occur at their existing owners. A retained description
does not waive any kernel operation-12 command admission or authorize a
physical frame by itself.

## Service admission by consumption

### Header and fixed metadata

Before any count-derived access, validate the current instance, phase,
imported geometry, transaction identity/extents and progress bounds.
Retain exact scalar reservations, PID/stack-value relation, endpoint
canonicality, page-count limits and six-bit token/process binding.
Check the retained entry and stack scalars without decoding input: the entry
is the fixed application entry, SP is 16-byte aligned in the permitted upper
half-page, occupied size equals stack top minus SP, and frame size rounds
to that same occupied extent. Stack layout offsets remain bounded and
ordered within that extent; their exact request-derived equality is checked
at stack fill and by complete diagnostics.

The three admitted segment descriptors are a fixed checked ELF-subset
contract, not an arbitrary segment capacity called constant. They may be
checked on an image step without scanning the page array. Validate their
aligned, contiguous virtual extents, checked file bounds, canonical RX/R/RW
roles and total image-page count before using a selected descriptor.

Do not call `vm_core_transaction_plans_are_canonical`, an ELF planner,
a request decoder or a stack planner on an ordinary image-page transition.
Do not visit all transaction frames, mapping entries, inactive page storage
or previously completed pages under a helper named selected.

### Selected image page

Select the next index from the checked progress fields. `next_page`,
`scratch_mapped`, image `fill_scratch` and `scratch_committed` retain their
original argument, phase, page-index and output order.

Prove the selected page using:

- the exact expected child VA and current data-frame address;
- its one local mapping-record entry and expected user-page role;
- at most the immediately preceding progress/mapping neighbor when ordering
  or a permission transition requires it;
- one of the three admitted segment extents; and
- the actual selected local frame-state and initial-free relation.

The page's file intersection is derived from its selected segment and
page offset. Require the stored VA, permissions, offset and byte length
to match that intersection. A wholly zero-filled page retains canonical
zero file offset and length. Checked subtraction/addition precedes every
source or destination access.

Before map, the selected local frame is `FREE`; while scratch is mapped
it is `TRANSFERABLE`. The record, transaction and selected action agree on
VA/frame/permissions/index. Previous mappings are not rediscovered merely
to establish uniqueness: actual FREE claim and the protected append
invariant preserve that property.

No read at `page_count` is allowed when all pages are completed. That is the
checked end boundary, not an implicit last-page selection.

### Stack page

The stack still occupies its one fixed VA and retains the exact initial
stack ABI and 2,048-byte occupied bound. Its selected action and local frame
are checked before the backend effect.

Keep the existing explicit stack decode/plan comparison before the first
scratch-page store, followed by the public stack builder and output-plan
comparison. That builder retains its independent pre-write validation.
Including preparation, at most three stack decode/plan passes occur per
whole load; none occurs for an image page or merely to admit frozen cleanup.

Do not remove the first comparison and discover a retained-plan mismatch
only after the public builder has written the destination. Do not introduce
another resident stack image or change the public builder's input contract
solely to remove this constant whole-load cost.

## Kernel selected loading prefix

Ordinary kernel loading admission validates the existing header, exact
load/reservation/process tuple and scratch phase. It does not invoke the
complete loading-record layout/key proof.

Append selection reads the current record count and, when nonempty, the
immediately preceding entry. Validate a consumed predecessor before using
its VA/permission group. Require:

- the first image page is the exact application entry with RX permissions;
- each subsequent image VA is the next page in the admitted image window;
- permission groups do not regress or skip the required read-only group;
- the stack appears only after the required image groups and is the last
  writable page; and
- the next index remains within the unchanged 15-plus-one limit.

The checked constructor and append commit maintain earlier entries and zero
unused storage. A selected command need not recertify that entire prefix
or its inactive tail. It must still reject malformed consumed counts,
previous-entry shape, scratch identity or token/generation relations.

VM02's actual owner-directed membership query remains. A healthy duplicate
transferable/bound frame keeps its existing failure result; selected
relationship corruption remains `INVARIANT`. Such invalid-frame paths may
inspect the chosen bounded record. A valid FREE next-page path has no
complete-record key proof, preceding-entry search or record-slot scan.

Actual claim, scratch-PTE preflight, map/removal fences and protected commits
are unchanged. No new fallible work follows the first authoritative store.

## Whole-load and destructive boundaries

Removing page-wide admission is not permission to trust every frame on
cleanup. Keep explicit read-only selected-plan/set preflight whenever the
operation consumes that entire set:

| Boundary | Required proof at its existing publication boundary |
| --- | --- |
| Preparation | Original input shape, complete ELF/request/stack planning, original allocator selection and output/error order. Publish the same candidate once. |
| Bind | After `LOAD_BEGIN`, check its exact result and generation/token, target record availability, complete selected frame/record construction and temporary membership before local publication. No source-plan reconstruction. |
| Freeze request | All pages completed, scratch clear, exact retained page/record/frame set, permissions, entry/SP and ordered digest. Kernel and memory keep their existing complete selected frame/root certificate. |
| Local frozen transition | Every local table/data index and required pre-transition state checked before any frame-state write; publish mapping generation and phase last. |
| Loading/frozen abort | Validate every local index/frame/record needed for cleanup before the backend effect and before any local store. Keep mapped-scratch removal and reverse ownership order. |
| Commit | Exact frozen token/generation/PID and selected record relation before backend/publication; retain existing transaction/input clearing. |
| Exit | Keep VM02's direct exact-tuple selection and complete selected release set. No new exit or cross-owner-plan algorithm is introduced. |

The service facade must retain before-backend checks. In particular,
changing the common selected validator must not move frozen release's
complete local cleanup proof until after `abort_load` or `commit_load`.
Standalone local APIs also retain their own preflight before stores.
Preparation proves the required frame set before `LOAD_BEGIN`; checks of
that call's returned identity necessarily follow it. A bind disagreement
after successful kernel load begin remains fatal at the service adapter,
not a new ordinary rollback protocol.

These are operation-specific `O(u)` plan/set checks, not whole-state
validation at a coarser boundary. Never reintroduce the boot digest,
whole frame census, all-record audit, allocator reconstruction or
`vm_core_validate_state` into ordinary creation, freeze, commit or abort.

Keep ADR-0087's preparation helper lifetime. Do not carry its large automatic
candidate/ELF/frame arrays into later service/backend calls.

## Failure and diagnostic scope

Retain all existing valid-input results, original null/phase/token/access
ordering, reply/output canaries and fatal-versus-recoverable boundaries.
No peer is treated as trusted because a prior call succeeded.

Consumed header, selected segment/page/frame, current scratch and bound
record disagreements fail before access or effects. All fields required by
a whole-set boundary are consumed and checked there.

Corruption confined to an unconsumed prepared page, older committed prefix,
inactive plan tail or encoded input metadata no longer outranks a valid
selected image-page operation. It is not a valid published state and is not
repaired. Complete diagnostics still reconstruct source plans and reject
the original corruption classes. Once a malformed field is selected, its
actual structural/relationship error must be rejected.

The retained description is the post-preparation policy representation.
Its permitted writers are preparation, the existing identity-binding
transitions and final clearing only. Progress changes cannot modify its
segment/page/stack/frame data. Tests must verify that immutability rather
than deriving expected data from the edited candidate.

Consistent changes to private data outside the selected proof are not
detected by inventing a repeated checksum or another whole-state audit.
This is the same maintained-invariant/explicit-diagnostic distinction as
VM01/VM02, now applied to the load description and committed prefix.

## Work, storage and acceptance

Use `u` admitted pages, `r` active geometry ranges and `s` request/string
bytes. Keep the inherited active-geometry cost explicit.

| Scope | Required bound |
| --- | --- |
| Service image-page admission | Fixed header, three segment descriptors, selected entry/frame and at most one progress neighbor; no term in `u`, `s`, frame or process capacity |
| Kernel valid FREE page step | Fixed loading header/current scratch/predecessor plus actual selected owner/PTE work; no committed-prefix or record-capacity term |
| Stack fill | `O(s + argument_count + environment_count)` validation/layout plus its unchanged page movement |
| Whole load control | `O(u*(r + log(r+1)) + s)`, plus separately disclosed existing allocator/membership, once-per-boundary selected-set and movement costs |
| Complete diagnosis | Existing complete source/plan/global-state checks and their full work remain visible |

The native image-page nonmovement budgets are `256 + 32*r` service
primitives and `1024 + 256*r` kernel/ownership primitives, applied separately
to comparisons, record visits and index probes. They allow inherited
metadata/geometry, not a hidden page/input-capacity pass.
The kernel budget covers the actual portable VM/ownership producer; it
does not relabel the target's separately retained caller, root and peer
authority work as a constant operation.
Equivalent selected image pages with different unconsumed page counts or
request lengths must have equal selected work. Kernel prefix cases compare
nonempty prefixes with the same immediate permission relation.

Stack cases have the additional nonmovement envelope
`16*s + 64*(argument_count + environment_count) + 512`; image and stack
copy/clear work remains separately counted, not relabeled as validation.
The operation ledger fixes one ELF plan construction and at most three
stack decode/plan passes per ordinary whole load. Complete diagnostics are
separately scoped positive controls.

Add no production backing storage or layout change. A necessary private
borrowed view is at most 64 bytes, contains data/identities rather than a
successful-validation flag, and does not own a capacity buffer.
The key workspace is not held across a backend/IPC operation.
Keep every 160-page VM image, four-KiB user/16-KiB kernel stack, deadline,
44-configuration/113-ELF/ten-scope gate and the B13 block.

Implementation begins only after separate design merge, with actual
unchanged-production native and existing-guest work Red. Do not count a
missing API, compiler failure, generic panic, timeout or source-scaffold
failure as that Red.

Retain every VM02 case, MEM05 guard, model seed/count and hardware/composed
scenario. Focused additions prove:

- selected-work independence from unconsumed pages, request lengths and
  completed-prefix length, with actual current source and complete
  observation closure;
- min/max image/stack plans, segment boundaries and BSS-only page slices;
- early rejection of corrupted selected descriptor/index/owner/scratch
  metadata and unchanged output bytes;
- immutable retained description across page transitions and generation/
  token rejection across abort/reuse;
- complete selected cleanup preflight before backend and local writes;
- successful selected use with unchanged unvisited corruption followed by
  explicit diagnostic rejection; and
- the original actual load, post-freeze rollback, scratch preemption,
  activation, exit/wait and resource-return witnesses.

Reuse the existing native and ELF-spawn producers. Extend their current
primitive sites, not another guest, collector ABI or transport framework.
Actual target planner/entry-loop work must reject the old multiplier while
retaining zero ordinary global audits, zero root enumeration and VM02's
five selected root certificates in the fixed scenario.

Current source/recipe/callback/stack qualifications belong to this outcome
under ADR-0123. Run focused Green, one sufficient final planner gate and a
fresh independent implementation review before the first permanent commit.

## Implementation sequence

After the separate design merge, prepare these dependency-ordered green
slices, each with its focused regression evidence:

1. Retain the service description, separate selected-page from complete
   selected-set admission, and preserve all before-backend cleanup checks.
2. Select the kernel loading header/scratch/predecessor while preserving
   complete diagnostic, freeze/release and actual ownership boundaries.
3. Complete current primitive/source qualification and same-guest whole-load
   evidence, then run the final author plan and independent review.

Do not publish an unreviewed intermediate slice. The first permanent
implementation commit requires the complete outcome's independent review;
the slices may remain staged/local until then. No later allocator, movement
or snapshot correction is part of this sequence.

## Ownership and phase barrier

`vm_load.c` remains the constructor/progress owner; `vm_validation.c` owns
the distinct header/page, whole-plan and complete-diagnostic predicates.
The existing ELF/stack owners retain their parsers and public builders.
The service adapter keeps the directed backend sequence.
Kernel VM core/scratch owners select the current loading prefix; freeze and
release retain their complete selected boundary checks. Memory ownership,
root certificates and other command authorities are not replaced.

The documentation-only design merged before implementation began.
Subsequent changes remain within this Accepted contract; a design defect
still requires ADR-0033's separate corrective-design barrier.

## Current implementation and evidence owners

| Responsibility | Owner |
| --- | --- |
| Prepared description and progress | Existing `vm_load.c` candidate, bind and scratch transitions; no public header or state-layout change. |
| Selected service admission | `vm_validation.c` checks scalar/segment/stack headers and the selected page plus its immediate predecessor. Frozen admission retains complete selected-set proof before the service facade invokes a backend. |
| Whole selected load set | `INPUT_COPIED`/`FROZEN` admission and `vm_core_validate_selected_load_plan` preserve bind/freeze/abort preflight; complete diagnostics retain their separate source-plan reconstruction. |
| Kernel loading prefix | `vm_handoff_core.c` checks the current record header/scratch/predecessor; freeze/abort use the complete selected load-plan boundary. Real owner/PTE and command admission remain in their original owners. |
| Native evidence | Existing `selected_vm_test.c` / `selected_vm_evidence.py` retain the original cases and add equivalent page/prefix work, corruption-scope, immutability, exact maximum-stack, BSS and pre-write-canary cases. |
| Same-guest reconstruction evidence | Existing `vm_command_evidence.py` binds actual linked ELF-plan and request-decoder entries. The original three-load guest requires three ELF plans and nine request decodes, including both stack-fill validations per load. |

The [build guide](../development/building.md#validation-planning) owns the
commands. Exact Red/Green execution, final candidate identities and
independent implementation review belong in the implementation PR, not a
duplicate history here.

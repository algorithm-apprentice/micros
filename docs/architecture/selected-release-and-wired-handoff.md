# Selected Release and Wired-Handoff Plans

## Status and scope

This is the Accepted MEM05 design under
[ADR-0114](../adr/0114-selected-release-and-wired-handoff-plans.md).
Its contracts and exact evidence scope are specified below.
Complete independent design/correction review is clean.
Design #233 is separately merged. Its pre-Red implementation preparation
found the [MEM05-C01 count-evidence conflict](#mem05-c01-count-evidence-scope).
The separate corrective design under
[Accepted ADR-0115](../adr/0115-selected-count-generation-evidence-scope.md)
merged as #234. Its fresh implementation then reached the
[MEM05-C02 handoff-fixture conflict](#mem05-c02-handed-off-corruption-fixture-coherence).
That unaccepted candidate is archived without Green, recertification or
retirement. The documentation-only correction under
[Accepted ADR-0116](../adr/0116-handed-off-leaf-fixture-coherence.md)
has clean independent review and separately merged as #235.
The fresh [implementation/evidence map](#implementation-and-evidence-map)
uses its thirty-path envelope. The runtime algorithm remains unchanged;
archived executions are not current acceptance.

The [fixed MINIX/current study](../research/minix-selected-release-and-wired-handoff.md)
owns the reference trace and current costs. The current source pin is:

```text
runtime commit: abe05cdd7bced5e77e86c1ec12754d017f920cab
runtime tree:   2c34dac3c259a4975d1c213e3e14c18d8a075658
MINIX commit:   4db99f4012570a577414fe2a43697b2f239b699e
```

MEM05 owns B02-F006/B02-F007 and the associated release-work evidence.
It changes selected release/destruction and wired-handoff composition,
not ordinary VM allocation/mapping policy or later lifecycle algorithms.

## Authority and persistent ownership index

The allocator bitmap remains availability authority. The eight-byte owner
record remains semantic frame authority. The new index enumerates those
owners; neither an index link nor a count alone authorizes physical access.

Append these logical records to `struct micros_frame_ownership`:

- `owner_links[P]`: two `uint32_t` fields, previous and next;
- `process_frames[64]`: two `uint32_t` fields, head and count.

`P` remains `MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES`, 262,144.
Every link/head is zero or an encoded `frame_index + 1` in `[1, p]`.
The original owner record, existing ledger members, capacities and
availability cursor are not repacked or reinterpreted.

The records are `struct micros_frame_owner_links`, with `previous` and
`next`, and `struct micros_process_frame_index`, with `head` and `count`.
Each record is exactly eight bytes. No process-object layout changes.

A process-bound record is `PROCESS_PAGE_TABLE`, `PROCESS_USER` or
`VM_WIRED`. Each such frame belongs exactly once to the list indexed by its
owner's process slot. The list may contain multiple encoded generations
in the portable mechanism; queries filter the exact requested generation.
Production target invariants still require every owner to resolve to its
exact live process generation.

### Published index invariants

For every process slot:

- count zero and head zero agree;
- a nonempty head has no predecessor;
- every reachable link is in the active managed range;
- links are reciprocal and never refer to their own node;
- each reached owner is process-bound and has that slot;
- traversal terminates after exactly the recorded count; and
- count does not exceed active managed/owned-frame bounds.

Every process-bound owner occurs in its slot's chain exactly once.
Non-process owners and unused frame entries have zero links.
The sum of slot counts equals the independently validated process-bound
owner-kind total.

Complete validation proves the index against the complete original owner
domain, including unused storage. Whole-active validation checks active
index membership/count agreement without replacing its existing active
owner/bitmap/target/count proof.

Selected mutation checks only the index node, selected slot metadata and
neighbors it consumes. A successful selected check is not a complete
diagnostic certificate.

There is an important completeness boundary. A locally consistent index
cannot discover an owner silently moved to another slot, or an owner omitted
from both the selected chain and the private tree, without inspecting the
physical owner domain. Only the complete/whole-active validators certify
that independent equality. Selected count/destruction certify the consumed
chain and tree, not arbitrary undiscovered owner corruption elsewhere.
Legitimate owner transitions must preserve complete membership; the index
is maintained ownership structure, not a cached successful audit.

An indexed orphan remains an ownership error during destruction.
A reachable frame omitted from the selected chain also fails its set
comparison. A separately corrupted, unindexed and unreachable owner is
outside that selected operation's diagnosis, but must fail complete
validation. Paired negative controls distinguish these cases explicitly.

Complete active-index validation consumes the process-bound total
independently observed by the original owner/bitmap pass in that same call.
Check every slot chain against its own count, owner slot and reciprocal
links; bound traversal by active capacity and require a null terminal link.
A terminating chain cannot repeat a node, and owner-slot equality prevents
cross-slot duplication. Its members are a subset of the independently
validated process-bound owner domain. Requiring the sum of chain lengths
to equal that domain's observed total therefore proves complete membership,
without another owner scan or a per-slot automatic count array.

An active link-storage pass checks that every nonzero link record belongs
to a process-bound owner; zero-link singleton members are already checked
through their slot head. The complete validator additionally checks every
inactive link for zero. No recursion, quadratic duplicate search or
capacity-sized visited array is needed. The original active/inactive
owner/bitmap helpers remain unchanged.

On MEM03's all-free diagnostic fixtures, all heads and links are zero:
the new index check reads no owner/target/bitmap again.
Preserve the exact original diagnostic record/range/bitmap counts.
Measure the added link/head work in its actual index/link dimensions;
do not hide another owner-domain pass by relabeling its record visits.

### Mutation order

Insertion prepends a newly process-bound frame to its slot list.
Removal preserves the relative order of remaining members.
Zero initialization and removal leave canonical zero links.

All possible index failures precede authoritative mutation:

| Transition | Required preflight and index effect |
| --- | --- |
| Bootstrap allocation | Validate the selected free node and target slot/head before owner publication; insert only after allocation and existing checks succeed |
| Bootstrap exact release | Validate the exact owner and consumed links before allocator release; unlink within the existing non-failing publication |
| Process-set release | Prove all selected members and count deltas; unlink exactly the distinct selected process frames |
| `PROCESS_USER -> VM_WIRED` | Preserve membership and exact identity |
| `PROCESS_USER -> VM_TRANSFERABLE` | Remove membership while clearing process identity |
| Kernel-retained transfer | Remain unindexed |
| Free/transferable claim or rollback | Require and preserve unindexed state |
| VM freeze | Preflight every unindexed source frame and target slot capacity; insert the data and table frames in existing commit order |
| VM process release | Preflight selected members and count deltas; remove them in existing commit order |

VM plan shapes, selection policy and existing duplicate-search algorithm
remain unchanged. No fallible callback/check is inserted into a prevalidated
commit. All target mutation remains SIE-clear.

Bootstrap post-allocation failure restores allocator bit/count/cursor,
owner/target/count state and index bytes exactly. A temporary allocated-bit/
free-owner interval must not be mistaken for a stable-state row.
The higher-level page-allocation cursor compensation remains unchanged.

`micros_frame_ownership_count_process()` keeps its signature, exact-generation
result and output-preservation contract. It validates/enumerates the selected
slot index, not all physical owners. Mixed-generation portable cases remain
supported; unrelated slots are not scanned.

Counting first checks the output, initialization, handle and metadata in
the original order. It then validates the complete selected slot chain,
including every reached record's encoding/bit/target relation, before
publishing the exact-generation count. A malformed reached link, head,
count, owner or reciprocal relation is `INVARIANT`.
The count's scope explicitly replaces the former global owner-encoding
scan; another slot's malformed record no longer outranks this query.

The private cross-module index interface is limited to:

```c
bool micros_frame_ownership_index_can_insert(
    const struct micros_frame_ownership *ownership,
    uint16_t slot, uint64_t frame_index, uint32_t additions
);
bool micros_frame_ownership_index_can_remove(
    const struct micros_frame_ownership *ownership,
    uint64_t frame_index
);
void micros_frame_ownership_index_insert_prevalidated(
    struct micros_frame_ownership *ownership, uint64_t frame_index
);
void micros_frame_ownership_index_remove_prevalidated(
    struct micros_frame_ownership *ownership, uint64_t frame_index
);
enum micros_frame_ownership_error micros_frame_ownership_check_selection(
    const struct micros_frame_selection *selection,
    uint64_t managed_frame_count
);
```

Callers establish metadata and selected semantic ownership first.
Insertion requires zero selected links, a valid target slot/head relation,
and capacity for the entire pending batch. Removal checks the selected
slot, node and consumed neighbors, including their encoded bounds,
process-bound slot and reciprocal links. It does not recursively audit
neighbor owners' unrelated mappings.

Insertion follows owner publication; removal precedes owner clearing.
Batch removal reads the current links after earlier removals, not stale
saved neighbor pointers. Complete preflight plus each preceding unlink's
invariant supplies that non-failing commit. VM freeze preflights its entire
data/table insertion count before any commit; VM release likewise
preflights all removals and the aggregate slot decrement.

## Indexed frame selections

`struct micros_frame_selection` has two scalar counts and fixed arrays:

```text
size_t word_count
uint64_t frame_count
uint64_t bitmap[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS]
uint16_t words[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS]
uint16_t positions[MICROS_FRAME_ALLOCATOR_BITMAP_WORDS]
```

There are 4,096 possible bitmap words. `words[0..word_count)` publishes the
nonempty word indices. `positions[word]` is the published position plus one.
Zero denotes no published position. Every selected bit appears in one
published word, and `frame_count` equals their population.

Only the published word prefix defines membership. Unpublished array cells
are not selected-frame authority and are not scanned by ordinary operations.
The kernel's owned construction and cleanup nevertheless leave all backing
storage zero at idle.

### Shape and membership checks

The checked shape requires:

- a non-null selection;
- `word_count <= 4096` and `frame_count <= 262144`;
- every published word index within the fixed array;
- `positions[words[i]] == i + 1`;
- every published word nonempty;
- population equal to `frame_count`; and
- every selected bit within the operation's active managed-frame range.

The inverse position check excludes duplicate published words in linear
time. Bits within one word already form an idempotent mathematical set.
No pairwise duplicate search or capacity-sized temporary clear is allowed.

The portable selection helpers are narrowly limited to addition,
membership and cleanup. They allocate no storage, operate on zero-initialized
caller-owned objects and preserve outputs/state on rejected mutations.
Adding a selected bit again succeeds without changing counts.
Addition validates the selected word's local/inverse relation before writing;
it never silently repairs a contradictory occupied cell.

Cleanup validates the published prefix before clearing its bitmap words,
inverse positions and dense entries, then clears both counts.
It performs no work over unpublished capacity.

The public portable helpers are:

```c
bool micros_frame_selection_add(
    struct micros_frame_selection *selection,
    uint64_t frame_index
);
bool micros_frame_selection_contains(
    const struct micros_frame_selection *selection,
    uint64_t frame_index
);
bool micros_frame_selection_clear(
    struct micros_frame_selection *selection
);
```

`add` rejects a null object, capacity overflow, out-of-capacity index or
contradictory selected-word/inverse metadata before writing.
`clear` validates the complete published shape and population before its
first store. Both return false on rejection and preserve their complete
input object. `contains` is a selected membership query, not an
authorization or complete validator; consumers validate the required
selection scope before relying on its result.

Header bounds include zero/nonzero count agreement and
`word_count <= frame_count`. A full valid selection still permits
idempotent re-addition; capacity rejection applies only to a new member.
An unpublished word must have zero local bitmap and inverse position before
`add` publishes it. `contains` returns false for a null/out-of-range object
or an absent/invalid local publication; false is not proof of global
selection validity.

Within each published word, enumerate bits in increasing bit order.
At most 64 bit positions per published word may be inspected.
No ordinary helper searches the 4,096-word capacity to find published
words. Prefix cleanup restores complete zero idle storage for a valid
construction history; it does not claim to diagnose arbitrary corruption
in unpublished backing cells.

### Release/staging inputs

The existing portable and runtime process-set release and wired-set staging
entry points take a pointer to this selection instead of a bare bitmap plus
full-capacity word count. There is no user-visible wire ABI change.
All directly dependent kernel and native-fixture callers change together;
an obsolete full-scan production fallback is not retained.

The exact resulting signatures are:

```c
enum micros_frame_ownership_error micros_frame_ownership_release_process_set(
    struct micros_frame_ownership *ownership,
    struct micros_process_handle process,
    const struct micros_frame_selection *selection
);
enum micros_frame_ownership_error micros_frame_ownership_prepare_wired_process_user_set(
    struct micros_frame_ownership *ownership,
    const struct micros_frame_selection *selection
);
enum micros_frame_ownership_error micros_frame_ownership_runtime_release_process_set(
    struct micros_process_handle process,
    const struct micros_frame_selection *selection
);
enum micros_frame_ownership_error micros_frame_ownership_runtime_prepare_wired_process_user_set(
    const struct micros_frame_selection *selection
);
```

Preserve argument/runtime/phase/owner/metadata ordering at the existing
boundaries. Selection shape and range failures are explicit before any
authoritative store. The new representation must not weaken exact owner,
generation, bit, target or count checks.

For release, validate the complete selected set and resulting aggregate
deltas before committing any bit/owner/index change. Empty selection is
still valid in the permitted phase after normal argument/owner/metadata
validation. Every output remains unchanged on failure.

For wired staging, require exactly all process-user frames, each currently
unplanned, and reject every selected wrong-class/free/unmanaged frame.
Whole-active ownership validation remains independent. Only after complete
preflight may the selected targets become `VM_WIRED`.

For portable release, retain this outer order: initialized ledger, bootstrap
phase, non-null selection/header shape, process-handle encoding, metadata,
complete selection shape/range, selected owner/index relations, aggregate
counts, then commit. Wired staging has no process-handle argument and keeps
whole-active ownership validation before selected-frame classification.

Malformed published-word metadata is `ARGUMENT`; selected unmanaged bits
are `UNMANAGED`; free or foreign/wrong-kind owners retain
`NOT_ALLOCATED`/`WRONG_OWNER`; ledger, index or count disagreement is
`INVARIANT`. An invalid stored target is an invariant failure, not
`TRANSITION`. Release may clear an otherwise valid staged target.

Wired staging rejects a valid selected user frame already carrying a
non-`NONE` target with `TRANSITION`. After complete selected classification,
a proper subset of the independently validated process-user population
is `WRONG_OWNER`, preserving the existing missing-member result.
Descriptor population disagreement is `ARGUMENT`, not a missing owner.
All selected argument/shape/range validation precedes owner mutation.
Published-word order, rather than an implicit physical-capacity scan,
orders multiple otherwise independent selected-owner failures.
This explicitly refines that internal representation's multi-error
precedence; it does not change any user wire result or permit partial state.

## Scratch ownership

Replace the two private BSS bitmap objects with two indexed selections.
Existing full-diagnostic leases continue to expose their bitmap members.
Their full-prefix clearing and diagnostic behavior remain unchanged.

Add a selected acquisition mode using the same checked non-reentrant owner and
SIE-clear epoch. It takes no new persistent authority and performs no
capacity clear. Only a selected lease may publish indexed members.

Selected cleanup clears only the two published word prefixes and restores
the complete idle lease state. Failure during collection cannot leave
stale membership for the next operation.
An impossible internal cleanup disagreement is fatal, not recoverable
success after a partial commit.

Legacy full-diagnostic bitmap writes must never be treated as an indexed
selection. Mode, storage identity and lease lifetime are checked explicitly.
No nested lease, concurrent shared scratch use or reusable validation
result is introduced.

Replace the old private Boolean busy storage with one byte-valued
`scratch_mode`: `IDLE = 0`, `FULL = 1`, `SELECTED = 2`.
Keep one owner; do not infer mode from caller-controlled bitmap contents.
The lease retains `active`, frame/word bounds and bitmap pointers, and adds
the matching mode and two selection pointers. Full-mode selection pointers
are null; selected-mode pointers identify the two exact BSS objects.
`require_idle` keeps its signature/call-set/empty-observation contract and
tests this mode rather than the old Boolean. Its existing identities remain
active; this scalar adaptation needs no additional retirement.

Both acquisitions preserve argument-before-busy-before-IRQ/range ordering.
An unknown mode or nonzero published counts at idle is `INVARIANT`.
Selected acquisition writes no capacity storage. Release checks owner/lease
mode and storage identity before clearing anything, validates both selected
prefixes before clearing either, then resets every lease field and the
owner mode to idle. Full release retains the old active-prefix clear.

The additional acquisition declaration is:

```c
enum micros_user_address_space_error
micros_user_address_space_acquire_selected_scratch(
    const struct micros_user_address_space_runtime_view *view,
    struct micros_user_address_space_scratch_lease *lease
);
```

## Bootstrap root plans

Add explicit bootstrap collection helpers in
`kernel/memory/user_address_space_validation.c`.
Keep the existing complete root walker, orphan scan and full VM/TTY getters
as diagnostic authority.

The bootstrap plan:

1. requires the same initialized runtimes, bootstrap phase and protected
   scratch ownership;
2. binds the requested process generation explicitly;
3. checks the root and canonical shared entries before physical consumption;
4. walks only the private three-level subtree;
5. requires exact process-table ownership before each table dereference;
6. accepts only 4 KiB process-user leaves with the existing permission rules;
7. records each private frame once in reachable selection;
8. accumulates user frames in the release/wired selection;
9. compares complete reachable membership with its checked ownership
   expectation; and
10. publishes a plan only after all checks succeed.

The fixed owner expectation comes from the same-epoch selected process
index for destruction, or the same-epoch global ownership partition for
handoff. It is not a reusable count-based authorization.
Only the declared destruction/handoff callers may consume these private
plans.

The private result has the resolved process pointer, root physical address,
reachable frame count and `wired_targets` Boolean; it is 32 bytes on RV64.
The two private entry points are:

```c
enum micros_user_address_space_error
micros_user_address_space_collect_bootstrap_release_locked(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    struct micros_user_address_space_bootstrap_plan *plan
);
enum micros_user_address_space_error
micros_user_address_space_collect_bootstrap_handoff_locked(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_user_address_space_scratch_lease *lease,
    struct micros_process_handle process,
    uint64_t expected_owned_frames,
    struct micros_user_address_space_bootstrap_plan *plan
);
```

The release entry first collects the checked tree, then validates the slot
chain through `count_process` and checks every exact-generation indexed
member against reachability. Cardinality alone is insufficient here: an
indexed orphan must not cancel an unindexed reachable frame.
It fills the release selection with all reachable private frames.

The handoff entry receives a same-epoch count independently collected from
the full active owner domain. Exact typed ownership and unique reachability
make cardinality equality sufficient for that independently counted set.
It clears only the previous root's sparse reachable prefix and accumulates
user frames in the separate handoff selection.
Neither entry publishes `plan` on failure.

Retain a per-root `wired_targets` result while walking, but do not let an
early target mismatch mask a malformed PTE or incomplete root in that same
process. Handoff classifies it only after successful root collection.

The existing checked table/shared-root helpers may supply descriptive
root checks in this owner. Walk the fixed three levels through direct
helpers, without recursion or new callbacks.
No VM/TTY getter or inspector is needed by the complete bootstrap collector:
this phase admits only ordinary exact process-user leaves.
An active VM scratch/device exception is not bootstrap user-frame authority.
The new path must not invoke a complete controller getter or admit an
exceptional post-handoff leaf.

## Page release and destruction

### Selected page release

Use one MEM04 checked selected path. Keep the existing at-most-two
512-entry empty-table inspections, exact inactive-root guard and pruning
decisions. Do not rediscover a different path after preparing the plan.

The selected set contains the user frame and zero, one or two pruned
private tables. The lower release commits exact ownership/index/count
changes; the existing prevalidated PTE/table-clearing stores and physical
address publication follow under the same SIE-clear epoch.

Keep `inspect_page_release`, `add_release_frame`,
`prepare_page_release_set` and `commit_page_release` unchanged.
The existing inspector already obtains one MEM04 checked path; it does not
need another walk. Its narrow bitmap wrapper accepts only the lease's exact
release-bitmap pointer and delegates to that lease's indexed selection.
The existing reset wrapper clears the selected prefix, never capacity.
Page release clears pruned table pages, not the released user page.
Complete zeroing on subsequent allocation and dirty-reuse evidence remain.

An unrelated malformed leaf, alias or orphan no longer makes this point
operation a complete root audit. Such state remains invalid and explicit
diagnostics still reject it. Every legitimate producer must preserve
the complete published invariants. Selected owner/ancestor/PTE corruption
and invalid pruning inputs still fail before mutation.

### Complete selected-process destruction

Validate the complete selected process/root/index relation, not unrelated
physical owners. Preserve duplicate reachability, orphan, foreign/stale
owner, root-composition and malformed-PTE rejection.
The process must have zero live threads and its root must be inactive.

Build its release selection by iterating the collected sparse reachability,
not by scanning physical capacity. Before release, resolve the exact mutable
production process slot and expected root in the authoritative registry.

Use the existing private object-runtime accessor, with its declaration
included in this C owner, and require equality with the runtime's resolved
registry/process/root. Do not cast away a read-only authority interface.
Remove the now-unused local bitmap membership/addition and reachability-copy
helpers; the bootstrap collector already supplies the exact release set.

Commit then releases the exact selected ownership set, clears all released
frames through the prevalidated compact indices, and clears that process's
root as the final non-failing store. Keep the complete-page clearing and
teardown-before-process-release contracts.
Do not call the old post-detach complete object-audit wrapper after release
has become irreversible.

## Wired-handoff composition

Retain the existing initial runtime/phase checks and one explicit complete
object audit. Build one fixed process-index array containing:

```text
uint64_t owned_frame_count
uint32_t thread_generation
uint16_t thread_slot
uint16_t thread_count
```

One active-frame pass counts nonfree exact-generation owners against live
process records. One thread pass records the actual slot/generation and
count for each owning process. Bound every index before use.
Do not identify a process with its sole thread slot.

The original process-slot-order checks then use that summary:

- rootless processes require zero threads, no primary endpoint and no
  matching process-owned frames;
- rooted processes require exactly one prepared thread and the existing
  exact context validation;
- each root is collected once against its precomputed owner count; and
- each process's target result is checked before advancing to the next root.

Preserve the two original process passes: validate all rootless/thread/
context conditions in process-slot order before acquiring scratch; only
then collect roots in process-slot order. Do not interleave the first
process's root walk with later processes' context preflight.

The owner summary counts every non-`FREE` record whose bounded slot and
generation match a live process, including a wrong-kind record that the
later root comparison must reject. It is not an earlier replacement for
the lower whole-active audit. Records that do not name a live generation
are not used as array indices or assigned authority by this summary;
the existing consuming root and lower preflight boundaries still decide
their errors. This preserves orphan `STATE`, invalid-context ordering and
the lower transition's ownership/phase responsibility.

The complete user selection is accumulated during root collection.
Preparation passes it once to wired-set staging.
Completion requires every live user target to be `VM_WIRED`, then performs
the existing lower whole-active classification and irreversible commit.

Root collection failures normalize to `STATE`, retaining ADR-0109.
A valid root with a transferable/unwired user target remains `OWNERSHIP`.
Runtime, phase, object/ledger, context and lower-transition errors retain
their documented categories. The final ledger must cover mixed-corruption
precedence explicitly rather than normalize all failures.

Preparation and completion independently construct their protected plans.
No plan or validated-state bit survives across VM startup, an interrupt
restoration or another mutation.

## Work and storage ledger

Let `q` be selected frames, `s` published selection words, `l` the selected
slot's indexed frame count, `f` the process's owned private frames, `e` its
inspected PTE entries, `p` active frames, `g` active ranges, `r` rooted
processes, `c` process capacity and `t` thread capacity.

| Operation | Required envelope |
| --- | --- |
| Selection add/contains | Constant selected-word work |
| Selection validation/cleanup | `O(s + q)`, with `s <= q` for nonempty sets; no inactive-capacity term |
| Exact process count | `O(g + l)` with exact-generation filtering |
| Portable selected release | `O(g + q + s)` |
| Point page release | Fixed-height selection and bounded hardware-table pruning plus selected release |
| Process destruction | `O(e + (g + 1) * f + l)`; no unrelated frame-capacity or post-detach object audit |
| Wired preparation/completion | One `Vobj` plus `O(p + c + t + sum(e_i + (g + 1) * f_i))`; no `r*p` or `r*t` discovery term |
| Full diagnostics | Retain complete original coverage and add complete index validation; their full-capacity cost remains explicit |

Initialization is a separate one-shot full-storage operation. It is not
hidden inside selected mutation captures.
These are worst-case envelopes; average work is no greater.
Fixed arrays, direct links and per-operation cleanup incur no deferred
resizing or amortized scan debt. Existing page clearing contributes its
explicit 4,096-byte-per-cleared-frame cost.
Native evidence counts actual primitive work, including index, selection,
helper and cleanup work. It cannot infer work from elapsed time or sum
unlike counter dimensions.

At current bounds:

- owner links: `262144 * 8 = 2097152` bytes;
- process heads/counts: `64 * 8 = 512` bytes;
- total new ledger storage: 2,097,664 bytes;
- each selection: 49,168 bytes at the RV64 layout above;
- two selections replace 65,536 bytes of old bitmap storage and add
  32,800 bytes; and
- the 64-entry handoff summary occupies 1,024 automatic bytes.

No frame-capacity selection, index or reference-model object is automatic.
The ledger increment applies to every ledger instance, including test
snapshots; it is not merely the one production object's increase.
Do not increase stack, service-image, timeout or capacity limits to admit
the change. The linker/FDT exclusion must reserve the real enlarged BSS,
and all existing RAM configurations and complete stack gates must pass.

## Cohesive source ownership

No new production source file is required.

| Owner | Change |
| --- | --- |
| `include/micros/frame_ownership.h` | Exact index/selection representation and selected-set interfaces |
| `include/micros/frame_ownership_runtime.h` | Matching runtime selected-set declarations |
| `kernel/memory/frame_ownership_internal.h` | Narrow cross-module index/selection preflight/commit helpers |
| `kernel/memory/frame_ownership.c` | Portable index and selection primitives |
| `kernel/memory/frame_ownership_validation.c` | Selected process enumeration, complete/active index checks and coupled VM-set preflight |
| `kernel/memory/frame_ownership_bootstrap.c` | Index-coherent point mutation and selected release |
| `kernel/memory/frame_ownership_handoff.c` | Indexed wired-set staging and index-coherent phase commit |
| `kernel/memory/frame_ownership_vm_claim.c` | Selected unindexed-frame preflight |
| `kernel/memory/frame_ownership_vm_freeze.c` | Index insertion in existing non-failing freeze |
| `kernel/memory/frame_ownership_vm_release.c` | Unindexed rollback checks and indexed process release |
| `kernel/memory/bootstrap_memory.c` | Exact runtime selected-set adapters |
| `kernel/memory/user_address_space_internal.h` | Protected selected lease and bootstrap-plan private declarations |
| `kernel/memory/user_address_space_runtime.c` | Sole scratch storage and lease-mode lifetime |
| `kernel/memory/user_address_space_validation.c` | Separate bootstrap collection, retaining complete diagnosis |
| `kernel/memory/user_address_space_mutation.c` | One selected release path and existing PTE commit |
| `kernel/memory/user_address_space.c` | Selected-process destruction and final prevalidated detach |
| `kernel/memory/user_address_space_handoff.c` | Single process/thread/owner partition and wired-plan composition |

Retain every unrelated body, include, owner, public user-address-space
interface and callback contract. In particular,
`user_address_space_mapping.c` and the public user-address-space header
need no change; the existing kernel-visitor stack group is not relaxed.

## Prospective exact transport selection

The completed fixed-source census identifies 82 active keys with digest:

```text
7175cf5a99cb2a1bf711c09de4f6e295b35e7f43c6e62900ec8a293c4b5c1b10
```

Its 28-path direct closure contains the seventeen production subjects above
and eleven existing assertion/declaration owners. No shared parser,
collector, command planner or stack policy occurs in that direct closure.

The closed census has one historical whole-file key for
`frame_ownership_vm_freeze.c`, 45 current function clauses, fourteen fixed
native inputs, six definition sets, four headers, five skeleton clauses,
the exact handoff dependency and six MEM03 observation contracts.

The six changed observation contracts are:

```text
frame_ownership_validation.c/micros_frame_ownership_count_process
frame_ownership_bootstrap.c/collect_process_release_summary
frame_ownership_bootstrap.c/commit_process_release
frame_ownership_handoff.c/micros_frame_ownership_prepare_wired_process_user_set
frame_ownership_handoff.c/micros_frame_ownership_complete_handoff
frame_ownership_vm_freeze.c/micros_frame_ownership_vm_freeze_commit_prevalidated
```

Keep the other existing MEM03 and MEM04 observation contracts active.
New helper observations must be independently declared and complete in
every actual current native consumer; unknown helpers cannot disappear
from an older producer's measured closure.

The initial eighty-key inspection was refined before acceptance:
retain the unchanged MEM04 release inspector; include destruction's removed
bitmap helper, changed definition set and private-accessor include skeleton.
The complete record digest above, not the earlier provisional census,
binds the following selectors.

### Fixed function and clause selectors

Resolve the following table only against the fixed #232 inventory.
Include a current function clause only when its physical subject and
function match a row and the identity is still active.
Do not select a whole family or derive names from edited source.
Names in the right column are complete C function identifiers.

| Subject under `kernel/memory/` | Existing functions invalidating a selected clause |
| --- | --- |
| `frame_ownership.c` | None; only new index/selection helpers |
| `frame_ownership_validation.c` | `micros_frame_ownership_count_process`, `micros_frame_ownership_validate`, `micros_frame_ownership_validate_active`, `micros_frame_ownership_vm_process_set_preflight` |
| `frame_ownership_bootstrap.c` | `micros_frame_ownership_allocate`, `micros_frame_ownership_release`, `collect_process_release_summary`, `process_release_counts_are_valid`, `commit_process_release`, `micros_frame_ownership_release_process_set` |
| `frame_ownership_handoff.c` | `micros_frame_ownership_prepare_wired_process_user_set`, `micros_frame_ownership_complete_handoff` |
| `frame_ownership_vm_claim.c` | `micros_frame_ownership_vm_claim_preflight` |
| `frame_ownership_vm_freeze.c` | `micros_frame_ownership_vm_freeze_commit_prevalidated` |
| `frame_ownership_vm_release.c` | `micros_frame_ownership_vm_release_transferable_preflight`, `micros_frame_ownership_vm_release_process_commit_prevalidated` |
| `bootstrap_memory.c` | `micros_frame_ownership_runtime_prepare_wired_process_user_set`, `micros_frame_ownership_runtime_release_process_set` |
| `user_address_space.c` | `scratch_bitmap_contains`, `scratch_bitmap_add`, `copy_reachable_frames_to_release_set`, `clear_released_frames_or_panic`, `micros_user_address_space_destroy` |
| `user_address_space_mutation.c` | `clear_release_bitmap`, `scratch_bitmap_add`, `acquire_scratch`, `release_page_frames`, `micros_user_address_space_release_page` |
| `user_address_space_runtime.c` | `micros_user_address_space_acquire_scratch`, `micros_user_address_space_release_scratch` |
| `user_address_space_validation.c` | None; new bootstrap collection helpers retain complete diagnostic bodies |
| `user_address_space_handoff.c` | `scratch_bitmap_contains`, `scratch_bitmap_add`, `find_one_prepared_thread`, `process_has_no_owned_frames`, `reachable_user_targets_are_wired`, `select_reachable_process_user_targets`, `validate_handoff_processes`, `micros_user_address_space_prepare_wired_handoff`, `micros_user_address_space_complete_wired_handoff`, `acquire_scratch` |

This table has forty existing functions. The separately specified scalar
mode adaptation in `require_idle` changes no compared clause and is not
selected for retirement. The old handoff bitmap wrappers
and per-process target-scan helpers may be removed rather than retained
as unused functions; their current clauses are explicitly included.

Additionally select only:

- the single active historical key
  `tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/frame_ownership_vm_freeze.c/2`;
- active `native-input` and `dependent-file` records whose exact physical
  subject is one of the seventeen production paths above;
- active `header` records for the four headers in that source table;
- active `definitions` records for `frame_ownership.c`,
  `frame_ownership_validation.c`, `user_address_space.c`, `user_address_space_runtime.c`,
  `user_address_space_validation.c` and `user_address_space_handoff.c`;
- active `skeleton` records for `user_address_space.c`, `user_address_space_runtime.c`,
  `user_address_space_validation.c` and `user_address_space_handoff.c`; and
- the six exact MEM03 observation scopes already listed.

The function/clause selectors apply only to the existing
`memory-native-admission`, `selected-ownership-native-admission`,
`selected-address-space-native-admission` and
`selected-ownership-observation-admission` families.
They yield respectively seven, forty-three, twenty-five and six keys, plus
the one historical file key: exactly 82, with the fixed digest above.
All previous retired keys are excluded, not retired again.

### Frozen candidate owners and target dependency

The eleven non-production members of the direct closure are:

```text
tests/host/bootstrap_allocation_source.py
tests/host/kernel_layout_source.py
tests/host/kernel_mapping_source.py
tests/host/memory_native_source_bindings.py
tests/host/selected_address_space_native_source_bindings.py
tests/host/selected_address_space_source.py
tests/host/selected_ownership_native_source_bindings.py
tests/host/selected_ownership_observation_bindings.py
tests/host/selected_ownership_source.py
tools/generate_kernel_mapping_fixture.py
tools/generate_selected_ownership_fixture.py
```

The four literal binding modules remain byte-identical.
The seven assertion/generator adapters are independently specified and
frozen before production Red. Their original declarations, values and
unaffected predicates remain checked.

MEM05-C02 defines exactly these two fixed dependent target inputs:

```text
kernel/tests/memory/address_space_handoff_test_runtime.c
kernel/tests/memory/user_address_space_test_validation.c
```

Both historical whole-file keys are already retired by MEM04.
Do not claim either again. After the corrective design merge, the
candidate domain is the 28-path direct closure plus these two explicitly
declared dependent paths: thirty paths.
Every B01-B12/B14 unit input projection includes that same exact domain.
Both target adaptations are frozen before Red, not mutable runtime scope.

## Current versus historical evidence

### Retained native predicates and audit ledger

Keep all old literal declarations and family fingerprints unchanged.
Existing MEM03/MEM04 source guards continue to enforce every unretired
clause. Current retired clauses use only the verified task-specific source
admission and independently frozen current helper/observation contracts.
No stale original primitive offsets may be applied to a different body.

New helpers must be measured by every current producer that executes them.
Extend the exact current observation closure; do not omit a helper because
the old selector did not name it. Preserve all unaffected sites, owner/
channel attribution, primitive kinds, placement, passivity and old bounds.

MEM04's historical twenty-row residual ledger remains unchanged.
Under verified MEM05 current admission only, its effective retained set is
that fixed ledger minus the four owned calls from page release, destruction,
wired preparation and wired completion. Require exactly sixteen remaining
rows, no reintroduced migrated call and the separately declared new plan
boundaries. Missing, partial or unverified authority cannot select this
current view. Complete diagnostics and the other sixteen owners are not
silently skipped.

Metadata tests keep their fixed-reference record expectations.
Historical mutation probes execute the fixed comparison against its fixed
subject in an explicitly isolated historical policy context; they must not
pretend that this body is current or borrow current MEM05 retirement.
Current tests separately require the exact original record, valid replacement
authority and enforcement of every unretired comparison.
The new actual-source MEM05 tests own replaced behavior.
No behavioral oracle is marked passed merely because it did not execute.

### Legacy native fixtures

Preserve the original MEM01/MEM03/MEM04 case inventories, results, passive
controls and work bounds, subject only to the exact current-count exception
in [MEM05-C01](#mem05-c01-count-evidence-scope) after that correction is
separately Accepted and merged. Their existing baseline compile flags select
the original structure/interface in actual historical builds:

```text
MICROS_BOOTSTRAP_ALLOCATION_BASELINE
MICROS_SELECTED_OWNERSHIP_BASELINE
MICROS_SELECTED_ADDRESS_SPACE_BASELINE
```

Current fixture branches use the real new index and selection interfaces.
Construct selection inputs before lower-operation capture; MEM05's
high-level captures additionally measure real plan construction.
Update independent expected-state/index transitions rather than copying
corrected output into expected data.
Failure snapshots include all new state.

The regular ownership and VM-bootstrap native cases change their selection
arguments consistently. Original case order, seeds, model step counts and
target identities remain. No extra production compatibility API or
full-scan fallback is introduced.

The legacy raw bitmap may remain only as independent fixture input.
An adapter constructs its current indexed selection before capture;
neither that fixture scan nor historical ABI handling is production work.
Do not move any actual production planning or cleanup outside capture.

### MEM05-C01 count evidence scope

This correction is Accepted after clean independent design/correction
review under
[ADR-0115](../adr/0115-selected-count-generation-evidence-scope.md).
Its separate Acceptance and merge precede the fresh implementation.
It preserves the count algorithm specified above: validate the complete
selected slot chain, including different encoded generations, then publish
only the matching generation's count. No other slot is searched.

The fixed #232 current MEM03 fixture's `selected_count_guard` first
rejects an invalid handoff target for the exact generation, then queries
the same slot with the next encoded generation. That second query formerly
returned `OK` and zero because the old implementation skipped the target
check for a nonmatching generation. The original source and actual probe
are recorded in the
[fixed-source investigation](../research/minix-selected-release-and-wired-handoff.md#mem05-c01-same-slot-count-evidence-conflict).

After this corrective design merges, change only that second current
expectation in `tests/host/selected_ownership_test.c`:

| Scope | Same-slot other-generation query with the original invalid target |
| --- | --- |
| Fixed #232 implementation and its historical current fixture | `OK`, output zero |
| Actual MEM05 current fixture | `INVARIANT`, original `UINT64_MAX` output preserved |

Keep the first rejection, exact geometry/process/owner/target inputs,
generation increment, guard name `selected-process-count`, invocation order
and complete snapshot comparisons. Do not repair the target or substitute
another slot before the second negative query. Do not add a fallback,
normalize the result or remove this guard.

The historical guard is inside MEM03's non-baseline branch. A historical
check of it must use fixed #232 policy, fixture and interfaces in their
historical current stage; an old baseline-only build does not execute this
guard. Keep all old facts, literal expectations and recorded results.
No historical native rerun is added to the canonical five-product phase
solely to relabel that old result as current.

Add current-only cases to the existing MEM05 owner-index oracle:

- the same-slot invalid target rejects both the matching and nonmatching
  encoded-generation query, preserving the original output and complete
  ledger/index/allocator state;
- the same nonmatching query with a valid target succeeds with count zero;
  mixed encoded generations remain valid portable state; and
- an invalid target in another slot does not reject a valid selected-slot
  query with an independently known nonzero count. Complete validation
  still rejects the corrupted ledger, and neither query mutates state.

These are scope and failure controls, not changes to the common 128-row
matrix, its numeric work bounds or the required actual Red.
The existing immutable MEM03 fixture owner receives this one additional
pre-Red adaptation. No source path, retirement key, oracle owner or final
gate is added or removed. The archived scaffold and preliminary runs
provide no author event; restart from corrected main and freeze all inputs
before renewed real Red.

### MEM05-C02 handed-off corruption fixture coherence

This is the Accepted correction under
[ADR-0116](../adr/0116-handed-off-leaf-fixture-coherence.md).
Independent design review is clean; its separate merge precedes the fresh
implementation. It does not weaken complete index validation or change
an expected runtime error.

At fixed #232,
`address_space_handoff_test_runtime.c:reject_corrupt_frame_owners`
has two cases. Preserve the first foreign-process-owner case exactly.
Its second case directly replaces one wired owner with `VM_TRANSFERABLE`
and adjusts kind counts. It then requires a valid complete ownership
ledger and an invalid process leaf. That setup leaves MEM05's process
index stale and cannot satisfy the first assertion.

Replace only the second setup/cleanup with this existing-interface sequence:

1. Require SIE clear, the kernel root still active, and the selected
   grantee `TEST_DATA_ADDRESS` PTE present with its existing user read/write
   mapping. Resolve the distinct grantee `TEST_UNMAPPED_ADDRESS` PTE and
   require its word to be zero. Save both exact PTE words. Recheck that
   `stale_snapshot_frame` is a managed `FREE` frame; do not use a fallback
   frame or direct allocator mutation.
2. Encode a replacement PTE for that free physical frame through the
   existing Sv39 codec. Take the existing complete fixture snapshot before
   claiming the frame or changing the PTE.
3. Use `micros_frame_ownership_runtime_vm_claim_preflight` and its matching
   prevalidated commit to claim exactly that frame as ownerless
   `VM_TRANSFERABLE`. Do not modify the original wired owner, its links,
   its slot count or its kind count manually.
4. Move the original wired PTE into the empty test slot and clear its
   original slot. Require complete ownership and grantee address-space
   validation to return `OK`. The original wired frame remains reachable
   exactly once, so an orphan cannot become another rejection reason.
5. Install the prepared transferable PTE in the original data slot.
   Require `micros_frame_ownership_runtime_validate` to return `OK`, then
   require complete grantee address-space validation to return
   `OWNERSHIP`. These are independently specified results, not
   alternatives selected from observed output. Keep the kernel root
   active throughout; never enter the altered user root or dereference
   its temporary mapping.
6. Restore both exact original PTE words before returning from the diagnostic
   case, even if a diagnostic result was unexpected. Release the temporary
   frame through the existing runtime transferable-release
   preflight/commit. A failed setup or cleanup is a component failure,
   never a bypass or success-shaped repair.
7. Require the existing complete snapshot comparison to recover the
   original allocator, ownership/index, object, endpoint, grant,
   page-table and user-data state. Also require the original root and
   interrupt state. No temporary allocation may remain.

`stale_snapshot_frame` is established by the earlier
`wired_snapshot_tracks_authority` control, which allocates and releases
one retained frame. The recheck in step 1 is mandatory.
A valid free index is not below the allocator's lower-bound cursor.
VM claim does not advance that cursor, so its matching release restores
the original availability and cursor as well as the owner counts.

This can be one cohesive private helper in the existing target owner.
Reuse its existing fixture storage and bounded local claim/release plans;
no private-header, global capacity buffer or production API change is
needed. Preserve the outer `reject_corrupt_frame_owners` order and its
following malformed-PTE check.

The same frozen fixture must pass at fixed #232 and with the current
index. It uses no new index fields, layout-dependent branch or baseline
compatibility macro. It changes no target marker, record grammar,
workflow, deadline or original expected result.

The added owner is
`kernel/tests/memory/address_space_handoff_test_runtime.c`.
Its old whole-file transport is already retired by MEM04; its active
fast/PR/full classification identities remain unchanged. The direct
28-path/82-key selection stays fixed. Together with the already declared
user-address-space target, the effective candidate domain becomes thirty
paths, of which seventeen are runtime and thirteen are frozen before Red.
Bind all thirteen through `@mem05/frozen-candidates` and all thirty through
every affected-unit projection. The original C01 envelope is historical;
this is its sole additional path.

Archive the blocked implementation and restart from the separately merged
correction with a fresh base and renewed actual Red. The stage-8 failure
and archived native/target results provide no Green, retirement or final
acceptance. All existing native, QEMU, integration, stack and independent
review requirements remain.

### Truthful target witness evolution

The existing target fixture performs release inside the five
`MEM04_ACTIVATION` corruption cases and assumes no mutation. That is not
the new scoped-release contract. Do not fabricate the old error or run
a complete validator instead of the advertised release operation.

The frozen MEM05 target adaptation retains the two `MEM04_TABLE` version-one
rows and the original complete diagnostic/activation/destruction coverage.
It emits `MEM04_ACTIVATION version=2` with exactly:

```text
case diagnostic activation destroy before_satp after_satp process_satp
restored_satp state_unchanged
```

The five case IDs and their diagnostic/destruction results stay unchanged.
Activation still succeeds through the exact process root, restores the
kernel root afterward and changes no unrelated state.
The global-corruption release call is no longer misreported as a retained
version-one failure.

Before the release cases, add two isolated `MEM05_TABLE version=1` rows.
One poisons the selected process root with `0x1000`; the other poisons its
root-index-1 table pointer with that address. Invoke actual page release
and destruction while poisoned. Each must return `OWNERSHIP` without a
supervisor fault or mutation, proving the new consuming paths rather than
only reusing MEM04 lookup/activation's hardware evidence.

The exact row fields are:

```text
case release destroy before_satp after_satp poison_unchanged
state_unchanged output_unchanged cleanup_complete
```

Case IDs are 0 and 1. Both results are 11; `satp` is unchanged and the four
predicates are one. Snapshot valid tables before poisoning, observe that
the poison remains, restore only the injected value before snapshot
comparison, and then perform ordinary cleanup. Each isolated root needs
only one user page and fits the existing snapshot bounds.

Add isolated release cases after those retained checks. Each uses its own
test process/root and one allocated user page, and repairs only the injected corruption before complete
cleanup. Do not rebuild a damaged shared fixture with a fake success state.
The alias occupies a second, originally absent PTE in that same leaf table;
it needs no second user allocation. Together with the original fixture's
three pages, this fits its existing four-user-frame snapshot capacity.
Restore that alias to zero after the operation, before normal destruction.
The cases are:

| Case | Selected condition | Required current release result |
| --- | --- | --- |
| 0 | Leaf redirected to a foreign process frame | `OWNERSHIP` |
| 1 | Leaf redirected to a private table frame | `OWNERSHIP` |
| 2 | Malformed selected leaf | `PTE` |
| 3 | Selected leaf absent while another exact owned frame is orphaned | `NOT_MAPPED` |
| 4 | Valid selected leaf with a separate injected alias | `OK`, exact selected release only |

Each emits `# MEM05_RELEASE version=1` with exactly these fields, in order:

```text
case diagnostic release before_satp after_satp frame output
selected_before selected_after owner_before owner_after
state_unchanged effect_matches unrelated_unchanged data_unchanged cleanup_complete
```

Each value is one lowercase `0x` plus sixteen hexadecimal digits.
The five final predicates are Boolean. Duplicate/unknown fields, unknown
record versions, extra cases or reordered cases fail.
The independently specified expected results are:

- diagnosis is `PTE` (12) in case 2 and `OWNERSHIP` (11) otherwise;
- release is 11, 11, 12, 10 and 0 in case order;
- `satp` remains the distinct kernel root throughout;
- output remains `0xfeedfacefeedface` on rejection and equals `frame` on
  case-4 success;
- the original allocated owner is `PROCESS_USER` (5); it becomes `FREE`
  only on success;
- the selected PTE stays byte-identical on rejection, including zero in
  case 3, and becomes zero on case-4 success;
- `state_unchanged` is one on rejection and zero on success; and
- effect, unrelated bindings, user data and completed cleanup predicates
  are all one.

Failure comparison covers the complete actual ownership object, including
new index storage. Successful target comparison checks the independently
defined owner/bitmap/count/cursor/PTE delta, all original user data and
objects, and complete ledger validity. It does not claim byte identity for
the intentionally changed index. The independent native model separately
checks the complete new index and exact insertion/removal order; there is
no old-prefix-only substitute for that proof.

The fixture uses no new production compatibility macro and never accesses
new index fields when compiled against #232. Cleanup restores only its
injected PTE, destroys the isolated root and releases its process through
ordinary APIs. Resource equality excludes the intentionally consumed
temporary process generation, not leaked frames or live objects.

The actual fixed #232 kernel must emit, in order, two retained table rows,
five version-two activation rows, two `MEM05_TABLE` rows and release cases
0 through 3. Case 3 exhibits
its old `OWNERSHIP` result, unchanged state and completed cleanup before
the exact `user-address-space-test` component panic and nonzero workflow
exit. No case-4 row or success marker is accepted in that Red.
Green must reach all
five cases and complete the ordinary component gate.
The separate handoff workflow remains a passing preservation control in
both phases, including its original handed-off grant and missing-translation
boundaries.

The MEM05 parser verifies the truthful version-two retained fields and its
new release records. Historical MEM04 version-one parsing remains unchanged;
old bytes or outputs are not relabeled as current.

## Evidence owners and immutable inputs

The implementation adds exactly these eight evidence/tool owners:

```text
tests/host/selected_release_handoff_test.c
tests/host/selected_release_handoff_test.h
tests/host/selected_release_handoff_source.py
tests/host/selected_release_handoff_evidence.py
tests/host/selected_release_handoff_authority.py
tests/host/test_selected_release_handoff.py
tools/generate_selected_release_handoff_fixture.py
tools/check_selected_release_handoff.py
```

They are immutable within an actual author attempt. The source/authority
declarations register one eight-fact `selected-release-handoff-baseline`
family: design pin, three oracle descriptors, source roles, recipe,
state/work contract and source projection.
The complete current catalog becomes 5,387 facts in twenty-three families.
The fixed 5,379-record predecessor digest is:

```text
916c05728bdc471ddde62b47376f0911d17046bda950b3c0a5da266615b38a19
```

Preserve every complete predecessor record, twenty-two fingerprints, six
claims and 103 retirements. After actual Green and recertification only,
the new claim gives seven claims and 185 total retired keys.
Historical predecessor comparison first validates the complete current
inventory, then positively selects the fixed #232 family set. It is not
an exclusion filter for unknown future metadata.

After MEM05-C02's separate merge, the thirty candidate paths are exactly
the domain declared above.
Only its seventeen production subjects may change after Red.
Its eleven assertion/declaration owners and two dependent target fixtures
are frozen before Red even though the affected facts place them inside
the candidate domain. Bind those bytes additionally through the immutable
external input `@mem05/frozen-candidates`; changing one invalidates the
attempt rather than expanding mutable scope.

The following existing noncandidate inputs receive only their finite,
independently specified prerequisite adaptations before Red:

| Inputs | Exact adaptation |
| --- | --- |
| `CMakeLists.txt` | Five native products, one full-only slow CTest, exact generator dependencies and the existing depth-one source-proof pool |
| `tests/host/evidence_authority_inventory.py` | One eight-fact family/owner registration and closed declaration dispatch; no old value changes |
| `tests/host/evidence_authority_scope.py` | Seventh provider and its existing compiler/`nm` external-input shape; no new cache or weaker closing check |
| `tests/host/test_evidence_authority.py` | Complete-current totals/conservation, fixed historical mutation probes and separately verified current source authority |
| `tests/host/test_evidence_authority_scope.py` | Include the seventh provider in the existing distinct-tool/lifetime controls |
| `tests/host/test_python_host_runtime.py` | Exact new generator/checker/authority process owners with the existing isolated Python prefix |
| `tests/host/selected_ownership_evidence.py` | Current-fixture syntax uses independently declared complete MEM05 interfaces, rather than Red headers with only append-only MEM03 declarations |
| `tests/host/test_selected_ownership.py` | Pin the historical freeze observation regression's policy, subject and prior authority context; preserve current-source and negative controls separately |
| `tests/host/selected_address_space_evidence.py` | Verified current sixteen-row audit view; historical replay remains fixed |
| `tests/host/test_selected_address_space.py` | Fixed #228 scaffold policy/read inputs and current audit/source-adapter negatives use their correct, explicitly pinned scopes |
| `tests/host/test_kernel_address_space_source_layout.py` | Require the two actual indexed-selection objects and one checked mode in the same sole runtime owner under verified MEM05 admission |
| `tests/host/bootstrap_allocation_test.c` | Existing baseline/current selected-set call adaptation |
| `tests/host/selected_ownership_test.c` | Selected-set arguments and independent complete expected index transitions; the one current same-slot count expectation specified by MEM05-C01 after its separate design merge |
| `tests/host/selected_address_space_test.c` and `.h` | Independent allocation index expectations and complete current scratch-selection/mode snapshots |
| `tests/host/frame_ownership_test_fixture.c` and `frame_ownership_test_internal.h` | One context-owned selection beside the independent raw selection input |
| `tests/host/frame_ownership_test_bootstrap.c`, `frame_ownership_test_handoff.c`, `frame_ownership_test_model.c` and `frame_ownership_test_validation.c` | Matching selection calls, explicit malformed-header counterparts, and independent index membership/order in the existing model |
| `tests/host/vm_bootstrap_test_fixture.c` and `vm_bootstrap_test_internal.h` | One context-owned selected wired-set input; same VM snapshot cases and results |

No `tools/validation_inputs.py`, shared C parser, collector, transition
validator, QEMU runner, stack policy or native-manifest relaxation is needed.
The ordinary native manifest already closes every absolute product argument
of the new CTest. Old native targets remain declared and required.

The historical MEM04 scaffold fixture supplies its exact fixed #228 policy
and complete read-input set, not just the seven registration files that
were sufficient before this runtime change. First prove its original
positive predicate, then each intended stack/admission mutation.
This is historical test data, not relaxation of the production scaffold or
replacement of an actual current native source.

MEM03 has two additional retained consumers, not just its ordinary native
generator. Its current-fixture syntax check currently starts from Red
headers, appends MEM03 declarations and compiles without the baseline flag.
The new fixture's index fields and changed selection signatures require
complete independently declared current interfaces instead.
Construct the three ownership public/runtime/private headers from fixed
#232 bytes plus the pre-Red frozen MEM05 header changes, including structure
and signature replacements. Do not copy candidate headers into the
expectation or use append-only `header_additions()` as if it could replace
an existing declaration.

This syntax-only check validates the declared-interface envelope, not
retirement or current production-source authority. Keep its current branch,
launcher guards, strict compiler flags, diagnostics and fixture/tool
closing checks, and additionally bind the immutable interface declaration
inputs. Historical actual Red compilation still uses its real old headers
and `MICROS_SELECTED_OWNERSHIP_BASELINE=1`.

The retained freeze round-trip regression currently labels a historical
body `Stage.GREEN` because that body was unchanged through MEM03/MEM04.
Run that regression with fixed #232 MEM03 policy, its fixed freeze subject
and the six prior claims/103 retirement decisions in an explicitly isolated
historical context. There, Green means the historical MEM03 current stage,
not the future MEM05 body. Do not consult a live MEM05 checked view or
provisional retirement, disable the original predicate, or silently change
it to Red. Retain the five-channel positive inverse and missing/duplicate/
moved/foreign-work negative controls. Actual MEM05 current freeze/index
observations remain separately checked by every executing current producer.

The four literal declaration modules stay byte-identical.
Earlier source/recipe adapters acquire only finite forward admission and
inverse hooks for this task, never an unconditional success path.
In particular, old raw-word-count negative cases become explicit malformed
selection-header negatives in current fixtures; the historical branch still
tests the actual old word-count argument. Preserve their `ARGUMENT` outcome,
state atomicity, case identity, order and historical execution.

The existing ownership model keeps its 4,096 steps, seed and transition
schedule. Independently track insertion order using model-owned ordinals
and derive expected links from owner membership plus those ordinals;
do not use production index helpers as the expected model.
VM freeze, transfer, rollback and process release must update that model.
Full failure comparisons include all new bytes.

The final current source guard preserves every unselected original function,
signature, include, definition and skeleton clause. The scalar mode change
in `require_idle` retains its existing admitted signature/call set and
empty observation contract. Six old MEM03 observation contracts are replaced;
all others and all 33 MEM04 current-observation contracts remain active.
New helper names, signatures, callee sets and complete primitive bindings
are frozen independently before production edits. Do not keep an obsolete
production helper solely to satisfy an old comparison, or remove a still
valid comparison merely because the subject file changes.

## Actual-source native execution

The native source set is the exact fifty-six C members of fixed #232's
MEM04 `RECIPE_ADMISSION`, plus:

```text
kernel/memory/user_address_space_handoff.c
kernel/execution/user_execution_validation.c
```

The header set is all 156 ordinary `arch/`, `include/` and `kernel/` header
blobs in #232. Its sorted path-list digest is:

```text
f6d999c1bb6b9f5fc57e097211f88ac72beb51577b964e6e4a24a777f057e3c2
```

Thus the fixed input set is 214 physical C/header files, not a live glob.
Red uses their exact fixed-runtime bytes; Green uses actual current bytes,
admitting changes only in the declared seventeen production subjects.
Every other native input remains fixed.

Retain MEM04's finite architecture-only IRQ, CSR, physical-arena and runtime
binding approach. Extend the bootstrap subset only for actual wired-set
staging and its reached helpers. The execution-validation slice contains
the original status constants and exactly `derive_user_status`,
`status_is_valid_user_return`, `validate_user_mappings`,
`micros_user_execution_map_object_error`,
`micros_user_execution_resolve_thread_and_process` and
`micros_user_execution_validate_context`.
It does not replace context validation with a stub or compile unrelated
entry/trap machinery.

Native contexts use real process/thread attachment, valid executable PC and
writable SP mappings, and canonical RV64 user status.
Where requested, thread allocation order is the reverse of process order.
A process slot is never used as an assumed thread slot.
Hardware bindings supply only the platform environment, not owner,
selection, root, context or handoff answers.

Build these five products:

```text
selected_release_handoff_aggregate_test
selected_release_handoff_frames_test
selected_release_handoff_plans_test
selected_release_handoff_context_test
selected_release_handoff_ordinary_test
```

`frames`, `plans` and `context` partition the complete primitive sites.
Their exact per-owner/per-kind composition equals `aggregate`.
The ordinary product contains no collector calls or state and agrees on
every operation/result/state record. Retain the 27-owner/14-kind ABI.
Producer base is `0x4d454d3035000000`, with the existing 256 current-stage
offset convention and distinct channel labels.
The owner mask is 11900: existing MEM04 owners plus `KERNEL_EXECUTION`.

The frames channel observes owner records, allocator words and index
records/links. Owner-record accesses are `RECORD_VISIT`; index-array
accesses are `INDEX_PROBE`, with `LINK_TRAVERSE` additionally recording
consumed edges. The plans channel observes selected-word accesses, PTE
consumption/stores and actual clearing. The context channel accounts for
every remaining loop, geometry/count probe, process/thread access and
complete-validator entry in the actual reached closure.
Source owner attribution remains the executing file's owner, not the
owner of the array it happens to access.

Bind every loop and indexed owner/bitmap/index/selection/PTE access, including
new helpers and cleanup. Count an index edge when followed, not from a
caller estimate. Include bounded scalar record accesses and all bulk
copy/clear primitives. Unknown or newly unobserved work fails source
admission. Native binding and observation inverses recover the exact
actual input; no historical body substitutes for current execution.

The CTest is `selected-release-handoff`, with the existing
`LABELS slow` recipe form. It runs the five products once in `test-unit`.
The complete native tier becomes 45 CTests; the fast tier and its Python
membership do not change. Join `mem03_source_proof` for generation only,
not C compilation/linking.

### Fixed common operation matrix

Execute exactly 128 common rows in the following group order.
Counts and topology come from these fixture inputs, not from observed
production work. Fixture setup and independent postcondition checks are
outside capture; the complete advertised operation, including its plan
construction and cleanup, is inside.

| Group | Closed inputs | Rows |
| --- | --- | ---: |
| Portable selected release | Empty and last-index selections at `p = 1, 63, 64, 65, 4096, 262144`; `{0,63,64}` and `{0,1,63,64}` at `p = 65,4096,262144`; one bit in every word and the complete set at maximum capacity | 20 |
| Exact-generation count | At each `p = 65,4096,262144`: zero members; one matching member; 16 matching; 16 alternating generations; 64 matching; 64 alternating; 64 other-generation members | 21 |
| Runtime empty-process release | `p = 65,4096,262144`, with zero or one exact owned frame; release succeeds or remains blocked respectively | 6 |
| Selected page release | `p = 4096,262144`, `g = 1,8`, each with the five topology shapes below, plus one clean absent leaf per geometry | 24 |
| Process destruction | Same four geometries, each with empty root, one leaf, 32 dense leaves, 32 sparse leaves or 513 dense leaves | 20 |
| Wired preparation/completion | `p = 4096,16384`, `g = 1,8`; `(rooted,rootless) = (1,0),(1,63),(16,0),(64,0)`; both operations | 32 |
| Scoped release versus diagnosis | The five selected corruption cases specified for the target, at `p = 4096,g = 1` | 5 |

Release inputs publish word indices in deterministic first-use order and
alternate table/user owner classes where both are represented.
All selected records name one exact process generation.
Unselected portable frames use a nonprocess owner, so unrelated capacity
cannot silently become the selected list length.

The five point-release shapes are: retain the leaf table with one sibling;
the same with 32 extra siblings; prune one leaf table while another
middle-table entry remains; the same with 32 leaves in that other table;
and prune both private subordinate tables when the selected leaf is alone.
The last shape may have foreign-process mappings, not extra mappings in
the selected process that would invalidate its pruning expectation.
An absent-leaf case retains a valid nonempty root.

Dense destruction leaves fill consecutive slots; 32 sparse leaves occupy
32 distinct middle entries. Include all table frames in `f`.
Handoff processes have two real PC/SP leaves; reverse thread/process order
in the multi-process cases. Complete-phase setup stages the exact wired
plan before capture; preparation is separately captured in its own row.
The rootless cases use genuine live empty objects, not free slots.

Baseline and current execute all common rows. The native fixture reports
actual scoped return values rather than aborting before the independent
oracle can observe the old result. A missing new type/API or a compiler
failure is never Red. Only the test fixture's explicit baseline branch
adapts old selected-set arguments; no production compatibility switch exists.

### Independent numeric work bounds

Use the symbols from the storage/work ledger. For a destroyed root, `f`
includes its private tables and user frames. Let `z` be pages actually
cleared: zero for lower ownership/count/handoff; `q - 1` for successful
page release; `f` for destruction. Let
`e = 1025 + 512 * (private_table_count - 1)` per root, accounting for shared
root comparison and every slot in each subordinate table.
Let `F`, `E` and `S` be the sums of owned frames, those PTE budgets and
published words across a handoff's roots.

Bounds are per dimension, not a sum of unlike counters:

| Capture | Frames-channel record visits | Link traversals | Bitmap-word loads |
| --- | --- | --- | --- |
| Selected ownership release | `8*q + 16` | `8*q + 16` | `8*s + 4*q + 16` |
| Exact process count | `4*l + 16` | `4*l + 8` | `2*l + 8` |
| Runtime process release | Same memory bounds as count | Same as count | Same as count |
| Page release | `16*q + 32` | `8*q + 16` | `16*s + 8*q + 32` |
| Destruction | `16*f + 8*l + 32` | `8*(f+l) + 16` | `16*s + 8*(f+l) + 32` |
| Handoff | `16*p + 32*F + 64*r + 256` | `8*p + 8*F + 32` | `16*ceil(p/64) + 16*S + 8*F + 64` |

Bitmap-word loads include both disjoint frames/plans slices, not just
selection words. Add matching dimensions only.

Count actual owner-record reads separately from scalar count-array probes.
For selected release/count, context index probes are at most
`512*(q+s+l+1) + 16*g`, with irrelevant parameters zero.
For page release/destruction they are at most
`4096*z + 128*(g+1)*(f+l+q+1) + 8*e + 1024`.
For handoff they are at most
`32*p + 128*(g+1)*(F+1) + 8*E + 64*c + 16*t + 1024`.
All are independently conservative loop/field budgets, not measured
candidate constants.

Point release reads at most 1,027 PTEs: three selected entries and at most
two 512-entry pruning inspections. Destruction reads at most
`2*e + 16*f + 32`; handoff at most `2*E + 16*F + 64*r + 32`.
No lower ownership/count operation reads a PTE.

For non-handoff operations, cleared bytes are at most
`4096*z + 64*s + 512`; handoff permits
`64*S + 16*c + 512*(r+1)`. Clearing-unit counts do not exceed those byte
budgets. The per-frame page-clearing term is explicit, never used to hide
a global owner/bitmap scan in another dimension.

Active-geometry range visits are bounded by
`8*(g+1)*(proofs+1)`, with at most `8*q+8` selected-release proofs,
two count metadata proofs, `16*q+16` page-release proofs,
`16*(f+l)+16` destruction proofs and `16*(F+r)+16` handoff proofs.
No selected operation inspects inactive frame capacity.

The handoff's memory-owner context record visits, including its own
process/thread discovery, are bounded by `8*c + 2*t + 32*r + 256`.
The independent object/scheduler audit retains its own actual per-owner
counts and unchanged source control; it is not subtracted from an
inclusive measured parent or called constant-time.
Exactly one object audit remains. Page release and destruction invoke
no object/VM/TTY complete audit.

The new bootstrap collector records one complete selected-root collection:
zero for point release, one for destruction and `r` for handoff.
There is no call to an old complete-root/whole-ledger diagnostic on these
paths. Source caller census and separate frame-capacity measurements
distinguish that collector from the old full root/owner scan.

Actual #232 release bitmap loads exceed the selected bound even for an
empty set; process count exceeds the list bound with large unrelated
capacity; destruction exceeds its owner bound for a small root; the
many-root/rootless handoff cases exceed the fixed linear frame/thread
bounds. Require those named witnessed failures in Red, not merely one
unrelated failing assertion.

### Transition, corruption and preservation matrix

The current-only guard records have this closed set of scenario families.
Expand each listed mutation independently; do not combine faults except
in the explicitly paired precedence cases.
Every rejected mutation preserves complete state and outputs.

| Family | Required cases and result |
| --- | --- |
| Selection publication | Empty/add/re-add/clear, low and final bit, 64-bit boundary, all 4,096 words and full-capacity re-add; exact independent set and canonical cleanup |
| Selection shape | Null; word/frame count overflow; zero-count disagreement in both directions; fewer frames than words; out-of-range word; duplicate published word; missing/wrong inverse; zero published word; population mismatch; unmanaged high bit; contradictory unpublished local cell. Reject with the operation's declared argument/range result |
| Index mutation | Empty/head/middle/tail insertion/removal; adjacent selected removals in both publication orders; mixed generations and reused slots; exact independent order and membership |
| Index rejection | Head/count disagreement in both directions; out-of-range head/link; count above active bounds; nonzero head predecessor; self-link; broken previous/next reciprocity; foreign-slot/free neighbor; insufficient batch capacity/count. Reject before authoritative mutation |
| Complete index diagnosis | Disconnected cycle, omitted indexed orphan, reachable unindexed member, unindexed/unreachable owner, cross-slot duplicate, inactive previous/next and nonprocess links. Full validation rejects every case; selected behavior follows its explicitly different consumed scope |
| Lifecycle coherence | Bootstrap allocation/release/selected release; wired preservation; transferable unlink; VM claim/rollback remaining unindexed; VM freeze insertion and process-release removal; defensive allocation compensation restores exact cursor/index bytes |
| Root failure atomicity | Selected foreign/table/stale/free/malformed/cyclic path; complete-tree alias/orphan/shared-root disagreement; live-thread and active-root destroy rejection; each pruning outcome and untouched unrelated PTE/data |
| Handoff failure atomicity | Missing or invalid prepared context; rootless thread/endpoint/owner; malformed root, alias and indexed orphan returning `STATE`; otherwise valid transferable target returning `OWNERSHIP`; no target/phase mutation on failed preparation/completion |
| Precedence/lifetime | Phase versus null selection; null/header versus invalid handle; metadata versus malformed published prefix; active root versus absent leaf; selected malformed PTE versus active root; later invalid context before earlier root walk; earlier target mismatch before later malformed root; nested lease and wrong-mode/storage rejection |

For page release, inspect the selected path before classifying its structural
failure. If the result is `OK` or `NOT_MAPPED`, an active root yields
`STATE` before mutation/output. Thus active-plus-missing does not become
`NOT_MAPPED`, while consumed PTE/ownership errors retain their precedence.
Destruction completes selected tree/index preflight before its original
live-thread/inactive-root state check.

New-field guard records are explicitly current-only, not claimed executed
against the old layout. Common 128-row source/work/semantic evidence and
actual target Red still supply every retirement's discriminating failure.
Wrong-mode/storage release reaches the existing fatal scratch boundary,
not a recoverable error. A test-only panic interception may record that
exact reason and inspect static snapshots before resetting its fixture;
it must not return from or resume the production operation.
Such a guard is never a replacement Red.
Keep the original complete diagnostics, native/model cases and negative
controls. Semantic passivity compares all five products within each phase;
baseline/current comparison uses the independently declared common
semantics, not byte equality between different structure layouts.

## Oracle partition and actual phase commands

Three oracles cover the complete fixed 82-key set without overlap:

| Oracle and test method | Exact partition | Keys |
| --- | --- | ---: |
| `mem05-owner-index` / `SelectedReleaseHandoffOracles.test_owner_index` | Every selected key not assigned below | 41 |
| `mem05-selected-release` / `SelectedReleaseHandoffOracles.test_selected_release` | Subjects `user_address_space.c`, its private header, mutation, runtime and validation owners | 21 |
| `mem05-wired-handoff` / `SelectedReleaseHandoffOracles.test_wired_handoff` | Both frame/user handoff C subjects and the exact `micros_frame_ownership_runtime_prepare_wired_process_user_set` function clauses | 20 |

The methods reside in `tests.host.selected_release_handoff_evidence`.
Their properties are respectively actual selected-set/exact-generation
work plus all index transitions; selected page/destruction work plus
failure/target scope; and linear complete handoff planning plus retained
target/error behavior. Provenance binds fixed MINIX release/startup,
ADR-0020/0021/0040/0109/0114 adaptations and the independent state model.

Every oracle must fail for its stated fixed-runtime divergence and pass
for the correction. The selected-release oracle additionally requires the
actual witnessed QEMU case-3 failure. The handoff target is a passing
preservation control in both phases, never a manufactured Red.
All three require source/observation closure and retained invariant checks.

After separate design merge and implementation of these declared tools:

```bash
"${MICROS_PYTHON}" -E -S -B tools/check_selected_release_handoff.py --red
"${MICROS_PYTHON}" -E -S -B tools/check_selected_release_handoff.py --green
"${MICROS_PYTHON}" -E -S -B tools/check_selected_release_handoff.py --recertify
```

Freeze the exact merged design/base membership, all expected records,
native/target fixtures, helper/observation declarations, source/recipe
inverses and tool inputs before production edits. Red uses unchanged #232
production. Green and recertification compile actual current production.
An immutable-input correction invalidates the attempt; restart with fresh
Red rather than borrow earlier logs.

Use the existing checked view and ordered hash-chained transaction.
Its provisional retirement is limited to these 82 keys after actual Green.
Register the seventh provider through the existing mechanisms, including
its distinct compiler/`nm`, actual target tool/firmware and closing epoch
checks. No source proof may authorize its own construction.

The recertification table is:

| Unit | Canonical command |
| --- | --- |
| B01 | `cmake --workflow --preset test-qemu-frame-allocator` |
| B02 | `cmake --workflow --preset test-qemu-user-address-space` |
| B03 | `cmake --workflow --preset test-qemu-scheduler` |
| B04 | `cmake --workflow --preset test-qemu-ipc` |
| B05 | `cmake --workflow --preset test-qemu-address-space-handoff` |
| B06 | `cmake --workflow --preset test-qemu-bootstrap-launcher` |
| B07 | `cmake --workflow --preset test-qemu-vm-scratch-preemption` |
| B08 | `cmake --workflow --preset test-qemu-pm-service` |
| B09 | `cmake --workflow --preset test-qemu-tty` |
| B10 | `cmake --workflow --preset test-qemu-ramfs` |
| B11 | `cmake --workflow --preset test-qemu-vfs` |
| B12 | `cmake --workflow --preset test-qemu-elf-spawn` |
| B14 | The actual `--recertify` command above, including the complete current native/target oracle and retained host checks |

B14 uses the exact fixed #232 MEM04 `retained_checks` selection plus
`test_selected_release_handoff`, through the existing owned runner.
Run it after current oracle capture and before the remaining unit workflows,
so a source/recipe failure is found before expensive target recertification.
Publish unit records in B01-B12/B14 order.

Each exact native product and target command executes once per phase.
The same owned fresh target output supplies the matching B02/B05 rows;
do not rerun QEMU for a second transcript or use a different attempt's
output. Retain the successful-output reporting contract and surface captured
failure output before propagating a parser/harness failure.

Focused harness iteration, once the new module exists, uses:

```bash
"${MICROS_PYTHON}" -E -S -B tools/run_host_tests.py --test test_selected_release_handoff --test test_selected_ownership --test test_selected_address_space --test test_evidence_authority --test test_evidence_authority_scope --test test_python_host_runtime.PythonHostRuntimeTests.test_python_process_construction_has_one_reviewed_owner_inventory
```

## Final acceptance and phase barrier

No runtime/header, test, model, fixture, generator, scaffold or reusable
executable prototype is authored during this design.
Documentation/structure and all three diff checks precede independent
complete design review. Only clean review, Acceptance and separate design
merge activate the implementation.

Implementation remains one cohesive outcome. Its slices are independently
frozen evidence and genuine Red; coherent index/selection maintenance;
selected release/destruction; composed wired handoff; and complete current
recertification/acceptance. A first permanent implementation commit follows
the complete green transition and independent review, rather than committing
an incoherent partial ownership index.

No stack-admission file changes are authorized. Existing registered visitor
and fatal declarations remain unchanged; new functions are direct,
callback-free helpers in existing source owners.
After actual Green, inspect their ordinary/metadata proof before the full
producer. Unknown machine edges or rejected source admission are blockers,
not permission to raise budgets or expand a historical allowlist.

After recertification, stage the complete candidate and establish native
no-work readiness before the final producer:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier full --execute --producer
```

Require all fifty current commands, 45 native CTests and complete Python
discovery, forty QEMU workflows, four standalone images, the three exact
same-run reusable profiles and complete stack coverage: 44 configurations,
113 ELFs and ten observed scopes. B13 remains blocked at STACK13.
All original RAM, stack, service-image, capacity and timeout limits remain.
Fast work that can rebuild receipt-bound outputs precedes that producer.

Independent implementation review receives the exact clean staged
`HEAD`/tree, actual Red/Green/recertification and final evidence.
Publish only identical reviewed, committed, remote, merge-preview and
squash content with the required attribution.
Verify fresh journal-free committed and actual-main authority, then remove
only completed task refs while preserving purposeful archives.
Hosted CI, VM02 and later MINIX/POSIX findings remain separately owned.

## Implementation and evidence map

The effective design and implementation base is #235,
`8f3b19ccae21a06345d05415708cd2dc1db32e76`, tree
`1a7500dac64cf8605faa2b699c68b2caf6217f6b`.
Its independently captured base has 1,518 files. The fixed runtime remains
#232. The archived implementation is reference only and supplies no event
for this fresh attempt.

The [source declaration](../../tests/host/selected_release_handoff_source.py)
binds complete current interfaces, 23 helpers, 32 mutable functions and nine
removals before production edits. Its 55 current function contracts and
566-function/532-site fixed primitive inventory remain independent of
edited production. Byte-identical unselected functions retain their exact
fixed observation bindings; changed functions use the separately declared
current bindings. Array declarators are not runtime accesses.
The scratch owner includes the private selection-preflight declaration
directly rather than duplicating it or exposing another public API.

Seven finite source/generator inverses preserve predecessor bytes.
The recipe inverse admits the fixed predecessor in its original form or
after the already specified two-command host-runner projection; all other
predecessor bytes remain exact.
Verified per-key forwarding retains the other 114 MEM03 observation
contracts and all 33 MEM04 current-observation contracts. Every reached
new helper is measured, including comparison and completed-store
placement, with the original legacy channel ownership.
MEM03's frame components still contain only owner-record visits; added
index and link work remains in its inclusive aggregate, not those components.
The current audit view requires sixteen retained calls and four replacement
boundaries; the historical twenty-row declaration remains unchanged.

Historical mutation probes use fixed #232 source/policy and the six prior
claims/103 retirement decisions in an isolated context. Separate current
probes exercise actual source and unretired comparisons.
The separate synthetic all-active-keys control accepts its fixed original
before rejecting a mutation and never consults a live authority provider.
The ownership model derives index order independently; failure snapshots
include every new field and both complete scratch selections/mode.

The [native fixture](../../tests/host/selected_release_handoff_test.c)
has 128 common rows and 154 current-only guard records.
Its context-before-root guard isolates an off-path table in a complete
native host page before applying protection. This preserves the same
forbidden early walk on hosts whose protection page exceeds one RISC-V
frame, without protecting earlier context paths.
The [generator](../../tools/generate_selected_release_handoff_fixture.py)
uses the 214 fixed native inputs or admitted actual current sources.
Five products add one full-only slow CTest and retain the existing
depth-one generator pool; fast-tier membership is unchanged.

Both dependent target files are independently frozen before Red.
The existing handoff owner implements the C02 positive relocation,
transferable-leaf rejection and complete cleanup through existing
interfaces. The same fixture compiles against fixed and current layouts;
it has no new index-field dependency or compatibility macro.

The [phase controller](../../tools/check_selected_release_handoff.py) and
[authority owner](../../tests/host/selected_release_handoff_authority.py)
bind the exact staged membership, thirty candidate paths, thirteen frozen
candidate inputs and all remaining immutable source/tool inputs.
Each native product and target executes once per phase. B14 precedes
the other recertification workflows, with the same fresh target captures
supplying B02/B05. Preparation/syntax checks and archived results cannot
replace a genuine Red, Green or recertification event.
Publication still requires full current acceptance, independent review,
identical publication trees and journal-free authority.

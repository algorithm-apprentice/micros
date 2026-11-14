# VM Mapping-Plan Uniqueness

## Status, goal and boundary

This is the Accepted VM02 contract under
[ADR-0136](../adr/0136-vm-mapping-plan-uniqueness.md). VM01 #272 and MEM04
#228 are merged. Design #273 and independently reviewed implementation #274
are separately merged. Complete acceptance closes the three findings below;
the next load-plan correction remains separately owned by VM03.

Close only B02-F008, B02-F009 and B07-F004: replace all-process VM mapping
audits and pairwise uniqueness searches with selected-root/owner indexes
and bounded ordered key proofs. The
[fixed study](../research/minix-vm-mapping-plan-uniqueness.md) owns reference
provenance and current measurements.

Preserve the boot transport, digests, public message/fixed-object layouts,
process and page capacities, mapping-record layouts, original frame-choice
policy, exact permissions, token consumption, protected publication and
failure atomicity. A routing representation for the existing opaque load
token is the only token change. No PM PID, reservation/prepared/exit token,
mapping-generation issuer or endpoint format changes.

VM03 retains active ELF/stack-plan reconstruction and full-load subplan
lifetime. VM04 retains the allocator's current lowest-frame policy and its
bitmap/cache replacement. VM05 retains existing large movement primitives;
VM06 retains snapshot lifetime. General regions, COW, faults, recovery,
concurrency, new protocols and budget increases are not included.

## Authority and invariant preservation

The existing allocator bit and typed frame owner remain physical authority.
MEM05's maintained process-frame chains enumerate that authority; neither
their counts nor a temporary key index authorizes a frame by itself.
Kernel VM records remain slot-indexed mirrors of the protected mapping
transaction. Service records remain policy state, not kernel authority.

Cold handoff and explicit complete diagnostics retain global membership,
inactive tails, digest and corruption checks. Every legitimate owner/PTE
mutation continues to preserve their published invariants. Ordinary
operations certify their selected root, actual frame owners, consumed
record/index relations and prepared plan, not unvisited process roots.
Bind the requested process and root explicitly; a reused runtime view is
not a selected-process capability.

An unindexed, unreachable foreign owner or malformed unvisited root no
longer outranks a valid selected VM operation. It is not legalized: the
unchanged complete diagnostics must still reject it. A consumed wrong
generation, owner kind, allocation bit, selected PTE, index link, count or
record relationship fails before effects.

Retain the original decode/caller/phase/fixed-object/token boundaries.
No descriptive scratch/TTY inspector becomes command authority. No
validation-success stub can supply the new owner-directed decisions.

## One bounded ordered-key mechanism

### Reference family and storage

Use an independently implemented fixed-width specialization of the fixed
MINIX/NetBSD unstable MSD radix family. Process the most significant
remaining unsigned bit, partition in place into two bins, and process the
smaller nontrivial bin before the larger. There is no insertion-sort
fallback, recursion, heap allocation, comparator callback, string encoding
or arbitrary-width configuration.

The two required key widths are 32 and 64 bits. Use unsigned shifts, not
host byte order. Stop at singleton/empty partitions or the last bit;
never shift by the type width or underflow a bit position.

One private workspace per linked participating image contains:

- one 16-KiB union for at most 4,096 `uint32_t` or 2,048 `uint64_t` keys;
- at most thirteen bounded partition descriptors; and
- an exclusive guard and bounded lease/prefix bookkeeping.

The complete object is at most 16 KiB plus 256 bytes. Partition descriptors
are workspace storage, not a new user-stack array. A caller's borrowed
lease is at most 64 bytes. Do not copy records, boot objects or executable
buffers into the workspace.

The thirteen-descriptor bound follows from smaller-bin-first processing
and at most 4,096 keys. A one-sided partition advances its bit without
pushing an empty bin. Exceeding any declared bound fails closed; it never
falls back to recursion or quadratic search.

### Lifetime and failure

The workspace is temporary work storage, not a persistent membership
index, validation cache, epoch or allocator. A single nonblocking,
lock-free compare-exchange acquires an idle guard. An occupied or malformed
guard does not permit waiting, fallback or clearing another borrower's
storage.

Bounds dominate every key write, partition extent and count-derived
access. Initialize only the populated prefix, never read an unwritten
key, clear that prefix before releasing the guard, and leave the workspace
idle on every ordinary exit. Do not clear the full capacity for a small
plan. Publish caller outputs or mutate semantic state only after the
required proof and successful release.

Unavailable or inconsistent workspace state maps to the existing owner's
`STATE` or `INVARIANT` error, not `NO_SPACE`, absence or success. No new
syscall/result value is needed. Native evidence must cover failure followed
by successful reuse and rejection of an overlapping borrow.

Do not hold the workspace across a backend/IPC operation or another
workspace acquisition. Complete record validation releases its proof lease
before the retained lowest-frame reconstruction. That reconstruction and
load binding acquire their own read-only membership view and release it
before publication. Small local record checks complete before a containing
global proof acquires its workspace.

### Numeric proofs and first-error order

Boot mapping uniqueness collects checked compact frame indices, sorts the
32-bit keys and rejects adjacent duplicates. Complete runtime physical and
scalar proofs use 64-bit keys. Only initialized/admitted record prefixes
are collected; zero/inactive storage keeps its existing separate checks.

For a small physical plan, compute the earliest duplicate position without
changing the original validation order. Page-aligned physical identifiers
have twelve zero low bits; a populated key may use those bits for the
original ordinal, which is below 4,096. Unaligned inputs do not enter this
duplicate proof and retain their original per-item alignment rejection.
After numeric ordering, equal physical groups are in ordinal order; the
minimum second ordinal identifies the first preceding-input duplicate.

The caller still visits its original input order and tests that duplicate
only at the old duplicate-check boundary. Preserve table-first ordering
in the portable PTE plan, data-first ordering in the ownership plan, and
the original precedence of argument, range, owner, permission and PTE
errors. Do not return an error discovered in a later element early.
Validated nonoverlapping geometry makes physical equality equivalent to
managed-index equality; actual ownership plans still retain normalized
frame indices in their original order.

For `n` populated keys and fixed width `w <= 64`, all key reads,
comparisons, swaps, prefix initialization and cleanup have an explicit
`O(w*n)` upper bound. There is no expected-hash or amortized-resize
assumption and no term in inactive frame capacity.
Independently bound bit tests by `w*n`, adjacent equality tests by
`max(n-1, 0)`, and total populated-key accesses by `6*w*n + 4*n`.
Count partition work as well as equality; moving a comparison into a
different counter category cannot satisfy the bound.

## Selected geometry and frame plans

Reuse checked sorted active geometry. Physical-to-index and index-to-
physical conversion use binary range selection, checked subtraction and
the existing exact compact offsets. Preserve gap, end-boundary, unaligned,
unmanaged and output-canary behavior.

Admit geometry once at each owning plan boundary, then share the frame
owner's actual selected-row/index predicate for its elements. A narrow
metadata-prevalidated read returns the compact index and exact owner
together; it does not skip the selected bit/owner/target relation.
Its result is borrowed data valid only in the same protected, non-mutating
epoch, never a reusable authorization.

The ordinary public lookup retains its own metadata admission. Do not
change its caller contract, duplicate the owner predicate in the memory
adapter, introduce a broad validation-mode flag, or create a reverse
dependency from portable ownership into target address-space code.

Replace preceding-frame scans in transferable release, VM freeze/process
release and the portable four-table builder. Preserve original candidate
publication, exact-generation owner checks, all count/index preflights and
non-failing commit order. Normalization work is `O(r + n*log(r+1))` per
owning batch, plus the fixed-word duplicate proof; do not revalidate all
geometry inside the element loop.

## Root mutation without an all-process audit

### Scratch map and removal

Retain actual VM authority, active-root equality, the exact load/scratch
tuple and expected `FREE`/`VM_TRANSFERABLE` ownership.

Replace the full scratch lease and all-root preflight with the checked
idle guard and the existing MEM04 selected slot walk. It already resolves
the requested process explicitly and proves each private table's owner
before dereference. Preserve the exact absent/present leaf, fixed scratch
VA, expected PTE and table/index outputs.

Do not clear capacity-sized scratch bitmaps, enumerate process slots,
visit unrelated roots or rebuild a second path after selection.
Commit still binds scratch metadata, claims ownership, writes the PTE,
performs the per-address publication fence and finishes the scratch phase
in its existing order. Removal still clears/fences before completing the
portable scratch transition.

### Freeze and teardown

Use the existing checked process slot, VM record and MEM05 owner index.
Freeze requires the exact rootless reservation and no owned frames for
that generation. Teardown requires the exact inactive frozen/detached
root, no live thread/endpoint/profile and the unchanged token/generation
tuple.

The proposed application root has exactly four private tables and `q`
data pages. Select and validate those frames before any physical table
access. Prove their distinctness and the exact `q+4` selected owner set
using the existing indexed scratch selections and actual reciprocal owner
chain, not just its declared count. Bound a target VM chain by that expected
size before traversal. Preserve the portable owner's general
exact-generation counting contract.

For teardown and constructed-root certification, verify every slot in
the four known tables against the prepared plan and immutable kernel root,
including all unused user slots. Consume the ordered planned leaves with
cursors/direct indexes instead of searching every leaf for each hardware
slot. Work is `O(512 + q)`, not `O(512*q)` or an all-process/managed-frame
walk. Owner/index work is `O(q)` after geometry admission.

Freeze still constructs the tables and publishes their owners before
installing the child root. The memory-owned installer certifies the exact
tables/owner set before attachment, attaches the root, and verifies the
exact resulting process/root binding in the same SIE-clear epoch. There
is no PTE writer between that certificate and the binding postcondition.
VM remains LOADING until the existing later FROZEN publication; no PM/VM
reauthorization occurs in this valid transient.
The four-table certificate consumes at most `5*512 + q + 4` PTE/plan
entries, including the shared-kernel reference, and the selected chain is
bounded by `q+4` members. Retain all zero-hole checks within that bound.

Keep that complete sequence in the existing memory owner. Do not expose
a detached success flag or reusable certificate, add a new transaction
framework, or move a fallible PTE/owner check after root publication.
The obsolete generic VM all-root/constructed-root bridge may be retired
when its real callers use this sequence; the original complete public
diagnostic APIs remain.

Teardown still preflights the complete selected set, detaches the root,
clears data/tables, publishes the fence and only then releases owners.
An indexed orphan, missing planned frame, extra selected-generation owner,
incorrect link, nonzero unused PTE or substituted table fails before
destructive stores.

### Why foreign aliases need no recurring scan

Cold certification establishes the complete invariants. Thereafter
`FREE` frames are not published user mappings; scratch is the sole
ownerless transferable exception; each frozen data/table frame is tied to
one process generation; and every mapping constructor/release preserves
those relations in a protected commit. Shared supervisor direct-map
aliases are unchanged and are not user mapping authority.

The selected plan consumes the actual owner ledger and the complete
selected root, rather than a remembered successful audit. A separately
corrupted foreign PTE or omitted foreign owner remains a complete-
diagnostic error. Explicit paired controls must distinguish that from
corruption in the consumed selected set.

## Kernel physical membership

Ordinary scratch/freeze must not search every VM record to classify one
physical address. Extend their portable preflights with the actual
read-only ownership ledger, not a caller-supplied success Boolean.

After the original argument, state, token and local-shape checks, use the
selected physical owner to choose the possible record:

- a process-bound wired/table owner selects its exact slot and generation;
- a transferable owner selects the sole current load record; and
- other owner kinds identify no dynamic-record membership.

Check the chosen record's bounded header, exact generation and consumed
physical entries. A healthy existing scratch collision remains `STATE`;
a conflicting freeze table remains `ARGUMENT`. Other nonfree/unmanaged
ownership still fails at the real authority boundary before mutation.
A corrupt consumed ledger/index/record relationship is `INVARIANT`,
never absence or a reason to fall back to a whole-table scan.

The real frame-claim/freeze/release preflight remains mandatory; the
record selector does not authorize an owner transition. Native evidence
composes the actual ownership and VM implementations. Legacy dispatcher
mocks are not evidence of this authority.

## Load-token routing and exit selection

The kernel remains the only load-token issuer. Reserve the low six bits
as the process-slot selector, for the current 64-slot capacity. Select the
smallest unsigned token greater than `last_load_token` with those low
bits. If the current 64-value block has no such value, advance exactly one
block with an overflow check. Zero is never issued. Publish the new counter
only at the existing successful load-begin commit.

This is a routing hint, not a process handle or authority. Keep full token,
process generation, mapping generation, phase and caller checks. Slot
reuse must reject the entire old token. Exhausting representable tokens
for the requested slot retains the existing no-wrap exhaustion policy;
never recycle a token or add an alternate token format.

Kernel record and current-load admission require that routing bits match
their existing process slot. The service validates the same relationship
when binding the real kernel result and when admitting a record.
Kernel/VM share only this checked decoding rule; PM/VFS remain opaque
consumers. Process, endpoint, PID, mapping generation and exit token stay
distinct identities. This explicitly refines ADR-0050's outside-owner
opacity only for the VM service's load-record selector.

Service and kernel exit selection decode one slot, validate that bounded
header, compare the complete existing tuple and phase, then validate the
matched record. Mismatch remains `STATE`; malformed selected metadata
remains `INVARIANT`. Preserve each API's original zero-token and output
argument order. Do not search other slots on mismatch or demand validation
of a mismatched record's unconsumed entry tail.

No persistent token hash/tree, PID encoding, public wire/layout/version
change, compatibility decoder or migration is introduced.

## Complete VM and boot uniqueness

Complete APIs remain complete and read-only with respect to their inputs.
Replace only their pairwise uniqueness mechanism:

- boot import advances through the already validated address-space mapping
  prefixes, normalizes physical frames and checks ordered distinctness;
- complete kernel VM validation checks every admitted local record, then
  all represented data/table physical keys and nonzero mapping/exit
  identities; slot-bound load tokens are unique by construction and their
  checked slot relation;
- complete service validation checks every original record/entry, role,
  frame state, tail and count, then global physical/PID distinctness.
  Keep the existing per-record physical ordering.

Boot root/service/process identities retain exact uniqueness, using bounded
slot membership or ordered scalar keys rather than preceding-record
searches. Keep the canonical digest and all ADR-0077/0078 active, inactive,
minimum-six and coherent-invalid-count evidence.

Do not silently remove allocator reconstruction, active ELF/stack parsing
or fixed inactive-tail checks from the complete service validator. Their
disclosed costs remain outside this uniqueness correction.

## VM-service membership during construction

The original allocator policy remains lowest eligible frames, including
its current ignored-loading-record behavior. Build one temporary physical
membership view per read-only allocation/bind operation instead of
searching all records for every candidate/entry.

Collect only bounded entry prefixes. An aligned physical key can carry
its six-bit record slot in otherwise zero page-offset bits. Sort by
physical/slot, then reduce each physical group to its slot or an explicit
multiple-distinct-slots indication. A binary query answers whether any
record other than the ignored slot contains that exact physical address.
Retain the selected ignored-record check; same-record repetition and
foreign membership are not interchangeable.

Unaligned unvisited record values cannot equal a valid page candidate and
do not become ordinary validity errors merely through index construction.
They remain errors in complete diagnostics. A malformed consumed header
or selected frame still fails before access.

This view changes no frame-state byte, ownership, allocation order or
mapping record. It is discarded before mutation. Keep the original
selected-record availability/error behavior and the same output arrays.
One load-token/PID admission scan may remain where it checks a genuinely
unknown semantic PID; do not claim constant whole-load work.

## Work, storage and acceptance

Use `r` active ranges, `n` populated keys, `q` selected data pages, `p`
record slots and fixed word width `w <= 64`.

| Scope | Required bound |
| --- | --- |
| Scratch mapping/removal | Selected three-level path and actual selected owners; no process-root enumeration or frame-capacity bitmap clear |
| Freeze/teardown memory proof | `O(r + q*log(r+1) + 512 + q)` with exact selected owner/index coverage |
| Frame-plan distinctness | `O(w*q)` key work; original-order normalization/owner checks; no preceding-input equality scans |
| Boot mapping uniqueness | `O(r + a + g*log(r+1) + w*g)` plus the retained complete frame/digest/tail work |
| Complete runtime uniqueness | `O(p + w*n)` plus original local shape/geometry, input and frame validation |
| Service membership view | `O(p + w*n)` construction and `O(log(n+1))` membership queries; disclose retained allocator/ignored-record work |
| Exit record selection | One checked slot and the selected record, with no fallback scan |

These are component bounds, not a claim that the entire load, allocator or
complete validator is constant or linear in every input.

Add no semantic-state cache or new capacity-sized automatic object.
Only the bounded shared key workspace is new production backing storage.
Retain VM's 160 resident pages, all 4-KiB user/16-KiB kernel stacks, every
deadline and all 44/113/ten stack scopes. Actual compiler, linked-image and
observed evidence must establish fit; the old six-page headroom is not a
waiver. B13 remains blocked.

Implementation starts with actual unchanged-production Red at both native
and existing ELF-spawn work boundaries. Extend existing real-source
evidence rather than introducing another guest or transport. Preserve
VM01's 48 cases, all original native/model seeds/counts, complete corruption
controls, scratch preemption and post-freeze rollback/exit/resource proof.

Focused additions include:

- maximal valid boot mappings and runtime records, sparse/maximal geometry,
  adversarial key order, all-equal/high-bit/late duplicate keys, and an
  independent simple reference set;
- first-error controls combining an early owner/permission error with a
  later duplicate or malformed address, plus exact output/state canaries;
- every local/global duplicate class, stale token on slot reuse, mismatched
  routing bits, foreign generation/PID/exit tuple and checked token exhaustion;
- actual owner-directed collision admission, selected-frame/chain/PTE
  corruption, exact unused-table-slot checks and failure before dereference;
- valid selected success with preserved unvisited corruption, followed by
  explicit complete-diagnostic rejection;
- workspace overlap, partial-fill failure, cleanup/reuse and zero semantic
  input mutation; and
- actual linked workspace uniqueness, prefix/partition work, geometry and
  selected-root ownership, with no hidden old scan or success-stub substitute.

Extend the existing two complete producer snapshots. Ordinary VM/BOOT/PM
full audits remain zero. Process-root enumeration becomes zero; complete
selected four-table certificates remain visible rather than being renamed
away. The fixed current ELF-spawn scenario has three freezes and two
teardowns, hence five such certificates, plus its selected-path and owner
work. This explicitly replaces VM01's 199 retained-root-walk observation;
it does not delete memory certification.

Current source/recipe/primitive/stack qualification upkeep belongs in this
implementation under ADR-0123. Run focused Green, one sufficient final
planner gate and fresh independent review before the first permanent
implementation commit. Do not execute a second historical recertification
ceremony or relax finite callback/fatal/stack checks.

## Implementation slices and rejected alternatives

After the separate design merge, establish actual Red before functional
changes, then implement three coherent logical slices:

1. Add the bounded key proof and checked geometry/index reads; preserve
   complete diagnostics, original-order errors and output canaries.
2. Connect selected physical/root/owner admission and direct load-token
   selection through the real kernel/service owners, preserving every
   forward and rollback commit boundary.
3. Complete native/same-guest/current-artifact evidence, phase-specific
   stack/image accounting and directly dependent qualification updates.

The complete outcome receives fresh independent review before its first
permanent green commit. Slices do not authorize a broken intermediate
commit, parallel implementation or a change to the design barrier.

Reject another dense VM bitmap or input-sized automatic key array: the
fixed resource study rules them out. Reject record/buffer repacking and
early snapshot reclamation merely to make room: they expand this outcome.
Reject a hash table, persistent tree or new process-capacity index when
the maintained owner index, bounded temporary keys and token selector
already supply the required information. Reject deleting all-root calls
without exact selected PTE/owner-set proof. Reject using a workspace-busy
condition as absence, exhaustion or permission to fall back to old scans.

The main implementation risks are overlapping workspace lifetimes,
unchanged first-error precedence, the constructed-root publication
transient and the observed VM's small image/stack margin. Their explicit
negative tests and actual compiler/linked/target gates are acceptance
requirements, not permission to increase a limit.

## Ownership map

| Owner | Required change |
| --- | --- |
| `lib/vm` | `vm_keys.c` owns the fixed-key workspace/radix proofs; `vm_load_token.c` owns checked routing decode; boot validation borrows the same keys. No kernel-private dependency or comparator callbacks |
| `kernel/memory` | `micros_frame_ownership_lookup_prevalidated` returns the checked compact index/owner; existing plan/path owners preserve order; `micros_user_address_space_validate_vm_table_plan_locked` owns the four-table/owner certificate |
| `kernel/vm` | Issuer and slot-bound admission, owner-directed collision selection, local/global uniqueness and direct exit selection |
| `servers/vm` | `vm_frames.c` owns geometry and the temporary construction membership view; existing validation/load/exit owners consume it and the checked token selector |
| Existing native/ELF-spawn/stack evidence owners | Real-source semantic/work controls and actual composed/artifact/storage proof, with unchanged workflows and budgets |

This map authorizes no implementation until the separate design barrier.

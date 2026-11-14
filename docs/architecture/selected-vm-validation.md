# Selected VM Validation

## Status and scope

This Accepted VM01 contract owns B07-F001 under
[ADR-0135](../adr/0135-selected-vm-validation.md).
BOOT02 #268, MEM04 #228 and GRANT01 #259 are its merged prerequisites.
BOOT03 #270 is the completed preferred predecessor, not a new graph edge.
The independently reviewed design is merged as #271. Its separate
implementation is active; B07-F001 closes only after the required final
evidence, independent implementation review and merge.

The [fixed study](../research/minix-selected-vm-validation.md) owns current
source/reference provenance, caller counts and hidden audit paths.
Do not apply the original audit's nested-cost formulas to already
corrected memory/object/IPC/grant leaves without tracing current code.

## Preserved authority and representation

Keep all public boot, service and operation-12 layouts, command numbers,
errors, tokens, state fields, capacities and checked one-thread/one-hart
policy. VM remains the mapping/frame-policy owner; PM owns lifecycle;
VFS owns executable/request snapshots; the kernel owns actual objects,
frame-owner transitions, PTEs, fences and publication.

Retain the one service loader transaction, slot-indexed mapping records,
mutable local frame view and original initial-free bitmap. Retain the
kernel's separate handoff/load/mapping records and monotonic counters.
No second database, persistent selection, validation bit or index is added.

Selected success proves current consumed shape and relationships, not
historical immutability or all unvisited records. Legitimate constructors
and mutations must still preserve complete published invariants.
Complete diagnostics retain all original global comparisons and tails.

## Exact replacement and retained boundaries

| Boundary | Replaced direct complete calls |
| --- | ---: |
| Service load transitions in `vm_load.c` | 19 |
| Service exit transitions in `vm_exit.c` | 4 |
| Service message-instance admission | 1 |
| Portable kernel dynamic transitions | 9 |
| Memory scratch/freeze/teardown VM-state preflights | 3 |
| Captured dynamic common preflight: BOOT, VM, PM and VM-root validation | 4 |
| Total | 40 |

Keep service initialization and the public complete service-validator
wrapper. Keep kernel VM-ready/TTY bootstrap acceptance and complete APIs.
The sixteen other production calls to the complete kernel-state validator
remain at their current PM/cold/runtime diagnostic owners; the four other
complete VM-runtime caller rows remain. Pure-predicate factoring must not
remove those retained checks or change their result semantics.

Five existing VM memory-plan root-walk sites change only exception-binding
admission: two scratch all-root walks, freeze all-root walk, teardown
all-root walk and post-freeze single-root certification.
The walks themselves remain complete and retain their actual work.
Standard complete-root and complete scratch-authority APIs do not change.

## Service-side selected validation

### Header and active geometry

At the existing entry positions, establish:

- nonnull state, original initialization magic and reserved fields;
- nonnull boot storage disjoint from the state and a recognized loader phase;
- fixed boot ABI/version/size/capacity fields and existing header/summary
  agreement, with range checks independent of agreement;
- nonzero managed-frame/range counts within their existing capacities;
- bounded snapshot summary counts and exact service endpoint identity; and
- active managed ranges with aligned nonzero extents, checked arithmetic,
  sorted nonoverlap, contiguous compact frame indices and exact coverage.

This is `O(r)` active geometry, not a boot-object digest or inactive-tail
scan. Do not copy the immutable database or cache validation success.
Every count-derived pointer/index/mask/subtraction remains dominated by its
range guard, including the private frame/bitmap helpers.

### Current transaction and selected record

Use narrowly owned private preflight predicates in the existing validation
owner. Share genuine local facts with the complete validator; do not make
the complete validator selected or clone its algorithm.

For active input/loading/frozen phases, retain:

- reservation/load/PID/process/mapping identities appropriate to the phase;
- active executable/request size bounds and existing canonical ELF/stack
  plan checks before their bytes or page plan are consumed;
- image/data-page bounds, completed-page count, scratch index and filled
  state, with no count-derived access before validation;
- ordered distinct table/data addresses and the local unused-array tails;
- exact transaction/selected-record identity, phase, entry count, table
  roles, virtual pages, permissions and reserved fields; and
- every consumed frame's initial-free bit, physical translation and
  phase-appropriate current local state.

Input-copied state has no bound process/load token and uses eligible free
frames. Loading state has exactly the selected loading record, reserved
table frames, and the original free/transferable data-page frontier.
Frozen state retains the exact generation, completed pages, cleared
scratch and process-table/process-wired frame kinds.

Do not recompute the lowest-frame selection as an ordinary validation
step. Actual construction still uses the unchanged deterministic allocator;
the complete diagnostic retains its original selection comparison.
Inactive executable/request tails and unrelated frame/record storage
remain complete-diagnostic scope.

IDLE admission retains the fixed transaction-zero relationship. An exit
completion obtains its record from the checked action slot and exact
token/generation/PID tuple; do not add a persistent exit index or scan for
an unknown record merely to admit a phase.

### Operation and backend ordering

| Operation | Consumed scope before effects |
| --- | --- |
| Load prepare | Header/IDLE relationship, original source-shape/staged-alias rules, then existing ELF/stack preparation and actual frame selection. |
| Load bind | Input transaction, requested process/empty slot, existing identity collision search, complete selected frame eligibility before reservation stores. |
| Next page / mapped / fill / committed | Current loading transaction and selected record/page/scratch relation; preserve each original argument/phase check and local mirroring order. |
| Freeze plan / frozen | Complete current selected page/table set and local frontier before publication or frame-state stores. |
| Abort / committed | Exact current transaction/record/token relation; preflight every address/state needed by cleanup before its first store. |
| Exit begin / complete | Existing unique matching-record search and checked action selection; exact record/frame set before phase/output/free/clear effects. |

Service instance admission retains magic, bindings, endpoint identity and
reply-token checks at their original boundary. Decode/caller checks and
backend completeness retain their valid-input error order.
Before any accepted command invokes its first grant-copy or kernel backend,
admit all selected state that command will consume.

This includes `service_release_frozen`: kernel abort/commit occurs before
the local public transition, so validating only inside that later transition
would be too late. Wrong command/phase or malformed peer input must not
cause an external effect. Preserve the staged-input path and original
input-buffer cleanup on expected prepare failures.

Kernel success is mirrored into the still-owned local representation in
the existing order. A selected service preflight must not consult a kernel
state that has intentionally advanced before its local mirror.
No borrowed proof survives a backend call as reusable authority; subsequent
local transitions retain their own required checks.

Remove complete post-transition audits only after all fallible consumed
checks precede the existing stores. Keep explicit diagnostics separately.
Retain ADR-0087's preparation-only helper lifetime and no escaping automatic
storage; do not move large plans into the facade or static state.

## Kernel portable selected validation

The existing VM-core owner supplies private header, local-record and
current-load preflights. Keep null/alignment and error contracts at the
same public entry points.

Header admission reuses the actual scalar identity predicate and validates
phase/counter/load-header readiness before indexing. A selected record
retains exact slot/generation, token bounds, phase, local page/permission
shape, table/root/entry/stack relation and local unused tails.
Current-load admission binds its process/reservation/load token to that
record, the loading/frozen/prepared/activated phase and exact scratch tuple.

Global token/physical overlap comparisons, inactive records and unrelated
loading/frozen history remain diagnostic scope except when an existing
operation actually searches that domain.
Keep required scratch physical-membership, identity/collision and
exit-token/unique-match scans. Every record header/count needed by those
scans is checked before dereference; corruption cannot become absence,
ordinary exhaustion or a successful fallback.

Retain local record-shape and distinctness algorithms, including their
current selected-record quadratic work where present. VM02 owns replacing
those algorithms, not this invocation-boundary correction.

Load begin still consumes a token only at commit. Scratch map keeps bind
-> owner claim -> PTE/fence -> MAPPED. Scratch commit removes/fences before
publishing its entry. Freeze keeps construct -> owner commit -> root install
-> VM state commit -> retained output write. Prepared/unprepared/activated,
abort, exit-detach and release keep their original owners and order.

## Captured operation-12 admission

Keep the actual current sequence: command/register decode, current-thread
ownership, exact VM authorization, bootstrap/handoff phase gate, common
consumed admission, then the original handler.
Unknown/malformed commands still fail before authority acquisition.
Dynamic commands still require SEALED/HANDED_OFF; boot READY/TTY mapping
retain their separate cold gates.

At the old common-audit position, admit the actual static VM/PM bindings
and current consumed VM/PM state without complete BOOT/VM/PM/root audits.
Preserve required registry availability, exact generations, installed
profiles/operation bits, thread/context relations and physical root owners.
Do not narrow a profile before proving its range.

### Read-only sealed bootstrap selection

Add only a private `micros_bootstrap_control_select_sealed_service` at the
existing bootstrap control owner, reusing BOOT02's real selected relation.
It requires agreeing bounded counts/phases, zero index-zero entries,
sealed order/starting state and cleared controller authority.
It selects a non-controller READY/active/profile-installed/scheduler-
assigned service with zero deadline and the exact manifest/runtime/live
object relation.

This borrowed view is at most 64 bytes, publishes only on success and grants
no operation by itself. Existing RUNNING selectors/control operations keep
their signatures and phase/error rules. Retained launcher bindings or this
read-only view cannot restore bootstrap authority.

### PM-owned consumed relation

Provide a narrow VM-command PM preflight in the existing PM runtime/core
owners, factoring their real predicates rather than treating a state getter
or successful stub as authority. Preserve the existing matrix:

| Current relation | Required selected facts |
| --- | --- |
| Empty reservation / loading | Exact active token and process, empty object/root/endpoint/profile state, zero owned frames; bind a present loading VM transaction to that reservation. |
| Frozen reservation | Exact selected VM record/root, no execution objects, matching process and exact frame-owner count/set. |
| Prepared reservation | Exact prepared/reservation/load/mapping tokens, process/thread/context/scheduler/endpoint relationships and the selected frozen frame set. |
| Activated pending commit | Existing last-transaction/load/process/record-phase correspondence; do not invent a new activation or execution policy. |
| Stopped/detached exit | Existing PM exit tuple and selected object/root/frame relations, including the accepted detached-empty-record/root-zero/count-zero end state. |

Retain real selected object and owner checks, including the existing
process-owned frame enumeration/count. Do not call a whole object/VM audit
or scan every managed owner under this new entry.
Full PM runtime/reservation/object APIs keep their original complete checks
and result semantics for their existing PM callers. Private fact sharing
must preserve conditional checks and valid intermediate states.

## Memory-plan dependency without a new walker

The three existing VM memory preflights independently admit the authoritative
VM header/current transaction/selected record and exact requested process,
root, scratch/action and phase. A descriptive exception inspector alone is
not sufficient.

Add narrowly named private VM-plan root-validation entry points in the
existing memory validation owner. Factor and reuse the same complete
root/PTE/owner/reachability/exception-leaf checks; retain the all-process
walk, capacity scratch, physical absence and collision work at the five
listed sites. Those costs belong to VM02/VM03 and remain visible.

For these entries, obtain the existing selected committed VM/TTY exception
descriptions after real operation authority has been established.
Use them only to classify read-only existing leaves, not to authorize a
scratch, freeze, teardown or TTY command. Existing complete diagnostic root
APIs continue using their full authority getters.
No optional skip-validation flag, cloned walker or weaker PTE rule is added.

The post-install freeze check is special only in timing, not strength:
owners and the child root have been installed while VM/PM metadata still
reflects the pre-publication phase. Retain its complete constructed-memory
certificate through the prepared address plan and unchanged exception
tuples. Do not rerun PM/VM command admission there or move VM publication
earlier to make such a check pass.

Keep the same SIE-clear lifetime, scratch busy/cleanup rules, active-root
requirements, local fences and owner-specific error normalization.
Bind each requested process explicitly; all-root iteration's final runtime
view is not selected-process authority.

## Corruption, error and failure scope

Only current consumed header/geometry/transaction/record/frame and searched
prefixes are ordinary preconditions. Unrelated records, inactive storage,
historical transport immutability and global aggregate/uniqueness facts no
longer outrank otherwise valid selected operations.
Complete diagnostics still reject the original corrupt inputs.

This is not a promise to detect every coherent valid-looking alteration
without the complete proof. Selected success certifies its stated relation,
not every byte of the database. All legitimate construction/mutation paths
must maintain the full invariants.

Keep original argument/alignment, known caller, legal phase, fixed-object,
token, capacity and local failure ordering. Bound every selected count before
access. Preserve same-state wrong-phase results; consumed malformed metadata
remains invariant/fatal at its owning boundary, never absence or repair.
Keep the current service/core distinction between expected results and fatal
trusted-peer/kernel disagreement.

Returning failures preserve original caller outputs, plans, resource state
and bytes except the already specified prepare-failure input cleanup.
Do not normalize/restore a candidate's actual result in tests. Preserve
irreversible commits, one-shot tokens, instruction synchronization, no
transferable child leaf, and exact process-generation teardown.

## Work and storage limits

Service header work is `O(r)` active geometry. Selected transaction/record
work is bounded by the current record/page/request plan and its selected
frame translations, not managed-frame count or unrelated mapping count.
Keep active ELF/stack-plan checks; VM03 owns retaining that proof across
the load rather than repeating it per operation.

The actual allocator, record/token lookup, physical-membership searches,
full root walks and process-owner enumeration retain their current
complexities. Kernel local record shape may remain `O(e^2)`.
Do not call whole commands constant or silently claim VM02-VM06 closure.
In particular, constructive lowest-frame search remains unchanged, while
its complete-validator recomputation moves off ordinary paths.

New ordinary scopes have zero complete service VM, kernel VM, BOOT-runtime
and PM-runtime audit calls, including transitive calls through memory
exception getters. Retained memory walks and finite scalar/geometry
admission are measured separately, not mislabelled or removed.

Production layouts/capacities and persistent storage remain unchanged.
Use private bounded automatic predicates/views, not new capacity arrays,
caches, epochs or retained proof objects. New views are at most 64 bytes;
existing plans and temporary-lifetime separation remain.
All current 4-KiB user/16-KiB kernel stacks, 160-page VM image, aggregate
images and deadlines remain authoritative.

## Implementation acceptance

Use real current C and the existing complete VM fixtures. Keep every
existing CTest, protocol/transition case, persistent model seed/count,
capacity scenario and complete diagnostic assertion. Preserve the
ADR-0078 managed-count matrix and original six-frame fixture topology.

Actual unchanged-production native Red must first agree on bytes, state,
outputs and protocol effects, then expose complete-audit work. Extend the
existing observation engine with a focused ordinary/observed VM pair,
keeping a new full-tier CTest separate from the fast loop if needed.
Backends may supply bounded platform/transport effects, never successful
validator or authority substitutes.

Green includes:

- all kernel/service load, page, scratch, freeze, abort, prepared/activated,
  commit and exit phases, including malformed/scanned count guards;
- service rejection before the first backend effect and exact local/kernel
  mirroring after success;
- coherent count/geometry corruption, stale/foreign tokens and generations,
  selected frame/record/scratch/PTE corruption, and output/state canaries;
- separately observable selected success on unvisited corruption, followed
  by rejection from the unchanged complete diagnostic;
- complete preflight before cleanup stores, legitimate freeze intermediate
  state and unchanged hardware/fence/error boundaries; and
- zero ordinary VM audits, positive complete controls, source/site/callee
  closure and independently bounded consumed work over sparse/maximal
  geometry, record and page shapes.

Use the existing real ELF-spawn guest for composed kernel/service work
evidence. Cover all seven dynamic operation-12 classes and four VM service
classes using one existing collector per participating image and complete,
overflow-checked producer snapshots. Cold READY/TTY bootstrap checks are
outside the zero-audit capture, not silently deleted.
Keep the original rollback-after-freeze, successful child, exit/wait,
physical drain and resource witness.

Reuse the generated VM-context object descriptor and checked user readback
for the test-owned VM producer. Do not invent another transport or obtain
authority from raw user pointers. Bound added test storage to two pages in
the VM image and four kernel pages, still within every unchanged image
budget. Finish/authenticate service and stack evidence before terminal UART
seizure; validate exact report ownership and preserve strict original
serial ordering. No additional guest or timeout is added.

Preserve actual scratch preemption/stale-translation, cold handoff/TTY,
PM/service/failure, application/standalone-image and all complete stack
gates: 44 configurations, 113 ELFs, ten observed scopes, B13 blocked.
One sufficient current author plan and fresh independent review precede
the first permanent implementation commit and exact-tree publication.
Current native/source/recipe/consumer maintenance stays in this outcome
under ADR-0123, not another metadata prerequisite or historical replay.

## Current implementation and evidence owners

| Responsibility | Current owner |
| --- | --- |
| Shared boot-header facts and mapping-search headers | `lib/vm/vm_boot_internal.h` / `vm_boot_validate.c`, and `servers/vm/vm_core.c`; frame selection never depends upward on complete validation. |
| Selected service admission | `servers/vm/vm_validation.c`; `vm_admission.h` exposes only the two read-only checks needed by the adapter, not the general core-private interface. |
| Kernel VM, sealed service and PM relations | Existing `kernel/vm`, `kernel/boot/bootstrap_control_core.c` and `kernel/pm` owners; complete APIs remain separate. |
| VM memory certificates | The existing memory validation owner shares its complete tree/owner walk and root iterator between diagnostics and the narrowly named VM-plan entries. |
| Native semantic/work evidence | `selected_vm_test.c` / `selected_vm_evidence.py`: 48 ordinary/observed cases, unchanged original fixtures, full diagnostic controls and maximal frame/range/page/record shapes. Existing dispatcher-mock tests retain their old seam but are not VM01 authority evidence. |
| Same-guest producers and readback | `vm_command_work.c` and `selected_vm_work.c`, with the existing generated VM-context descriptor. Cross-image capture flags are integer fields with exact canonical checks. |
| Source, machine and storage qualification | `vm_command_evidence.py` checks actual linked observation calls, one collector per image, exact compiler/object/image ownership and the fixed two/four-page additions. Unrelated UART, copy and history work annotations remain disabled in this guest; their actual objects must have no work-observer references. Stack instrumentation and fatal-path qualification remain unchanged. |

The [build guide](../development/building.md) owns the focused native and
same-guest commands. The [stack contract](stack-bound-evidence.md#phase-bounded-vm-preparation)
retains separate preparation frames and actual return dominance without
reintroducing the superseded final complete audit.

The VM callback qualification admits only the callback-free mapping-header
guard at its exact position before `service_copy_inputs`. It retains the
original constructor, table, carrier and forwarded arguments, returns the
actual current source record, and proves the real guard call's normal-return
continuation dominates the copy call. Actual frames and direct callees are
fully counted; no other callback anchor or expected target is rebaselined.

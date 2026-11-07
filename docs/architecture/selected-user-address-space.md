# Selected User Address-Space Resolution

## Status and fixed baseline

This is the Accepted MEM04 contract under
[ADR-0107](../adr/0107-selected-user-address-space-validation.md), with the
Accepted [ADR-0109 dependency refinement](../adr/0109-handoff-preflight-error-ownership.md).
The [fixed-source study](../research/minix-selected-user-address-space.md)
records the MINIX baseline, current costs, controller call chains and
complete-validator census. Independent design review found no substantive
issue, and design #222 separately merged. The first uncommitted attempt is
archived without Green, retirement or implementation publication because
successful QEMU output did not expose the required witnesses. The separate
[MEM04-C01 prerequisite](qemu-runner-source-organization.md#mem04-c01-accepted-transcript-reporting)
has separately merged as design #223 and implementation #224.
That fresh restart is also archived without Green or retirement after
exposing an indirect handoff error dependency. The
[Accepted ADR-0109 correction](../adr/0109-handoff-preflight-error-ownership.md)
has completed independent design review and separately merged as #225.
The subsequent candidate completed actual Green and recertification, but
its final stack gate rejected the unrelated new public prototype.
[Accepted ADR-0110](../adr/0110-kernel-visitor-extension-admission.md) defines
the required bounded header and callback-free helper admission.
That candidate is archived. Corrective design #226 separately merged, and
the fresh candidate passed actual native/target Green and repaired current
visitor binding in an early ordinary/metadata stack check. The following
fatal source-unit guard rejected the ordinary TTY inspector declaration.
The candidate is archived; implementation remains blocked for the separate
[Accepted ADR-0111 correction](../adr/0111-fatal-tty-header-extension.md).
The dependency section below retains
the original 28-path affected-fact closure while adding one production path.

```text
runtime reference: f28377decc0d20533e05330f2f2211db3f230910
runtime tree:      4b56076479ac53092c4c83d9c9b52829b66139ba
MINIX reference:   4db99f4012570a577414fe2a43697b2f239b699e
MINIX tree:        80db89de58d5ecaf31efe48b4d1d98c7e285e81f
```

The runtime is byte-identical to MEM03 #219. EV01-C05 #220/#221 supplied
individual current-source identities without changing admission. Its 484
records and both older memory-native declarations remain unchanged.

## Scope and preserved invariants

Replace complete preflight only in lookup, translation, bootstrap
page-allocation preflight, process-root activation and scheduler
selected-thread root preparation. Keep their owning source modules and
share the checked path with directly dependent private-walker consumers.
No new production source file is required.

Preserve:

- `[0x40000000, 0x80000000)` and private root slot 1;
- distinct process/thread/endpoint identities and exact generations;
- the canonical allocator/ledger/object/kernel-map runtimes;
- exact `PROCESS_PAGE_TABLE` ownership for private tables;
- exact `PROCESS_USER` or `VM_WIRED` regular leaves according to phase;
- the unchanged Sv39 decoder, 4 KiB private leaves and permission reporting;
- the exact non-transitive VM scratch and TTY UART exceptions;
- immutable shared kernel subtrees, including MEM02's legal large leaves;
- existing mutation order, allocator search/release semantics and exact
  failed-operation compensation;
- prepared-address-space seals, PM/VM tokens and authority;
- complete diagnostics and their corruption cases;
- full ASID-zero activation fences and readback;
- the sole existing scratch owner and both unchanged bitmaps; and
- original native/target/integration/stack acceptance and budgets.

No point-query success is a certificate for every unconsumed entry in the
address space. Global validity remains an invariant of authoritative
construction and mutation, with the complete validator as its explicit
diagnostic.

## Selected path and ownership

Use one private, fixed-size path record, bounded to 128 bytes on RV64 and
the native 64-bit ABI. It may retain the three table identities/pointers,
three indices, decoded selected leaf, exact selected process identity and
presence/classification information. No field is a persistent cache or
cross-process capability.

The walker accepts the exact requested process handle and canonical runtime
inputs. It resolves that handle rather than relying on `runtime.process`,
`process_handle` or `process_root` left by an earlier all-root traversal.
The resolution, path consumption and any dependent stores stay within one
SIE-clear epoch.

For each present private level:

1. require a managed, allocated frame;
2. require exact `PROCESS_PAGE_TABLE` ownership for the requested generation;
3. reject equality with an already consumed ancestor frame; and
4. only then dereference its table and decode the selected entry.

There are at most three table identities and three scalar ancestor
comparisons. No whole-owner scan, global reachability bitmap, process-table
scan or recursive walk belongs in this operation.

Use the existing decoder unchanged. Only exact zero is absent. Upper
private levels must be tables, the final present entry must be a user leaf,
and existing reserved/permission/accessed/dirty rules remain exact.

For a regular present leaf, use the existing phase-aware owner rule and
exact process generation. For an exceptional slot, use the bounded binding
inspection below and the existing pure leaf classifiers. A page-table
frame, kernel frame, free frame, stale generation, foreign owner or
unbound transferable frame cannot become regular readable data.

Build the path result locally and publish it only on a successful selected
prefix examination. A valid absent prefix is explicit data, not a partially
initialized failure result. Lookup converts that result to `NOT_MAPPED`;
allocation may consume it as the location of a new mapping. A legacy
leaf-slot consumer still requires the leaf table to exist before receiving
a writable slot pointer.

Existing private-walker callers pass the requested handle explicitly.
They may already own the scratch lease and retain their complete preflight;
the shared lower walker must not reject its caller merely because that
caller's own lease is busy. The read-only idle guard belongs at the
scratch-free public operation boundary.

## Read-only scratch admission

Add a bounded check in the existing runtime owner, not a second busy flag.
At the same admission point as the current acquisition it checks:

- valid runtime-view/ledger arguments;
- existing scratch reentrancy, returning `BUSY`;
- SIE clear; and
- nonzero managed-frame count within the existing maximum.

It neither takes a lease nor clears a bitmap. Keep the existing acquisition,
release and diagnostic scratch behavior unchanged.

The five selected paths perform zero scratch-word clears and zero
reachability/release bitmap work. They must not call a full validator
indirectly through a convenience helper.

## Bounded committed-binding inspection

### Authority boundary

Add only these owning-component interfaces:

```c
enum micros_vm_handoff_error
micros_vm_handoff_inspect_scratch_binding(
    const struct micros_vm_handoff_state *state,
    struct micros_user_address_space_vm_scratch_authority *binding
);

enum micros_vm_scratch_authority_status
micros_vm_handoff_runtime_inspect_scratch_binding(
    struct micros_user_address_space_vm_scratch_authority *binding
);

enum micros_tty_device_authority_status
micros_tty_handoff_runtime_inspect_device_binding(
    const struct micros_kernel_objects *objects,
    struct micros_tty_device_authority *binding
);
```

The portable VM declaration may forward-declare the existing output type;
do not move its definition, existing enums or any wire/state layout.
The runtime wrappers read the sole existing owning controller state.
Their user-address-space callers hold SIE clear for the complete inspection
and use. They do not publish or retain a separate validated-state cache.

These interfaces describe an already committed mapping binding. They do
not authorize a VM/TTY command, install a PTE, transfer a frame, certify a
load plan, resolve a grant or validate the complete endpoint/bootstrap
registries. Only the new selected address-space validation helpers and
their evidence may consume them. Other command callers retain the original
complete authority interfaces.

When an initialized controller has no active exception, return its checked
process/root/fixed-slot identity with inactive state. For an uninitialized
controller return a zero binding. Errors leave the supplied output unchanged.
An inactive binding does not authorize an exceptional leaf.

### VM binding

Preserve the fixed scalar identity constraints of
`vm_handoff_identity_is_valid()`: magic, recognized phase, service/endpoint/
process/thread shape, image/profile/root/boot-information shape, summary
bounds and monotonic token relation.

Check the fixed-size load metadata required by the current phase:

- `PREPARED` retains zero last-load/last-mapping tokens and zero load
  transaction; handed-off `IDLE` retains a zero load transaction.
- `LOADING`/`FROZEN` retain nonzero reservation/load tokens, the existing
  `load_token <= last_load_token` bound, valid target handle distinct from
  the VM process, zero reserved field and a recognized scratch phase.
- `FROZEN` requires `CLEAR` and a zero scratch descriptor.
- `LOADING/CLEAR` requires a zero scratch descriptor.
- `LOADING/MAPPED` selects `mappings[load.process.slot]` only after the slot
  bound. Require `LOADING`, matching process/generation/load token, zero
  mapping-generation/exit/root/table-frame/entry/stack fields, and
  `entry_count < MICROS_APPLICATION_RESIDENT_PAGE_MAX`. Apply the existing
  constant-size mapping-entry validity predicate to the scratch descriptor.

Do not call `micros_vm_handoff_state_validate()`,
`mapping_slots_are_valid()`, `record_entries_are_loading_shape()` or
`micros_vm_handoff_loading_record_can_append()` from this path. Do not
scan mapping-entry prefixes/tails, all process slots or physical duplicates.
Those are publication/transaction invariants and explicit diagnostics,
not new authority minted by this read.

The mapped output retains the original load token, VM process/root, fixed
scratch virtual address and actual pending physical page. Its owner and
PTE agreement are checked by the selected address-space operation.
The existing complete scratch getter and all map/commit/freeze/abort/exit
preflights remain unchanged.

### TTY binding

Preserve the fixed controller identity and pure console/route state
checks. An active device binding additionally checks the selected live
process/thread relation, exact root and primary endpoint identity,
context attachment and applicable prepared/active profile/scheduler flags.

The exact active tuple is: both handles resolve live, the process root and
primary endpoint match the controller, the thread belongs to that process,
and its context is attached. For `MAPPED`, the process profile is zero,
the thread is exactly inactive and not scheduler-assigned. For `STARTING`
and `OWNED`, the process has the controller's TTY profile and the thread is
scheduler-assigned. The unchanged pure handoff validator checks console,
route and deadline-field shape. Bootstrap transition/deadline agreement,
endpoint-record state and immutable profile-table integrity remain in the
owning publication/complete-validation paths, not this descriptive read.

Inactive initialized bindings retain checked controller identity without
claiming a live dispatch authorization. The current process/root generation
must still match before an address-space caller treats the fixed slot as
its selected controller slot.

Do not search bootstrap binding/transition arrays, call endpoint/profile
resolvers that first audit complete registries, or call the complete TTY
runtime validator. Immutable profile policy and the sole binding were
established by the original publication path; this inspector does not
re-authorize that path.

Keep the exact current TTY process/root and console phase in the output.
The pure classifier still rejects UART physical aliases and a substituted
ordinary page at the active owner's reserved virtual slot. An unrelated
process may still use the same virtual address for a regular owned page.
Endpoint loss remains fatal under ADR-0047, not a recovery path introduced
by this inspection.

## Lookup and translation

Keep existing public signatures and the original prefix order:

1. null output arguments;
2. unavailable runtime and unrecognized ownership phase;
3. selected process generation/state and required root;
4. page/byte range and alignment;
5. read-only scratch admission;
6. VM binding, TTY binding and then active runtime metadata checks; and
7. the checked selected path and leaf classification.

A missing selected path is `NOT_MAPPED`, except that the selected active
controller's required slot is an ownership failure when missing.
Malformed consumed PTEs are `PTE`; wrong selected owner/role/generation or
ancestor alias is `OWNERSHIP`; invalid consumed runtime/binding metadata
is `INVARIANT`.

Regular success returns the same physical base and stored permissions
without `USER`. Translation adds the byte offset and returns exactly the
remaining bytes in the selected 4 KiB page. A valid exceptional leaf is
still rejected with `OWNERSHIP`, never exposed to IPC/grant/PM output.

Unrelated private corruption, other-root corruption and whole-registry
diagnostics no longer precede this selected result. In particular, an
absent requested leaf may return `NOT_MAPPED` while explicit full validation
reports unrelated `PTE` or `OWNERSHIP`. This is the deliberate ADR-0040
precedence refinement, not silent repair or acceptance of a globally
corrupt state.

Every returning failure preserves all output arguments. Read-only success
and failure preserve PTEs, owners, allocator state, objects and scratch.
Restore the caller's original interrupt status on every public exit.

## Bootstrap allocation

Keep allocation bootstrap-only and retain its original argument/process/
range/permission prefix, including the distinction between its deferred
missing-root check and lookup's earlier required-root check.

After idle admission, inspect the selected prefix and any existing leaf
before the active-root rejection. Then apply the original active-root and
occupied-slot ordering. Consume the retained path in the allocation plan;
do not repeat a blind path walk.

Retain the existing order of at most one middle-table, one leaf-table and
one user-frame allocation; page clearing/PTE construction; reverse-order
compensation; and success-only PTE/output commit.

Save the exact allocator pointer and cursor in the operation-local plan
after selected preflight and before the first allocation. After successful
reverse-order release of every unpublished frame, restore that exact cursor
with one prevalidated scalar store. The immutable allocator identity,
SIE-clear epoch and restored availability state establish its validity.
Unexpected compensation failure retains the existing fatal invariant path.

This closes the coupled cursor-compensation gap: with valid `c = 64` and
bits through 127 allocated, allocating frame 128 advances to 129 and ordinary
release rewinds only to 128, not 64. ADR-0021 requires the allocator to be
unchanged on page-allocation failure; ADR-0100 explicitly permits a cursor
behind allocated words. Do not weaken that invariant to mere logical
availability equality. Primitive allocation/release policy, public layouts
and lowest-free-frame results remain unchanged.

Failure evidence includes a cursor behind the first free word and
interruption after each possible unpublished allocation. It compares all
allocator metadata/bitmap, ledger, process/PTE and output state exactly.
Clearing unpublished free-frame contents remains the existing compensation
behavior, not a promise to restore unowned RAM bytes.

Allocation changes no unrelated mapping and cannot introduce a duplicate
private-frame alias from a valid initial state. Release and destruction
keep their existing full preflight and release-set behavior until MEM05.

## Root preparation and activation

The new public `micros_user_address_space_prepare_activation(process, root)`
requires a nonnull output and SIE clear on entry. It leaves output unchanged
on failure and does not change `satp`, IRQ state, PTEs, owners or scratch.
The caller may use the result only in the same SIE-clear epoch with no
intervening address-space mutation.

After the original runtime/phase/process/idle prefix, inspect VM and TTY
bindings in that order, then check active runtime metadata. Preparation:

1. retains the exact resolved process/root and checked active metadata;
2. validates the selected process root's table ownership;
3. validates the canonical kernel-root frame before consuming it;
4. requires canonical private slot 1 to be zero and compares the other
   511 shared entries;
5. checks the selected private-root descriptor and a present first child;
6. probes at most the two known VM/TTY slots whose controller identity
   matches this process/root; and
7. publishes the root only after every required check succeeds.

An active binding requires its exact exceptional leaf. An inactive known
binding permits an absent or regular owned leaf, but not a stale
transferable/MMIO leaf. Other private leaves are not traversed.
Shared-kernel disagreement is `INVARIANT`; consumed private owner/PTE
errors retain their classifications. No failed preflight writes `satp`.

Public activation saves/clears SIE, prepares, performs exactly the existing
architecture call/readback check and restores the original SIE state.
Kernel activation remains unchanged. Do not introduce a same-root shortcut
or a recovery rollback for a failed hardware readback.

The scheduler retains its selected thread/owner/context checks and obtains
the root through this preparation interface, publishing `selected_root`
only after success. Its later prevalidated-root hardware commit stays
unchanged and non-failing except for the existing fatal readback path.
Ordinary PC/SP and access-mode checks remain in their current consumers.

## Work, automatic storage and inherited costs

Write `h = 3`, `g/k` for MEM03's active geometry dimensions, and `s <= 2`
for known selected controller slots.

| Operation | Required shape |
| --- | --- |
| Present lookup/translation | One selected PTE per level, three distinct table proofs and one selected leaf classification |
| Absent lookup/translation | Stop at the absent level; no remaining table dereference or whole-root audit |
| Selected metadata | Fixed controller/load/record headers and selected object relations; no VM/endpoint/object/bootstrap-capacity scan |
| Root preparation | One fixed shared-root comparison plus at most `s` selected slot walks |
| Allocation preflight | One retained selected prefix; original allocator/page-initialization work remains separate |
| Complete diagnostics | Original complete traversal, orphan, exception and owner checks remain |

Point work is `O(h * (g + k))` with fixed selected metadata. Root preparation
adds the fixed 512-entry hardware-root work and at most `s` such paths.
No read/root-preparation term depends on managed-frame count, mapped
leaves, process/thread/endpoint capacity or bootstrap-service count.

The root comparison has one canonical private-slot read, 511 pairwise
shared comparisons and the process private-descriptor read. Count actual
PTE loads as well as comparison/owner events; do not hide those fixed costs
behind a universal constant-time claim.

Keep at most one 128-byte path record, one 128-byte binding bundle and the
existing runtime view live as owned operation data. Reuse fixed temporaries
for controller-slot probes. No VLA, recursion, capacity-sized automatic
storage, new kernel BSS or persistent cache is admitted.
Actual linked stack/ELF/observed bounds remain mandatory; these data-object
bounds are not a substitute for compiler evidence.

## Complete-audit ownership ledger

The fixed source has 25 production lexical caller rows. Remove exactly the
five named selected-operation rows. Preserve the remaining twenty rows
until their existing owning outcomes change them:

| Owner/boundary | Retained callers |
| --- | --- |
| Explicit diagnostics/enumeration mechanism | Public `validate`, `validate_all_process_roots_locked` |
| Existing construction/certification | Create, prepared-service validation, execution-context preparation, installed VM-freeze validation |
| MEM05 | Release, destroy, inventory, prepare/complete wired handoff |
| VM correction nodes | VM freeze all-root preflight, scratch map/remove all-root preflight, VM teardown all-root preflight |
| BOOT/VM/TTY command owners | Bootstrap runtime validation, TTY mapping prepare/validate, post-handoff VM runtime and TTY runtime preflights |

This ledger is not permission to keep those ordinary complete audits
forever. The broad B02-F005 end-state is reached by the existing correction
DAG's separately owned outcomes. MEM04 closes the named selected-operation
divergence, not the remaining release/handoff, VM transaction or whole-system
validation findings.

Also retain the original complete VM/TTY authorization getters and complete
diagnostic bodies. The old all-root operation may mutate its runtime view
while enumerating; new shared callers must not mistake that temporary
selection for their requested process.

## Handoff preflight error ownership

This section is Accepted under
[ADR-0109](../adr/0109-handoff-preflight-error-ownership.md).
It is a corrective design, not an implemented exception. Its
[observed source dependency](../research/minix-selected-user-address-space.md#indirect-handoff-preflight-error-dependence)
requires one additional owning source, not a restored global lookup audit.

The explicit dependent production path is:

```text
kernel/memory/user_address_space_handoff.c
```

In each of `micros_user_address_space_prepare_wired_handoff()` and
`micros_user_address_space_complete_wired_handoff()`, retain the existing
`micros_user_address_space_validate_locked()` call and its non-OK guard.
Inside that guard, set the returned error to `STATE` before the existing
scratch-release cleanup. Change only those two scalar assignments.

Handoff owns this normalization because it already reports an unusable
prepared execution state as `STATE`. Previously its context check inherited
complete-root rejection accidentally through the PC/SP lookups. The selected
lookup must not restore that hidden scan. The explicit complete validator
retains its own specific diagnostic errors and all rejection/ownership checks.

Preserve every other error and its order: runtime/phase, object/ledger
metadata, missing/invalid context, scratch admission, reachable-set
selection, required `VM_WIRED` targets and lower ownership preflight.
In particular, target mismatch still returns `OWNERSHIP`; this is not a
blanket error mapping. No PTE, owner, target, phase, scratch lifecycle or
non-failing commit changes. No validation call is added or moved, so the
25-to-20 complete-audit caller ledger and MEM05's later algorithm ownership
remain intact.

### Exact dependency and authority partition

After the separate corrective design merge, the source domain is seventeen production
files and twelve frozen candidate/assertion paths: 29 total. The original
33 active retirement keys still derive exactly their original 28-path
closure. Independently declare the handoff path as the sole
`dependent_production_paths` member, disjoint from that closure and included
in the seventeen production paths. Candidate-domain equality is exactly
the derived closure plus this one dependency, not an open extra-path allowance.

Include that exact dependency in every B01-B12/B14 unit input projection and
in the reference/current production snapshots. Exclude it from immutable
command/oracle inputs only by this explicit partition. The existing EV01
closure rules already union affected-fact paths with declared unit input
paths; do not change the shared rules.

The handoff file's whole-file historical key is already retired by MEM03.
Do not add it to MEM04's claim. Preserve every active MEM03 clause: the
definition set, signatures, CPP guards, exact callee sets, skeleton and all
other function bodies. The two functions are already mutable under their
fixed call sets; scalar normalization invalidates no additional active key.

The new MEM04 source guard must admit only the two exact scalar insertions
at the specified guards and recover the fixed original bodies after their
removal. All other handoff bytes remain checked. Freeze this inverse before
new production editing. Keep 33 keys, the 26/6/1 oracle partition, eight new
evidence owners, 4,774 predecessor facts, nineteen fingerprints, five prior
claims and seventy prior retirements unchanged.

### Required evidence

Retain the original strict handoff fixture unchanged, including `STATE` for
an orphan process frame/missing context and `OWNERSHIP` for a transferable
target. The earlier sixteen-subject candidate fails that orphan regression;
do not accept its stage-5 panic as the planned semantic Red.

Restart from the corrected design's current main and renew the original
native/target Red before production edits. The corrected Green must pass
those original handoff assertions and reach the independent stage-8 scoped
translation witness. Keep same-run target ownership, complete native
composition, B01-B12/B14 recertification and the full producer.

The existing MEM04 target-capture owner must show the already captured output
when capture/schema checking fails, then propagate failure without recording
Green. This diagnostic path must not execute a second guest or become a
replacement oracle. No additional evidence file or target fixture is needed.

## Exact source subjects and existing identities

The production subjects are the existing public/private user-address-space
headers; runtime, validation, mapping, activation, mutation, TTY and VM
scratch owners; scheduler context; VM core/runtime and their headers; and
TTY runtime validation/header. These are the sixteen original production
subjects. The one explicit handoff dependency above gives seventeen total.

Two existing target fixture owners receive frozen, exact expectation/
observation adaptations:

```text
kernel/tests/memory/user_address_space_test_validation.c
kernel/tests/memory/address_space_handoff_test_runtime.c
```

Do not change a complete diagnostic assertion merely to make selected
activation pass. The first fixture separates unchanged diagnostic/release/
destroy rejection from the newly bounded activation contract, observes
actual `satp`, and restores the original valid kernel root. The handed-off
fixture preserves explicit full diagnosis while checking scoped
`NOT_MAPPED` and unchanged output sentinels.

The prospective retirement set is exactly 33 existing active keys:

| Owner/kind | Exact scopes | Count |
| --- | --- | ---: |
| `kernel_layout_source.py:data-entry` | `KERNEL_FILE_FACTS/kernel/<name>/2` for the names below | 14 |
| `memory_native_source_bindings.py:native-source-admission` | `MEM02/native-input/<header>` for the five headers below | 5 |
| `selected_ownership_native_source_bindings.py:native-source-admission` | `MEM03/native-input/<header>` for those same five headers | 5 |
| `test_kernel_vm_handoff_source_layout.py:data-entry` | `STABLE_HEADER_HASHES/kernel/vm_handoff_core.h` | 1 |
| `test_tty_source_layout.py:data-entry` | `STABLE_HASHES/kernel/tty_handoff_runtime.h` | 1 |
| `test_scheduler_source_layout.py:function-body` | `FROZEN_FUNCTIONS/kernel/scheduler.c/validate_selected_thread` | 1 |
| `selected_ownership_native_source_bindings.py:native-source-admission` | `MEM03/native-input/kernel/vm/vm_handoff_core.c` | 1 |
| Same owner/kind | `MEM03/definitions/kernel/memory/user_address_space_validation.c` and `MEM03/definitions/kernel/vm/vm_handoff_runtime.c` | 2 |
| Same owner/kind | Three `MEM03/function/kernel/memory/user_address_space_vm_scratch.c/<name>` scopes below | 3 |

Every owner in the table has its `tests/host/` prefix. The fourteen logical
names are:

```text
user_address_space_internal.h
user_address_space_runtime.c
user_address_space_mapping.c
user_address_space_activation.c
user_address_space_mutation.c
user_address_space_tty.c
scheduler_context.c
vm_handoff_core.c
vm_handoff_core.h
vm_handoff_runtime.h
tty_handoff_runtime_validate.c
tty_handoff_runtime.h
user_address_space_test_validation.c
address_space_handoff_test_runtime.c
```

The five native headers are:

```text
include/micros/user_address_space.h
kernel/memory/user_address_space_internal.h
kernel/vm/vm_handoff_core.h
kernel/vm/vm_handoff_runtime.h
kernel/tty/tty_handoff_runtime.h
```

The three changed scratch-function clauses are:

```text
prepare_vm_scratch_mapping_leaf
prepare_vm_scratch_removal_leaf
micros_user_address_space_prepare_vm_scratch_removal
```

The mapping caller's existing MEM03 clause compares its signature/CPP/call
set, not raw argument text. Passing an explicit runtime/handle to the same
callee does not invalidate that clause. Keep it, both scratch-file
definition/skeleton clauses, the validation/runtime skeleton clauses and
all unchanged complete bodies active.

Put shared path declarations in the already owned private header. The
validation and VM-runtime C owners add only bounded functions, not new
file-scope storage, types or includes that would invalidate their retained
skeletons. A needed expansion beyond this exact scope is a design finding,
not permission to append retirement keys during implementation.

The direct existing 33-key closure has 28 paths: sixteen original production subjects,
two target fixtures and ten declaration/assertion owners. It excludes
shared C inspectors, operation-count policy, stack tooling and aggregate
historical reference records. A shared payload intersection, including a
stack authority anchor, is not automatically a byte identity to retire.

The Accepted [ADR-0110 extension](../adr/0110-kernel-visitor-extension-admission.md)
addresses that exact visitor-group boundary without retiring its authority
pin. It permits only the original public header or its one independently
specified preparation prototype for the registered kernel-visitor group.
It also declares exactly five callback-free mapping helper signatures and
requires exact origin and actual machine no-indirect-call evidence, rather
than rejecting them solely because the original source predates them.
All existing callback data, body anchors and source-machine obligations
stay unchanged.
ADR-0110's two existing owners remain bounded as specified there.
[Accepted ADR-0111](../adr/0111-fatal-tty-header-extension.md) adds only the
fatal-source predicate owner for the exact TTY declaration. After its
separate Acceptance/merge, the complete immutable stack-admission register is:

```text
tools/stack_bound_bindings.py
tools/stack_bound_fatal.py
tests/host/test_stack_bound_evidence.py
```

They are exact frozen registration/admission adapters, not mutable
production candidates. Seventeen production subjects, 29 candidate paths,
the original 33 keys and eight new evidence owners do not expand.
The exception changes no shared transition, path, source parser, collector,
receipt, stack budget or workflow.

Both MEM02 and MEM03 native consumers of changed headers remain consistent.
Use exact finite header extensions/inverses; a trailing-byte mutation of an
otherwise fixed input must still fail. The VM core native dependency admits
only the reviewed bounded helper/include addition with all original
functions retained. Do not run historical VM code in the current fixture.

## Independent evidence and phase sequence

Preserve all 4,774 old complete facts, nineteen fingerprints, five claims
and seventy retired keys. Independently freeze the 33 complete affected
records, role partition, actual-source closures, recipe inverses, expected
case matrix and work/semantic predicates before production changes.
The old declarations and values remain unchanged after retirement.

### Closed evidence owners

The implementation adds only these eight evidence/tool owners:

```text
tests/host/selected_address_space_test.c
tests/host/selected_address_space_test.h
tests/host/selected_address_space_source.py
tests/host/selected_address_space_evidence.py
tests/host/selected_address_space_authority.py
tests/host/test_selected_address_space.py
tools/generate_selected_address_space_fixture.py
tools/check_selected_address_space.py
```

The authority/source declarations contribute one eight-fact
`selected-address-space-baseline` family: the design pin, three independent
oracle descriptors, source roles, recipe admission, state/work contract and
source projection. Preserve all old fingerprints; the resulting catalog has
4,782 facts in twenty families. A completed transition adds one sixth claim
and only its 33 new retired keys.

The direct 28-path closure has sixteen original production subjects and
twelve frozen candidate/declaration/assertion paths. The explicit dependent
handoff source makes the complete candidate domain 29 paths, with seventeen
production subjects. The two target fixture
adaptations and ten existing assertion/declaration owners are frozen before
actual Red. Both native-binding literal modules remain byte-identical.
An owner being in the candidate closure is not permission to change it
during the correction attempt.

The new fixture/evidence owners, accepted design, unchanged shared
inspectors, compiler/runtime inputs and exact recipe/view registration
extensions are immutable within the attempt. Register the sixth checked
source-view provider through the existing mechanism, including the native
compiler and native `nm`, actual target tool/firmware and exact source epoch.
Do not alter shared transition, path, parser, collector or receipt rules.

Use the existing evidence transition and checked source view, extending
their closed task/provider registration rather than creating another
cache, collector, journal protocol or comparison engine. Frozen target
fixture adaptations and legacy source readers are immutable within an
attempt even where an affected identity places them in the candidate
closure. Only the declared production subjects may change during the
actual correction attempt.

Native evidence executes actual fixed/current C bodies. Architecture-only
IRQ/CSR/physical-memory bindings must be finite, explicitly declared and
invertible, with an ordinary control. They may not replace the changed
selection algorithm, perform ownership checks on its behalf or provide its
expected answers.

### Native execution and work bounds

Build four products over the same independently frozen cases:

```text
selected_address_space_aggregate_test
selected_address_space_path_test
selected_address_space_context_test
selected_address_space_ordinary_test
```

The path and context channels partition a closed primitive-site inventory;
the aggregate is their exact composition, not a separately estimated count.
The ordinary product contains no observation calls or state. All four
products must agree on every operation result and state snapshot.
Keep the existing 27-kind/14-scope collector ABI.

The path channel observes actual PTE consumption, selected frame proofs,
ancestor comparisons and planned PTE writes. The context channel observes
geometry work, shared-root comparisons, scratch writes and complete-audit
entry/capacity work, including VM/TTY/endpoint/object callees. A complete
audit cannot become invisible merely by moving it behind a getter.

Required upper bounds include:

- a present ordinary query consumes three PTEs and at most four selected
  table/leaf owner proofs;
- an absent path stops after one, two or three PTEs according to its level;
- a rejected table address is never dereferenced;
- a root preflight consumes at most 1,024 root-comparison/descriptor PTE
  reads plus three per selected controller slot, and at most three initial
  frame proofs plus four per slot;
- each selected operation inspects each VM/TTY binding at most once;
- every selected operation performs zero scratch-word clears and zero
  complete root/frame/object/endpoint/VM/TTY audits; and
- allocation adds its existing selected allocator/initialization/release
  work and one saved-cursor restoration after compensation to the retained
  prefix.

Bind active-geometry coefficients from the fixed MEM03 primitives; do not
hide `g/k` work in a capacity-insensitive constant. Measure low/high selected
addresses, absence and failure prefixes, not just successful first-page
lookup.

The independently declared corpus covers:

- both ownership phases, sparse/dense selected paths and independent
  managed-memory/geometry scaling;
- present paths and absence at each level, low/high user boundaries and
  byte offsets;
- bad selected table/leaf owner, generation, allocation and PTE shape;
- ancestor cycles and selected-before-dereference failure;
- active and inactive VM/TTY slots, required-leaf absence, wrong physical
  substitution, foreign virtual reuse and prohibited ordinary copying;
- invalid selected metadata versus unrelated registry/root corruption;
- original argument/phase/process/range/busy precedence and output/IRQ
  preservation;
- scoped query/activation results paired with unchanged complete diagnosis;
- every allocation-prefix shape, exhaustion/compensation boundary,
  occupied/active-root ordering and exact saved-cursor restoration; and
- scheduler preparation without new fallible post-commit work.

Work evidence counts actual PTE reads, selected owners/geometry, scratch
stores and complete-validator invocations, including hidden VM/TTY callees.
Use complementary native channels plus an ordinary control; their exact
primitive declarations and expectations are frozen before Red. Add one
full-only slow CTest, not duplicate behavioral workflows or fast-tier work.

The CTest is `selected-address-space`, marked with the existing bare
`LABELS slow` recipe form. It orchestrates the four products once. The full
native tier advances from 43 to 44 CTests; the fast tier stays at its current
37 CTests and closed Python membership.

The fourth memory fixture generator joins the existing depth-one
`mem03_source_proof` job pool. Do not rename the earlier pool, serialize C
compilation/linking, or run competing actual-root `git write-tree` proof
processes. Its exact source inputs and dependencies must be visible to the
existing native manifest before producer readiness.

### Oracle partition

Use three independently declared replacement oracles:

| Oracle | Exact replaced identities | Count |
| --- | --- | ---: |
| `mem04-selected-path` | All remaining keys in the 33-key set after the two partitions below | 26 |
| `mem04-root-activation` | Both public `user_address_space.h` native-input keys; the activation and scheduler-context `KERNEL_FILE_FACTS` keys; the frozen scheduler `validate_selected_thread` key; and the `user_address_space_test_validation.c` key | 6 |
| `mem04-scoped-diagnostic` | The `address_space_handoff_test_runtime.c` key | 1 |

The first oracle checks selected work, ownership/exception semantics and
allocation composition against independent fixtures/models. The second
checks bounded root preparation, scheduler use and actual activation
admission. The third pairs scoped missing-translation behavior with the
unchanged complete diagnosis and output boundary. Retained diagnostic and
source-closure requirements are mandatory across all three, not optional
because a key belongs to a different partition.

Require actual failing QEMU evidence for the changed activation and
handed-off missing-translation behavior. The fixed-runtime image must reach
and report the intended observed return/state mismatch; source/scaffold
rejection, a missing new API, a generic panic or timeout cannot stand in for
that evidence. Target observations remain test-fixture data, not production
work-counter hooks. Current target mode executes the same independently
fixed cases against actual current runtime.

Freeze the target adaptations before production edits. They emit bounded
machine-readable records of actual return values, `satp` observations and
existing snapshot/output comparisons; the independent host oracle owns the
expected result. Do not trust a candidate test's aggregate success marker
alone or derive the expected error from the runtime under test.

Use these existing workflows, without new target identities or timeouts:

```bash
cmake --workflow --preset test-qemu-user-address-space
cmake --workflow --preset test-qemu-address-space-handoff
```

The Accepted [ADR-0108](../adr/0108-qemu-accepted-transcript-reporting.md)
prerequisite supplies opt-in accepted diagnostic output through their existing
runner. After its separate implementation merge, the fresh MEM04 attempt
freezes those reporting inputs and actual new base membership before renewed
Red. Its host oracle consumes the same run's diagnostic witnesses on Green
and recertification; a TAP success alone remains insufficient. The reporting
prerequisite itself adds no runtime scope. ADR-0109 separately adds only the
explicit handoff dependency; the 33-key plan and eight evidence owners remain.

The user-address-space fixture retains the five existing corruption inputs:
foreign-owner leaf, table-as-leaf, malformed leaf, alias and unreachable
owner. Complete validation/release/destroy still reject them. Its activation
observation distinguishes that complete diagnosis from the new root-context
result and restores the original valid kernel root before later checks.
The existing ordered scenario returns to the kernel root before these
corruption cases; the adaptation checks that precondition rather than
assuming an arbitrary active root is interchangeable with it.
Add target negative observations for invalid selected root/intermediate
table addresses: the operation must return ownership failure without a
supervisor fault, PTE/output mutation or `satp` change. Prepare snapshots
before poisoning an address, record its unmodified poison afterward, and
restore the fixture before using any snapshot helper that walks tables.
The handed-off fixture separately observes complete `PTE` diagnosis and
scoped `NOT_MAPPED` for its unmapped request.

An owned, well-formed semantic witness followed by the existing component
failure path can establish Red. An unwitnessed panic or timeout cannot.
The baseline cases invoke existing interfaces, not the new preparation API.
Current-only API admission cases supplement Green and do not masquerade as
the discriminating baseline failure.

### Actual author commands

After implementation supplies the declared checker, use:

```bash
"${MICROS_PYTHON}" -E -S -B tools/check_selected_address_space.py --red
"${MICROS_PYTHON}" -E -S -B tools/check_selected_address_space.py --green
"${MICROS_PYTHON}" -E -S -B tools/check_selected_address_space.py --recertify
```

Red binds the unchanged fixed production subjects, immutable native cases
and target adaptations. All three oracles must demonstrate their intended
baseline failure. Green executes actual current sources and the same
expectations. Recertification repeats the complete current oracle/closure
evidence before recording each affected unit below.

| Unit | Canonical recertification command |
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
| B14 | The actual `--recertify` invocation above, including the complete current native/target oracle replay, retained MEM01/MEM02/MEM03 evidence, granular authority, source/recipe and Python-owner checks |

B14 uses the actual author transaction, not a recursive invocation or a
success-shaped replacement command. Its host coverage includes the new
evidence tests, existing authority/checked-view tests, all three earlier
memory-evidence tests, user-address-space/scheduler/VM/TTY source owners,
kernel directory ownership and the existing Python-owner/runner checks.

Within one phase, execute each exact native product/target command once.
The same fresh owned QEMU output may satisfy both its target-oracle replay
and the matching B02/B05 recertification row in that invocation. Do not
repeat a command just because it has two evidence consumers, and do not
substitute output from another phase, candidate or attempt.

Provisional admission is limited to the exact 33 keys and captured candidate.
Freeze every prior record, native input, output envelope, adaptation,
primitive site and tool input before actual Red. A changed immutable input
invalidates the attempt and requires renewed real execution; never rename
the immutable input to evade overlap rejection.

Every Green variant must preserve actual operation results/state, ordinary
observer absence, finite source/recipe inverses and complete diagnostics.
Recertify every B01-B12/B14 unit under the complete current source view.
Then run the current complete fresh producer:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier full --execute --producer
```

Retain all fifty workflow/document/diff commands, forty QEMU workflows,
four standalone images, 44 static configurations, 113 ELF outputs, ten
observed scopes and explicit blocked B13. The new native CTest extends the
complete native tier; the existing fast membership remains unchanged.
Do not increase a capacity, timeout or stack/image budget.

Establish an actual native no-work build before that final producer.
Independent review precedes the first permanent implementation commit.
Publication requires clean staged/worktree evidence, ordinary journal-free
committed authority, exact reviewed/committed/remote/preview/squash content
and protected cleanup of only the completed task refs.

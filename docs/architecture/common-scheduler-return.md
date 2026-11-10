# Common Scheduler Return

## Status, ownership and fixed inputs

This is the Accepted OBJ02 contract under
[ADR-0121](../adr/0121-bounded-common-scheduler-return.md), following the
[fixed-source study](../research/minix-common-scheduler-return.md).
Complete independent design review is clean.
Only design is active; implementation remains pending the separate design
merge.

```text
task:       OBJ02
owner:      B03
findings:   B03-F002, B03-F005
runtime:    34125516b60e9e0a9e5362ff2ee60555691adb99
tree:       1de53de9dcf5102dbef3e76d59b7f2fef28cee9e
MINIX:      4db99f4012570a577414fe2a43697b2f239b699e
```

OBJ01 #243 and MEM04 #228 are merged prerequisites.
Preserve independent process/thread/hart objects, full generations, public
scheduler operation signatures, current queue reachability, original policy,
serialization and all unchanged stack/image/time budgets.
The kernel-internal return-plan representation changes; this is not a user
ABI or an authority token.

## Goal and retained boundaries

Replace the selected-hart queue matrix, shifts, full projection and
capacity-wide apply with one bounded affected-state plan.
Keep the portable checked-commit API and the target's existing direct
prevalidated apply; do not claim the target currently recomputes that check.

A normal return may reuse its already validated outgoing context only when
that exact thread is selected again in the same protected epoch.
Keep selected root preparation and its complete ASID-zero hardware activation.
Different-thread selection, captured return, first start and idle wake still
require their own selected-context validation.

Do not change IPC completion's endpoint/object audit, buffer preparation,
message/result publication or pending-state cleanup. Those belong to IPC01.
Do not change diagnostic notification, queue admission, ordinary flag/policy
operations, minimal trap accounting, timer policy, mapping mutation or
process lifecycle.

## Compact portable plan

Retain the public plan's hart, outgoing, selected, action, `repair_preempted`
and `renew_quantum` observations. Replace the capacity-sized bitmaps and
head/tail/next projection with:

- at most two explicitly counted priority-queue descriptions;
- each queue's original and resulting head/tail handles;
- an original-list renewal prefix identified by its first handle and count;
- at most three counted next-link patches per queue, each naming one exact
  thread and its resulting next handle; and
- separate flags for renewal of the original current and its ready-link
  publication after preemption repair.

Queue descriptions are canonical, contain no duplicate priority, and link
patches name no duplicate destination. Unused descriptions and defined
fields are initialized deterministically; structure padding is not authority.
Keep the complete object at most 320 RV64 bytes, independent of process,
thread and hart capacity. No persistent plan, queue matrix, heap, VLA,
recursion, generation lease or generalized write-log framework is introduced.

`clear_return_candidate`, `publish_return_candidate` and
`preflight_return_plan` retain their existing source bodies.
Their complete fixed-size clear/publication operates on the new bounded
representation, not an array proportional to all thread slots.
Remove `plan_scratch` and its type. Remove the obsolete scratch
load/shift/head/tail/pick and whole-projection phases rather than leaving
unused compatibility helpers.

### Why two queues and three link patches suffice

Preemption repair can affect only the outgoing thread's priority.
A runnable current may first renew and move at its own priority, after which
selection chooses the highest nonempty queue. Synchronous positive-quantum
renewal never removes the last runnable member of that selected queue.
Further renewal therefore affects that same queue, not successively lower
priorities. At most the current priority and one selected priority differ.

Rotating a singly linked queue prefix preserves its interior links.
Only the old tail, the final rotated original-prefix member and a separately
reinserted current can require new next links. Equal actors are coalesced.
The planner records final links rather than a history of individual rotations.

A nonpreemptible zero-quantum member renews in place and terminates selection.
It may extend the renewal prefix without extending the rotated prefix.
A repaired current that was outside the original queue is handled separately;
it never masquerades as an original-list prefix member.
When the originally runnable current is renewed separately, its queue's
original renewal prefix starts after that head. No record belongs to both
the current-renew flag and a counted prefix.

## Preflight and virtual selection

Use the existing checked registry/hart/thread and queue-record leaves.
Keep argument/init/caller-generation precedence and the current accounting
restriction: return planning does not begin under THREAD or IDLE accounting.
No new complete registry or root audit belongs in this operation.

1. Resolve the exact hart/current relation and original current state.
2. Record preemption repair without mutating RTS, current, stacks or links.
   Clear only PREEMPTED logically. If another flag remains, do not insert
   the current. Otherwise insert it at its priority head for nonzero
   remaining quantum, or tail for zero.
3. Keep an originally runnable current, including a nonpreemptible current
   with a higher-priority ready peer. Otherwise inspect the priority prefix
   and select the first nonempty virtual head.
4. Validate every consumed queue endpoint, head/successor, exact generation,
   owner, assigned hart, priority, ready state and quantum before indexing
   or recording a store.
5. A selected positive or already renewed member terminates selection.
   For a zero-quantum member, record one renewal. A nonpreemptible member
   stays selected; a preemptible member moves logically to the tail and
   selection repeats.
6. Publish only after every consumed record and resulting patch is checked.
   Every failure preserves the registry and complete caller plan.

Virtual traversal consumes original links plus the finite current/prefix
overlay; it does not copy a queue, search a growing patch list or repeatedly
shift records. Bound unsuccessful traversal by thread capacity.
Validate the prospective successor before using it. Recognize a legitimate
wrap only after reaching the actual original tail with its null successor,
including the separately inserted current where applicable.
A consumed backedge or repeated malformed prefix cannot become an
already-renewed success. Renew each selected original member and the
separate current at most once.

Keep the fixed null rules. Generation zero still denotes a null head/next;
an empty tail retains its exact required null encoding.
Do not normalize unrelated null slots, clear unrelated links or inspect
other harts merely because the former whole projection did so.
Valid queue order, selected identity, flags, quantum values and ownership
remain equivalent; untouched permitted null encodings now remain untouched.

### Consumed-state scope

The old all-selected-hart thread scan existed because apply rewrote every
assigned record. The delta apply no longer consumes that state.
Remove that scan and its reschedule-to-assigned-existence check from this
ordinary operation. A reschedule request is consumed as a request to select
again, not proof that some unexamined assigned thread exists.

Complete object/scheduler diagnosis remains unchanged and still rejects
orphan assignment, unreachable ready state, cross-queue aliases and
reschedule without assignment.
Ordinary delta success does not certify those unconsumed properties.
Malformed consumed endpoints, membership, generations or successor links
remain `INVARIANT`; stale explicit caller handles retain `STALE`.

## Checked and prevalidated commit

`micros_hart_commit_user_return` retains its existing recoverable interface.
It constructs the bounded expected delta from current consumed state,
compares every defined plan field, and returns `STATE` without mutation
for a mismatch. Preserve the existing stale-tail regression: the consumed
queue's before-head/tail witness participates even when selection does not
move a link. Do not compare padding or trust a caller-supplied patch count
before its fixed bound is established.

The target consumes the exact prepared plan through
`micros_scheduler_apply_return_plan` in its existing serialized epoch.
No second target plan construction or persistent approval flag is added.

Apply performs only non-failing consumption of the prevalidated plan:

1. clear the exact outgoing PREEMPTED flag when required;
2. renew the separately recorded current once and each checked original
   prefix in order, preserving unrelated RTS reasons;
3. finish all renewal-prefix consumption before writing any next-link patch;
4. publish the repaired current's ready state, finite next-link patches and
   at most two queue head/tail pairs;
5. publish selected current, clear reschedule pending and select the exact
   selected or idle primary stack.

The renewal consumer may read the already checked original successor and
quantum for exactly the declared count. Those links remain unchanged until
all renewal reads finish. This is not a new search, validation, owner lookup
or rediscovery after stores begin. No callback, allocation or recoverable
operation occurs during apply.

## Retained outgoing-context proof

Use one caller-owned outgoing snapshot containing the exact thread handle
and 264-byte context. A null handle means no reusable outgoing proof.
It is temporary data under the caller's SIE-clear lifetime, not a seal,
cache, token, global current object or cross-process pointer authority.

Ordinary capture retains its current order: resolve current, copy the actual
frame, validate user status and PC/SP mappings, then store that same context.
Publish the local proof handle only after those checks succeed.
Captured mode still requires exact null current and cannot borrow an earlier
trap's proof.

Keep selected root preparation before completion preflight.
After that preflight, the selected-context loader may copy the retained
snapshot only when the full selected handle equals its validated outgoing
handle. Otherwise retain inspect followed by fresh context validation.
Start and idle pass no outgoing proof.

The reuse interval includes only the traced diagnostic notification,
portable planning, selected-root preparation and completion preflight.
They may change queue/notification state but do not replace saved contexts,
generations or mappings. The first context publication after validation
stores the retained image itself; later completion may patch only `a0`
in the exact selected image before its existing commit.

Do not move or repeat selected root preparation, introduce a same-root fence
shortcut, change a memory/execution API, or change the complete context
validator. Preserve all first-start accounting samples, timer failure
boundaries, invalid outgoing/selected diagnostics, idle pivot and frame
custody described by the fixed study.

## Work and storage evidence

Let `z` be newly renewed records and `p` the actually inspected priority
positions. The selected algorithm costs `O(p + z + 1)`, with
`p <= (z + 1)*k`; it has constant auxiliary storage.
Best ordinary keep-current work has no term in `T`, `pcap` or `hcap`.
Checked commit may repeat that bounded construction, not a whole queue
projection. Apply costs `O(z + 1)` and does not scan all slots or priorities.

Independent native evidence must count actual probes, copied/zeroed bytes,
shifted elements and commit stores. Require:

- no capacity-sized queue workspace or projection;
- zero queue-array shift work;
- plan clear/publication bounded by fixed storage, with no more than
  640 zeroed bytes and 320 copied bytes per constructor;
- no more than `128 + 64*z + 16*p` selected record/index probes;
- no more than `48 + 4*z` logical mutation stores in apply; and
- at most 320 bytes per plan, with no new persistent workspace.

Keep the first two bounds separate from probe totals; a permissive probe
ceiling cannot hide the original matrix shifts or full-plan movement.
Measure constructor, checked commit and prevalidated apply separately.
Cover empty, singleton, sparse and maximum queues, first/last priorities,
all-zero and mixed quanta, preemption with additional flags, nonpreemptible
selection, same/different priority rotation, permitted null encodings,
stale plans, consumed cycles and unselected corruption.
Compare complete logical state with an independent linked-queue model and
retain the original seeded models.

Real QEMU evidence measures actual context-validation call sites, not
elapsed time or estimated callee work. Require complete captured snapshots
for ordinary same-thread, ordinary different-thread and captured-selected
returns. Their current call counts are one, two and one respectively.
The fixed source performs two in the first case, providing an independent
discriminating target Red.

The existing scheduler component already exercises start, normal, captured
and idle completion. Keep all original scenarios, markers and deadlines.
Record only into bounded test-owned storage while a return is timing
sensitive. Publish the selected complete snapshots from the final scheduler
report after original semantic/cleanup checks; do not print after timer
preparation on an ordinary return or add a new early timer storm.

Use the shared test-only collector and existing dimensions with explicit
producer, capture and owner identities. Reject incomplete, duplicate,
overflowed, misattributed or missing captures. Discarded error/control
captures are not successful evidence. Test-only scope begin/end/cancel and
validation-site hooks have fixed source-stage locations and no ordinary
production effects. No counter ABI or generic harness change is authorized.

Begin the test-owned scope after entry argument/custody admission and before
context validation. End it immediately after successful context preflight,
before timer preparation; the idle result ends without selected validation.
Every recoverable early return after begin closes/cancels its capture rather
than leaking an active collector into the next call.
Start's injected timer failure occurs after context capture has closed.
Retain the first complete successful snapshot for each of the three named
required classes, with its actual mode, outgoing/selected handles and capture
label. Do not replace these snapshots with only a maximum, average or Boolean
summary. Unknown or invalid captures invalidate the target evidence.

The scheduler target may report the fixed-source work mismatch after its
original semantic/cleanup checks and fail its final test result.
Red accepts only the complete exact work witnesses and prescribed failure,
never a compiler error, timeout, generic panic or missing old behavior.
Current target acceptance still requires all original pass markers.
The fixed mismatch must reach the complete final work report before the
existing `MICROS_TEST_FAILURE scheduler-test` failure exit. Current success
emits that work report before the unchanged scheduler, IPC-return and
TTY-trap pass markers.

## Exact legacy evidence adaptation

Preserve all original fact values and fingerprints.
The original OBJ01 tables, including their baseline/current digests and
4,224/1,344-byte historical scope, remain fixed historical declarations.
Do not replace them with a digest derived from edited runtime.

The newly qualified current return contract explicitly changes only these
three old current result expectations from `INVARIANT` to success:

- `return-plan / nonlive-assigned-record`;
- `return-plan / reschedule-without-assignment`; and
- `return-plan / unreachable-assigned-ready`.

Keep the identical corrupt inputs, unchanged registry on planning, complete
diagnostic `INVARIANT`, case names/order and all other original semantic
results. The fixed #240/#243 expectations remain independently available;
do not move a mutation, normalize actual results or report a historical
implementation as current.

The current-only full-plan byte comparison for the unselected-hart queue
case becomes an independently specified logical-result check for the new
plan, retaining its exact intended selected thread and unrelated corruption.
Keep original failure/output preservation and ordinary/observed agreement.

### Plan representation observations

The original fixture's combined output digest includes every plan byte in
all cases. Keep that raw record unchanged. Add one complete, explicitly
versioned plan side-record per case, containing the same case/capture
identity, actual plan size, raw before/after plan digests, and an explicit
presence flag plus defined-field scheduler-effect digest for successful
return planning.

Use the same existing byte-digest algorithm for the raw plan contribution.
The non-plan part of the original XOR-composed output observation is exactly
the original combined digest XOR that measured plan digest.
Compare this non-plan portion across source stages; do not waive guard,
wake, selected-output or scalar-value comparisons because the plan ABI
changed. Retain the original raw combined and plan values in evidence.
Within each source stage, every rejection still requires complete byte-exact
plan/output preservation and ordinary/observed agreement.

For successful planning, apply the actual produced plan to an isolated
test-owned copy after measurement and observe its defined scheduler fields,
not C padding. Include flags, quantum/remaining values, membership/next,
all queue endpoints, current, reschedule and primary-stack state.
Check every other field remains unchanged.
Unchanged valid legacy cases compare this effect with their actual fixed
reference execution. The three newly scoped successes and unselected-hart
case additionally have independently specified expected effects: the two
empty-queue cases select idle, the coherent singleton selects thread zero,
and unrelated corruption remains unchanged and diagnosable.

The side-record has a closed schema and exact membership/order; missing,
duplicate, foreign or mismatched records reject.
It does not replace or rewrite an observed result, invent historical
1,344-byte output, change the original semantic table digests or weaken an
unchanged output check. Historical protocol fixtures remain explicitly
historical; current acceptance cannot silently fall back to a record without
the required representation observation.

These are finite newer-current adaptations, qualified by the complete OBJ02
source/authority decision. They do not alter the original literal tables,
change raw observations or grant permission through one unrelated retired
key. Isolated historical protocol tests retain explicit cold/fixed context.

The scheduler structure gate replaces only its owned exact workspace and
projection requirements with the independently declared zero-workspace and
delta-source invariants. Preserve every other family, source owner,
declaration, dependency, model, phase failure boundary and historical value.
The final-report insertion preserves the existing finish-phase minima;
its original unqueried whole-body record is not retired.

## Source-authority boundary

Select exactly 58 active records from fixed #243 with complete-record digest:

```text
7e80ec298823df18ec39c2805e7cc2a9035edae211778c4db4761b370e836fc2
```

The partition is 45 delta-work and thirteen context-reuse identities.
Keep all 6,327 predecessor records, twenty-seven fingerprints, eight claims
and 280 prior retirements. Successful renewed recertification adds one claim
and reaches 338 retirements.

The eight mutable runtime/header subjects are:

```text
include/micros/scheduler_core.h
kernel/scheduler/scheduler.c
kernel/scheduler/scheduler_context.c
kernel/scheduler/scheduler_core_return.c
kernel/scheduler/scheduler_core_return_plan.c
kernel/scheduler/scheduler_idle.c
kernel/scheduler/scheduler_return.c
kernel/scheduler/scheduler_runtime_internal.h
```

The direct domain also contains these twenty existing members:

```text
kernel/scheduler/scheduler_completion.c
kernel/scheduler/scheduler_interrupt.c
kernel/tests/scheduler/scheduler_test_finish.c
kernel/tests/scheduler/scheduler_test_support.c
tests/host/kernel_layout_source.py
tests/host/kernel_mapping_source.py
tests/host/memory_native_source_bindings.py
tests/host/object_hart_native_source_bindings.py
tests/host/object_hart_validation_source.py
tests/host/selected_address_space_native_source_bindings.py
tests/host/selected_address_space_source.py
tests/host/selected_ownership_native_source_bindings.py
tests/host/selected_ownership_source.py
tests/host/selected_release_handoff_native_source_bindings.py
tests/host/selected_release_handoff_source.py
tests/host/test_kernel_object_source_layout.py
tests/host/test_scheduler_source_layout.py
tools/generate_kernel_mapping_fixture.py
tools/generate_selected_ownership_fixture.py
tools/generate_selected_release_handoff_fixture.py
```

Keep completion, interrupt and scheduler-test-support source byte-identical.
Keep all five literal native-binding modules byte-identical.
Freeze the remaining twelve adapters, including the final-report owner,
before Red through exact fixed-source forward transformations and inverses.
They are not mutable algorithm subjects.

The selection consists of four directory byte identities; two stable public
plan-header hashes; seven portable original body identities, the original
selected-context loader and three target phase minima; three fixed native
inputs in each of MEM02/MEM03; four in each MEM04/MEM05, including the
selected-context function/file comparison; and 27 OBJ01 current clauses.

The OBJ01 clauses select its current public plan-header and return-commit
file inputs, the latter's three observation contracts, and the planner's
definition/skeleton plus changed/removed function and observation contracts.
Keep the planner's preflight/clear/publish clauses, baseline-only clauses,
already retired identities and unrelated native predicates.
Do not retire coarse source-projection records or shared parser/collector
authority.

The implementation must freeze the actual merged design/base membership,
all new current function/header/definition/skeleton/observation predicates,
individual keys and fingerprints before production Red.
Retained expectations derive from fixed #243; mutable/helper and observation
contracts derive from the Accepted algorithm and independent declarations,
never from edited runtime bodies or their measured output.
Native source closure remains the actual portable object/scheduler owners
and their required project headers; target scope additionally binds the
actual scheduler component, observer and linked source/artifact identities.
No final new-family count is guessed during design.

Existing noncandidate adaptations are limited to:

```text
CMakeLists.txt
tests/host/evidence_authority_inventory.py
tests/host/evidence_authority_scope.py
tests/host/test_evidence_authority.py
tests/host/test_evidence_authority_scope.py
tests/host/test_python_host_runtime.py
tests/host/object_hart_validation_evidence.py
tests/host/object_hart_validation_test.c
tests/host/test_object_hart_validation.py
tools/generate_object_hart_validation_fixture.py
```

They register the outcome, add the exact full-only native/test-only target
recipe and implement the finite current/historical observation adaptation.
The new immutable owners are:

```text
tests/host/common_scheduler_return_test.c
tests/host/common_scheduler_return_test.h
tests/host/common_scheduler_return_source.py
tests/host/common_scheduler_return_evidence.py
tests/host/common_scheduler_return_authority.py
tests/host/common_scheduler_return_native_source_bindings.py
tests/host/test_common_scheduler_return.py
tests/qemu/common_scheduler_return_work.c
tests/qemu/common_scheduler_return_work.h
tools/generate_common_scheduler_return_fixture.py
tools/check_common_scheduler_return.py
```

The target backend is compiled only into the existing scheduler component.
The collector remains private to that test evidence, not option-free kernel
or service images. Shared parsers, collector, planner and stack tooling remain
unchanged. All new roles and exact adapters are frozen before production Red.

## Acceptance and phase barrier

After separate design merge, canonical author phases are:

```bash
"${MICROS_PYTHON}" -E -S -B tools/check_common_scheduler_return.py --red
"${MICROS_PYTHON}" -E -S -B tools/check_common_scheduler_return.py --green
"${MICROS_PYTHON}" -E -S -B tools/check_common_scheduler_return.py --recertify
```

These are future interfaces, not existing commands in this design phase.
Red requires genuine fixed-source native delta-work and real QEMU
same-context-validation failures, with independent semantic controls.
Green uses actual current source. Any immutable-input change invalidates
the attempt; synthetic models and earlier task events supply no authority.

Recertification is one transaction over these thirteen units:

| Unit | Required target or transaction |
| --- | --- |
| B01 | `cmake --workflow --preset test-qemu-panic-trace` |
| B02 | `cmake --workflow --preset test-qemu-address-space-handoff` |
| B03 | `cmake --workflow --preset test-qemu-scheduler` |
| B04 | `cmake --workflow --preset test-qemu-ipc-syscall` |
| B05 | `cmake --workflow --preset test-qemu-grant-syscall` |
| B06 | `cmake --workflow --preset test-qemu-bootstrap-launcher` |
| B07 | `cmake --workflow --preset test-qemu-vm-handoff` |
| B08 | `cmake --workflow --preset test-qemu-pm-service` |
| B09 | `cmake --workflow --preset test-qemu-tty` |
| B10 | `cmake --workflow --preset test-qemu-ramfs` |
| B11 | `cmake --workflow --preset test-qemu-vfs` |
| B12 | `cmake --workflow --preset test-qemu-elf-spawn` |
| B14 | The actual task `--recertify` transaction with all new native/target oracles, original models, retained memory/object work and complete affected source/observation/recipe/authority/stack qualification |

Retain the complete affected-source B14 modules, including full
object/scheduler layout, panic and stack-source projection coverage.
Keep every old case once; add the new task evidence rather than narrowing
selection or moving orchestration into the fast tier.

Verify actual terminal checked-source-view construction before ordinary
generation consumes retirement. Prepare a native no-work state before the
final producer; generated compiler inputs are receipt inputs, not permission
to relax postflight equality.
Then run all fifty fresh author commands, forty QEMU workflows, four
standalone images, 44 static configurations, 113 ELFs, ten observed scopes,
blocked B13 and three same-run profiles under unchanged budgets.

Independent implementation review precedes the first permanent
completed-claim commit. Preserve exact reviewed/committed/remote/preview/
squash content, fresh journal-free committed/main authority and protected
completed-ref cleanup.
This design authors no executable test, model, fixture, observer, harness,
build scaffold or reusable prototype.

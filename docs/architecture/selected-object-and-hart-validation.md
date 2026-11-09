# Selected Object and Hart Validation

## Status, ownership and fixed inputs

This Accepted OBJ01 contract follows
[ADR-0119](../adr/0119-selected-object-and-hart-validation.md) and its
[fixed-source study](../research/minix-selected-object-and-hart-validation.md).
Complete independent design review is clean. Only design is active until
its separate merge; implementation remains pending.

```text
task:       OBJ01
owner:      B03
findings:   B03-F001, B03-F003, B03-F004
runtime:    9b880468192e61177ffe57ec69079fe398160a42
tree:       80b93b111b5a1387d812da87c539e56da042777b
MINIX:      4db99f4012570a577414fe2a43697b2f239b699e
```

MEM04 is the runtime prerequisite. MEM05 and EV01-C08/C09 are also merged.
Preserve distinct process/thread/hart objects, public layouts and APIs,
the checked one-thread/one-hart production policy, larger native policies,
all generation rules and the existing serialization boundary.

Portable callers serialize access. Target callers retain their existing
IRQ-save lifetimes; helpers do not change SIE or create an authority lease.

## Scope and retained boundaries

Replace exactly the sixteen scheduler entry audits in the fixed study.
Retain all twenty-six other direct calls and the internal complete
scheduler-to-object-base edge. Complete diagnostics continue to inspect
all slots, counts, ownership, queue membership, current uniqueness,
timer/trap state and stack disjointness. They never repair state.

Selected success means only that the operation's declared consumed
preconditions hold. It does not certify unselected slots, cross-queue
aliases, other stack owners or global count equality. Ordinary valid
states preserve every existing observable result and mutation order.
Complete diagnostics still reject unrelated corruption.

No public layout, endpoint, IPC ABI, root/context format, policy or
return-plan shape changes. Cold object allocation/release/detach and
stack installation retain their existing scans. OBJ02 still owns the
whole return projection and repeated context/root work. IPC01 and later
owners retain their endpoint/completion and outside audit boundaries.

## Selected metadata layers

The common scheduler owner gains only concrete selected preflight
helpers. It may include the existing object-private declarations to use
their stateless initialization, handle, context-zero and stack geometry
leaves. That is a downward dependency; object base validation never calls
scheduler selection or operation handlers.

| Layer | Required consumed checks |
| --- | --- |
| Registry header | Existing initialization magic; nonzero bounded thread/hart policies; stored live counts within representable/policy bounds, without recomputing global equality |
| Exact thread | Bounded live slot and full generation; exact live process owner; selected owner count/policy consistency; defined RTS bits; attached stack geometry or unchanged detached zero-state rules |
| Scheduled thread | Exact live assigned hart; attached context; bounded priority; positive quantum and remaining not above quantum; runnable/ready-bit agreement; null link when not ready; PREEMPTED only for the exact assigned current |
| Unscheduled thread | Existing INACTIVE and zero scheduler-metadata shape; not an operational current authority |
| Selected hart | Exact live generation; valid installed anchor/idle/emergency local geometry and zero entry scratch, or original uninstalled zero shape; current/primary and local accounting agreement |
| Current relation | Exact current thread, its assignment back to this hart, attached bounds matching primary; runnable current is its priority head; nonrunnable current is unlinked |
| Accounting | Original owner range, NONE zero-state, THREAD/current/accounted agreement, IDLE/no-current and null accounted-handle rules; original overflow and counter arithmetic |

Whole-hart uniqueness, recomputed counts and reschedule-to-assigned-thread
existence remain complete-diagnostic predicates. The return scope below
also checks assigned existence when it consumes reschedule state.
Unconsumed timer/device metadata is not audited by an ordinary queue
operation.

Preserve argument/flag/priority checks that currently precede validation.
Initialization and selected header admission precede indexed access.
Caller-supplied stale handles retain `STALE`; malformed consumed
relationships return `INVARIANT`; legitimate wrong lifecycle/accounting
states retain `STATE`. An out-of-scope corruption no longer has global
error precedence over an otherwise valid selected operation.

Do not silently strengthen all null encodings to `{0,0}`.
The existing predicates generally define null by generation zero.
An empty queue's tail still equals the exact null handle required by
complete queue validation; a null head or nonready next link retains its
existing generation-only interpretation. Guard-zero admission remains
byte/field-exact as before.

## Queue preflight and non-failing commit

### Endpoints and current preemption

Before queue indexing, prove priority is below sixteen. For each consumed
queue, require coherent head/tail emptiness. Nonempty endpoints resolve
exact live runnable linked threads with the matching assigned hart and
priority. The tail's next is null. A singleton head/tail has null next;
a nonsingleton head has a valid distinct same-queue successor.

This validates the endpoints and directly consumed successor, not every
unread interior link. Full diagnostics retain whole-queue coverage.

Tail insertion additionally validates the exact current and, when a
higher-priority preemptible insertion would remove it, its current-head
removal and successor. Preserve rejection while THREAD accounting would
make preemption unsafe.

### Dequeue

Walk from the exact queue head to the requested thread, validating every
consumed handle, generation, owner, assignment, priority and ready state.
Bound a nonterminating/missing-target walk by thread capacity.
Validate the requested node's successor before any store:

- a selected tail has a null successor;
- a non-tail has a valid same-queue successor;
- the successor is not the selected node or any earlier consumed prefix
  node.

The last check uses at most one bounded prefix replay. It adds `O(prefix)`
work and constant storage, not a cleared thread-capacity array.
After those checks, predecessor/head/tail repair and link clearing are
non-failing stores. No error is returned after the first store.

### Compound policy and guard operations

Admission, final-flag clear and policy replacement preflight destination
and current preemption before changing assignment, policy, RTS or links.
Policy replacement also preflights its old queue removal.
Keep the existing order: remove old membership if needed, publish the
accepted policy/flags, append, then preempt the old current.

Same-priority removal may change the destination tail; removing a peer
may change current's successor. Commit uses the live post-removal links
whose changes were already proved, not saved stale endpoints.

Use `scheduler_core_queue_enqueue_tail_prevalidated` after those checks.
Remove the obsolete fallible `scheduler_core_queue_enqueue_tail` and its
private declaration; require no remaining exact-name callers.
Keep the existing raw and void prevalidated enqueue bodies.

IPC rollback preflights head insertion before restoring flags/current/
primary and clearing the guard. Immediate IPC commit preflights its
dequeue/head reinsertion before clearing the guard. Preserve guard field
ownership, exact rollback stack bounds, blocked combinations and input
order. `begin_current_ipc` retains its existing custody sequence.

### Ready selection

The private `scheduler_core_queue_pick` becomes an error-returning
selected-prefix operation over `(objects, hart, output)`.
It checks each empty head/tail prefix and the first nonempty queue's
consumed endpoints, then publishes that head without dequeueing it.
`EMPTY` and every failure leave the output unchanged.
Do not walk lower-priority queues or all ready records.
The public `micros_hart_pick_ready` signature remains unchanged.

## IPC batch duplicate stamps

Retain the existing `tcap` candidate array and its 5,120-byte RV64 size.
Before preparing candidates, its handle cells serve only as temporary
slot-indexed stamps:

1. check each request's original flag shape;
2. for each in-range slot, initialize only that stamp's slot field to an
   impossible slot sentinel;
3. revisit requests in order; an unseen stamp receives the full handle;
4. an exact duplicate is `ARGUMENT`;
5. an out-of-range slot or repeated slot with another generation uses
   the unchanged complete pairwise shape check before any candidate
   resolution.

Read a stamp's generation only after its seen-slot field proves it was
written. Do not clear the whole array or read uninitialized cells.
Use `UINT16_MAX` only with a checked capacity assertion that it cannot
name a thread slot; do not silently assume a future larger table retains
that sentinel.

A valid batch of exact live handles has at most one generation per slot,
so its shape work is `O(m)`. The malformed fallback retains the old
`O(m*m)` upper bound and exact duplicate/flag-before-stale precedence,
including `a,b,a` generations for one slot.

The ordinary candidate pass then overwrites every field it consumes in
input positions `[0,m)`. No later step reads stamp-only positions.
Commit remains the existing input-order flag writes and void enqueues.
Earlier enqueues may extend a shared tail or preempt current once; all
later commits consume those already-proved valid changes.
No new persistent state, extra capacity-sized array or authoritative
preflight mutation is introduced.

## Direct hart and current ownership

`micros_hart_resolve_context` checks arguments and initialization, then
uses integer base/extent/stride arithmetic over `objects->harts`.
Reject below-base, out-of-extent and non-record-aligned values before
reading an array element. Subtract only after the lower-bound check;
do not compute an overflowing end address or dereference the supplied
integer. Inspect one derived live record and require nonzero generation.
Absent/misaligned/nonlive addresses remain `STALE`; a selected live
zero-generation record is `INVARIANT`. Preserve output on failure.

`scheduler_core_thread_is_current` resolves the exact thread and its
assigned hart, then compares the full current handle. It performs no
hart scan and cannot index an invalid handle. Operation preflight rejects
malformed selected assignment before consuming this Boolean.

Capture keeps the original argument/init/thread/attachment checks.
It additionally requires scheduled assignment, exact live owner and
assigned hart, valid selected stack bounds and matching current handle.
An unattached/unscheduled or legitimately noncurrent thread is `STATE`;
malformed selected owner/assignment is `INVARIANT`.
Only after admission does it copy the same 264-byte context.
Foreign duplicate-current corruption is detected by complete diagnosis,
not a repeated whole-hart search.

Complete scheduler validation now rejects an unscheduled current thread.
No public construction sequence produces it. Preserve all remaining
complete current, queue and accounting predicates.

The native corpus freezes these intentional corrupted-state distinctions,
not candidate-derived outputs:

| Named input | Fixed baseline | Corrected selected/diagnostic result |
| --- | --- | --- |
| Live hart pointer with zero stored generation | Pointer lookup `OK` | `INVARIANT`, output preserved |
| Otherwise coherent unscheduled current | Complete validation `OK` | Complete validation `INVARIANT` |
| Capture of that unscheduled current | Capture `OK` | `STATE`, saved context preserved |
| Corruption wholly outside a scheduler operation's consumed scope | Global preflight `INVARIANT` | Selected operation may succeed; complete diagnosis still fails |
| Foreign duplicate current while the assigned hart remains exact | Capture `INVARIANT` after whole-hart count | Selected capture may succeed; complete diagnosis still fails |

Valid-state transcripts remain identical. Ordinary/observed products
agree within each source stage; they do not erase these declared
baseline/current distinctions.

## Complete stack-disjointness proof

Let `T`, `H`, `N`, `E` denote thread capacity, hart capacity, owned active
interval count and exceptional interval count. Let `D = T + 2*H`.
The ownership set contains attached live thread stacks and installed
hart idle/emergency stacks, at most `D` intervals.

### Classification in existing passes

First retain every original range/alignment/minimum-size and zero-state
check. In the existing thread/hart slot passes, accumulate three cohort
descriptors with strides 16 KiB, 16 KiB and 4 KiB respectively.

For a range at slot `i`, check multiplication is representable before
forming `i*stride`. Exact-sized ranges with bottom at least that offset
can propose `base = bottom - offset`. The first eligible range seeds its
cohort. Another range is regular only when its exact size and derived
base agree. Everything else that passed original range validity is an
exception, not an error.

A separate seeded flag permits base zero. No unchecked addition is
needed to recognize a regular range. Distinct slots in one cohort have
disjoint half-open intervals by the checked positive stride relation.
The descriptor is an ephemeral proof partition, not new stack authority.

### Merge and exceptions

After original counts, hart state and current/primary alias checks:

- skip further stack scanning when fewer than two owned intervals exist;
- advance one cursor through each cohort's slots, selecting only regular
  active intervals;
- merge those at most three ordered streams by bottom address and reject
  an adjacent overlap; touching bounds are allowed;
- if exceptions exist, enumerate them and compare each against every
  other actual owned interval, excluding itself.

Do not compare whole cohort envelopes: legitimate other owners can occupy
unused slots. Do not count selected primary as an extra interval; its
alias is already validated. Check exception/regular and exception/
exception conflicts, including a malformed late interval, without
silently assuming a target linker arena.

The geometry descriptor is at most 48 RV64 bytes: three bases, three
seed flags and two counts. Three cursor/index/range records require at
most 72 additional bytes, plus bounded scalar/range locals.
Keep all of them automatic and caller-owned. No heap, recursion,
persistent index, mutable global or new public layout is introduced.
The existing process-count and current-thread witnesses remain automatic.

Stack work is `O(D + E*D)`, with constant extra storage.
`E = 0` is linear; arbitrary exceptional portable layouts can still
require quadratic diagnostic work. The original hardware-ID and
assigned-thread diagnostic terms remain separately visible.

## Return preflight without implementing OBJ02

Retain the existing plan shape and static projection.
`clear_return_candidate` already zeros the candidate before preflight.
Use its ready bitmap temporarily while `scratch_load` walks the selected
hart's actual queues:

- check each endpoint pair, exact ready record and final tail;
- reject repeated slot membership across all its queues;
- preserve bounded walks and original input order.

Then scan thread slots once for assignment to this exact selected hart.
Validate every such consumed record, including nonlive records claiming
assignment, against the membership witness and current relation.
This also supplies the selected reschedule/assigned-existence check.
Unrelated hart records and other assigned queues remain outside scope.

The existing bitmap clear precedes preemption repair and final queue
projection. Retain all scratch shifts, renewal bounds, complete next/
head/tail construction, checked-plan comparison and full apply loop.
Failure preserves the caller plan and registry; private partial scratch
is overwritten by the next complete load as before.

Target outgoing capture, selected completion/context/root preflight,
expired-only timer preparation, accounting samples, completion commit,
plan apply, root activation, context installation and idle pivot retain
their order. User/idle trap accounting bodies remain unchanged.

## Work and storage evidence

Use fixed capacities as logical variables, not a claim of constant
algorithmic work.

| Operation | Valid average/worst/amortized work | Additional storage |
| --- | --- | --- |
| Hart context lookup | One selected hart probe, `O(1)` | Constant |
| Current membership | At most one thread and one assigned-hart probe | Constant |
| Context capture | At most three selected object probes plus the fixed 264-byte copy | Constant |
| Ordinary metadata/endpoint checks | `O(1)` in process/thread/hart capacities | Constant |
| Dequeue | `O(prefix)`, worst `O(q)`; at most one prefix replay | Constant |
| Pick | `O(priority-prefix)`, worst `O(k)` | Constant |
| IPC batch | `O(m)` valid; `O(m*m)` malformed fallback | Existing candidate array only |
| Selected return preflight | `O(T + k + r)` | Existing candidate bitmap |
| Retained return algorithm | `O(T + k + r + z*r)` | Existing 4,224-byte scratch and 1,344-byte plans |
| Complete stack proof | `O(D + E*D)`; linear when `E=0` | Constant geometry/cursors |

The complete validator still includes `O(pcap + T + H*k + r)` record/
queue/witness work, hardware-ID `O(H*H)` and possible assigned-thread
`O(H*T)` work. It is not globally claimed linear.

The new native evidence compiles the fixed and actual current eighteen
portable object/scheduler C owners with their eight actual project
headers, not every unrelated kernel header. Retain source/include
closure, compiler/system-header identity and ordinary-file checks.
The work collector and its two headers are separate test inputs.

Use ordinary, aggregate and stack-only products for each source stage.
All six run the same deterministic semantic corpus. Observed products
must preserve ordinary outcomes/state; counters record actual primitive
sites and never estimate callee work from caller counts.

Three independent replacement oracles are required:

| Oracle | Discriminating property |
| --- | --- |
| `obj01-local-validation-work` | Zero complete-validator entries at all sixteen migrated operations; bounded consumed records/links and valid-batch linear work |
| `obj01-stack-geometry-work` | Actual regular/exceptional stack comparisons and probes satisfy the declared geometry envelope while complete corruption diagnosis remains |
| `obj01-direct-current-ownership` | Exact one/two/three selected-probe bounds for routed hart/current/capture, including final hart slots and checked stale/invalid input |

For ordinary selected operations, bound record probes by
`128 + 8*L + 8*k`, omitting `L` or `k` where not consumed; separately
require zero complete calls and no undeclared capacity walk.
For a valid batch, use at most `128*m + 32` selected probes and linear
stamp visits. These are conservative ceilings, not permission to hide
unobserved work.

In the stack-only product, count every overlap/order/affine-fit predicate
and every stack cursor/exception slot probe. Require at most
`16*(N+1) + 2*E*max(N-1,0)` comparisons and
`4*D + 2*E*D + 32` probes. Cover maximum regular occupancy so the fixed
pairwise implementation genuinely fails, not just small permissive cases.

Cover empty/singleton/sparse/maximal registries; first/last and malformed
hart addresses; exact/stale generations; assigned/noncurrent and
unsupported unscheduled current; all queue removal positions; same-priority
policy replacement; priority preemption; IPC rollback/commit; shared-queue
batches; `a,b,a` malformed generations; duplicate/cyclic/stale consumed
links; unselected corruption versus full diagnosis; regular gaps,
interleaved cohorts, touching ranges, oversize/underflow exceptions,
all cross-role overlaps and primary aliases.
Every failure checks registry and outputs unchanged. Existing models
remain independent and unchanged.

## Exact transport selection

Select only the fixed ninety-four active records with digest:

```text
3cd5b0132a30b6525df53e59daa282e6cbc781dc2cc4612baf8b37a73164936d
```

| Family/category | Keys |
| --- | --- |
| Historical directory identities for the twelve mutable subjects | 12 |
| Object's three original bodies, its complete byte inverse, scheduler/endpoint complete object inverses | 6 |
| Packed scheduler non-phase bodies | 21 |
| Packed scheduler active semantic phase-root minima | 3 |
| Fixed native inputs in MEM02, MEM03, MEM04 and MEM05 | 12 per family, 48 total |
| MEM03 observation contracts named in the study | 3 |
| Panic projected hart-source policy | 1 |

The object originals are `micros_hart_resolve_context`,
`micros_thread_capture_execution_context` and
`micros_kernel_objects_validate_base`. Preserve the original aggregate
signature identity and every other object body.

The twenty-one scheduler original body names are:

```text
thread_is_current
queue_dequeue
queue_enqueue_tail
queue_pick
preflight_tail_enqueue
scratch_load
micros_thread_scheduler_admit_preflight
micros_thread_scheduler_admit
micros_thread_scheduler_hold
micros_thread_scheduler_remove
micros_thread_runtime_flags_set
micros_thread_runtime_flags_unset
micros_thread_install_policy
micros_hart_pick_ready
micros_scheduler_accounting_initialize
micros_scheduler_account_enter_thread
micros_scheduler_account_enter_idle
micros_scheduler_preflight_current_ipc
micros_scheduler_rollback_current_ipc
micros_scheduler_commit_current_ipc
micros_scheduler_prepare_ipc_wake
```

The semantic roots are `micros_hart_plan_user_return`,
`micros_scheduler_commit_ipc_transitions` and
`micros_scheduler_core_validate`. Preserve their unqueried historical
whole-body digest records and all other active minima.

### Candidate roles

The twelve mutable production subjects are:

```text
kernel/objects/kernel_object_hart.c
kernel/objects/kernel_object_context.c
kernel/objects/kernel_object_validation.c
kernel/scheduler/scheduler_core.c
kernel/scheduler/scheduler_core_private.h
kernel/scheduler/scheduler_core_queue.c
kernel/scheduler/scheduler_core_policy.c
kernel/scheduler/scheduler_core_accounting.c
kernel/scheduler/scheduler_core_ipc.c
kernel/scheduler/scheduler_core_ipc_transition.c
kernel/scheduler/scheduler_core_return_plan.c
kernel/scheduler/scheduler_core_validate.c
```

The complete object/scheduler inverse closures additionally include the
other seven portable C owners and the object-private header. Keep those
eight candidate members byte-identical.

The direct closure's eighteen existing declaration/enforcer members are:

```text
tests/host/kernel_layout_source.py
tests/host/kernel_mapping_source.py
tests/host/memory_native_source_bindings.py
tests/host/panic_trace_source_bindings.py
tests/host/selected_address_space_native_source_bindings.py
tests/host/selected_address_space_source.py
tests/host/selected_ownership_native_source_bindings.py
tests/host/selected_ownership_observation_bindings.py
tests/host/selected_ownership_source.py
tests/host/selected_release_handoff_native_source_bindings.py
tests/host/selected_release_handoff_source.py
tests/host/test_endpoint_source_layout.py
tests/host/test_kernel_object_source_layout.py
tests/host/test_scheduler_source_layout.py
tools/generate_kernel_mapping_fixture.py
tools/generate_selected_ownership_fixture.py
tools/generate_selected_release_handoff_fixture.py
tools/stack_bound_fatal.py
```

The six literal binding modules remain byte-identical. Freeze the other
twelve adapters before Red and bind their exact bytes through the
immutable task declaration; they are not editable algorithm subjects.
Retain every old literal hash/value, private ownership and unrelated
predicate. Admit removed helper absence only under its exact replaced
identity and require no stale exact-name declaration or caller.

The direct candidate domain is exactly thirty-eight paths: twenty
production-family members and eighteen evidence members. Shared parser,
collector, planner, recipes, new fixtures and stack-effect qualification
remain disjoint immutable inputs.

### Current source clauses are individually named from the start

The new outcome registers its normal design/source/work/oracle records
and a separate closed `object-hart-native-admission` family. Do not repeat
the earlier missing-name prerequisites by publishing only an aggregate
source-projection identity.

Before production Red, independently freeze one literal row for each
actual current function, definition set, skeleton, header/fixed input and
independent observation predicate enforced by the new producer.
Retained function values derive from fixed #240. Mutable/helper signature,
guard, callee and complete primitive contracts derive from the accepted
algorithm and immutable pre-Red declarations, never the edited body.
Every exact comparison has its subject and actual enforcer or enforcers.
Repeated use of the same comparison is not a duplicate identity.

The finite helper inventory may express only the selected metadata,
endpoint/prefix, batch-stamp, cohort/merge/exception and selected-return
phases specified here. Freeze its complete names, signatures, source
owners, comparison policies, key set and fingerprints before Red.
Unknown helpers, fields, predicates or native inputs fail closed.
Use existing literal/identity/projection machinery, not a new registry
framework. No final fact count is inferred before that independent freeze.

All 5,851 old complete records and twenty-five old fingerprints remain
unchanged. Actual Green and recertification may retire only the specified
ninety-four keys, adding one claim and reaching 279 retirements.
Every prior claim remains valid without author journals.

## Legacy and machine evidence adapters

MEM03's three changed observation contracts use the verified OBJ01
current source view; all other existing sites, including empty contracts,
remain checked. Measure every new helper in any current producer that
executes it. Preserve the PM operation-six complete object/scheduler work
equality and every old memory bound/component rule.

Current-versus-historical test fixtures remain explicit. A fixed historical
body may still be tested against its original predicate, but it is never
reported as actual current source. No live current guard is relaxed merely
to make a historical scaffold fixture pass.

The panic hart-file exception requires qualified current admission for
only `micros_hart_resolve_context`; all other functions, declarations and
source effects in that file remain fixed. Keep both actual enforcers.
The existing finite machine proof must still inspect actual linked
objects, all hart slots, output-only resolver effects and complete trap/
frame/panic custody.

Freeze `tools/stack_bound_fatal_effects.py`'s exact adaptation before Red.
It may bind the selected live generation and add scalar-only RV64
`MULHU`, `DIVU`, `REMU` interpretation where required by the direct
stride calculation. Implement full unsigned 64-bit instruction semantics,
including architectural zero-divisor behavior, without inferring unknown
operands or accepting unrelated instructions. Retain original visit
bounds, call/effect checks and all stack budgets. Focused existing
machine-test owners cover these new cases before actual target proof.

## Phase evidence and publication

After separate design merge, the implementation adds one task-specific
native fixture/header, source/authority/evidence owners, a literal native
binding declaration, focused regressions, fixture generator and driver.
The canonical future author intents are:

```bash
"${MICROS_PYTHON}" -E -S -B tools/check_object_hart_validation.py --red
"${MICROS_PYTHON}" -E -S -B tools/check_object_hart_validation.py --green
"${MICROS_PYTHON}" -E -S -B tools/check_object_hart_validation.py --recertify
```

These are specified interfaces, not existing commands in this design.
The real fixed-source Red must distinguish all three oracles and cover
every planned retirement. Green uses actual current production.
An immutable-input change invalidates the attempt; synthetic journals
never provide author events.

Recertification is one transaction over all thirteen required units:

| Unit | Required existing target or transaction |
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
| B14 | The actual task `--recertify` transaction, including all six native products, original object/scheduler models, legacy memory work, source/observation/recipe/authority checks and retained stack qualification |

Use one owned checked source view per native generation/host invocation.
The six-product orchestration belongs in the complete tier, not the fast
Python loop. Keep only a small directly runnable launcher regression.
No receipt, source decision or author event is borrowed across candidates.

Before final review, run the complete fresh author producer and all
current native/QEMU/image/stack coverage. Retain fifty author commands,
forty QEMU workflows, four standalone images, 44 static configurations,
113 ELFs, ten observed scopes, blocked B13 and three same-run profiles.
The new native case adds coverage; no existing case, model step, target
marker, capacity, timeout or stack/image budget is reduced or increased.

Stage the complete change and record exact HEAD/tree/commands/results,
clean tracked state and no untracked task files. Independent review
precedes the first permanent completed-claim commit. Publish only exact
reviewed/committed/remote/preview/squash content, verify fresh journal-free
committed/main authority and clean only completed task refs.

Only this outcome closes B03-F001/F003/F004. OBJ02, IPC01, hosted CI and
the remaining MINIX/POSIX roadmap retain their separate owners.

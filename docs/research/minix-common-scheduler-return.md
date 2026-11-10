# MINIX Common Scheduler Return

## Status and fixed references

This is the read-only baseline study for OBJ02, the next preferred correction
after independently merged OBJ01 #243. It owns only B03-F002 and B03-F005.
MEM04 #228 and OBJ01 #243 satisfy its runtime prerequisites.
The [Accepted contract](../architecture/common-scheduler-return.md) and
[ADR-0121](../adr/0121-bounded-common-scheduler-return.md) record the current
design. Complete independent design review is clean and design #244 is merged.
The uncommitted implementation reached actual runtime Green but is archived
after the consumer conflicts below. Its
[Accepted corrective contract](../architecture/common-scheduler-return.md#obj02-c01-complete-evidence-consumer-scope)
must separately merge before a fresh implementation.

```text
micros:       34125516b60e9e0a9e5362ff2ee60555691adb99
micros tree:  1de53de9dcf5102dbef3e76d59b7f2fef28cee9e
MINIX:        4db99f4012570a577414fe2a43697b2f239b699e
MINIX tree:   80db89de58d5ecaf31efe48b4d1d98c7e285e81f
```

The documented local reference at `/Users/alephling/Repo/minix` was verified
at that commit and tree, with the selected source files unchanged.
MINIX supplies mechanism evidence, not source to copy.

The [original B03 audit](minix-complete-baseline-b03-objects-scheduler.md)
describes an older fixed tree. Its findings remain the task owners, but the
current behavior and costs below account for merged MEM04 and OBJ01.

## Fixed MINIX behavior and logical structure

| Fixed source | Behavior and mechanism |
| --- | --- |
| `minix/kernel/proc.c:297-355` | Read CPU-local current directly; keep a runnable current, otherwise repair PREEMPTED and select the first ready head or idle |
| `minix/kernel/proc.c:357-428` | Complete selected deferred work, recheck runnability, then handle quantum exhaustion; a blocking result returns to selection |
| `minix/kernel/proc.c:437-474` | Finish architecture binding, close kernel accounting, restart the local timer, then restore the selected context |
| `minix/kernel/proc.c:1595-1711` | Append or prepend through CPU/priority head and tail pointers; priority preemption removes the old current through the ordinary flag boundary |
| `minix/kernel/proc.c:1716-1782` | Singly linked dequeue walks to the selected record, repairs predecessor/head/tail and stops |
| `minix/kernel/proc.c:1785-1813` | Priority selection returns the first nonempty head without removing it |
| `minix/kernel/proc.c:1893-1909` | External preemptible policy receives a no-time notification; kernel-scheduled or nonpreemptible execution renews without that notification |
| `minix/kernel/arch/earm/arch_system.c:152-170` | Bind the selected process to the architecture stack and prepare its interrupt-return state |
| `minix/kernel/arch/earm/arch_system.c:196-217` | Use the selected process root directly; the ARM implementation can skip an unchanged root |

The representation is a CPU-local current pointer and singly linked priority
queues, not a copied queue matrix or a capacity-wide next-link projection.
Selected queue changes update only affected records. Queue selection costs
`O(k)`, a particular dequeue costs `O(prefix)`, and repeated no-time selection
has the disclosed `O(z*k + affected links)` envelope.
There is no general return-plan array proportional to all process slots.

The `micros` bootstrap policy is already an Accepted staged substitution:
it renews a preemptible exhausted quantum synchronously and moves that thread
to its priority tail. A nonpreemptible exhausted thread renews in place.
OBJ02 must preserve those rules rather than introduce user SCHED or copy
MINIX's broader deferred-work policy.

MINIX's same-root hardware shortcut is not an available optimization here.
Accepted ADR-0021/0107 retain complete ASID-zero fence/write/fence activation
and checked readback. The target's distinct process/thread/hart identities,
264-byte user context, generation handles and supervisor-stack ownership
remain authoritative.

## What remains at fixed micros

### Portable planning and checked commit

`kernel/scheduler/scheduler_core_return_plan.c` still owns one static
`slots[16][128]` plus sixteen counts: 4,224 RV64 bytes.
After the selected preflight introduced by OBJ01, `scratch_load` copies
every selected-hart queue and `preflight_return_threads` scans all thread
slots assigned to that hart.

The planner repairs PREEMPTED in that projection, selects current or a ready
head, and renews exhausted candidates. `scratch_remove` shifts the remaining
queue for every moved candidate. `project_candidate_queues` reconstructs
complete head/tail/next arrays and ready/renew bitmaps, then the publisher
copies the complete 1,344-byte plan.

`kernel/scheduler/scheduler_core_return.c` has two distinct entry boundaries:

- `micros_hart_commit_user_return` is the checked portable API. It rebuilds
  an expected plan, compares every defined field and applies only a match.
- `micros_scheduler_apply_return_plan` is the existing internal prevalidated
  commit. It scans all thread slots and priorities while applying the plan.

The target already calls the prevalidated entry directly. It does **not**
recompute the checked portable plan on every production return.
The current target cost is one complete projection plus its capacity-wide
apply; the extra expected-plan construction belongs to the portable checked
API and its callers.

With `T` thread capacity, `k` priorities, `r` queued records and `z`
zero-quantum candidates, the remaining planning work includes
`O(T + k + r + z*r)`, with `O(T + k)` apply.
The existing target can hold caller and planner plans simultaneously;
checked commit additionally holds its expected plan.
These are current costs, not a claim that OBJ01 failed to remove its sixteen
owned complete-audit entries.

The whole projection also canonicalizes otherwise valid null encodings:
it fills every projected next link and empty queue head with `{0,0}`, then
apply writes every assigned thread's link. Existing null predicates generally
use generation zero, and OBJ01 explicitly preserves that interpretation.
A delta contract must distinguish logical queue equivalence from these
incidental writes to untouched state; it cannot reintroduce a capacity sweep
merely to normalize an unconsumed null slot.

### Target entry and selected context

The production callers are:

| Path | Outgoing custody | Selected work and commit |
| --- | --- | --- |
| `scheduler.c:micros_scheduler_start` | No current thread; explicit IRQ-save lifetime | Plan, root, completion and context preflight; two accounting samples around timer preparation; prevalidated apply and direct user entry |
| `scheduler_return.c:select_user_return` | Normal mode validates and stores the outgoing frame; captured mode requires exact null current and does not store it again | Notify pending diagnostics, plan, selected root/completion/context preflight, timer/accounting preparation, completion, apply, root/frame/accounting publication |
| `scheduler_idle.c:micros_scheduler_idle_select` | The idle IRQ has already changed accounting to KERNEL | Plan idle or selected user; retain the idle timer/accounting boundary and direct user-entry path |

`scheduler_runtime_validate_selected_thread` resolves the selected thread and
owner, then calls the existing selected root-preparation interface once.
`scheduler_runtime_load_selected_context` inspects the saved context and
validates its status plus executable PC and readable/writable stack mappings.

Several distinctions are important:

1. MEM04 already replaced complete root auditing inside ordinary lookup with
   selected path validation. The old audit's repeated complete `Rmem` formula
   must not be reported as the current implementation.
2. `micros_user_execution_inspect` copies the attached context; it does not
   validate PC/SP mappings.
3. `micros_user_execution_store_context` invokes the portable current-context
   capture. It does not repeat PC/SP validation.
4. A normal return that selects the same thread still validates the outgoing
   PC/SP and later validates the inspected selected PC/SP again.
5. A different selected thread legitimately needs its own context proof.
   Captured return has no reusable ordinary-outgoing proof.

Counting only the explicit context PC/SP lookup calls, a valid same-thread
normal return performs four; a different-thread normal return also performs
four, while start, captured return and idle-to-user perform two.
Selected-root preparation has its separate shared-root and controller-slot
checks; these numbers do not hide those checks or claim total PTE counts.

The pending diagnostic notification can change notification, ready-queue and
preemption state. The concrete commit trace is:

- `diagnostic_control.c:249-275` preflights and commits the exact kernel
  notification;
- `ipc_notifications.c:459-495` commits a scheduler wake, unlinks a receiver
  and stages delivery, or accumulates pending events;
- `ipc_completion.c:30-40` writes only staged-delivery fields;
- `ipc_queue.c:193-212` repairs only IPC queue links; and
- `scheduler_core_ipc_transition.c:309-325` updates RTS/enqueue state.

Those commits do not replace saved user contexts, process generations or
mappings. Any proposed reuse must bind that actual effect boundary and its
SIE-clear lifetime; interrupt exclusion alone is not permission to assume
every kernel callee is read-only. IPC completion later patches `a0` in the
selected snapshot and still owns that separate publication.

### Retained completion and failure boundaries

`scheduler_completion.c` still performs complete endpoint/object validation
for pending IPC, prepares the user-buffer write, and retains that exact plan.
It later writes the message, patches only the ABI result in the selected
context, stores that context and clears the staged completion.
That separate B03-F006/IPC01 correction is not part of OBJ02.

Preserve the existing order:

- invalid ordinary outgoing context is fatal before diagnostic notification
  and scheduler-plan mutation;
- invalid selected root precedes completion preflight;
- completion failure precedes selected-context loading;
- selected-context rejection precedes timer preparation and completion stores;
- ordinary return prepares its expired-only timer before the accounting
  sample and first commit store;
- first start retains both accounting samples and their distinct
  recoverable/fatal boundaries;
- root activation keeps its complete fence sequence and fatal readback rule;
- idle pivot, `sscratch`, delivered-frame custody, S-origin resume and the
  adjacent idle SIE window remain unchanged.

The exact contracts remain in Accepted ADR-0026/0027/0035/0062/0107 and the
current source owners. No later process-fault recovery or scheduler policy is
introduced by this study.

## Existing negative and legacy evidence

The checked portable stale-plan regression in
`tests/host/scheduler_return_test.c:scheduler_host_test_current_block_and_stale_plan_are_atomic`
prepares a two-thread queue, holds its tail, then requires `STATE` without
mutation even though the selected head remains the same.
A new compact plan cannot silently discard this consumed-endpoint witness.
Its precise treatment of genuinely unconsumed changes must be designed
separately from stale selected handles or changed consumed links.

The current OBJ01 corpus also records whole-selected-hart return failures:
duplicate queue membership, wrong final tail, a nonlive assigned record,
reschedule without assignment and an unreachable assigned-ready record.
Its unselected-hart queue case succeeds while complete diagnosis fails, and
one positive control compares the old complete plan representation.
These are real current assertions, not tests to remove when a layout changes.

Three inputs specifically expose the old whole-hart scan:

| Existing current case | Actual injected state |
| --- | --- |
| `nonlive-assigned-record` | An otherwise unused final thread slot claims assignment to the selected hart |
| `reschedule-without-assignment` | An empty hart has a pending reschedule request |
| `unreachable-assigned-ready` | A second assigned-ready thread remains outside an otherwise coherent singleton queue |

Fixed #243 returns `INVARIANT` for each and preserves registry and output.
Removing the whole-hart scan while consuming only selected links changes
that boundary. The design must explicitly decide the current expectation
and retain complete-diagnostic rejection on the identical corrupt inputs.
It must not move those mutations onto a different consumed record to hide
the distinction. The duplicate-priority and wrong-tail cases already corrupt
consumed endpoints and retain their separate rejection obligations.

`STATE_WORK_CONTRACT` preserves the old 4,224-byte workspace, 1,344-byte plan,
171-row baseline/current table identities and original return-projection
scope. The next design must explicitly separate preserved historical
expectations, current representation adaptation and any deliberately changed
consumed-state scope before implementation. Accepted ADR-0115 supplies the
precedent for an exact, independently declared current-fixture correction;
moving a corrupt input elsewhere or normalizing actual results is not valid.

There is a separate representation boundary in
`object_hart_validation_test.c:output_digest`: the raw digest includes the
entire plan even for operations that do not use it.
`object_hart_validation_evidence.py:compare_source_stages` compares that
combined value across source stages. A new plan size therefore changes the
before-output digest in all cases, not only successful return planning.
Dropping the combined comparison would also discard unrelated guard, wake,
selected-output and value checks.
The proposed contract requires explicit raw plan observations and a separate
defined-field plan-effect observation; it preserves the original raw record
and extracts its non-plan portion rather than fabricating old plan bytes.

All existing object/scheduler/IPC models, seeds, cases and target scenarios
remain independent evidence. New work and hardware observations complement
them rather than substituting source matching or elapsed time for behavior.

## Current authority census

At fixed #243 the complete inventory has 6,327 facts in twenty-seven families,
eight committed claims and 280 retired identities. Its complete fact-set
digest is:

```text
026081f993bb3d79d88337ceff3cc083a04668f7ddf075b58ea12e6b17d16ce1
```

A preliminary query over ten possible return/header/context subjects finds
260 related active records, including source, native input/observation,
recipe and permanent declarations. That broad intersection is **not** a
retirement selection or a final candidate domain.
The complete records and prior claims were captured before this study edited
repository documentation.

The design must enumerate only the assertions actually invalidated by its
final contract, preserve unrelated facts and enforcers, and keep immutable
oracle/fixture/adapter inputs disjoint from runtime candidates.
Any missing identity or incorrectly bound closure is a real prerequisite,
not authority to replace an aggregate hash from edited runtime output.

The first narrower selector had 57 active records and 27 paths for eight
runtime/header subjects. The selected target-report placement also changes
the existing byte identity for
`kernel/tests/scheduler/scheduler_test_finish.c`.
The Accepted design therefore selects 58 active records and a 28-path direct
closure, with complete-record digest:

```text
7e80ec298823df18ec39c2805e7cc2a9035edae211778c4db4761b370e836fc2
```

| Existing family | Selected records |
| --- | ---: |
| Directory source identities, including the final target report | 4 |
| Stable public plan-header identities | 2 |
| Scheduler original bodies and active phase minima | 11 |
| MEM02 native inputs | 3 |
| MEM03 native inputs | 3 |
| MEM04 native inputs and selected-context loader body | 4 |
| MEM05 native inputs | 4 |
| OBJ01 current input/function/definition/skeleton/observation clauses | 27 |

This selector excludes all prior retirements, fixed baseline-only clauses,
unqueried whole-body records for completed phase roots, and the unchanged
planner preflight/clear/publish functions. It does not select the coarse
PLAT01, MEM04 or OBJ01 source-projection records or shared parser/collector
authority. The final-report guard is inserted after existing cleanup checks
and before the original success report, preserving the existing finish-phase
minima. Its old unqueried whole-body record is not selected.
The partition is 45 delta-work and thirteen context-reuse records.
Independent design review is clean. Acceptance fixes this selection, not
implementation evidence; no identity has retired for OBJ02.

## Design review obligations

The Accepted contract's independent review examined these boundaries:

- the exact finite delta representation, affected-queue/link bound, renewal
  ordering, cycle rejection and non-failing commit reads/stores;
- valid average/worst/amortized work and simultaneous automatic storage,
  including checked portable commit rather than only target apply;
- preservation of preemption repair, nonpreemptible renewal, current
  retention, priority selection and all-zero/sparse/wrapping queue cases;
- exact outgoing-versus-selected context proof lifetime and invalidation,
  with no persistent validation cache or borrowed captured-return authority;
- complete scope of retained current fixture changes and historical evidence;
- individual source-authority selection and all affected evidence owners;
- actual native and QEMU Red properties, primitive observations, complete
  recertification and unchanged-budget stack acceptance.

No executable prototype, test, harness, build scaffold or runtime change is
part of this research phase.

## Difference classification

| Difference | Classification |
| --- | --- |
| CPU-local current, singly linked priority queues and affected-link selection | Baseline parity |
| Split generation handles, RV64 context/stack custody and complete ASID-zero fences | Required adaptation |
| Bounded non-mutating delta preflight before an atomic affected-state commit | Compatible extension |
| Same-epoch retained context proof with fresh validation for a different or captured selection | Compatible extension |
| Synchronous bootstrap renewal before a user SCHED service exists | Staged substitution |
| Whole queue projection/shifts/apply and duplicated same-context validation at fixed #243 | Divergence requiring correction |

The Accepted contract preserves the reference algorithm family and its
logical work/storage envelope while retaining explicit failure-atomic
adaptations. It does not import MINIX source, its broader deferred-work
policy or its architecture-specific root shortcut.

## OBJ02-C01 observed consumer scope conflicts

The unaccepted implementation was based on separately merged #244,
`015ec2d7920551caacbdcb753ecadedd7fd3deba`.
Its independently declared native corpus contains 174 fixed records and
180 current records; the additional six are checked/apply observations for
the three already Accepted scoped successes. Actual execution measured:

| Property | Fixed runtime | Unaccepted current runtime |
| --- | ---: | ---: |
| Plan bytes | 1,344 | 220 |
| Static queue workspace bytes | 4,224 | 0 |
| Maximum queue shifts in the corpus | 16,256 | 0 |
| Maximum constructor copied/cleared bytes | 1,344 / 1,344 | 220 / 220 |
| Same-thread / different-thread / captured context validations | 2 / 2 / 1 | 1 / 2 / 1 |

Actual Red and Green were recorded in attempt
`fd5dbd294d72408e814f5ef544c9e47b`.
Red record `1a6f67fc6d6ebe480c835e2e4b10e37d46f83c53c7a9426299deecd399cdaec0`
binds staged tree `aaf404695567884f43b2ab84ff34e74e0986debd`.
Green record `393807a064a0ef5a5add1120c53b6f67db875ca1945071662b9efd3bba706a95`
binds staged tree `d1ab57b7d4306291f8af8d53f32db97894855dd5`.
The original object/scheduler models and scheduler/IPC-return/TTY-trap
target markers passed. These are execution facts, not final acceptance.

The same attempt's B14 recertification passed retained MEM01-MEM05 work
and all six 171-case OBJ01 products. Its host selection then ran 695 of
715 cases in 697.225 seconds, with seven failures, six errors and twenty
not-run cases after scheduler-layout setup failed. It produced no terminal
recertification event and no retirement.

| Fixed/current consumer | Actual conflict | Required bounded correction |
| --- | --- | --- |
| `tests/host/qemu_transcript_source.py:112-128` | Its original CMake inverse explicitly accepts exactly two report tokens; the current scheduler needs a third, independently owned use. | Keep that shared helper unchanged. Check and remove the complete OBJ02 extension before entering its original historical boundary. |
| `tests/host/test_selected_ownership.py:445-480` | The direct forward comparison calls the old transcript inverse on raw current CMake. | Add this previously omitted immutable test owner and project the new addition from the observed side only. Keep the fixed builder and expected recipe. |
| `tests/host/test_qemu_transcript.py:183-197` | The original test directly requires two tokens in raw current CMake. | Add this omitted immutable test owner; retain its two-target historical assertions and malformed-input cases after the bounded projection. |
| `tests/host/test_selected_release_handoff.py:390-421` | Its explicitly historical MEM05 fixture disables OBJ01 authority but can still borrow newer OBJ02 admission for old planner/apply/context source. | Add this omitted immutable test owner and extend only its cold historical context. Do not infer a historical stage inside current admission. |
| `tests/host/stack_source_projection.py:174-189` | The new finish-report include displaced the existing exact second-line stack-observer include. | Correct the already owned finish adapter's include placement; do not change stack tooling or its invariant. |
| Existing current-catalog assertions | Five checks still expected 27 families or 6,327 total current facts after the independently declared additions. | Update the already permitted current-count checks while preserving every complete predecessor record. |
| Existing isolated OBJ01 replay model | The model's mocked products omitted the newly mandatory `plans` member. | Complete that already permitted protocol fixture explicitly; do not add a live fallback. |

The original native/runtime contracts did not fail. The two recipe tests
and historical MEM05 test are nevertheless real source consumers and are
absent from ADR-0121's exact immutable-owner list. They cannot be silently
edited or skipped. The
[Accepted ADR-0122](../adr/0122-obj02-evidence-consumer-scope.md) adds only those
owners and the precise reporting/historical-view boundary, retaining the
runtime, finding and 58-key/28-path scopes.

The complete forty-file candidate is preserved in a verified local archive
and stash `092de580edf54e72d3daa90dd68c8850d78e6f42`, whose tree is the Green
tree above. No implementation PR or permanent implementation commit exists.
The old working branch was abandoned after preservation. A fresh restart
must use the separately merged correction and renew its own actual evidence.
Broader workflow simplification remains a later user-requested outcome.

## References

- [Current OBJ01 contract and implementation](../architecture/selected-object-and-hart-validation.md)
- [B03 audit](minix-complete-baseline-b03-objects-scheduler.md)
- [Correction DAG](minix-complete-baseline-findings-and-correction-dag.md)
- [Original scheduler/context study](minix-scheduler-and-context-switch.md)
- [MINIX-baseline scheduler](../adr/0026-minix-baseline-kernel-scheduler.md)
- [Return timer preparation](../adr/0027-return-boundary-timer-rearming.md)
- [Selected user-address-space validation](../adr/0107-selected-user-address-space-validation.md)
- [Exact current/historical fixture correction](../adr/0115-selected-count-generation-evidence-scope.md)
- [Accepted OBJ02 consumer-scope correction](../adr/0122-obj02-evidence-consumer-scope.md)

# MINIX Selected Object and Hart Validation

## Fixed references and scope

```text
micros policy/runtime: 9b880468192e61177ffe57ec69079fe398160a42
micros tree:           80b93b111b5a1387d812da87c539e56da042777b
MINIX:                4db99f4012570a577414fe2a43697b2f239b699e
MINIX tree:           80db89de58d5ecaf31efe48b4d1d98c7e285e81f
```

The fixed `micros` source is independently reviewed EV01-C09 #240.
Its catalog has 5,851 facts, twenty-five families, seven committed claims
and 185 retired identities. OBJ01 owns B03-F001/F003/F004. Its original
design #241 is merged, but the unaccepted implementation is archived for the
OBJ01-C01 correction below. MEM04 is its actual runtime prerequisite; MEM05 and
the C08/C09 authority prerequisites are also merged.

The [Accepted contract](../architecture/selected-object-and-hart-validation.md)
does not start OBJ02, IPC01 or a later VM correction.

## Fixed MINIX trace

The local reference files were checked against the fixed commit, not
inferred from a newer MINIX implementation.

| Reference | Mechanism and bound |
| --- | --- |
| `minix/kernel/cpulocals.h` | CPU-indexed local state under SMP and one scalar CPU-local object otherwise; current process and queue roots are direct |
| `minix/kernel/proc.c:290-355`, `switch_to_user` | Read current directly, repair preemption, select ready or idle, then publish CPU-local current |
| `minix/kernel/proc.c:1595-1711`, enqueue/head enqueue | Select the assigned CPU's queue directly, change bounded links, then apply priority preemption; optional sanity is separate |
| `minix/kernel/proc.c:1716-1782`, dequeue | Walk only the requested priority prefix, repair predecessor/head/tail and stop; `O(prefix)` work, constant auxiliary storage |
| `minix/kernel/proc.c:1785-1813`, `pick_proc` | Select the first nonempty priority head without dequeueing current; `O(priority-prefix)` |
| `minix/kernel/debug.c:16-104`, `runqueues_ok_cpu` | Optional complete slot/queue sanity; range and record-stride checks precede pointer use; linear slot/link work |
| `minix/kernel/arch/earm/mpx.S:274-287` | Fixed CPU stack storage with a known stride and guard allocation |
| `minix/kernel/arch/earm/include/arch_proto.h:49-53` and i386 counterpart | Compute CPU stack top from base, CPU index and constant stack stride |
| `minix/kernel/arch/i386/arch_system.c:246-256` | Bind the fixed stack arena and initialize CPU zero directly in the single-CPU configuration |

MINIX does not supply a portable arbitrary-range stack registry, a
generation-safe split process/thread model, an atomic multi-request IPC
scheduler API or these test counters. Those are existing `micros`
adaptation/safety boundaries, not source to copy.

## Current object and mutation facts

`kernel_objects.h` retains 64 process, 128 thread and eight hart slots.
Generation-safe identities, one-thread/one-hart checked production policy
and larger native policies remain distinct.

Current-handle production stores occur only in:

- `scheduler_core_return.c:micros_scheduler_apply_return_plan`;
- `scheduler_core_ipc.c:micros_scheduler_begin_current_ipc`; and
- `scheduler_core_ipc.c:micros_scheduler_rollback_current_ipc`.

Initialization requires zero storage. Admission installs the assigned
hart before a thread can become current; removal rejects current before
clearing assignment. The normal native object and scheduler paths also
use these operations. The two direct native current-handle assignments
found in the object/scheduler validation cases are corruption mutations,
not supported constructors.

Nevertheless, `scheduler_core_validate.c:validate_harts` still accepts an
unscheduled current thread when that hart has no assigned thread.
ADR-0026 already requires scheduled policy for current. Removing this
unreachable operational escape is directly coupled to replacing current
search with assigned-hart identity; it is not a new running-state field.

The three audited hot scans are unchanged at this baseline:

| Owner | Current work | Proposed scope |
| --- | --- | --- |
| `kernel_object_hart.c:micros_hart_resolve_context` | Search all hart slots for a live pointer match | Checked array extent/stride and one selected slot |
| `scheduler_core.c:scheduler_core_thread_is_current` | Search all live current handles | Exact selected thread, assigned hart and current handle |
| `kernel_object_context.c:micros_thread_capture_execution_context` | Count every matching hart before copying context | Exact selected ownership and the same 264-byte copy |

Cold release/detach and stack-installation scans are not silently included
in this hot-path correction.

## Complete-audit census

The actual source-function census finds forty-two direct production calls,
excluding `kernel/tests`. It excludes declarations/definitions and records
the test-only guard where applicable:

```text
micros_kernel_objects_validate:         19
micros_kernel_object_runtime_validate:   2
micros_scheduler_core_validate:         21
```

Sixteen calls belong to the five scheduler owners below.
All call `micros_scheduler_core_validate`.

| Owner | Calling functions | Calls |
| --- | --- | --- |
| `scheduler_core_policy.c` | `micros_thread_scheduler_admit_preflight`, `micros_thread_scheduler_hold`, `micros_thread_scheduler_remove`, `micros_thread_runtime_flags_set`, `micros_thread_runtime_flags_unset`, `micros_thread_install_policy`, `micros_hart_pick_ready` | 7 |
| `scheduler_core_accounting.c` | `micros_scheduler_accounting_initialize`, `micros_scheduler_account_enter_thread`, `micros_scheduler_account_enter_idle` | 3 |
| `scheduler_core_ipc.c` | `micros_scheduler_preflight_current_ipc`, `micros_scheduler_rollback_current_ipc`, `micros_scheduler_commit_current_ipc` | 3 |
| `scheduler_core_ipc_transition.c` | `micros_scheduler_commit_ipc_transitions`, `micros_scheduler_prepare_ipc_wake` | 2 |
| `scheduler_core_return_plan.c` | `preflight_return_plan` | 1 |

The following twenty-six calls remain at their exact owning functions:

| Owner | Retained callers | Calls |
| --- | --- | --- |
| `kernel/boot/bootstrap_control_core.c` | `micros_bootstrap_control_validate` | 1 |
| `kernel/boot/bootstrap_preparation_rollback.c` | `bootstrap_preparation_rollback_preparation` | 1 |
| `kernel/endpoint/endpoint_object_validation.c` | `micros_endpoint_registry_validate_objects` | 1 |
| `kernel/execution/user_execution.c` | `prepare_post_handoff_binding`, `prepare_kernel_stack`, `commit_execution_context`, `micros_user_execution_detach` | 4 |
| `kernel/grant/grant_runtime.c` | `micros_grant_runtime_initialize` | 1 |
| `kernel/ipc/ipc_runtime.c` | `micros_ipc_runtime_initialize` | 1 |
| `kernel/memory/bootstrap_memory.c` | `validate_process_owners` | 1 |
| `kernel/memory/user_address_space_handoff.c` | Both wired prepare/complete operations | 2 |
| `kernel/objects/kernel_object_runtime.c` | Initialize, install trap stacks, explicit validate, attach/detach address space, guarded test emergency-stack replacement | 6 |
| `kernel/objects/kernel_objects.c` | Public combined validator delegation | 1 |
| `kernel/pm/pm_control_execution.c` | Both object and scheduler calls in `prepare_environment_is_valid` | 2 |
| `kernel/pm/pm_control_exit.c` | `micros_pm_control_exit_begin_preflight`, `validate_stopped_exit` | 2 |
| `kernel/pm/pm_control_runtime_core.c` | `micros_pm_control_runtime_validate_reservation` | 1 |
| `kernel/pm/pm_control_validation.c` | `micros_pm_control_state_validate_objects`, `micros_pm_control_validate_prepared_shape` | 2 |

The internal scheduler-to-object-base validation edge also remains.
OBJ01 does not remove an endpoint, bootstrap, PM, memory or lifecycle
owner's audit merely because its callees become cheaper.

## Failure and work dependencies

### Tail enqueue cannot simply lose its global preflight

`scheduler_core_queue_enqueue_tail` publishes the new node before calling
recoverable dequeue for a preempted current thread. The old complete
validator makes that failure unreachable on valid input.
Removing the audit without consumed endpoint/current preflight would
allow a partial update.

The existing void prevalidated enqueue already performs bounded current
head removal. The proposal keeps enqueue-before-preemption order while
moving all recoverable work before policy/flag/queue publication.

Same-priority policy replacement can remove the destination tail itself.
Removing a peer can also change the current head's successor before
priority preemption. A commit must consume the live links resulting from
those already-proved changes, not a stale copied predecessor or tail.

### Batch shapes have another visible cost

`preflight_transition_shapes` currently compares each handle with all
earlier handles. That is `O(m*m)`, not the linear term in the older B03
summary. Its input-order candidate array is already `tcap` entries and
5,120 bytes on RV64.

The proposed valid-path stamps reuse only its handle cells before
candidate preparation. Same-slot/different-generation input is malformed
but is not an exact duplicate. Retaining the old pairwise check on that
exceptional path preserves cases such as generations `a,b,a`, where
argument failure must still precede eventual stale resolution.

### Return remains a staged whole projection

The current planner clears a complete candidate, loads every selected-hart
queue, shifts projected slots for quantum renewal, rebuilds links and
publishes the whole plan. Apply scans every thread for the selected hart.
OBJ01 must validate that consumed selected-hart scope, including a
malformed nonlive record that nevertheless claims the selected assignment.
It must not claim constant return work or accidentally implement OBJ02.

## Stack geometry and exception boundary

Target thread storage is the fixed 128 by 16 KiB arena in
`arch/riscv64/user_entry.S`, retained by `.thread_kernel_stacks` in the
linker script. `user_execution.c:derive_stack_bounds` derives exact slot
bounds and stack preparation clears the full slot.
The ordinary trap and emergency arenas in `trap.S` are 16 KiB and 4 KiB.

Portable attach deliberately accepts arbitrary disjoint aligned ranges
of at least 16 KiB. Hart installation likewise accepts caller-supplied
valid ranges, and the nested target uses its separate emergency arena.
Neither current policy nor this correction can replace that portable
contract with an assumed universal linker address.

The proposed cohorts are ephemeral geometric witnesses, not new address
authority. A caller-supplied range that satisfies the checked affine
relation can participate; a larger, displaced or nonrepresentable range
is an exception. Actual regular intervals can have gaps. Another valid
owner may occupy a gap, so testing whole pool envelopes would be wrong.

The selected primary is already required to equal its exact current
thread stack, or idle when there is no current. Counting it as a new
owner would manufacture an overlap. Existing current uniqueness and
alias checks precede the unified owned-interval proof.

Hardware-ID uniqueness and `reschedule_pending`'s assigned-thread
existence are separate complete-diagnostic costs. Replacing stack pairs
does not remove their `O(hcap*hcap)` and possible `O(hcap*tcap)` terms.

## Fixed evidence dependencies

EV01-C09 supplies the object body/whole-source bindings and individual
panic source policies. The original #241 active selection is ninety-four
keys with digest:

```text
3cd5b0132a30b6525df53e59daa282e6cbc781dc2cc4612baf8b37a73164936d
```

Its direct closure has thirty-eight paths. No prior retirement appears
in that selection. The canonical contract specifies the selectors and
candidate/frozen roles.

The scheduler's three outlined portable roots use active `FROZEN_FACTS`
minimum-multisets. Their old whole-body digests are not queried in the
completed decomposition state; do not retire those hashes merely because
the current phase implementation changes.

MEM03 has sixty-five non-memory observation contracts in eight source
owners. Only three existing contracts need replacement here:
`kernel_object_validation_thread_context_matches`,
`kernel_object_validation_hart_stacks_avoid_peers`, and
`scheduler_core_thread_is_current`. Other retained loops and required
empty contracts remain active; new helper work must be measured by every
current producer that executes it.

MEM03's PM reservation corpus has one unattached thread and no installed
hart stacks. Accumulating geometry in the existing passes and skipping
additional stack scans for zero owners preserves its exact object/
scheduler work equality. That equality is not rebaselined.

MEM01's generator reads current other native sources without a fixed-byte
comparison. Do not invent a missing MEM01 fixed-input identity. The four
actual fixed native-input families are already included in the census.

The panic fault-prefix machine checker currently models scalar `MUL`,
but not the unsigned multiply-high/divide/remainder operations a direct
constant-stride lookup may produce. It also does not currently bind a
selected hart generation read. The exact qualification extension belongs
in the pre-Red immutable adapter set, not in a late waiver or larger
instruction/stack budget.

## OBJ01-C01 complete source-authority gap

### Observed boundary and preserved candidate

Design #241 merged as `a803c2d9e2692b148b7c8d74d02e8304406ce89f`,
tree `99736be3086b80f6a08a5223b4ef74bd1fc9e213`.
The uncommitted candidate at tree
`13cfcd48998ddfe85db8d2b04766214a9f59e72d` recorded actual fixed-source
Red, current Green and all thirteen declared units. Its B14 subset ran
600 cases. Its real terminal source view also verified the declared
ninety-four retirements without recursive proof construction.

The complete native workflow then passed 46 CTests in 804.85 seconds but
failed three of 1,667 host cases in 869.579 seconds. The complete fifty-command
producer remained unexecuted after its earlier readiness barrier; no complete
current stack certificate, implementation review, commit or merge followed.
These durations diagnose the run, not algorithmic or acceptance thresholds.

The exact unaccepted worktree and index are preserved in local archive
`e6f2c3dc05ca8fbcb04451f40abeeb06a968efd5`; both trees equal the candidate
above. The additional non-authoritative patch
`obj01-abandoned-implementation.patch` has SHA-256
`df45bfe3b3d52cd99a487820725750640c35cf61581e14112e50fe6e060925e9`.
The implementation branch is abandoned. None of these artifacts supplies
authority for a corrected transition.

### One existing identity was omitted

At fixed #240, `tests/host/test_kernel_object_source_layout.py:550-568`
contains eighteen stable neighbor hashes. Its
`STABLE_HASHES/kernel/scheduler_core_validate.c` entry has value
`73bd02fdac06a05c0bdc33130258132e3fe90cfe8c2204f5502e054d5021f38c`.
The corresponding complete-catalog record is already a
`TRANSPORT_IDENTITY` in `kernel-structure`, with the assertion module and
`kernel/scheduler/scheduler_core_validate.c` as its exact payloads,
primary unit B03 and consumers B03/B14.

The assertion
`KernelObjectSourceLayoutTest.test_public_runtime_architecture_driver_and_neighbor_artifacts_are_byte_exact`
compares the complete bytes returned by the existing
`kernel_layout_source.read_bytes_path` directory/include projection, not an
unprojected physical-file hash. The record belongs neither to the original
ninety-four-key plan nor any previous retirement. Retiring the directory
identity or another scheduler body identity does not retire it.

The same fixed C source's `validate_harts`, lines 234-238, accepts an
unscheduled current thread when the hart has no assigned thread.
ADR-0119 already requires removing that unsupported diagnostic exception.
The candidate therefore changed the guarded source, and the complete host
run correctly rejected its old stable-neighbor equality.
This is a missing authority selection, not a bad historical hash or
permission to remove complete diagnostics.

An independent query of the fixed 5,851 records reproduced the original
ninety-four-record digest, then added only this existing record:

| Quantity | Original | Accepted correction |
| --- | --- | --- |
| Selected complete records | 94 | 95 |
| Direct candidate paths | 38 | 38 |
| Local/stack/direct oracle partition | 68 / 11 / 15 | 68 / 11 / 16 |
| Prior claims/retirements | 7 / 185 | 7 / 185 |
| Eventual claims/retirements | 8 / 279 | 8 / 280 |

The corrected complete-record digest is
`df04235e5286127c14a13ce42fdba7eda52bbe8835bb6e72090eccfe62b40c41`.
The complete predecessor digest remains
`2406b1b080963f2e5adc920fc78ea29c1fd8743dd38f25218003bd994a8e2646`.
The runtime/MINIX pins, data structures, mutation rules and complexity
ledger above do not change.

### Why the declared B14 pass did not catch it

The attempted driver inherited the literal `retained_checks` selection from
fixed `tools/check_selected_address_space.py` and added the MEM05/OBJ01
modules. That memory-oriented selection does not include the complete
object source-layout module or `test_stack_source_projection`.
It selects only `PanicTraceAuthorityTest` and `PanicTraceExecutionScopeTest`,
not `PanicTraceMachineTest`. The actual B14 log therefore contains no
execution of the three failed full-suite checks.

The correction requires their complete owning modules in B14, preserving
all inherited cases without duplicate selection. This changes neither the
thirteen-unit set nor the mandatory full-discovery gate.

### Two fixtures confuse historical and current source

`PanicTraceMachineTest.test_real_fault_dispatch_preserves_the_latch_and_exact_context_output`
first compiles actual current fault-prefix sources successfully, then
searches for the old loop-indented output assignment. Direct hart lookup
returns outside that old loop, so the mutation fails before exercising the
machine negative. Select the unique store within the actual named
`micros_hart_resolve_context` definition and retain its wrong-output
compiled negative; do not weaken source/machine admission.

`StackSourceProjectionTest.test_historical_kernel_oracle_keeps_its_original_hash_after_exact_projection`
constructs its temporary input from the fixed historical context source,
but takes its expected bytes from the live current source.
OBJ01 legitimately changes context capture. The fixture must use fixed
historical expected bytes and exact historical projection, while current
source qualification stays independent.

These are bounded immutable fixture adaptations under
[Accepted ADR-0120](../adr/0120-obj01-complete-source-authority.md).
They add no production permission, source-parser behavior, stack budget or
runtime algorithm. Implementation remains blocked until that separate
design separately merges after its clean independent review.

## Classification

| Difference | Classification |
| --- | --- |
| CPU-local/assigned-owner access and singly linked priority queues | Baseline parity |
| RV64 routed pointer, split generation handles and per-thread supervisor stacks | Required adaptation |
| Selected metadata, bounded consumed-prefix checks and failure-atomic commits | Compatible extension |
| Complete corruption diagnostics and exceptional portable range comparison outside ordinary paths | Compatible extension |
| Existing kernel policy before SCHED and unchanged full return projection pending OBJ02 | Staged substitution |
| Global ordinary audits, hot hart scans and unconditional regular stack pairs in the fixed source | Divergence requiring correction |

No timing threshold is the replacement oracle. Native primitive counts,
independent state/ownership cases and retained actual target/stack
evidence supply the proof.

## References

- [Accepted ADR-0119](../adr/0119-selected-object-and-hart-validation.md)
- [Exact contract](../architecture/selected-object-and-hart-validation.md)
- [B03 audit](minix-complete-baseline-b03-objects-scheduler.md)
- [Correction DAG](minix-complete-baseline-findings-and-correction-dag.md)
- [Object/panic authority prerequisite](../architecture/object-and-panic-source-authority.md)

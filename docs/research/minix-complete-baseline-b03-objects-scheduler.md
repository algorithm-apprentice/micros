# B03 Object and Scheduler Baseline Audit

## Status

```text
audit unit:       B03 objects and scheduler
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; six blocking findings; not certified
implementation:   forbidden during complete-audit phase
```

B03 audits process/thread/hart representation, identity and lifecycle,
supervisor stacks, complete registry validation, ready queues, runtime flags,
policy installation, accounting, preemption, common user return, context
selection, and idle.

## Exact primary path manifest

B03 has 35 primary paths in the pinned tree.

### Public contracts

| Path | Responsibility |
| --- | --- |
| `include/micros/kernel_object_runtime.h` | Authoritative target registry and exact runtime wrappers |
| `include/micros/kernel_objects.h` | Process/thread/hart layouts, handles, capacities, flags, queues |
| `include/micros/scheduler.h` | Target scheduler initialization, trap, timer, and return API |
| `include/micros/scheduler_core.h` | Portable scheduler operations, plans, guards, IPC transitions |
| `include/micros/user_context.h` | Saved RV64 user context layout |

### Object owners

| Path | Responsibility |
| --- | --- |
| `kernel/objects/kernel_object_context.c` | Context/stack overlap preflight |
| `kernel/objects/kernel_object_hart.c` | Hart register/resolve/context/stacks/current |
| `kernel/objects/kernel_object_identity.c` | Handle validation, resolution, generation advance |
| `kernel/objects/kernel_object_private.h` | Private object declarations |
| `kernel/objects/kernel_object_process.c` | Process create/attach/detach/release |
| `kernel/objects/kernel_object_runtime.c` | Static authoritative target registry |
| `kernel/objects/kernel_object_runtime_internal.h` | Runtime-private declarations |
| `kernel/objects/kernel_object_stack.c` | Stack range/overlap primitives |
| `kernel/objects/kernel_object_state.c` | Zero-state, context, scheduler, IPC, trap/timer helpers |
| `kernel/objects/kernel_object_thread.c` | Thread create/resolve/release |
| `kernel/objects/kernel_object_validation.c` | Complete process/thread/hart base validation |
| `kernel/objects/kernel_objects.c` | Initialization and scheduler-complete validator entry |

### Scheduler owners

| Path | Responsibility |
| --- | --- |
| `kernel/scheduler/scheduler.c` | Target initialization, admission, first start |
| `kernel/scheduler/scheduler_completion.c` | Deferred completion preflight/commit |
| `kernel/scheduler/scheduler_context.c` | Selected context/root/accounting preparation |
| `kernel/scheduler/scheduler_core.c` | Identity/bitmap/common portable primitives |
| `kernel/scheduler/scheduler_core_accounting.c` | Thread/kernel/idle counter accounting |
| `kernel/scheduler/scheduler_core_internal.h` | Portable internal contract |
| `kernel/scheduler/scheduler_core_ipc.c` | Reversible current-thread IPC guard |
| `kernel/scheduler/scheduler_core_ipc_transition.c` | Atomic IPC block/wake transitions |
| `kernel/scheduler/scheduler_core_policy.c` | Admission, hold/remove, RTS flags, policy renewal |
| `kernel/scheduler/scheduler_core_private.h` | Scheduler-private declarations |
| `kernel/scheduler/scheduler_core_queue.c` | Queue enqueue/dequeue/pick |
| `kernel/scheduler/scheduler_core_return.c` | Return-plan verification and commit |
| `kernel/scheduler/scheduler_core_return_plan.c` | Queue projection and selected-return plan |
| `kernel/scheduler/scheduler_core_validate.c` | Complete queue/current/accounting validation |
| `kernel/scheduler/scheduler_idle.c` | Idle root/accounting/stack selection |
| `kernel/scheduler/scheduler_interrupt.c` | U/S timer entry adaptation |
| `kernel/scheduler/scheduler_return.c` | Common selected user return |
| `kernel/scheduler/scheduler_runtime_internal.h` | Target scheduler state and helpers |

## Cross-owner source dependencies

| Path | B03 operation | Primary/later owner |
| --- | --- | --- |
| `arch/riscv64/scheduler.S`, `arch/riscv64/scheduler_context.h` | B03-O06 idle pivot/wait state | B01 primary, B03 semantic |
| `arch/riscv64/scheduler_test.S` | B03-O06 target context evidence | B14 |
| `arch/riscv64/user_entry.S` | B03-O06 direct initial register restore | B01 primary, B05-O03 |
| `arch/riscv64/trap.S`, `arch/riscv64/trap_context.h` | B03-O06 ordinary trap-frame restore | B01 primary, B05-O01 |
| `arch/riscv64/interrupt.h` | B03-O07 serialization and timer masks | B01 primary |
| `arch/riscv64/kernel.ld` | B03-O02 thread/trap stack geometry | B01 primary |
| `kernel/memory/user_address_space_activation.c` and validation paths | B03-O06 root validation/activation | B02 |
| `kernel/execution/user_execution_validation.c` and context owners | B03-O06 outgoing/selected context validation | B05 |
| `kernel/trap/**` | B03-O06/O07 U-entry/common return | B05 |
| `kernel/ipc/**` | B03-O05 IPC RTS reasons and wake paths | B04 |
| `kernel/endpoint/endpoint_object_validation.c`, `kernel/ipc/ipc_buffer.c` | B03-O06 pending completion validation/write plan | B04 |
| `kernel/platform/timer.c` | B03-O07 forced entry and return rearm | B01 |

## Fixed MINIX source evidence

The B03 comparison uses only the local fixed checkout:

- `minix/kernel/proc.h`
- `minix/kernel/proc.c`
- `minix/kernel/cpulocals.h`
- `minix/kernel/main.c`
- `minix/kernel/clock.c`
- `minix/kernel/interrupt.c`
- `minix/kernel/debug.c`
- `minix/include/minix/config.h`
- `minix/kernel/system.c`
- `minix/kernel/system/do_fork.c`
- `minix/kernel/system/do_clear.c`
- `minix/kernel/system/do_exec.c`
- `minix/kernel/system/do_schedctl.c`
- `minix/kernel/system/do_schedule.c`
- `minix/kernel/arch/i386/mpx.S`
- `minix/kernel/arch/i386/arch_clock.c`
- `minix/kernel/arch/i386/arch_system.c`
- `minix/kernel/arch/earm/mpx.S`
- `minix/kernel/arch/earm/arch_clock.c`
- `minix/kernel/arch/earm/arch_system.c`
- `minix/servers/sched/main.c`
- `minix/servers/sched/schedule.c`
- `minix/servers/sched/schedproc.h`

## Logical variables

| Variable | Meaning |
| --- | --- |
| `pcap` | process-slot capacity |
| `tcap` | thread-slot capacity |
| `hcap` | hart-slot capacity |
| `q` | members in one ready queue |
| `r` | all runnable/queued threads on one hart |
| `k` | fixed priority count, 16 |
| `z` | zero-quantum candidates examined in one return |
| `Rmem` | B02 complete selected-root/context validation work |
| `Vipc` | B04 complete endpoint/object validation work for one pending completion |

The pinned capacities are 64 processes, 128 threads, 8 harts, and 16
priorities. One-thread and one-hart v0.1 policy does not remove the capacity
variables from the audit.

## Operation ledger

### B03-O01 Complete object and scheduler validation

MINIX uses local assertions and optional queue sanity checks. Ready-queue
sanity walks process slots and queue links linearly. Ordinary queue, flag,
accounting, and selection operations do not first validate every process,
stack, CPU, and queue.

`micros_kernel_objects_validate()` delegates to
`micros_scheduler_core_validate()`. One call performs:

- `O(pcap)` process-slot checks;
- `O(tcap)` thread-slot checks;
- pairwise prior-thread stack comparison, `O(tcap^2)`;
- thread-versus-hart stack comparison, `O(tcap*hcap)`;
- pairwise hart identity/stack comparison, `O(hcap^2)`;
- queue traversal across live harts, `O(r + hcap*k)`;
- complete thread/queue/current/accounting agreement, `O(tcap+hcap)`; and
- scheduler/object witness arrays with peak automatic storage
  `O(pcap+tcap)`.

This complete validator runs at the start of admission, hold, remove, generic
RTS transitions, policy installation, ready selection, return planning,
accounting-entry transitions, IPC scheduler transitions, and many target
wrapper operations.

| Difference | Classification |
| --- | --- |
| Complete corruption validator and generation/stack checks | Compatible extension |
| Complete capacity/pairwise validation on ordinary scheduler valid paths | Divergence requiring correction |

See B03-F001 and B03-F003.

### B03-O02 Process, thread, hart, context, and stack lifecycle

MINIX stores saved registers, scheduling, endpoint, address-space, IPC, and
runtime flags in one fixed `struct proc` table. Privileged fork prepares an
exact requested destination slot. Per-CPU current/idle and stack state lives
in CPU-local structures.

`micros` separates:

- generation-safe process slots;
- generation-safe thread slots;
- generation-safe hart slots;
- exact process-to-thread ownership;
- per-thread user context and fixed supervisor stack;
- per-hart primary/idle/emergency trap stacks; and
- endpoint/address-space state owned by other subsystems.

Exact handle resolution is direct slot lookup, `O(1)`. Production process
creation uses `create_at`, also `O(1)`. The convenience process allocator scans
`pcap`. Thread creation scans `tcap` for the first reusable slot. Hart
registration scans `hcap` for duplicate hardware ID and then for a free slot.
Thread release checks all `hcap` current bindings. Stack installation checks
existing `tcap+hcap` stack ranges.

| Difference | Classification |
| --- | --- |
| Separate process/thread/hart objects | Compatible extension |
| Exact generation handles and terminal quarantine | Compatible extension |
| RV64 context and per-thread supervisor stack | Required adaptation |
| One-thread-per-process policy | Staged substitution; SOM-08 |
| One-hart v0.1 target policy | Required adaptation |

The first-free thread allocator is revisited with SOM-08 rather than treated
as the final POSIX thread-allocation policy.

### B03-O03 Current-hart and exact identity resolution

MINIX obtains CPU-local state directly from CPU-local storage and resolves a
process by exact slot/endpoint.

Most `micros` process, thread, and hart handles resolve in `O(1)` by slot and
generation. Three hot ownership helpers instead scan all `hcap` harts:

- trap-time hart-context resolution searches for the object whose address
  equals the supplied context pointer;
- `scheduler_core_thread_is_current()` searches every current-hart handle; and
- execution-context capture searches every hart for the exact current thread.

```text
MINIX CPU-local current: O(1)
micros hart/current ownership: O(hcap)
```

**Classification:** Divergence requiring correction. See B03-F004.

### B03-O04 Ready queues and selection

Both systems use:

- one singly linked queue per CPU/hart and priority;
- head and tail pointers;
- tail enqueue for newly runnable work;
- head reinsertion for early-preempted work with remaining quantum;
- arbitrary-position dequeue with tail repair; and
- first-nonempty-priority head selection without dequeueing current.

```text
tail enqueue: O(1)
head enqueue: O(1)
dequeue:      O(q)
pick:         O(k), k = 16
storage:      O(k) per hart plus one next link per schedulable record
```

| Difference | Classification |
| --- | --- |
| Priority queue structure and enqueue/dequeue/pick algorithms | Baseline parity |
| Generation-safe links and explicit membership bits | Compatible extension |

### B03-O05 Runtime flags and policy transitions

MINIX derives runnable state from `p_rts_flags == 0`. First flag set dequeues;
last flag clear enqueues. The kernel retains queue/selection authority while
SCHED supplies priority, quantum, CPU, and niceness.

`micros` reproduces the zero/nonzero RTS boundary on threads. Current-thread
preemption repair remains part of one return plan. The current bootstrap policy
renews quantum in kernel because SCHED does not yet exist.

| Difference | Classification |
| --- | --- |
| Independent RTS reasons and zero-flags runnable state | Baseline parity |
| Strict priority bound and failure-atomic policy stores | Compatible extension |
| In-kernel fixed policy instead of user SCHED takeover/demotion/restoration | Staged substitution; SOM-02 |
| Missing user-signal/process-fault blocking/delivery reasons | Staged substitution; SOM-09 |

The transition logic itself uses the baseline queue operations, but every
public policy/flag operation currently pays B03-O01.

### B03-O06 Common selected-user return and context restore

MINIX `switch_to_user()` repairs the current process state, selects the first
ready head or idle, handles one quantum-expiration transition, accounts the
kernel interval, restarts the timer if needed, binds the root, and restores the
selected frame. It mutates only affected links/state.

`micros_hart_plan_user_return()`:

1. runs B03-O01;
2. copies every ready queue into a static
   `slots[k][tcap]` projection;
3. repairs preemption in the projection;
4. repeatedly removes/requeues zero-quantum candidates;
5. reconstructs complete head/tail/next arrays and ready/renew bitmaps;
6. publishes a 1,344-byte return plan; and
7. commits by scanning all `tcap` threads and all `k` queues.

`scratch_remove()` shifts the remaining projected queue. With `z` zero-quantum
candidates, planning can reach `O(r + z*r)`, worst `O(tcap^2)`. Commit is
`O(tcap+k)`. The portable checked commit recomputes the complete plan and
compares every field before applying it.

The target common return also validates/stores the outgoing context, validates
the selected root, inspects and validates the selected context, and may invoke
multiple B02 address-space lookups. Its memory/context term is recorded as
`Rmem` and remains the blocking B02-F005 dependency.

If the selected thread has a staged IPC completion,
`scheduler_completion.c` additionally:

1. validates the complete endpoint/object relation through B04;
2. maps the staged result;
3. prepares a user-buffer write that may translate two pages; and
4. commits the message/result before return.

This adds `Vipc + 2*Rmem` to a pending-completion return. MINIX performs the
selected process's deferred message copy without first auditing every object
and endpoint.

```text
MINIX queue/selection work: O(z*k + affected queue links)
micros scheduler-only work: O(tcap^2 + tcap + k)
micros complete return:     scheduler work + repeated Rmem
pending IPC completion:     + Vipc + up to 2*Rmem
static scheduler scratch:   O(k*tcap)
automatic plan copies:      two plans can coexist, O(tcap+k)
```

| Difference | Classification |
| --- | --- |
| One common reschedulable U-return boundary | Baseline parity |
| RISC-V context/root/fence restore | Required adaptation |
| Complete queue projection/rebuild for one return | Divergence requiring correction |
| Repeated complete outgoing/selected root-context validation | Divergence requiring correction |
| Complete endpoint/object audit and repeated translation for one pending completion | Divergence requiring correction |

See B03-F002, B03-F005, and B03-F006.

### B03-O07 Accounting, preemption, timer, and idle

MINIX charges elapsed process execution on kernel entry, accounts kernel/idle
intervals separately, notices exhausted quantum at common return, and uses the
timer for bounded preemption rather than treating one tick as one quantum.

`micros_scheduler_account_user_trap()` performs direct hart/thread checks and
subtracts the elapsed counter delta in `O(1)`. Kernel/idle accounting uses the
same counter boundary. Timer return preparation remains B01-O09. Idle selects
the kernel root, idle stack, idle accounting owner, and WFI continuation.

Entering thread or idle accounting currently invokes B03-O01 first; the
arithmetic and ownership transition themselves are `O(1)`.

| Difference | Classification |
| --- | --- |
| Elapsed thread, separate kernel, and separate idle accounting | Baseline parity |
| SBI counter/timer and RISC-V idle transition | Required adaptation |
| Complete validator before enter-thread/enter-idle accounting | Divergence requiring correction; B03-F001 |

### B03-O08 User scheduler and load history

MINIX SCHED owns takeover, priority/quantum/CPU/niceness policy, no-quantum
handling, demotion, and periodic priority restoration. CLOCK maintains
runnable-load history.

`micros` currently has neither SCHED nor load-history publication.

| Difference | Classification |
| --- | --- |
| User-space scheduling policy | Staged substitution; SOM-02 |
| Runnable-load history and query | Staged substitution; SOM-21 |

### B03-O09 Invalid context and untrusted fault policy

The current trusted-bootstrap path panics when outgoing or selected context is
invalid. MINIX removes or inhibits invalid/faulted processes and routes policy
through VM/PM/signals rather than leaving a bad process selectable.

**Classification:** Staged substitution; SOM-09. The later process-fault
design must atomically remove scheduler ownership and publish the POSIX-visible
fault/signal outcome.

## Time, stack, and auxiliary-space envelope

Persistent object storage is `O(pcap+tcap+hcap)`. The slot-derived supervisor
stack pool reserves `tcap * 16 KiB`, or 2 MiB at the pinned capacity.

Return planning owns 4,224 bytes of static queue-projection scratch. The target
return path can hold the caller's 1,344-byte plan and the planner's
1,344-byte candidate simultaneously. Checked portable commit can hold the
caller's plan, a 1,344-byte `expected`, and the nested 1,344-byte candidate:
4,032 bytes of plan objects before ordinary call frames.

`micros_scheduler_commit_ipc_transitions()` places
`tcap` transition candidates on the stack, 5,120 bytes on RV64. Complete
registry validation adds `O(pcap+tcap)` automatic witness arrays. These
objects share the 16 KiB supervisor-stack budget with their callers.

| Operation | MINIX valid path | `micros` valid path | Auxiliary/stack |
| --- | --- | --- | --- |
| Exact slot/handle resolve | `O(1)` | `O(1)` except trap hart context `O(hcap)` | Constant |
| Complete registry check | Optional/debug linear queue/process sanity | `O(pcap+tcap^2+tcap*hcap+hcap^2+r+hcap*k)` | `O(pcap+tcap)` automatic witnesses |
| Enqueue/pick/dequeue | `O(1)`, `O(k)`, `O(q)` | Same queue mechanism after validation | Queue links in records |
| RTS set/unset/policy | Affected queue operation | Complete registry check plus affected queue | Validator witnesses |
| User-trap accounting | `O(1)` | `O(1)` | Constant |
| Return planning/commit | `O(z*k + affected queue links)` | Worst `O(tcap^2+tcap+k)` plus repeated `Rmem` and optional `Vipc+2*Rmem` | 4,224-byte static projection; target peak 2,688 plan bytes; checked-commit peak 4,032 plan bytes |
| Stack installation | Per-CPU stack setup | `O(tcap+hcap)` overlap checks | Constant |
| Batched IPC scheduler transitions | Affected participants | Complete validator plus `O(request_count)` | 5,120-byte maximum transition-candidate array plus validator witnesses |

## Executable evidence mapping

| Evidence | What it proves | What it does not prove |
| --- | --- | --- |
| `kernel-objects` | generation lifecycle, distinct objects, contexts/stacks, seeded object model | hot-path validator cost; trap-context lookup bound |
| `scheduler-core` | RTS flags, queues, policy, accounting, return plans, two-hart model | parity of work performed per return |
| `test-qemu-object-model` | real object generations and one-thread policy | POSIX thread allocation |
| `test-qemu-scheduler` | repeated U-mode switching, accounting, idle, context preservation | queue/validation operation counts |
| invalid-outgoing/invalid-next scheduler workflows | fatal trusted-context diagnostics | later process-fault/signal policy |
| downstream IPC/grant/address-space workflows | scheduler integration and wakeups | scheduler-only versus cross-owner work attribution |

## Evidence gaps

| ID | Gap | Later owner |
| --- | --- | --- |
| B03-E001 | No validator counter separates process, thread-pair, thread-hart, hart-pair, queue, and witness work | B14 |
| B03-E002 | No return-plan counter records queue loads, shifted slots, zero-quantum iterations, projected links, plan comparison, and commit stores | B14 |
| B03-E003 | No trap-entry counter proves `O(1)` hart-context ownership resolution | B14 |
| B03-E004 | No common-return counter separates scheduler work from repeated B02 root/context validation | B14 |
| B03-E005 | No load-history evidence exists because SOM-21 is absent | B14 after SOM-21 design |
| B03-E006 | No target stack-usage evidence proves worst nested return, checked-commit, IPC-transition, and validator frames remain within 16 KiB | B14 |
| B03-E007 | No pending-completion counter separates endpoint/object audit, translated pages, message copy, and local selected-thread commit | B14 |

## Findings register

### B03-F001: Complete registry validation precedes ordinary scheduler operations

- Severity: High
- `micros` evidence:
  - `micros_scheduler_core_validate`
  - policy/flag/accounting/IPC/return call sites
- MINIX evidence: local queue operations and optional queue sanity checks in
  `proc.c`/`debug.c`
- Difference: ordinary valid paths pay
  `O(pcap+tcap^2+tcap*hcap+hcap^2+r+hcap*k)` before their local transition.
- Impact: queue wakeups, policy changes, ready selection, and return planning
  scale with configured object capacities rather than affected state.
- Smallest correction boundary: retain one explicit complete diagnostic
  validator while ordinary transitions use exact local preconditions and
  incrementally maintained counts/membership invariants.
- Dependencies: B02-F002/B02-F005 and B04 IPC scheduler consumers.

### B03-F002: Common return rebuilds complete queue state

- Severity: High
- `micros` evidence:
  - `scheduler_core_return_plan.c`
  - `scheduler_core_return.c`
- MINIX evidence: `switch_to_user()`, queue helpers, and `proc_no_time()` in
  `minix/kernel/proc.c`
- Difference: one return copies, shifts, projects, compares, and reapplies
  whole-capacity queue/link state.
- Impact: worst `O(tcap^2)` scheduler work and kilobyte-scale plan copies on
  every reschedulable return instead of affected-link updates.
- Smallest correction boundary: a bounded delta plan containing only outgoing,
  selected, affected queue head/tail/link, preemption repair, and quantum
  renewal stores, with local preflight and non-failing commit.
- Dependencies: B03-F001 and B04 completion/IPC transitions.

### B03-F003: Stack-disjointness validation is pairwise

- Severity: Medium
- `micros` evidence:
  - `kernel_object_validation_thread_context_matches`
  - `kernel_object_validation_hart_stacks_avoid_peers`
- Difference: complete validation compares every attached thread stack with
  prior threads and every hart stack with prior harts, despite slot-derived
  thread-stack geometry and fixed hart stack owners.
- Impact: `O(tcap^2+tcap*hcap+hcap^2)` work dominates the complete validator.
- Smallest correction boundary: prove slot-derived thread stacks and
  linker-owned hart stacks through direct index/range geometry, reserving
  pairwise checks only for genuinely caller-supplied exceptional stacks.
- Dependencies: B05 trap/stack audit.

### B03-F004: Hot current-hart/thread ownership scans every hart

- Severity: High
- `micros` evidence:
  - `micros_hart_resolve_context`
  - `micros_kernel_object_runtime_hart_from_context`
  - `scheduler_core_thread_is_current`
  - `micros_thread_capture_execution_context`
  - trap-context call sites
- MINIX evidence: CPU-local current/stack ownership
- Difference: the supplied context is already a hart-object pointer and each
  thread stores its assigned hart, but trap resolution, current-membership
  checks, and execution-context capture linearly search all `hcap` slots.
- Impact: every interrupt, exception, syscall, generic scheduler/IPC current
  check, and normal context capture can gain capacity-linear ownership work.
- Smallest correction boundary: range/alignment/slot derivation and direct
  assigned/current handle comparison with exact live-generation validation in
  `O(1)`. Whole-hart uniqueness remains only in the explicit diagnostic
  validator.
- Dependencies: B05 trap entry.

### B03-F005: Common return repeats complete memory/context validation

- Severity: High
- `micros` evidence:
  - `scheduler_return.c:capture_outgoing_custody`
  - `scheduler_runtime_validate_selected_thread`
  - `scheduler_runtime_load_selected_context`
- MINIX evidence: selected process frame/root is bound directly at common
  return after local state checks
- Difference: outgoing and selected context checks invoke multiple public
  B02 lookups/root audits around one scheduler return.
- Impact: one context switch inherits repeated
  `Rmem = O(Vobj+pmax+p+512*t+(t+l)*gmax+p/64)` terms in addition to
  B03-F001/F002.
- Smallest correction boundary: one retained path-local validation plan for
  outgoing/selected context and root, consumed once by the non-failing return
  commit; complete B02 audits remain explicit diagnostic boundaries.
- Dependencies: B02-F005 and B05 user-context audit.

### B03-F006: Deferred IPC completion repeats global validation

- Severity: High
- `micros` evidence:
  - `kernel/scheduler/scheduler_completion.c`
  - `kernel/endpoint/endpoint_object_validation.c`
  - `kernel/ipc/ipc_buffer.c`
- MINIX evidence: selected-process deferred message copy in
  `minix/kernel/proc.c`
- Difference: one pending completion runs complete endpoint/object validation
  and may run two more complete address translations before copying one
  message/result to the selected thread.
- Impact: a common return with pending IPC adds `Vipc + 2*Rmem`, coupling
  selected-thread completion to global endpoint/object and physical-memory
  capacities.
- Smallest correction boundary: a retained completion plan that validates only
  the selected endpoint/thread/token and at most two path-local PTE/owner
  translations, then commits the message and result once. Complete B04 audits
  remain explicit diagnostic boundaries.
- Dependencies: B02-F005, B03-F001, and B04 endpoint/IPC audit.

## Unit conclusion

B03 primary-path and operation coverage is complete for the pinned tree. The
audit adds B03-F001 through B03-F006 as blocking findings and
records SOM-02, SOM-08, SOM-09, and SOM-21 staged obligations.

B03 is **audited but not certified**. Independent review corrected deferred
IPC completion, current-hart ownership scans, persistent/automatic stack
storage, and the exact restore/source manifests. Final re-review reported B03
complete. No B03 correction may start during the read-only audit.

## References

- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)
- [B02 memory audit](minix-complete-baseline-b02-memory.md)
- [Scheduler/context-switch study](minix-scheduler-and-context-switch.md)
- [Kernel object source baseline](minix-kernel-object-source-organization.md)
- [Kernel scheduler source baseline](minix-kernel-scheduler-source-organization.md)

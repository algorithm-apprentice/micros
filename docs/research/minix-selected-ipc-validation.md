# MINIX Selected IPC Validation Study

## Fixed inputs and question

```text
micros source: c8b8a26766f4908016d0aeedc8af95f990eca283
micros tree:   06cb9cbb41fb576d6d8e8dd1f41e0620b17359d5
MINIX source:  4db99f4012570a577414fe2a43697b2f239b699e
```

The local MINIX checkout at `/Users/alephling/Repo/minix` was verified at
that commit with a clean worktree. This study reads the actual reference and
current `micros` source; it contains no implementation prototype or claimed
runtime Red/Green.

IPC01 owns only B03-F006, B04-F001 and B04-F002. MEM04 supplies selected
translation, OBJ01 selected scheduler/object checks, and OBJ02 bounded return
planning/context reuse. The remaining question is why one endpoint query,
local IPC transition or selected completion still audits unrelated global
state, and which concrete checks must replace that implicit proof.

The original [B04 audit](minix-complete-baseline-b04-endpoint-ipc.md) and
[B03 completion finding](minix-complete-baseline-b03-objects-scheduler.md#b03-f006-deferred-ipc-completion-repeats-global-validation)
remain historical evidence. This study updates their call-path analysis
against the fixed current tree; it does not reopen their completed prerequisites.

## Reference mechanism

All reference paths below are relative to the fixed MINIX checkout.

| Mechanism | Fixed source | Behavior, structure and work |
| --- | --- | --- |
| Endpoint resolution | `minix/kernel/proc.c:1828-1860`, `minix/include/minix/endpoint.h` | Decode a process slot, require a nonempty slot and full endpoint equality. One table entry; `O(1)` time and auxiliary space. |
| Operation/destination permission | `minix/kernel/proc.c:479-598`, `minix/kernel/priv.h:20-66` | Resolve the named peer and test installed operation/destination bitmaps. No profile-name or whole-process-table audit per bit query. |
| Immediate send | `minix/kernel/proc.c:868-927` | Test the destination's receiving state, copy the fixed message before publication, set the source and delivery state, then clear the receiving reason. Constant local work, apart from receiver-filter matching. |
| Blocked send | `minix/kernel/proc.c:928-960` | Check deadlock, snapshot the message, publish sending state and append by walking the caller queue. `O(d + q_s)` time, constant queue auxiliary space. |
| Receive | `minix/kernel/proc.c:965-1117` | Pending notification, asynchronous work, then the first matching blocked sender; preserve unmatched order. No global relation reconstruction. |
| Pending source selection | `minix/kernel/proc.c:770-845` | Specific-source bit test or lowest nonzero bitmap chunk/bit search. `O(1)` specific lookup; worst case linear in bitmap/source capacity for `ANY`. |
| Notify | `minix/kernel/proc.c:1120-1168` | Immediate selected-receiver delivery or one destination pending-bit update; reply-pending excludes notification delivery. No notifier wait or full-table validation. |
| Deferred user delivery | `minix/kernel/proc.c:255-290` | Copy the selected process's retained message, clear its delivery state only after success, and return the result. A user-copy fault enters the reference VM/signal protocol. |
| Endpoint teardown | `minix/kernel/system.c:500-610` | Inhibit the closing endpoint, unlink its send, then scan process references and clear/wake affected peers. The global reference scan is real teardown work, not an ordinary-operation prerequisite. |
| Deadlock projection | `minix/kernel/proc.c:701-767` | Follow blocked endpoint dependencies until no dependency, `ANY` or a cycle. `O(d)` reference mechanism; IPC02 owns the remaining `micros` projection divergence. |

These are deterministic mechanisms; average and amortized bounds do not
hide a larger ordinary-path scan. Receiver-owned filters add their own
matching cost in MINIX. They remain the explicit SOM-24 staged substitution,
not an omitted cost to claim implemented in IPC01.

MINIX uses a process-centric `struct proc` and installed privilege pointers.
`micros` retains distinct process/thread/endpoint/hart identities, exact
generations, immutable per-operation profiles and a 64-byte message with a
48-byte payload. Raw reference pointers do not cross a `micros` IPC boundary.
MINIX's ordinary authorized reply is not the `micros` one-shot token model.

## Actual current hot paths

Let `P`, `T`, `H` and `F` be process/endpoint, thread, hart and profile
capacities; `q_s`/`q_r` the inspected sender/receiver queue lengths; `d` the
dependency-chain length; and `m` the number of scheduler transitions.
Current capacities are 64, 128, 8 and 32 respectively; they remain logical
variables for complexity analysis.

`endpoint_registry.c:profile_table_is_canonical` checks canonical profile
bytes, duplicate names and target closure. `endpoint_records_are_canonical`
also checks every endpoint and its entire pending-event array:

```text
Vreg = O(F^2 * name_bytes + P^2)
```

`endpoint_object_validation.c:micros_endpoint_registry_validate_objects`
adds complete object/scheduler validation, all process/endpoint relations,
queues, pending sources, pairwise matchability and token uniqueness.
Its process loop calls `micros_privilege_profile_resolve`, which invokes
`Vreg` again for each installed process profile. Thus the current combined
cost still includes `P * Vreg` and a `P^3` term, even after OBJ01/OBJ02.

Representative composition:

- `endpoint.c`: internal resolution first invokes the complete combined
  validator; active resolution and endpoint authorization wrap it.
- `endpoint_privilege.c`: one target authorization resolves the source for
  the operation bit, resolves it again for the destination mask, then resolves
  the destination profile. Each resolution reruns `Vreg`.
- `ipc.c`: send/receive/call directly invoke the complete validator, then
  authorization and rendezvous perform further complete preflights. A failed
  immediate match calls enqueue, which resolves/searches again.
- `ipc_notifications.c`: user notify validates, authorizes and re-resolves
  the destination; kernel-notification preparation also validates the complete
  state before resolving the same selected destination.
- `ipc_tokens.c`: reply preflight adds complete validation before its existing
  bounded token and undelivered-request scans.
- `ipc_syscall.c:resolve_current_runtime` invokes the complete runtime audit
  before request preflight, then the portable operation repeats it.
- `scheduler_completion.c:preflight_selected_completion` invokes the combined
  endpoint/object audit for one pending completion. Its existing buffer plan
  already uses MEM04's selected translations and retains at most two chunks.

The fixed source has **31 direct production call sites** to
`micros_endpoint_registry_validate`, `micros_endpoint_registry_validate_objects`
or `micros_ipc_runtime_validate`. The proposed contract assigns nineteen
ordinary-path sites to selected admission and preserves twelve explicit
initialization/diagnostic/other-subsystem sites. Target test audit calls are
additional preservation evidence, not part of this production census.

This is a source-call census, not a measured timing or executed Red claim.
Actual work-count Red begins only after the design merges.

## What the removed audit currently proves

Deleting calls alone is incorrect. Current local helpers rely on the complete
validator for safety and authority:

| Consumer | Implicit proof that needs an explicit selected owner |
| --- | --- |
| `ipc_common.c:ipc_resolve_operation_thread` | Bounds/live generation for the thread's process before indexing it; canonical held/clear state; non-current ownership |
| `ipc_queue.c` matching/readiness helpers | Head/next slot and generation before indexing; queue-kind/owner/RTS/message shape; termination and exact splice neighbors |
| `ipc_rendezvous.c` | Caller/destination identity, a non-stale append tail, absence of an immediate match before blocking, and no fallible operation after scheduler mutation |
| `ipc_tokens.c` | Canonical selected reply waiter and callee; token range/uniqueness; staged-request return boundary; no guessing a not-yet-presentable token |
| `endpoint_completion_validation.c` | Selected source/receiver/caller bounds before array access, full completion shape, special kernel/source-only envelopes |
| `ipc_close.c` | Complete consumed queue topology and relevant references before rebuilding queues and canceling dependents; stale same-slot references must not escape by failing full-endpoint equality |
| Endpoint lifecycle helpers | Exact endpoint/process/lifecycle/profile relation and held/current-thread conditions; safe traversal of the harts/threads actually consulted |

`ipc_find_matching_*` currently returns Boolean. After global admission is
removed, corruption must be distinguishable from ordinary no-match; otherwise
a malformed queue could turn into a blocked success. Similarly a prefix proof
must validate a successor before an unlink publishes it, not just validate
the matched node.

`ipc_close.c:preflight_close_queue` also has a directly coupled boundary
defect: it checks null at the beginning of each of `T` iterations, then
unconditionally rejects. A valid queue whose `T`th node points to null is
therefore rejected. The replacement needs a full-capacity regression and a
final-null success check, not a larger cap. This is static source evidence;
the implementation phase must supply the actual reproducing Red.

## Existing mechanisms that remain separate

The current token allocation/lookup and undelivered-request checks scan
thread slots. IPC03 owns their indexed replacement. A selected token-bearing
completion may need one such bounded scan to establish exact caller binding
and staged uniqueness; IPC01 must not claim that path is constant-time.

The current deadlock walk costs `O(d*T + d^2)` with an `O(T)` visited array.
IPC02 owns its replacement. Sender-tail storage/append remains the existing
IPC04 divergence until that separate outcome; IPC01 must not silently change
queue order or introduce another persistent index.

The selected scheduler transition family is already supplied by OBJ01.
Its capacity-sized transition workspace and the existing close-plan workspace
remain stack-accounted. Replacing global endpoint admission is not permission
to enlarge stacks, hide those live frames or import a scheduler redesign.

Initialization, complete diagnostics and whole-owner teardown have different
claims from a selected query. A close still has to find affected references;
replacing it with an unchecked constant-time clear would not reproduce MINIX.

## Classification and target adaptation

| Difference | Classification | IPC01 treatment |
| --- | --- | --- |
| Direct endpoint identity and installed permission-bit queries | Baseline parity | Restore operation-local admission and reuse its selected records |
| Distinct thread queues, generations and process-owned endpoints | Compatible extension | Preserve exact owner/generation checks on every consumed link |
| One-shot token and request-return authority, apart from the search algorithm | Compatible extension | Preserve semantics; the separate divergence row accounts for current linear token scans |
| Checked retained plans and failure-atomic publication | Compatible extension | Keep checks before mutation and plans within one serialized operation |
| RV64 traps and at-most-two-page retained physical copy | Required adaptation | Preserve SIE-clear lifetime, ABI, buffer checks and real hardware evidence |
| Trusted-bootstrap invalid-buffer panic instead of VM-assisted retry | Staged substitution | Retain SOM-04 and the exact existing fatal diagnostic |
| Receiver filters, asynchronous IPC and multithreaded deadlock exploration | Staged substitution | Retain SOM-24, SOM-23 and SOM-08 replacement points |
| Full endpoint/profile/object audits on selected operations | Divergence requiring correction | IPC01's three owned findings |
| Deadlock projection, token indexing and sender-tail organization | Divergence requiring correction | Remain IPC02, IPC03 and IPC04; no closure claimed here |

The reviewed [selected IPC contract](../architecture/selected-ipc-validation.md)
defines consumed state, failure boundaries, work/storage evidence and the
retained audit ledger. It does not derive expected results from edited code,
create source-retirement metadata or borrow historical executions as current
acceptance.

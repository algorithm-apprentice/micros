# B08 PM and Spawn Baseline Audit

## Status

```text
audit unit:       B08 PM and spawn
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; six blocking findings; not certified
implementation:   forbidden during complete-audit phase
```

B08 audits PM semantic identity, process-table representation, PID and slot
allocation, parent/reaper relationships, hidden spawn/rollback, exit,
zombie/wait, kernel operation 13, PM/VFS/VM coordination, and canonical spawn
request/stack-layout algorithms.

## Exact primary path manifest

B08 has 59 primary paths in the pinned tree.

### Public contracts

| Path | Responsibility |
| --- | --- |
| `include/micros/pm.h` | PM messages/results, operation-13 commands, fixed output objects |
| `include/micros/spawn.h` | Spawn object bounds, descriptor actions, application addresses, auxv types |

### Kernel PM-control owners

| Path | Responsibility |
| --- | --- |
| `kernel/pm/pm_control_abi.c` | Operation-13 register decoding |
| `kernel/pm/pm_control_activation.c` | Final activation preflight/commit |
| `kernel/pm/pm_control_core.c` | Common predicates, storage, state preparation |
| `kernel/pm/pm_control_core.h` | PM-control state, plans, errors, transitions |
| `kernel/pm/pm_control_execution.c` | Execution preparation and prepared rollback |
| `kernel/pm/pm_control_exit.c` | Exit begin, detach, and empty-process release |
| `kernel/pm/pm_control_internal.h` | Private common PM-control contracts |
| `kernel/pm/pm_control_reservation.c` | Empty-process reserve/abort |
| `kernel/pm/pm_control_runtime.c` | Static authoritative state and complete runtime validation |
| `kernel/pm/pm_control_runtime.h` | Target runtime interface |
| `kernel/pm/pm_control_runtime_core.c` | PM/VM/frame reservation and exit agreement |
| `kernel/pm/pm_control_runtime_core.h` | Private runtime agreement interface |
| `kernel/pm/pm_control_syscall.c` | Captured operation-13 authority, phase, runtime validation, dispatch |
| `kernel/pm/pm_control_syscall.h` | Captured operation-13 adapter |
| `kernel/pm/pm_control_syscall_activation.c` | Activation adapter |
| `kernel/pm/pm_control_syscall_core.c` | Caller authority and retained output plan |
| `kernel/pm/pm_control_syscall_core.h` | Authority/output translation contract |
| `kernel/pm/pm_control_syscall_execution.c` | Prepare/abort-prepared adapters |
| `kernel/pm/pm_control_syscall_exit.c` | Exit lifecycle adapters |
| `kernel/pm/pm_control_syscall_internal.h` | Private syscall runtime contract |
| `kernel/pm/pm_control_syscall_reservation.c` | Reserve/abort-reserved adapters |
| `kernel/pm/pm_control_syscall_runtime.c` | Result/fatal/output helpers |
| `kernel/pm/pm_control_validation.c` | Complete scalar/object/prepared-shape validation |

### PM portable lifecycle and coordinator owners

| Path | Responsibility |
| --- | --- |
| `servers/pm/pm_common.c` | Byte codecs, handles, endpoint/record/wait primitives |
| `servers/pm/pm_control.c` | PM-private operation-13 wrappers |
| `servers/pm/pm_control.h` | PM-private wrapper interface |
| `servers/pm/pm_coordinator.c` | Coordinator state, configuration, init spawn, parent loss |
| `servers/pm/pm_coordinator.h` | Coordinator/backend/protocol/response interface |
| `servers/pm/pm_coordinator_exit.c` | Stop, VFS cleanup, detach, VM/process release |
| `servers/pm/pm_coordinator_frontend.c` | Common request/caller/reply shaping |
| `servers/pm/pm_coordinator_internal.h` | Private spawn steps and peer helpers |
| `servers/pm/pm_coordinator_peers.c` | Validated VFS/VM exchanges and result classification |
| `servers/pm/pm_coordinator_protocol.c` | VFS/VM message construction and result decoding |
| `servers/pm/pm_coordinator_requests.c` | Parent grant copy, public reservation, returning result |
| `servers/pm/pm_coordinator_rollback.c` | Reverse collaborator release |
| `servers/pm/pm_coordinator_spawn.c` | Forward spawn stages and activation boundary |
| `servers/pm/pm_coordinator_wait.c` | Wait reply completion/restoration |
| `servers/pm/pm_core.c` | Table initialization/runtime enable/public queries |
| `servers/pm/pm_core.h` | PM table, records, resources, wait outcomes, lifecycle API |
| `servers/pm/pm_exit.c` | Exit preflight/commit, reparenting, staged waits |
| `servers/pm/pm_internal.h` | Private common, activation, exit, and wait contracts |
| `servers/pm/pm_protocol.c` | PM EXIT/WAIT/SPAWN decode and result encode |
| `servers/pm/pm_rollback.c` | Reverse spawn-state transition and action publication |
| `servers/pm/pm_spawn.c` | Slot/PID reservation, forward stages, activation |
| `servers/pm/pm_validation.c` | Complete table/identity/relationship/state validation |
| `servers/pm/pm_wait.c` | Child selection, wait begin/resume/reply completion |

### PM service owners

| Path | Responsibility |
| --- | --- |
| `servers/pm/pm_service.c` | Service entry and receive loop |
| `servers/pm/pm_service_backend.c` | Runtime IPC/grant/operation-13 backend |
| `servers/pm/pm_service_bootstrap.c` | Launcher readiness |
| `servers/pm/pm_service_fixture.c` | Sole service context/configuration/codecs/coordinator setup |
| `servers/pm/pm_service_internal.h` | Service context and private adapters |
| `servers/pm/pm_service_requests.c` | Ordinary request dispatch/response ownership |
| `servers/pm/pm_service_scratch.c` | Scratch-preemption peer |
| `servers/pm/pm_service_self_test.c` | Existing target synthetic lifecycle scenario |

### Spawn object owners

| Path | Responsibility |
| --- | --- |
| `lib/spawn/spawn_request.c` | Canonical pointer-free request decoder |
| `lib/spawn/spawn_request.h` | Decoded request/string/action and layout contracts |
| `lib/spawn/spawn_stack_layout.c` | Checked initial-stack occupancy/layout |

## Cross-owner source dependencies

| Path family | B08 operation | Primary/later owner |
| --- | --- | --- |
| objects, scheduler, execution contexts | B08-O04/O06 reserve/prepare/activate/stop/detach | B03/B05 |
| endpoint, IPC, grants | B08-O03-O08 publication, blocked calls, cleanup, copied requests | B04/B05 |
| static bootstrap/runtime gate | B08-O01/O05 PM role and sealed event | B06 |
| VM mapping records, freeze, abort, commit, release | B08-O03-O07 memory lifecycle | B07 |
| TTY/RAMFS/VFS descriptor and executable policy | B08-O03/O06-O08 collaborator state | B09-B12 |
| application runtime and final spawn integration | B08-O03/O06-O08 observable child lifecycle | B12 |
| B08 native/model/QEMU evidence | All B08 operations | B14 |

## Fixed MINIX source evidence

The B08 comparison uses only local source:

- `minix/servers/pm/main.c`
- `minix/servers/pm/mproc.h`
- `minix/servers/pm/forkexit.c`
- `minix/servers/pm/exec.c`
- `minix/servers/pm/utility.c`
- `minix/servers/pm/schedule.c`
- `minix/servers/vm/fork.c`
- `minix/servers/vm/exit.c`
- `minix/servers/vfs/main.c`
- `minix/servers/vfs/misc.c`
- `minix/servers/vfs/exec.c`
- `minix/kernel/system.c`
- `minix/kernel/system/do_fork.c`
- `minix/kernel/system/do_exec.c`
- `minix/kernel/system/do_clear.c`
- `minix/kernel/system/do_runctl.c`
- `minix/kernel/system/do_privctl.c`
- `minix/kernel/system/do_schedctl.c`
- `minix/kernel/system/do_schedule.c`
- `minix/lib/libc/sys/stack_utils.c`
- `common/lib/libc/arch/arm/string/memcpy.S`
- `common/lib/libc/arch/arm/string/memset.S`

## Logical variables

| Variable | Meaning |
| --- | --- |
| `p` | PM application-record capacity, 64 |
| `c` | children matching one parent/selector |
| `m` | managed frames examined by kernel PM runtime validation |
| `e` | mappings/table frames in the selected VM record |
| `g` | active managed frame ranges |
| `gmax` | configured managed-range capacity |
| `Vpm` | complete portable PM table validation |
| `Vpmrt` | complete target PM-control runtime validation |
| `Vboot` | B06 complete bootstrap runtime validation |
| `Vvmrt` | B07 complete target VM runtime validation |
| `Vkh` | B07 complete kernel VM-handoff state validation |
| `Vobj` | B03 complete object/scheduler validation |
| `Vep` | B04 complete endpoint/object/IPC validation |
| `Vreg` | B04 complete privilege-profile validation |
| `Vgrant` | B05 complete grant relation validation |
| `Fown` | B02 complete frame-ownership validation |
| `Lown` | one frame-owner lookup, `O(gmax + g)` |
| `Rmem` | B02 complete address-space translation/validation work |

## Complete validation cost definitions

The PM table validator:

1. scans all `p` records and fixed record bytes;
2. compares every live PID/endpoint against later records;
3. scans all records for endpoint publication and matching children;
4. validates parent/reaper/wait relationships.

```text
Vpm = O(p^2 + p * record_bytes)
```

The target PM runtime:

1. validates PM scalar/object state and `Vobj`;
2. validates `Fown`, scans all managed frame owners, and counts selected
   process frames;
3. invokes `Vkh` for the active VM transaction;
4. resolves endpoint/profile authority through `Vep`/`Vreg`.

```text
Vpmrt = O(Vobj + Fown + Vkh + m + e*Lown + Vep + 2*Vreg)
```

One ordinary operation-13 command performs initial PM authority resolution,
then:

```text
Vboot + Vvmrt + Vpmrt + Vgrant
```

before command-specific PM/object/endpoint/VM/scheduler validation.

## Operation ledger

### B08-O01 PM table, semantic identity, PID, and slot lifecycle

MINIX PM owns one bounded `mproc` table. Source endpoints map directly to the
kernel process slot and therefore the exact `mproc` record. Child allocation
continues from a remembered rotating `next_child` cursor. PID allocation
advances a bounded candidate and scans process and process-group IDs until the
candidate is free.

`micros` owns one 64-record generation-safe application table. Semantic PIDs
are monotonic 64-bit values with no reuse; PM handles are slot/generation
pairs. Source lookup scans all records by endpoint. Child allocation restarts
at slot zero.

| Difference | Classification |
| --- | --- |
| PM owns semantic PID/parent/exit/wait state separately from kernel identity | Baseline parity |
| Generation-safe PM handles and quarantine | Compatible extension |
| Monotonic nonreused 64-bit PID without process-group collision | Staged substitution; SOM-06 and SOM-07 |
| Linear endpoint lookup instead of endpoint-slot direct lookup | Divergence requiring correction |
| Lowest-free restart instead of remembered rotating slot cursor | Divergence requiring correction |

See B08-F003.

### B08-O02 Complete PM table validation

MINIX validates the selected source, parent, child, PID, flags, and lifecycle
state in each handler. Optional sanity/debug paths may inspect broader state;
ordinary operations do not first prove every PID, endpoint, parent, wait, and
record relationship.

`micros` runs `Vpm` before every public table operation and public query.
Pairwise PID/endpoint uniqueness and nested matching-child scans make that
validation quadratic.

| Difference | Classification |
| --- | --- |
| Complete table validation as explicit diagnostic evidence | Compatible extension |
| Complete quadratic validation on every ordinary transition/query | Divergence requiring correction |

See B08-F001.

### B08-O03 Hidden spawn and reverse rollback

MINIX fork checks table capacity, chooses a rotating free slot/PID, asks VM/
kernel for a hidden inhibited child, copies semantic state, asks VFS to copy
descriptors, and publishes success only after required owners complete.
Ordinary failure is unsafe after its VM/kernel child creation boundary.

`micros` performs a safer direct-spawn transaction:

```text
PM record
-> empty kernel process
-> VFS image
-> VM frozen mappings
-> VFS descriptors
-> kernel execution
-> hidden PM/VFS commit
-> activation
-> mandatory VM/VFS postpublication commits
```

Expected prepublication failure releases those resources in reverse order.
Each local PM stage invokes `Vpm`.

```text
successful public spawn end to end:
                         12*Vpm
latest-stage rollback:   up to 7 additional Vpm
```

The hidden transaction and failure-atomic rollback are compatible safety
extensions. The validation multiplication is B08-F002.

### B08-O04 Kernel reservation, preparation, activation, and exit mechanism

MINIX separates `SYS_FORK`, privilege installation, scheduler policy,
`SYS_EXEC`, stop, clear, and VM release below PM lifecycle policy.

`micros` operation 13 similarly separates empty reservation, execution
preparation, activation, exit begin, detach, and empty-process release. Exact
tokens, generations, retained output plans, fixed application profile, and
non-failing commits are required/compatible adaptations.

Before every command, however, the adapter validates bootstrap, VM, PM,
grants, ownership, objects, endpoints, profiles, and process-bound frame
owners. Command preflights then repeat selected complete validators.

See B08-F005.

The eight commands add these lower-bound terms after common authority plus
`Vboot + Vvmrt + Vpmrt + Vgrant`:

| Command | Additional validation/preflight work |
| --- | --- |
| `RESERVE` | writable output `Rmem`; VM-state `Vkh`; PM/object `Vobj`; process-slot scan |
| `ABORT_RESERVED` | VM-state `Vkh`; PM/object `Vobj` |
| `PREPARE_EXECUTION` | writable output `Rmem`; `Vobj`; endpoint `Vep`; VM `Vkh`; scheduler validation; profile `Vreg`; user-execution mapping/context validation |
| `ABORT_PREPARED` | `Vobj`; endpoint `Vep`; VM `Vkh`; scheduler/profile checks; user-execution detach |
| `ACTIVATE` | prepared-shape `Vobj + Vep + Vkh + Vreg` plus scheduler validation and another endpoint resolution |
| `EXIT_BEGIN` | writable output `Rmem`; `Vobj + Vep + Vkh + Vreg` plus scheduler and source-only preflight |
| `EXIT_DETACH` | `Vobj + Vep + Vkh` plus scheduler checks, complete grant cancellation `Vgrant`, and IPC endpoint close validation |
| `RELEASE_EXITED_PROCESS` | `Vobj + Vkh + Fown` plus empty-process release checks |

### B08-O05 PM service dispatch and exact caller identity

MINIX resolves `who_p` from the kernel-written endpoint and indexes
`mproc[who_p]` directly before call dispatch.

`micros` decodes a fixed 64-byte message, validates the coordinator instance
through `Vpm`, then calls `micros_pm_find_running_by_endpoint()`, which reruns
`Vpm` and scans all `p` records. The selected lifecycle operation invokes
`Vpm` again.

```text
ordinary application request preflight: 2*Vpm + O(p)
```

Exact generation and reply-token authority are compatible extensions. The
lookup/cadence costs are B08-F001/F002/F003.

### B08-O06 Exit, reparenting, teardown, and zombie publication

MINIX first prevents execution, starts VFS cleanup while identity remains,
separately releases kernel/VM state, reparents children to init, and retains
the PM record until cleanup and collection permit reuse. A normal zombie may
be wait-visible before all cleanup completes.

`micros` stops execution, reparents in one `O(p)` scan, completes VFS detach,
kernel detach, VM release, and process release, then publishes the resource-
free zombie and stages waits for init and the original parent.

```text
reparenting: O(p)
complete exit end to end: up to 9*Vpm plus collaborator work
```

The stricter resource-complete zombie publication is a compatible extension.
MINIX checks the exiting process's original parent before reparents/notifications
for init; `micros` stages init first and the original parent second. That
ordering divergence is included in B08-F004. Signals, core dumps, tracing, and
rusage remain staged.

### B08-O07 Wait selection and one-shot completion

MINIX `wait4` scans `mproc` in slot order and returns the first qualifying
zombie encountered. If only live children match, it returns zero for
`WNOHANG` or stores one wait selector and suspends. No match returns `ECHILD`.

`micros` scans all records and deliberately chooses the matching zombie with
the lowest semantic PID. It then stages a one-shot reply token and releases
the record only after reply acceptance.

```text
MINIX any-child zombie choice: first matching table slot, O(p)
micros any-child zombie choice: complete scan plus minimum PID, O(p)
```

The token-bound acceptance/restoration is a compatible extension. Lowest-PID
selection is a different accepted algorithm/observable choice and requires an
explicit ADR-0046/ADR-0050 supersession under ADR-0080.

Exit-triggered two-parent completion also differs: MINIX handles the exiting
process's original parent before iterating/reparenting children to init,
whereas `micros` stages and sends init's outcome first.

See B08-F004.

### B08-O08 Spawn request and initial-stack layout

MINIX libc computes a bounded offset-based argument/environment frame, checks
overflow, fills pointer vectors and strings, and VFS copies the complete frame
before destructive executable work.

`micros` decodes one canonical pointer-free request, ordered string references,
bounded descriptor actions, and dense zero tails. A separate checked planner
computes the aligned one-page application stack occupancy.

```text
decode/layout: O(argument count + environment count + descriptor count
                 + request string/tail bytes)
```

The pointer-free RV64 representation and explicit descriptor actions are
required adaptations. Fixed counts and no full POSIX spawn/fork/exec breadth
are staged under SOM-06/SOM-12/SOM-17.

### B08-O09 Deferred MINIX/POSIX process breadth

Current PM lacks:

- fork/copy-on-write/destructive exec and unrestricted inheritance;
- process groups, sessions, and job-control wait selection;
- signals and fault-to-exit conversion;
- credentials and permission transitions;
- tracing, stop/continue status, rusage, and core-dump lifecycle;
- static-service recovery and general asynchronous collaborator breadth.

These map to SOM-01, SOM-06, SOM-07, SOM-09, SOM-11, SOM-12, SOM-17,
SOM-23, and new SOM-26.

### B08-O10 PM copy and clear algorithms

MINIX uses architecture/generic `memcpy`/`memset` block algorithms for process
records, signal state, messages, argument frames, and temporary storage.

`micros` PM common/service/kernel helpers clear and copy byte by byte.
Coordinator request snapshots copy up to 4,096 bytes with one explicit byte
iteration per byte.

The canonical wire decoder's byte reads are format semantics; general table,
request, context, and plan movement is not. See B08-F006.

## Validation multiplication

Representative complete-table validation counts are:

```text
successful public spawn:  up to 12*Vpm
rollback from final hidden stage:
                          up to 7 additional Vpm
complete ordinary exit with two staged waits:
                          up to 9*Vpm
immediate collected wait: up to 5*Vpm
```

These counts exclude B04/B05 IPC/grant validation and B07 VM validation in
collaborator calls.

## Difference summary

| Area | Result |
| --- | --- |
| PM semantic table and lifecycle authority | Baseline parity with generation-safe extensions |
| PID/process-group breadth | Staged substitution |
| Complete PM validation cadence/algorithm | Blocking divergence |
| Caller endpoint and free-slot indexes | Blocking divergence |
| Spawn/rollback and exit ordering | Baseline authority with safer failure/publication extensions |
| Any-child wait choice | Blocking divergence |
| Kernel PM-control validation cadence | Blocking divergence |
| Spawn request/stack algorithm | Baseline boundary with required fixed-wire adaptation |
| Copy/clear algorithms | Blocking divergence |

## Time, stack, and auxiliary-space envelope

Persistent B08-owned state includes:

```text
PM process table:              12,832 bytes
PM coordinator:                 4,152 bytes
production PM service context: 17,000 bytes
kernel PM-control state:          208 bytes
```

The coordinator includes one resident 4,096-byte spawn-request page. The PM
table contains 64 exact 200-byte records.

Representative automatic storage is:

```text
kernel prepare plan:             352 bytes
kernel execution binding:        296 bytes
PM coordinator spawn:            112 bytes
decoded spawn request:            144 bytes
wait outcome bundle:              104 bytes
coordinator response:              80 bytes
rollback action:                   48 bytes
```

The PM service has one 4 KiB user stack. Coordinator request handling can nest
decoded request/layout/response/peer messages with blocking IPC wrappers.
Kernel preparation/exit paths nest their plans with B02-B07 complete
validators and B03 scheduler transition work on a 16 KiB supervisor stack.
Neither complete envelope has direct target stack-usage evidence.

## Staged omissions

| ID | B08 relation |
| --- | --- |
| SOM-01 | Static-service lifecycle, recovery, reconstruction, and RS ownership |
| SOM-06 | Fork, copy-on-write, destructive exec, complete process replacement/inheritance |
| SOM-07 | Process groups, sessions, controlling terminal, job control, group waits |
| SOM-08 | Multiple application threads and thread lifecycle |
| SOM-09 | Signals, stop/continue, asynchronous fault/exit delivery |
| SOM-11 | Credentials and executable permission transitions |
| SOM-12 | Complete descriptor inheritance/close/dup/fcntl and pipe/FIFO breadth |
| SOM-17 | Public POSIX process/wait/spawn/exec/libc interfaces and errno |
| SOM-22 | Complete process/service/accounting inspection |
| SOM-23 | Asynchronous collaborator/control transport where required |
| SOM-26 | Process tracing, per-process rusage, and core-dump lifecycle |

## Evidence gaps

| ID | Missing evidence | Owner |
| --- | --- | --- |
| B08-E001 | No counter records `Vpm` calls/comparisons per spawn stage, rollback, exit, and wait | B14 |
| B08-E002 | No direct-index probe distinguishes endpoint-slot lookup and rotating free-slot allocation from capacity scans | B14 |
| B08-E003 | No wait-order evidence compares MINIX first-slot and original-parent-before-init behavior with lowest-PID/init-first behavior | B14 |
| B08-E004 | No operation-13 trace counts `Vboot`, `Vvmrt`, `Vpmrt`, `Vgrant`, and command-specific nested validators | B14 |
| B08-E005 | No PM request/table/context copy-work counter distinguishes byte, word, and block paths | B14 |
| B08-E006 | No direct 4 KiB PM user-stack or 16 KiB supervisor-stack evidence covers worst coordinator/kernel nesting | B14 |
| B08-E007 | No tracing/rusage/core-dump evidence exists because SOM-26 is absent | B14 after SOM-26 design |

## Findings register

### B08-F001: Complete quadratic PM-table validation precedes ordinary operations

- Severity: High
- `micros` evidence:
  - `micros_pm_table_validate`
  - every public PM lifecycle/query entry
- MINIX evidence: selected `mproc` source/parent/child/wait checks and optional
  broader sanity paths
- Difference: one local PM transition first scans all records, compares every
  live PID/endpoint pair, and performs nested matching-child/relationship
  scans.
- Impact: ordinary PM work pays `Vpm = O(p^2)` before direct handle or local
  stage checks.
- Smallest correction boundary: retain complete table validation as an
  explicit diagnostic operation; ordinary transitions use direct
  slot/generation resolution and path-local parent/child/wait/token checks.
- Dependencies: B14 PM comparison counts.

### B08-F002: One coordinator operation reruns complete PM validation many times

- Severity: High
- `micros` evidence:
  - `pm_coordinator_validate_instance`
  - `pm_coordinator_preflight_call`
  - every forward/rollback/exit/wait lifecycle call
  - unconditional `pm_service_serve_request` post-validation
- MINIX evidence: one table/source preflight followed by direct selected-record
  lifecycle updates and collaborator completion
- Difference: coordinator preflight runs `Vpm` twice, then each PM stage or
  rollback acknowledgment reruns it.
- Impact: successful spawn reaches 12 complete validations, latest-stage
  rollback adds up to seven more, complete exit reaches nine, and immediate
  collected wait reaches five.
- Smallest correction boundary: one retained coordinator transaction plan
  validates the selected parent/child and expected PM stage once; each local
  acknowledgment performs exact token/stage checks without a complete table
  audit.
- Dependencies: B08-F001.

### B08-F003: PM lacks the MINIX caller index and rotating free-slot cursor

- Severity: Medium
- `micros` evidence:
  - `micros_pm_find_running_by_endpoint`
  - `find_free_slot`
  - kernel `micros_pm_control_reserve_preflight`
  - duplicate capacity scan before `micros_pm_begin_spawn`
- MINIX evidence:
  - `_ENDPOINT_P` plus direct `mproc[who_p]`
  - remembered rotating `next_child`
- Difference: every caller lookup scans `p` records. PM-record allocation and
  the independent kernel-process reservation both restart free-slot search at
  zero instead of continuing from the last slot.
- Impact: hot PM dispatch becomes `O(p)` instead of `O(1)`, while repeated
  child creation concentrates generation consumption/quarantine on low slots.
- Smallest correction boundary: one exact endpoint-slot/generation-to-PM-
  handle index plus validated rotating cursors for PM records and kernel
  process slots, or PM supplies one exact prevalidated kernel destination
  slot; preserve generation quarantine and capacity failure.
- Dependencies: B08-F001/F002.

### B08-F004: Wait selection and cross-parent reply order differ from MINIX

- Severity: Medium
- `micros` evidence:
  - `find_lowest_matching_zombie`
  - init-first staging in `micros_pm_exit_release_process`
  - ordered replies in `pm_coordinator_complete_wait_outcomes`
- MINIX evidence:
  - first qualifying zombie in the `mproc[0..NR_PROCS)` scan of `do_wait4`
  - `zombify()`/original-parent notification before child reparenting and
    init notification in `exit_proc`
- Difference: within one parent, `micros` chooses minimum PID rather than the
  first matching table slot. Across affected parents, it replies to init
  before the exiting process's original parent, reversing MINIX order.
- Impact: the first implementation does not reproduce MINIX wait ordering;
  accepted ADR-0046/ADR-0050 tests lock both post-baseline choices.
- Smallest correction boundary: a corrective design explicitly supersedes
  the lowest-PID and init-first clauses, restores first matching table-order
  selection and original-parent-before-init completion; measured deterministic
  alternatives may follow only after baseline.
- Dependencies: explicit ADR-0046/ADR-0050 supersession and B14 multi-zombie
  and two-parent ordering evidence.

### B08-F005: Every operation-13 command validates whole-system PM state

- Severity: High
- `micros` evidence:
  - `runtime_state_is_valid`
  - `micros_pm_control_runtime_validate`
  - reservation/preparation/activation/exit preflights
- MINIX evidence: common caller-mask dispatch plus selected process/context/
  privilege/scheduler/clear handler checks
- Difference: one command validates bootstrap, VM, PM, grants, complete
  object/frame ownership, all process-bound frame owners, endpoint/profile
  state, and then repeats the command-specific validators listed in the
  eight-command cadence table.
- Impact: one local PM-control transition pays at least
  `Vboot + Vvmrt + Vpmrt + Vgrant` before command-specific work.
- Smallest correction boundary: keep complete PM-control/system agreement as
  an explicit diagnostic boundary; ordinary commands validate exact PM
  authority, selected reservation/process/thread/record, and consumed
  cross-owner plans once.
- Dependencies: B02-F002, B03-F001, B04-F001/F002, B05-F001, B06-F002,
  B07-F001.

### B08-F006: PM request and state movement uses byte-at-a-time loops

- Severity: Medium
- `micros` evidence:
  - `pm_common_zero_bytes`
  - coordinator request snapshot loop
  - PM service and kernel PM-control copy/clear helpers
- MINIX evidence: architecture/generic `memcpy`/`memset` word/block paths
- Difference: PM initializes/copies a 12,832-byte table, 4,096-byte request
  snapshots, contexts, messages, and plans through source-level byte loops.
- Impact: every spawn performs additional byte-serial snapshot/clear work and
  PM initialization pays the same algorithmic constant-factor regression.
- Smallest correction boundary: use independently written standard-defined
  RV64 word/block copy and fill primitives with alignment handling and byte
  tails; retain wire-format byte codecs separately.
- Dependencies: B05-F006, B06-F004, B07-F005, and B14 transfer-work evidence.

## Unit conclusion

B08 primary-path and operation coverage is complete for the pinned tree. The
audit adds B08-F001 through B08-F006 as blocking findings,
confirms B02-F002, B03-F001, B04-F001/F002, B05-F001/F006, B06-F002/F004,
and B07-F001/F005, adds SOM-26, shifts complete conformance to SOM-27, and
records SOM-01, SOM-06, SOM-07, SOM-08, SOM-09, SOM-11, SOM-12, SOM-17,
SOM-22, and SOM-23.

B08 is **audited but not certified**. Independent review corrected
operation-13 lookup/cadence accounting, end-to-end service validation counts,
PM and kernel allocation cursor coverage, and both within-parent and
cross-parent wait ordering. Final re-review reported B08 complete. No B08
correction may start during the read-only audit.

## References

- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)
- [B04 endpoint/IPC audit](minix-complete-baseline-b04-endpoint-ipc.md)
- [B05 grant/user-execution audit](minix-complete-baseline-b05-grants-user-execution.md)
- [B06 static-bootstrap audit](minix-complete-baseline-b06-static-bootstrap.md)
- [B07 VM audit](minix-complete-baseline-b07-vm.md)
- [MINIX PM lifecycle study](minix-pm-process-lifecycle.md)
- [MINIX PM service source study](minix-pm-service-source-organization.md)
- [MINIX kernel PM-control source study](minix-kernel-pm-control-source-organization.md)
- [MINIX ELF/spawn study](minix-elf-exec-and-spawn.md)

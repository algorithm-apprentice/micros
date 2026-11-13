# MINIX Selected Bootstrap Command Validation Study

## Fixed inputs

```text
micros source: 266189efd06807acaa46e5544b247fca0e309872
micros tree:   7833d5eefb41549e23216fb979ef95ed3f55a303
MINIX source:  4db99f4012570a577414fe2a43697b2f239b699e
```

Both repositories were checked clean at these revisions. MINIX research
uses the existing local `/Users/alephling/Repo/minix` checkout read-only.
It supplies structural and behavioral evidence, not copied implementation.

BOOT01 #266 is merged. BOOT02 owns B06-F002/F003 and has its six registered
predecessors: BOOT01, MEM04, OBJ01, OBJ02, IPC01 and GRANT01. This study
does not add an edge merely because other corrections have also merged.

## Fixed MINIX mechanism

| Reference | Observed structure and behavior |
| --- | --- |
| `minix/servers/rs/main.c:323-345` | Construction fills one service record and its separate endpoint-slot lookup. Global boot policy is established before release. |
| `main.c:349-399` | Release already holds the selected `rproc`; scheduler initialization and privilege allowance precede its initialization request. It does not reconstruct every service's memory, grants and execution state. |
| `minix/servers/rs/utility.c:18-61`, `init_service` | Updates the selected initializing/timing fields and constructs one initialization message. Restart/update branches remain distinct. |
| `main.c:784-827`, `catch_boot_init_ready` | Maps the received source to its record, rejects a failed boot result, replies to ordinary services and clears selected initialization state. Fresh VM's asynchronous exception is explicit. |
| `minix/servers/rs/request.c:462-530`, `do_init_ready` | Checks the selected initializing flag/result. Normal readiness clears that record, replies and finalizes it; update/recovery policy is separate. |
| `minix/kernel/system/do_privctl.c:43-65` | Privileged caller and exact target endpoint admission precede `SYS_PRIV_ALLOW`; the target must remain inhibited and have assigned privileges before its inhibition bit is cleared. |
| `do_privctl.c:88-183` | Privilege creation and bitmap/resource initialization are separately scoped setup work, not a complete audit repeated by `SYS_PRIV_ALLOW`. |

Selected record/privilege admission and state changes are constant work
with fixed scalar/message storage. Boot-table construction, scheduler
mechanisms and later periodic/recovery work have their own costs.
There is no cross-call amortization claim and no complete-system proof
attached to each ordinary selected release or readiness result.

## Current micros command trace

The original B06 audit's nested-cost formulas are historical. BOOT01
already removed its named core lookup/snapshot costs, while MEM04, OBJ01,
OBJ02 and IPC01 made many underlying leaves selected. The remaining
defect is the actual complete-audit invocation boundary, not the old
implementation of every callee.

| Owner | Current command work |
| --- | --- |
| `kernel/boot/bootstrap_syscall.c`, `preflight_runtime` | Decoded requests resolve bootstrap/IPC state, authorize the controller, run complete `micros_bootstrap_runtime_validate`, then require the grant registry. |
| `bootstrap_runtime_validation.c`, `micros_bootstrap_runtime_validate` | Complete control, frame-owner metadata, grant and optional TTY diagnosis, followed by every service's complete address-space check and stored execution-context admission. |
| `bootstrap_control_core.c`, `control_release` | Complete control diagnosis before and after the selected runtime, endpoint publication and scheduler admission. |
| `bootstrap_syscall_release.c`, `finish_release` | Another complete target runtime diagnosis after successful release. |
| `bootstrap_control_core.c`, `micros_bootstrap_control_prepare_ready` | Complete control diagnosis before the selected ready plan and acknowledgment. |
| `bootstrap_syscall_ready.c` | Role preparation, reversible current-IPC guard and exact expected-caller reply precede runtime/TTY commit; captured completion and another complete target diagnosis follow. |
| `bootstrap_control_core.c`, `micros_bootstrap_control_prepare_complete` | Complete control diagnosis plus the necessary all-ready, PM-role, staged-acknowledgment, grant-dependency and endpoint-reference sealing checks. |
| `bootstrap_syscall_complete.c` | Current-IPC preflight/begin, scheduler removal, source-only endpoint and selected seal/notification, then complete control and runtime diagnosis again. |
| `bootstrap_syscall_release.c`, console begin | Identity/order checks, complete VM/TTY/frame-owner diagnosis, a full-root UART preparation used only as a read preflight, retained deadline and VM notification; another complete runtime diagnosis follows publication. |
| `bootstrap_syscall_failure.c` | Reason/detail and implicated-binding checks precede the existing one-shot nonreturning failure record. |

The BOOT01 native evidence measured actual work without inferring it
from elapsed time. At this fixed source, portable control release still
records four BOOT complete-audit calls, including index diagnostics,
and twenty complete calls across all owners. That measurement is
diagnostic baseline data, not a future expected result.

### Complete bootstrap call census

The current BOOT01 source ledger records nineteen caller rows and twenty
direct calls to `micros_bootstrap_control_validate` or
`micros_bootstrap_runtime_validate`.

The B06 command-owned replacement is nine rows / ten direct calls:

| Caller | Complete callee | Calls |
| --- | --- | --- |
| `control_release` | control validate | 2 |
| `micros_bootstrap_control_prepare_ready` | control validate | 1 |
| `micros_bootstrap_control_prepare_complete` | control validate | 1 |
| `preflight_runtime` | runtime validate | 1 |
| `bootstrap_syscall_handle_accept_ready` | runtime validate | 1 |
| `bootstrap_syscall_handle_complete` | control validate / runtime validate | 1 each |
| `bootstrap_syscall_handle_console_begin` | runtime validate | 1 |
| `finish_release` | runtime validate | 1 |

The ten other rows remain: two controller-publication boundaries,
the complete runtime validator's control call, diagnostic-control dispatch,
PM command validation, two TTY boundaries and three VM boundaries.
Removing B06 invocation does not authorize changing those owners or
weakening either complete validator.

## Consumed cross-owner boundaries

### Publication, scheduling, reply and seal

`endpoint_publication.c` already uses selected endpoint/process/profile
admission. Its required owned-thread/inhibition search remains a lifecycle
cost, not a complete endpoint registry audit.
`endpoint_close.c` retains the source-only pending-source and foreign IPC
reference checks. Completion's staged-thread and grant scans are explicit
authority-revocation barriers, not ordinary whole-system diagnosis.

`scheduler_core_policy.c` already has selected admission preflight and
prevalidated queue stores. The current bootstrap caller preflights admission
before endpoint publication, then calls the checked admission wrapper,
which repeats that preflight. Exposing its existing non-failing store
portion to this protected caller avoids a second validation or any new
scheduler policy.

`scheduler_core_ipc.c` already has selected guard preflight, rollback and
commit. Ready's reply failure restores the guard; only a successful exact
expected-caller reply permits the ready commit. A reversible guard is not
permission to move bootstrap publication before reply success.

### VM and TTY roles

The existing VM and TTY `*_runtime_role_ready` functions have only the
B06 role-gate production caller. Their current implementations reenter
complete role validators. BOOT02 may refine this bootstrap-specific use;
ordinary VM/TTY command and interrupt validation remains with later owners.

VM role readiness currently reaches complete application-mapping state
and complete frame-owner metadata through `micros_vm_handoff_runtime_validate`.
Bootstrap consumes the fixed VM identity, exact live binding and committed
handoff/ownership phase, not unrelated dynamic application transactions.

TTY's complete relationship helper currently nests complete endpoint/object
diagnosis. Its consumed identity and phase relations are still necessary:
exact stored bootstrap owner, service/image/profile/process/thread/root/
endpoint, selected runtime state, scheduler policy, retained deadline,
console phase and IRQ route. The pure `micros_tty_handoff_validate` is a
constant-size state predicate and is not the complete runtime audit being
removed.

The MEM04/GRANT01 device inspector remains descriptive/exclusion-only.
It cannot authorize a bootstrap command. A command-specific TTY relation
check must establish actual authority independently.

### UART mapping and execution context

`user_address_space_tty.c` currently acquires complete scratch and calls
`micros_user_address_space_validate_locked` in both UART preparation and
mapping validation. Console begin discards the resulting mapping plan;
only VM later owns the mapping mutation.

The existing selected path in `user_address_space_mapping.c` can resolve
the exact fixed UART slot after admitting its process/root and all three
table/owner records. A B06-only read probe can check absent or exact mapped
state without certifying unrelated leaves, clearing scratch, retaining a
PTE mutation plan or changing VM/TTY's existing complete APIs.
The unmanaged physical UART, exact permissions, inactive-root rule for
preparation and current scratch-busy boundary remain relevant.

`user_execution_validation.c`, `micros_user_execution_validate_context`,
already checks the selected thread/process, status and PC/SP mappings
through MEM04 lookups. The complete root validator is not needed to
reconstruct those selected facts. No B06 proof may be reused across
common return, a different thread, a user return or a mapping mutation.

## Scope and classification

| Mechanism | Classification |
| --- | --- |
| Selected service/controller authority and one local transition | Baseline parity |
| Exact generations, immutable profile and process/thread separation | Required adaptation |
| Checked local views, bounded after-plans and failure-atomic publication | Compatible extension |
| Absolute deadline, VM/TTY role ordering and source-only launcher | Required adaptation or existing staged substitution |
| Complete initialization/diagnostic audits | Compatible diagnostic extension, not an ordinary-command precondition |
| One-time all-ready, staged-acknowledgment, grant and endpoint-reference seal barriers | Existing required authority-revocation adaptation |
| Whole-system and repeated complete command audits | B06-F002/F003 divergence requiring correction |
| Byte-copy primitive family | Separately owned BOOT03 divergence |

Selected success will not certify unvisited services, mappings, grants,
queues or registry rows. Complete diagnostics must continue to reject
their corruption. Consumed count/index/identity/role/queue/PTE corruption
must remain explicit failure before publication, not a successful lookup,
repair or no-match fallback.

The remaining console identity helper has its own legacy service-ID
searches and an all-entry deadline search. Its command-plan replacement
must use BOOT01's indices and the owned starting/deadline state rather
than hiding those scans behind a new selected API.

## Work and storage implications

Use the logical selected dependencies, owned lifecycle/reference search
and actual queue prefix as work variables. Do not call every complete
command constant-time: endpoint inhibition still inspects owned-thread
membership, and final revocation deliberately visits the necessary
service/thread/grant/reference domains.

The required improvement removes whole managed-frame, unrelated root,
grant and service validation from ordinary B06 command preflight/postflight.
Remaining selected page walks and existing MEM03 geometry costs must be
measured separately from retained lifecycle scans.

No persistent storage is needed beyond BOOT01's 448-byte runtime,
2,416-byte control state and 3,816-byte target context.
Existing selected/ready/complete plans are 64/136/200 bytes;
the TTY candidate is 32 bytes and a captured user context is 264 bytes.
Any new borrowed selected view must be fixed automatic state, not another
registry, capacity-sized candidate, validation bit or cross-call lease.
Actual compiler/linked/target evidence still owns the unchanged stack limit.

This is design research only. No replacement code, model, fixture,
observation harness, build scaffold or executable prototype was authored.

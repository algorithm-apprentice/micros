# MINIX Bootstrap Index and Transition Study

## Fixed inputs

```text
micros source: e8c98806d1eb2e9b57a438a0c09ac23f48a8b3ba
micros tree:   d609006253590a2c79e82e3600d71aedb7a5d7eb
MINIX source:  4db99f4012570a577414fe2a43697b2f239b699e
```

Both repositories were checked clean at these revisions. Reference research
uses the existing local `/Users/alephling/Repo/minix` checkout read-only.
MINIX supplies evidence, not source to copy.

BOOT01 owns B06-F001 after preferred predecessor GRANT03 #264; its actual
dependency is merged EV04 #189. The
[canonical contract](../architecture/indexed-bootstrap-transitions.md)
owns the Accepted replacement. BOOT02 and BOOT03 remain separate.

## Fixed MINIX trace

| Owner | Structure, algorithm and lifetime |
| --- | --- |
| `minix/servers/rs/glo.h:27-35` | `rproc`/`rprocpub` are compact system-service tables; `rproc_ptr[NR_PROCS]` separately maps process slots to their records. These domains are explicitly not equivalent. |
| `minix/servers/rs/main.c:323-345` | Boot construction initializes one selected service record and publishes its process-slot lookup pointer. |
| `main.c:349-399` | Boot release obtains the exact `rproc` slot by table-relative position, installs scheduling/privileges and invokes initialization without searching the whole service table again. |
| `minix/servers/rs/utility.c:18-61`, `init_service` | Updates the selected record's initializing flag and timing fields, constructs the initialization request and sends it; no whole-runtime snapshot is copied. |
| `minix/servers/rs/request.c:462-530`, `do_init_ready` | Maps source process slot through `rproc_ptr`, checks initializing/result state, updates selected flags/times and replies. Update/restart branches remain separate. |
| `main.c:784-827`, `catch_boot_init_ready` | Boot readiness receives the expected initialization result, directly selects the source record, replies where appropriate and clears that record's initialization state. |

Selected boot/readiness record lookup and mutation are `O(1)` with fixed
scalar/message storage. Construction and complete boot policy still have
their separate table-wide costs. MINIX's overlapping initialization,
recovery and live-update state is not imported into the static sequential
micros launcher.

## Current micros trace

| Owner | Current work and required boundary |
| --- | --- |
| `kernel/boot/bootstrap_transitions.c:3-31` | Mutable/const runtime lookup scans active entries by service ID. |
| `bootstrap_transitions.c:33-106` | Cold initialization builds runtime rows in manifest order and the independent release-order list. |
| `bootstrap_transitions.c:108-177` | Release searches twice, copies the full runtime in/out and reconstructs ready IDs by visiting every service. |
| `bootstrap_transitions.c:211-302` | Ready and complete copy the full runtime for one row/header change. Completion also has a required final all-READY/deadline audit. |
| `kernel/boot/bootstrap_control_internal.h:24-81` | Control embeds a runtime; ready/complete plans each embed another full runtime. The bounded inline binding lookup still scans the binding table. |
| `kernel/boot/bootstrap_control_core.c:72-128,190-199,906-922` | Repeated runtime/binding searches appear in selected operations and complete relation/staged-ack diagnosis. |
| `bootstrap_control_core.c:258-306,413-494` | Control PREPARING has bindings but a zero runtime; controller publication initializes/publishes the runtime later. |
| `bootstrap_control_core.c:510-643,706-809,1013-1143` | Control release/ready/complete add another whole-runtime snapshot around the pure transition, then commit at the existing cross-owner boundary. |
| `kernel/boot/bootstrap_diagnostics.c:74-118,166-230` | State formatting and the active deadline repeat service-ID traversal. |
| `kernel/boot/bootstrap_syscall_ready.c`, `bootstrap_syscall_complete.c` | Existing protected reply/guard, selected seal, source-only endpoint and PM notification order must remain. |
| `kernel/vm/vm_handoff_syscall_runtime.c:158-195`, `kernel/tty/tty_handoff_runtime_validate.c:72-91` | Separate VM/TTY role-audit searches remain their owners' work, not a BOOT01 policy rewrite. |

The binding array accepts its caller's order. Each binding separately
names `manifest_index`; runtime rows have manifest order and release order
has its own list. IDs range from one through 63, not merely one through
the current active count or a process slot.

The existing PREPARING zero-runtime invariant and standalone pure-runtime
API explain why the bounded proposal needs one index per row owner.
Sharing one array would require changing initialization or normalization
contracts merely to save 64 bytes.

## Mapping and classifications

| Mechanism | Classification | Proposed mapping |
| --- | --- | --- |
| Direct selected service record | Baseline parity | Checked ID-to-row selection, not repeated capacity search. |
| Numeric ID map instead of endpoint-slot pointer | Required adaptation | Preserve static manifest IDs, exact generation-bound external bindings and distinct row domains. |
| One immutable index per actual owner | Required adaptation | Preserve PREPARING and standalone runtime lifetime; two byte arrays, no third registry. |
| Failure-atomic local mutation | Compatible extension | One selected after-record/header plan, then non-failing protected commit. |
| Explicit prerequisite DAG | Compatible extension | Check only named prerequisite rows, `O(d+1)`, without a ready cache or unrelated scan. |
| Final all-ready sealing barrier | Required adaptation | Retain its complete read-only `O(n)` predicate; mutate only the controller. |
| Complete diagnostics and malformed selected-index failure | Compatible extension | Independently prove full index coverage and fail closed before mutation; never repair on lookup. |
| Whole-system command audits and repeated validation | Divergence owned by BOOT02 | Retain their current boundaries in BOOT01. |
| Byte-oriented initialization/plan movement | Divergence owned by BOOT03 where applicable | Remove unnecessary capacity movement here, not change the remaining primitive family. |

## Work and storage

For `n` services and `d` named prerequisites, selected lookup/classification
is constant, release planning is `O(d+1)`, ready planning/commit is
constant and final completion retains its `O(n)` readiness audit.
Cold construction/index proof is `O(64+n)`; the fixed ID domain is counted,
not described as active-service-proportional storage.

Current LP64 runtime/control/context sizes are 384/2,288/3,688 bytes.
Two separate 64-byte indices make them 448/2,416/3,816 respectively.
The proposed plan's two 16-bit indices, phase, two IDs and 48-byte
after-record total 64 bytes. Existing 64-byte acknowledgment and 128-byte
notification values give outer ready/complete bounds 136/200 bytes.
No stack budget is raised; actual compiler and target stack evidence must
cover the new cold sizes and reduced hot plans.

## Evidence implications

The current 4,096-step model is an independent flat reference with seed
`0x42535452504c4159`; its draws/dispatch/final-sixteen trace remain.
Expected index bytes can be projected by enumerating its own records,
without adopting production lookup or plan logic.

Existing selected-memory/release-handoff/grant, PM/TTY/VM and target-handoff
fixtures construct some controller rows directly. Their corresponding
index initialization must become coherent, without imposing unrelated
full-bootstrap validity or weakening selected checks. The current native
header consumers must retain exact primitive/source qualification.

The new work pair must execute real portable control and dependencies,
count actual indexed selection/plan movement separately from retained
full audits, and retain actual target/composed/stack acceptance.
No executable prototype, new fixture, model, harness or measurement was
authored in this design phase.

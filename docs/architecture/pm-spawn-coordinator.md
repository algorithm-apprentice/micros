# PM Spawn and Exit Coordinator

This is the implementation map for the already Accepted
[ADR-0050](../adr/0050-post-handoff-elf-spawn.md) transaction. Its design
merged in [PR #87](https://github.com/algorithm-apprentice/micros/pull/87),
commit `b6b291d9f7acf922759b732362be38a1ad67ecd1`.
The fixed behavioral trace is
[the MINIX execution and spawn baseline](../research/minix-elf-exec-and-spawn.md).
This document does not introduce another wire format or lifecycle authority.

## Outcome and readiness

The `elf-spawn-pm-coordinator` implementation connects the existing PM
lifecycle, VFS transaction, VM loader, and kernel operations 12/13.
The VFS exit-cleanup and six immediate PM/VM/kernel source corrections are
merged. The application runtime and final dynamic PID-1 integration remain
dependent tasks; production init and shell remain the later DAG node.

The implementation is in progress. The owners and evidence below describe
its intended completion boundary, not a claim that every listed source or
test target already exists.

## Ownership

PM remains the sole process-lifecycle coordinator. It owns semantic PIDs,
parent/reaper relationships, the PM spawn counter, collaborator ordering,
rollback, and wait completion. The kernel owns execution publication and
teardown; VM owns mapping policy; VFS owns paths and descriptor references.
IPC words and grants never become pointer or transitive authority.

The service retains one process table in its existing context. The coordinator
borrows that table instead of allocating another. Its persistent additions
are one resident request page and a nonreused PM spawn-token counter.
Per-transaction handles, plans, messages, and rollback actions stay automatic.
VM copies into its existing resident input buffers; it does not allocate
another executable snapshot.

The intended responsibility owners are:

| Owner | Task |
| --- | --- |
| `servers/pm/pm_coordinator.c` | Configuration, public request preflight, trusted-init entry, and dispatch |
| `servers/pm/pm_coordinator_protocol.c` | Exact VFS/VM request words, reply validation, and result mapping |
| `servers/pm/pm_coordinator_spawn.c` | Forward spawn phases and sole activation boundary |
| `servers/pm/pm_coordinator_rollback.c` | Reverse collaborator release and exact PM acknowledgments |
| `servers/pm/pm_coordinator_exit.c` | Stop, VFS release, detach, VM release, process release, and zombie publication |
| `servers/pm/pm_coordinator_wait.c` | Immediate/deferred waits and independent token-bound reply completion |
| `servers/pm/pm_coordinator.h` | Supported portable coordinator and transport contract |
| `servers/pm/pm_coordinator_internal.h` | Narrow declarations-only coordinator helpers |
| `servers/pm/pm_service_backend.c` | Real runtime grants, IPC, and existing PM-control wrappers |
| `tests/qemu/vm_server_control.c` | Ordinary PM control dispatch and exact peer-message validation |
| `tests/qemu/vm_server_load.c` | Grant snapshots and real scratch/freeze load execution |
| `tests/qemu/vm_server_release.c` | Exact abort, post-activation commit, and exit-release acknowledgments |

The VM adapter paths follow the real service's existing historical source
location. They invoke production `servers/vm` state and image owners, not a
fixture-only loader. Existing synthetic PM and scratch scenarios remain
separate compile-time adapters.

Coordinator policy uses the supported lifecycle operations and narrow private
prevalidated commit plans. Service adapters do not include `pm_internal.h`.
Reference decisions use neither production-private transitions nor production
coordinator helpers.

Configuration initialization borrows one valid PM table before its first
dynamic PID is consumed. It requires three distinct concrete endpoint
generations and zero reserved binding fields, initializes the resident request
page and PM spawn counter, and publishes readiness last. It is one-shot;
reinitialization or reconstruction around an already used lifecycle table is
rejected without changing either object.
Binding input is captured before owned storage is cleared, including when
the caller supplied the coordinator's own binding member.

The constructor, byte-exact VFS/VM codecs, and the trusted-init forward
transaction with every completed-stage rollback are implemented in
`MICROS_PM_COORDINATOR_SOURCES` and `MICROS_PM_COORDINATOR_HOST_TEST_SOURCES`.
The fast `pm-coordinator` CTest is included by both native aggregates.
Ordinary application spawn, exit/wait coordination, runtime peer adapters,
and the separate mixed lifecycle model remain in progress.

VFS control identity remains in its existing shared header. The separate
`include/micros/vm_service.h` defines only the already Accepted user-service
IPC types, commands, and results; the kernel operation-12 header is unchanged.
Request builders check every command's scalar and reserved words. Reply
decoders require exact peer generation, type, echoed version/command,
zero-upper-half signed results, and command-specific output shape before
publishing any output. Grant/size pairs and VM entry/stack diagnostics are
data, never cross-process pointer authority. This shared header inherits the
existing complete-matrix public-header validation policy.
The trusted-init `PREPARE_IMAGE` parent word is literal zero, not the distinct
internal `MICROS_ENDPOINT_NONE` sentinel; ordinary spawn carries its concrete
parent endpoint.

The typed backend is borrowed for one operation. Init requires only the
kernel reservation/preparation/activation, grant creation/revocation, and IPC
ports it can reach, including every reverse release. It does not gain exit or
reply authority through a missing callback. The native transition fixture
validates each collaborator's exact token, stage, word, and resource shape.
It checks the complete success trace, all eight prepublication rejection
points, and mandatory post-activation failure without rollback.

Init copies the validated absolute-path request before PID consumption,
issues a separate monotonic PM spawn token, revokes its VFS request grant
after every returning image call, and retains the exact VM diagnostic stack
cross-check. Kernel activation immediately composes the prevalidated local
commit before VM commit or VFS release. Expected prepublication failure
unwinds exact resource acknowledgments and remains terminal for PID 1;
postpublication failure is an explicit invariant outcome, never failed spawn.

## Active mapping identity

ADR-0050 requires the running PM record to retain the kernel load token and
mapping generation needed by normal exit. The existing metadata-only
activation clears all temporary spawn resources; the coordinator extension
must not lose those two identities.

The process record therefore gains `load_token` and `mapping_generation`
outside temporary `spawn` state. Both are:

- zero for free, quarantined, spawning, and zombie records;
- nonzero for a running record and its stopped/detached exit stages; and
- cleared together only after VM acknowledges complete mapping release.

Activation copies the exact frozen pair before clearing spawn state.
Endpoint detach does not clear it. VM-release acknowledgment clears it before
the kernel-process-empty phase, and zombie publication retains no mapping
authority. Complete table validation checks the pair at every state.

The existing replayable PM model keeps its seed, draw schedule, operations,
and step count while adding independent comparisons of the retained pair.
This is a Step 14 behavior extension, not a change to the previously merged
source-only correction's preservation evidence.

## Commit boundaries

Before kernel activation, PM preflights its local activation plan. After the
kernel publishes the child, the immediate PM `RUNNING` update is a bounded,
non-failing commit; no new lookup, capacity check, or collaborator call may
intervene. The same pattern applies to local exit/reparent commit after
kernel `EXIT_BEGIN`.

Existing public lifecycle operations retain their argument and validation
order. Their private prepare/commit helpers also let the higher coordinator
preflight before an irreversible kernel transition without duplicating
record-mutation logic.

`pm_spawn_prepare_activation` and `pm_exit_prepare_begin` publish private
automatic plans only after complete preflight. Their corresponding
`pm_spawn_commit_activation` and `pm_exit_commit_begin` functions return
`void` and perform only bounded prevalidated stores. The public lifecycle
operations compose those same helpers; service and reference code do not
import them.

The exact collaborator sequence, rollback rows, command words, error
precedence, and post-activation obligations remain those in ADR-0050.
Every peer reply is checked for exact endpoint generation, type, version,
echoed command, result, output shape, and zero reserved words before use.
Unexpected internal failures are explicit invariant outcomes that the target
turns into its existing fatal service diagnostic.

A PM-to-VFS request grant is revoked after every returning VFS image result.
Every expected preactivation failure unwinds the highest completed stage;
failure in rollback is fatal. VM internally removes every partial scratch or
transferable resource before returning an expected load failure.

After activation, VM load commit and VFS image release are mandatory before
the parent reply. Their failure cannot become an ordinary failed spawn.
Trusted-init failure is terminal and cannot retry or reuse PID 1.

The native model also exercises the Accepted parent-loss rollback input and
reply suppression. This adds no target fault-notification or token-query ABI:
current application faults remain fatal, and fault-generated exit remains
deferred.

## Exit and wait

Accepted exit retains no reply to the exiting application. PM completes VFS
cleanup before kernel detach, retains load identity through detach, completes
VM release before empty-process release, and only then publishes the zombie.

The existing two-parent outcome bundle is preserved. Init's complete child
set is scanned first; the original parent's set is scanned separately only
when it differs. Every staged wait reply is accepted or restored independently,
in order, before receiving another request. One failed reply must not consume
another parent's status or prevent its attempted completion.

An immediate wait with a staged zombie uses `reply` followed by the matching
local completion; it cannot use `reply_receive` before recording acceptance.
A blocked wait or accepted exit returns to ordinary receive without replying.
Ordinary rejection and spawn-result paths may retain atomic `reply_receive`
when no local post-reply commit remains.

## Existing service boundaries

Historical dependency-closed QEMU prefixes retain their existing readiness,
synthetic lifecycle, scratch, console, and failure behavior. They do not gain
an application or infer a missing VFS authority.

The coordinator derives VM and VFS endpoints only from the checked immutable
service configuration. A valid dynamic transaction requires the complete
peer bindings; missing required authority is fatal rather than a fallback.
The fixed PM priority 8 and application priority 9 preserve the Accepted
post-activation ordering.

The PM 32-page, VM 160-page, and VFS 80-page bounds remain enforced by the
existing single post-link checks. Public wire headers, syscall numbers,
profiles, page permissions, and service startup ABI do not change.

## Test-first implementation sequence

1. Retained mapping identity and prevalidated local commit phases, with PM
   activation/exit regressions and independent model comparisons.
2. Portable request and peer-protocol preflight, directed spawn, and every
   reverse rollback row with native transport/failure injection.
3. Exit/wait coordination, including the nested two-parent regression and
   independent failed/successful wait replies.
4. Real PM runtime and VM control adapters, preserving existing QEMU prefixes.
5. One replayable mixed lifecycle model and `test-elf-spawn-model`, added to
   complete native validation and excluded from the fast loop.
6. Exact source/build/validation ownership, service budgets, one authoritative
   PR plan, and independent implementation review.

The smallest existing native gate runs for each green slice. The final PR
plan runs once; selected workflows are not repeated separately. App startup,
seeded application images, and `test-qemu-elf-spawn` remain their later
documented task gates.

The first lifecycle slice uses the existing configured native graph:

```bash
cmake --build build/host-debug --target pm_test --parallel
ctest --test-dir build/host-debug --output-on-failure -R '^pm-lifecycle$'
python3 -m unittest tests.host.test_pm_service_source_layout
```

Coordinator configuration and later deterministic transaction cases use:

```bash
cmake --build build/host-debug --target pm_coordinator_test --parallel
ctest --test-dir build/host-debug --output-on-failure -R '^pm-coordinator$'
```

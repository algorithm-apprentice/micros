# MINIX PM Service Source-Organization Baseline

## Scope and fixed reference

This note traces the current dependency-ready PM lifecycle, dispatch,
argument capture, and evidence boundaries before the source-only correction
in ADR-0058. The local MINIX checkout was verified at:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

The `micros` source baseline is merged VM outcome
`6fa6984a2491bafa005feb205438a3c4c63508b6`.

The older [Step 10 study](minix-pm-process-lifecycle.md) and
[ELF/spawn study](minix-elf-exec-and-spawn.md) provide behavioral context.
This ledger applies the current ADR-0046/ADR-0050 contract and ADR-0025's
classification definitions; it does not import deferred MINIX breadth or
reclassify that breadth as a bootstrap policy substitution.

## Traced responsibilities

### Dispatch and exact semantic caller

`minix/servers/pm/main.c:45-101` receives messages, handles notifications first,
resolves the exact source endpoint to PM state, rejects delayed calls from
exiting processes, dispatches known handlers, and replies unless the handler
suspends. Startup initializes the table and imports authoritative boot
identities at `main.c:137-238`.

The reusable organization is common startup/dispatch over cohesive handlers,
with exact source identity and explicit suspension/reply boundaries.
`micros` retains its launcher readiness and kernel-sealed event rather than
adding SEF, RS, timers, or asynchronous VFS reply machinery.

### One PM-owned semantic table

`minix/servers/pm/mproc.h:19-83` separates PID, exact endpoint, parent, wait,
status, and lifecycle flags from kernel execution and VM/VFS resources.
`utility.c:31-50` allocates semantic PIDs independently from endpoint identity;
`utility.c:73-83` resolves a semantic PID in that table.

The `micros` table retains monotonic PIDs, generation-safe PM handles,
explicit init/reaper state, one hidden spawn, and separate wait state.
Complete validation and shared record queries must not become additional
authorities or caches.

### Creation and hidden completion

`minix/servers/pm/forkexit.c:45-139` checks table capacity, obtains a child
from VM/kernel, records parent/PID/endpoint metadata, then suspends until
VFS and scheduling completion. It explicitly forbids ordinary failure after
VM has already created the kernel child.

`micros` preserves PM's coordinating and publication role with the Accepted
hidden spawn transaction, exact stage/resource tokens, and reverse rollback.
This source correction separates existing forward and reverse policy; it
does not implement the still-pending production coordinator.

### Exit and wait

`forkexit.c:265-384` stops execution before informing VFS, retains identity
for cleanup, and records status. `forkexit.c:417-468` continues teardown.
`forkexit.c:472-585` selects qualifying children and distinguishes an immediate
zombie, `WNOHANG`, blocking wait, and no child. Later wait/parent helpers
retain status and prevent premature record reuse.

`micros` retains the already Accepted stricter publication point: no zombie
or wait result until all non-PM resources are gone. ADR-0050 further requires
init and the original parent to be reconsidered independently after zombie
publication, with one deduplicated scan when they are the same handle.
Splitting exit from wait requires only the existing private wait-staging
edge, not a new policy or callback framework.

### Executable and argument boundaries

`minix/servers/pm/exec.c:35-55` forwards execution to VFS and suspends the
caller. Its later metadata/context phases remain distinct from pathname and
image policy. `minix/lib/libc/sys/stack_utils.c:19-112` documents offset-based
frame pointers and computes checked argument/environment occupancy before
the frame is filled.

`micros` preserves captured request bytes and separate stack sizing under its
fixed RV64 pointer-free format. PM does not acquire VM ELF or frame policy.
The canonical decoder and layout planner remain separately callable through
their unchanged header; VM owns final stack construction and ELF planning.

## Current `micros` authority and readiness

The portable table already models the complete lifecycle. The real PM service
initializes its table and rejects ordinary requests according to the current
protocol/caller boundary. Its isolated PM build drives the synthetic kernel
prepare/abort/re-prepare/activate/exit path; the scratch build acts as the
preemption peer.

Neither is production application spawn, PID-1 launch, or a substitute for
the later PM/VFS/VM coordinator. Native PM cases, the 4,096-step model,
spawn-ABI cases, final kernel report, and generated fixture retain their
existing distinct evidence roles.

## ADR-0025 classification

| Behavior or difference | Classification | Current treatment |
| --- | --- | --- |
| PM owns semantic identity, parentage, exit and wait | Baseline parity | Preserve one explicit PM table |
| Dispatch resolves exact source and distinguishes suspension/reply | Baseline parity | Preserve current adapter branch/order and exact kernel-written identity |
| PM, VM, VFS, and kernel own separate creation/teardown resources | Baseline parity | No authority moves during decomposition |
| Hidden creation completes before user success | Baseline parity | Keep existing stage and publication boundaries |
| Exit stops execution before identity-dependent cleanup | Baseline parity | Keep the accepted ordered lifecycle |
| Wait distinguishes zombie, live child, nohang, and no child | Baseline parity | Preserve exact selectors and reply ownership |
| Offset-based captured argument frame and checked occupancy | Baseline parity | Preserve decoder/layout separation and snapshot timing |
| RV64 fixed messages, direct grants, and one-shot reply tokens | Required adaptation | Retain the Accepted RISC-V and IPC authority contract |
| Launcher readiness, sealed event, and static-service/application separation | Required adaptation | Retain the Accepted non-RS bootstrap contract |
| Generation-safe PM handles and nonreused semantic PIDs | Compatible extension | Preserve stale rejection and explicit exhaustion |
| Hidden spawn with complete preactivation rollback | Compatible extension | Preserve ADR-0050 rather than destructive fork/exec failure boundaries |
| Resource-complete zombie publication and deterministic independent wait sets | Compatible extension | Preserve existing safety and development extensions |
| Fine-grained cohesive owners and deterministic evidence | Compatible extension | Apply Accepted ADR-0051 without changing behavior |

No current dependency-ready behavior requires correction. No new policy is
temporarily moved into the kernel or launcher, so this source correction
introduces no staged substitution.

MINIX fork/COW, destructive exec, credentials, process groups, tracing,
signals, resource accounting, procfs/NetBSD compatibility, RS recovery,
live update, and general asynchronous collaborators are outside this parity
analysis. They remain explicit later scope, not missing mechanisms to add
while reorganizing source.

## Required adaptations and implementation evidence

- Keep every public PM/spawn/test header and state layout byte-identical.
- Keep common primitives below facade and validation to avoid a dependency
  cycle; never let validation call transitions.
- Preserve the existing exit-to-private-wait staging edge and no reverse edge.
- Keep PM case/model state automatic. Consolidate only existing service
  globals and spawn-ABI static buffers; do not invent a kernel report context.
- Keep native case order, model seed/draws/trace/comparison, negative cases,
  generator bytes for identical inputs, target symbols, markers, and timeouts.
- Use exact source sets and original validation inheritance so coverage is
  neither duplicated nor narrowed.

The exact accepted implementation map and future Red/Green evidence are
defined by
[PM Service Source Organization](../architecture/pm-service-source-organization.md).
This design phase authors no executable scaffold or prototype and copies no
MINIX source.

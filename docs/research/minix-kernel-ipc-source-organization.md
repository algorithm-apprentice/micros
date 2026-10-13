# MINIX Kernel IPC Source-Organization Baseline

## Scope and reference

This note traces the dependency-ready synchronous IPC responsibilities before
the behavior-preserving source correction in ADR-0059. The local reference
checkout was verified at:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

The `micros` source base is merged PM outcome:

```text
11c1c43a3fff4c3104eac3f5eb656ae27185a61a
```

The existing [endpoint/IPC study](minix-endpoint-and-ipc.md) and
[IRQ notification study](minix-irq-notification.md) retain the broader
behavioral evidence. Accepted ADR-0030, ADR-0035, ADR-0036, and ADR-0037,
including their later bootstrap and exit consumers, remain authoritative.
This correction does not replace them with MINIX's historical file layout.

## Traced boundaries

### Entry, identity, and permission

`minix/kernel/proc.c:479-683` separates operation recognition, endpoint
resolution, destination permission, operation permission, and dispatch.
Its execution context supplies the caller; request payloads do not choose
caller identity.

`micros` already separates the generic captured syscall context from IPC's
request/buffer adapter and portable held-thread operations. The correction
keeps that split, including upper-endpoint rejection, complete snapshots,
controller reply restrictions, guard rollback, and shared selected-thread
completion. It does not move RISC-V current-thread policy into portable IPC.

### FIFO rendezvous

`proc.c:868-960` attempts matching delivery before deadlock and blocking.
It copies an outgoing message before publishing sending state and appends
the sender at the destination tail. `proc.c:965-1117` searches acceptable
pending work, consumes one matching sender without reordering unmatched
peers, and clears only the sending reason.

The reusable responsibilities are exact identity/shape queries, bounded
queue search and linkage, staged completion, and the higher-level
send/receive/call transaction. `micros` retains distinct thread and endpoint
objects, separate sender/receiver queues, scheduler-held portable callers,
and failure-atomic scheduler transitions.

### Request and reply

`proc.c:569-585` combines send with reply-pending receive. Its receive path
excludes notifications while that reply remains outstanding. A MINIX server
observes a completed receive before using ordinary send to reply.

`micros` retains its Accepted one-shot token extension instead: a token
binds the exact caller thread and callee endpoint, and is not presentable
while its request is queued or staged but undrained. After request return,
an authorized sibling thread may reply. The shared token preflight therefore
belongs below ordinary reply and atomic reply/receive, not in a new token
cache or provenance table.

### Notifications

`proc.c:1120-1168` delivers to an eligible receiver or records pending source
state without blocking. Repeated events coalesce. `proc.c:965-1117` selects
notifications before ordinary blocked senders and excludes reply waits.

The Accepted `micros` representation remains destination-owned endpoint
source masks plus an independent kernel-event mask. Kernel pseudo-source
events are consumed only by `ANY`; a specific-source receive does not
consume them. The exact prepared kernel-notification plan and its
non-failing commit remain available to console and bootstrap callers.

### Deadlock projection

`proc.c:701-767` follows blocked-on endpoint dependencies and allows a
complementary two-party send/receive pair. `micros` already attempts matching
first, then performs a bounded generation-safe walk with `SEND`, `REPLY`,
and specific `RECEIVE` precedence. Its checked sole-live-thread projection
is policy over distinct process/thread objects, not structural equivalence.

The correction retains this cohesive bounded algorithm and its existing
corruption/deadlock distinction. It does not introduce wait-set exploration,
priority inheritance, or another graph representation.

### Cancellation and delayed delivery

`minix/kernel/system.c:500-610` removes a closing process from queues,
clears communication state, removes pending source references, and wakes
dependents with a dead-peer result. `proc.c:255-290` keeps delivery staged
until return and asks VM to resolve a failed user copy.

`micros` retains the stricter complete close preflight, exact-generation
queue/token cancellation, unrelated `ANY` preservation, staged-success
close rejection, cascading owned dead-endpoint completion, and non-failing
close commit. Shared return continues revalidating its retained virtual
buffer; the current bootstrap mapping-loss panic is not replaced by VM
fault recovery.

## Current implementation observations

- Portable IPC has no file-scope mutable state. Endpoint/thread tables remain
  explicit parameters, and complete validation remains in the existing
  endpoint/object owners.
- `kernel/ipc_runtime.c` retains the sole authoritative target registry,
  initialization scratch, and readiness flag. It is already cohesive and is
  not decomposed by this task.
- The signal files mix raw queue primitives, completion shaping, token
  preflight, notification selection, rendezvous, deadlock, close, captured
  syscall adaptation, fixtures, case scripts, and independent reference
  operations.
- Five native case families, the acceptance model, and three target families
  have existing mutable static storage. Consolidating that storage requires
  nine explicitly owned contexts, not additional authoritative registries.
- The acceptance model's reference table is automatic. Reference decisions
  must remain independent of production helpers after extraction.
- The original target entry points capture supervisor `sp`, `gp`, and `tp`
  immediately before scheduler entry. Those captures cannot move into a
  returning helper that would leave a dead stack frame as the resume point.

## ADR-0025 classification

Each row uses exactly one classification.

| Behavior or difference | Classification | Required treatment |
| --- | --- | --- |
| Exact endpoint caller and operation/target permission | Baseline parity | Preserve current preflight and source identity |
| Matching before blocking, FIFO append, unmatched preservation | Baseline parity | Preserve current raw queue and rendezvous order |
| Sending clears independently from request reply wait | Baseline parity | Preserve independent RTS reasons |
| Notification coalescing and reply-wait exclusion | Baseline parity | Preserve both pending representations and selection |
| Closing endpoint cancels exact dependents | Baseline parity | Preserve bounded preflight and cancellation |
| Kernel-owned snapshot and deferred return delivery | Baseline parity | Preserve current selected-thread completion |
| RV64 captured registers, Sv39 translation, one-hart SIE boundary | Required adaptation | Keep target adaptation outside portable operations |
| Fixed unsigned generation ABI and quarantine | Compatible extension | Preserve stale rejection and exhaustion |
| Distinct process, thread, endpoint, and hart objects | Compatible extension | Preserve typed identities and checked policy limits |
| One-shot reply right and request-return availability | Compatible extension | Preserve ADR-0036 without historical token state |
| Atomic reply/receive and complete failure preservation | Compatible extension | Preserve preflight and one commit boundary |
| Prepared kernel-event injection | Compatible extension | Preserve exact plan, authority supplied by its caller, and consumption |
| Deterministic reference models and cohesive source owners | Compatible extension | Preserve evidence while applying ADR-0051 |
| Fatal trusted-bootstrap buffer loss until VM fault delivery exists | Staged substitution | Preserve ADR-0035; replacement requires the later fault contract |

No dependency-ready behavioral divergence requires correction in this
source-only outcome. Asynchronous/nonblocking IPC, VM-assisted retry,
signals, tracing, broad cancellation, multihart synchronization, and recovery
are outside its parity analysis; they are not new mechanisms to import.

## Evidence boundary

The implementation must retain the native `endpoint-profile` and `ipc-model`
executions and the three existing IPC target scenarios plus the unchanged
buffer-loss panic gate. The complete accepted source/owner/state/case/model/
assembly/validation inventory and semantic function dispositions are defined
in [Kernel IPC Source Organization](../architecture/kernel-ipc-source-organization.md).

This design authors no executable scaffold, test, model, or prototype and
copies no MINIX source.

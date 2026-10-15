# MINIX Kernel Endpoint Source-Organization Baseline

## Purpose and fixed reference

This study precedes the behavior-preserving endpoint correction in
[ADR-0067](../adr/0067-kernel-endpoint-source-decomposition.md). The fixed,
read-only local reference is MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

The [endpoint and blocking IPC study](minix-endpoint-and-ipc.md) remains the
behavioral classification ledger. This document records source ownership and
the already accepted adaptations needed to preserve that behavior. MINIX is
evidence, not a source donor.

## Source trace

| Responsibility | Fixed MINIX evidence | Organizational lesson |
| --- | --- | --- |
| Endpoint representation | `minix/include/minix/endpoint.h:10-70` | Slot/generation layout and special identities have one shared representation boundary; their encoding is not privilege policy. |
| Exact live resolution | `minix/kernel/proc.c:1828-1860` | Resolution compares the complete endpoint with one nonempty process slot before communication. |
| Privilege state | `minix/kernel/priv.h:20-66,86-87` | Operation, destination, and kernel-call masks remain distinct authority fields; notification/device state is not an implicit reply capability. |
| Privilege association | `minix/kernel/system.c:274-300` | Privilege identity is explicitly associated with the target process; allocation is separate from communication. |
| Mask installation and reply symmetry | `minix/kernel/system.c:307-344` | Mask construction has a distinct owner. The selected `micros` token-based reply extension must not acquire MINIX's reverse-send mutation during a file move. |
| Inhibited installation and release | `minix/kernel/system/do_privctl.c:26-100,285-367` | Authority is installed while execution is inhibited, before the separate transition that permits running. |
| Endpoint shutdown | `minix/kernel/system.c:540-611` | Inhibit communication first, then detach exact IPC references and wake dependents; reference cancellation has an explicit lifecycle owner. |

The fixed tree separates ABI and state headers, common process/IPC mechanism,
system lifecycle helpers, and individual privileged-operation handlers.
It does not provide the exact `micros` file list or a reason to merge
process, thread, endpoint, and profile state.

## End-to-end authority path

Endpoint data is decoded and checked against exact live state before IPC.
The caller's operation and destination masks then authorize the requested
communication. Privilege installation is a separate privileged operation on
an inhibited process, not a side effect of endpoint lookup. Shutdown marks
the target unable to communicate before exact dependent references are
removed.

`micros` preserves these boundaries through its existing immutable profile
table, reserved publication, public checked resolution, complete relation
validator, and IPC/grant cancellation owners. The endpoint source correction
only makes their existing responsibilities visible. In particular, it does
not move queue mutation or grant cancellation into the endpoint validator.

## Classified differences

Each row uses one ADR-0025 classification. No new runtime behavior is selected.

| Difference | Classification | Required disposition |
| --- | --- | --- |
| Exact generation and live-slot resolution before communication | Baseline parity | Preserve complete identity checks and output preservation, including stale versus reserved/source-only results. |
| Separate process/thread/endpoint objects, unsigned 12/20 packing, terminal quarantine, and one consumed lifecycle per process generation | Compatible extension | Retain Accepted ADR-0019/0029/0043/0050 representations and anti-resurrection rules; do not import MINIX's task encoding or generation reuse. |
| Sealed named profiles and distinct call/send/notify masks instead of mutable symmetric send masks | Compatible extension | Preserve canonical construction, whole-table checks, one-time installation, and existing token reply authority. |
| Launcher-controlled bootstrap publication until RS exists | Staged substitution | Keep exact held-image/profile/publication commits and irreversible launcher sealing; no service discovery or recovery is introduced. |
| Exact source-only launcher acknowledgment and stopped PM reply-wait teardown | Compatible extension | Preserve both Accepted ADR-0043/0050 paths and their different preconditions; do not require all source-only owners to have empty reply state. |
| One-hart SIE-clear external serialization and RV64 return integration | Required adaptation | Retain the target IRQ lifetime and existing runtime authority rather than copying architecture-specific MINIX entry or locking. |
| Explicit portable/native/model/target owners and deterministic typed reconstruction | Compatible extension | Apply ADR-0051 without copying MINIX's file granularity or weakening runtime evidence. |

The existing ledger documents deferred asynchronous IPC, broader privilege
mutation, VM fault recovery, and service restart. This correction adds none
of them. No divergence requiring correction was found that authorizes a
behavior change in this task.

## `micros` source constraints derived from the trace

1. Canonical profile construction is a leaf. Checked profile queries may
   invoke registry validation without creating a reverse dependency.
2. Complete object/queue/completion validation is read-only and does not
   perform public resolution recursively, publish authority, or repair state.
3. Publication and close retain the existing serialized preflight/commit
   interfaces; a private header does not grant a new caller privilege.
4. Native and model fixtures remain separate; expected-state logic does not
   import production private helpers.
5. Target initialization and grant-cancellation evidence uses the same
   authoritative IPC/grant runtimes and one original IRQ-save/restore scope.

The exact source, storage, phase, build, and evidence map is the
[kernel endpoint source contract](../architecture/kernel-endpoint-source-organization.md).
Design acceptance still requires independent review; this study does not
implement the proposed split.

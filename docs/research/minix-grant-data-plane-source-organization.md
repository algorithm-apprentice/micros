# MINIX Grant Data-Plane Source-Organization Baseline

## Fixed reference and purpose

This read-only trace supports Accepted
[ADR-0069](../adr/0069-grant-data-plane-source-decomposition.md) and its
[exact source contract](../architecture/grant-data-plane-source-organization.md).
The local reference is `/Users/alephling/Repo/minix`, fixed commit
`4db99f4012570a577414fe2a43697b2f239b699e`; selected trace files were verified
unchanged against that commit before inspection.

The current `micros` base is
`6269d9247de6beafed2448aa0ffd6904de5e8833`, after kernel-object #138.
MINIX is behavioral evidence, not source to import.

The existing [direct-grant/safe-copy study](minix-direct-grants-and-safecopy.md),
[syscall-boundary study](minix-user-grant-syscall-boundary.md), and
[post-handoff resolution study](minix-post-handoff-wired-address-resolution.md)
remain the canonical semantic analyses. This note traces source/task owners
for maintenance rather than duplicating those protocols.

## Entry to return

### User lifecycle and kernel verification are distinct

`minix/lib/libsys/safecopies.c:90-174` owns table growth/free-list acquisition
and direct-entry publication. It fills authority before publishing validity.
`222-273` owns revoke-time invalidation, sequence advance and LIFO reuse.
`minix/kernel/system/do_setgrant.c:15-29` separately binds the table address/
extent to the current privilege owner.

Those are distinct lifecycle/publication tasks, but `micros` already places
its active records in one fixed kernel registry. That registry is cohesive
and explicitly outside this source correction.

### Directional adapters do not own memory authority

`minix/lib/libsys/sys_safecopy.c:5-43` packages endpoint/token/offset/local
address/length into the existing kernel-call protocol. The kernel's
`do_safecopy_from/to` adapters at
`minix/kernel/system/do_safecopy.c:374-394` select read/write direction and
the caller endpoint before delegating.

The corresponding `micros` captured adapter must retain its already captured
current-process authority, exact register-shape decode and stable result
mapping. It must not independently hoist a participant/token/range check
from the selected portable operation.

### Verification precedes transfer

`verify_grant` at `do_safecopy.c:41-216` resolves endpoint/table lifetime,
copies a bounded entry, checks flags/sequence, follows only bounded indirect
authority, then checks direction, grantee and range before publishing the
resolved remote address. Magic and soft-fault metadata are separate
branches at `217-265`.

`safecopy` at `270-371` selects source/destination, calls verification,
binds final addresses and only then invokes transfer. The ordinary VM-aware
path and fail-fast soft-fault path are distinct; neither makes a raw message
pointer itself authority.

`micros` retains its stricter complete dual-plan preflight and non-failing
physical commit. Source extraction may expose those phases but cannot
replace their order with per-page validate-and-copy.

### Partial vector progress is not the scalar MVP contract

`do_vsafecopy` at `396-446` obtains the vector, selects each direction and
executes elements in order, returning at the first failure. Earlier elements
may already have completed. `micros` does not gain vectors, partial results,
fault suspension or retries from source organization.

## Difference classification

Each row has exactly one ADR-0025 classification.

| Topic | Classification | Required maintenance treatment |
| --- | --- | --- |
| Exact peer/range/direction before transfer | Baseline parity | Preserve all authority and participant checks before retained-plan commit. |
| Lifecycle, verification, adapter and transfer owners | Baseline parity | Expose those separate tasks without moving registry policy into the adapter. |
| RV64 register `ecall` instead of kernel-call messages | Required adaptation | Preserve the existing one namespace and full-width scalar ABI. |
| Kernel-managed fixed records and generation quarantine | Compatible extension | Keep the Accepted registry and cancellation implementation byte-exact. |
| Direct-only, exact generations, no wildcard/magic forwarding | Compatible extension | Keep non-transitive authority; no new grant kind or delegated pointer. |
| Resident mappings without VM retry | Staged substitution | Keep current wired/frozen mapping authority and explicit faults; add no callback. |
| Complete one-page dual planning, overlap rejection and failure-atomic commit | Compatible extension | Preserve both plans before error classification and copy zero bytes on every recoverable failure. |
| Sv39 physical chunks and one-hart SIE-clear execution | Required adaptation | Preserve actual IRQ lifetime, permissions, owner validation and operation-local plans. |
| TTY device/VM scratch exclusion and non-copying operation 15 | Compatible extension | Preserve the later Accepted exact exclusions and remote-only preflight, not a lease. |
| Native deterministic/model and real target scripts | Compatible extension | Preserve actual fixtures, data, draw/trace/coverage, GPR/scheduler checks and cleanup rather than importing MINIX's test harness. |

No new behavioral divergence is proposed. Existing Accepted ADRs remain
authoritative until explicitly superseded.

## Evidence-derived target boundaries

The inspected seven `micros` signals have 8,018 lines, 95 definitions and
fourteen long functions. They combine mechanisms that the baseline already
keeps conceptually distinct:

- range translation/device exclusion versus copy commit orchestration;
- captured syscall decode, operation authority and result publication;
- native lifecycle/negative cases versus independent reference/trace/model;
- raw command arming versus real post-return transitions and effect checks;
- fixture snapshots/observations versus resource lifecycle and trap hooks.

The Accepted 22-owner contract keeps exactly one mutable context for each
existing evidence family. In particular, raw arming/control are a lower
fixture boundary rather than living with dispatch, avoiding a command/
copy-helper cycle. Existing assembly save objects remain outside the context.

The implementation evidence must derive complete-body and actual-data
inverses from the reviewed Git base, retain independent expectations and
all real source consumers, and inspect narrow existing native/QEMU output
before one final full author/reuse gate. The design itself authors no
executable test, model, harness, build scaffold or prototype.

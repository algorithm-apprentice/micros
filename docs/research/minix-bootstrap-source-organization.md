# MINIX Bootstrap Source-Organization Baseline

## Reference and purpose

The fixed behavioral reference is MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

The canonical boot trace and runtime classification ledger are already in
[the RS/SEF bootstrap study](minix-rs-sef-bootstrap.md). This note applies
that verified trace to source responsibility boundaries before the
behavior-preserving `micros` bootstrap decomposition. It copies no MINIX
implementation or source layout wholesale.

The `micros` source base is
`49706a8ef8ef2296c42dc1ecadef0df375d2ee2f`, after the independently reviewed
production spawn/exit coordinator in PR #118.

## Observed responsibility boundaries

| MINIX owner/evidence | Responsibility relevant to this correction | `micros` consequence |
| --- | --- | --- |
| `minix/kernel/table.c:36-66`, `minix/include/minix/type.h:148-155` | Fixed boot image identity and descriptor representation | Keep immutable catalog/manifest validation separate from live lifecycle mutation |
| `minix/kernel/main.c:150-270` | Prepare boot process/image identity and inhibit execution before service release | Expose static resource preparation as named phases before publication |
| `minix/kernel/system/do_privctl.c:38-64,86-175` | Validate privilege setup and release only the inhibited exact process | Preserve kernel-owned preflight/publication and its failure boundary |
| `minix/servers/rs/table.c:10-51` | Immutable service privilege, system and device boot policy | Give fixed profile/relationship validation its own owner rather than mixing it with runtime transitions |
| `minix/servers/rs/main.c:177-354` | Construct runtime descriptors and install policy before allowing execution | Keep descriptor preparation and exact generation bindings separate from orchestration |
| `minix/servers/rs/main.c:356-417` | Ordered release and initialization-result collection | Keep the launcher outline visible without importing MINIX's overlapping startup breadth |
| `minix/servers/rs/utility.c:18-62` | Initialization request and deadline construction | Keep readiness/deadline transaction phases distinct from message transport |
| `minix/servers/rs/request.c:462-526` | Readiness validation, one completion and reply | Preserve independent readiness protocol and token-bound kernel acknowledgment owners |
| `minix/lib/libsys/sef.c:68-142`, `sef_init.c:43-138,191-217,455-483` | Service startup protocol, callback result and reply gate | Keep user entry/runtime separate from service readiness and launcher policy |
| `minix/servers/rs/request.c:943-1045` | Internal progress clock and initialization failure/liveness handling | Preserve target-owned deadlines and diagnostics rather than host timing or sleeps |

These locations are traced in the canonical study, including failure,
inhibition, visibility, readiness reply and deferred recovery. The source
correction must retain the current independently implemented `micros`
behavior; MINIX is evidence for responsibility direction, not a code donor.

## Classification of the proposed source boundaries

| Boundary | ADR-0025 classification | Reason |
| --- | --- | --- |
| Immutable identity/policy validation separate from runtime state transitions | Baseline parity | MINIX separates fixed tables, kernel preparation and RS runtime startup |
| Service preparation, exact publication and readiness completion as visible tasks | Baseline parity | The traced boot flow crosses those distinct authority/commit boundaries |
| RISC-V FDT/allocator/Sv39/PLIC boot foundation separated from component dispatch | Required adaptation | These target mechanisms replace MINIX architecture/bootstrap details; they do not become service policy |
| Kernel-controlled complete static preparation before VM handoff | Required adaptation | Accepted ADR-0040/0043 handoff closes later generic preparation |
| Exact generation-bound profiles, staged endpoints and source-only sealing | Compatible extension | Existing accepted safety boundaries remain unchanged by source movement |
| One explicit context replacing only existing related target statics | Compatible extension | Storage ownership becomes explicit without introducing another runtime or global current object |
| Independent launcher validator and native reference source owners | Compatible extension | Defense in depth and independent evidence remain intact instead of sharing production decisions |
| Static launcher and serial readiness in place of full RS/SEF lifecycle management | Staged substitution | Existing DAG substitution remains until later DS/RS recovery; no new recovery machinery is imported |
| Separate C17 responsibility modules and declarations-only private contracts | Compatible extension | Accepted ADR-0051 organizes the independent implementation without changing behavior |

No new runtime divergence or authority is introduced. Existing behavioral
classifications remain those in the canonical study and relevant Accepted ADRs.

## Deliberately rejected imports

Do not import MINIX's complete RS process descriptors, privilege construction,
overlapping initialization, workers, timers, heartbeat/restart policy, replicas,
live update, DS publication, driver discovery or recovery merely to resemble its
file list.

The `micros` correction needs explicit owners for its current bounded
manifest/profile validation, preparation, rollback, captured control,
diagnostics, boot outline, launcher and evidence. It does not need the full
MINIX component graph or additional runtime abstractions.

## Derived design

The exact proposed file ownership, stable interfaces, function phase outlines,
context lifetimes, build consumers and validation inheritance are defined by
[the bootstrap source contract](../architecture/kernel-bootstrap-source-organization.md)
and [ADR-0060](../adr/0060-kernel-bootstrap-source-decomposition.md).
Implementation remains blocked until their design review and merge.

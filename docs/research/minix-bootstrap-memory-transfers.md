# MINIX Bootstrap Memory Transfer Study

## Fixed inputs

```text
micros source: 256272cd747b439efc73941feab79ae4e6e0a8a1
micros tree:   0afa711ec607855bdd82d50f08d89dcb7881a227
MINIX source:  4db99f4012570a577414fe2a43697b2f239b699e
```

Both repositories were checked clean at these revisions. Reference research
uses the local `/Users/alephling/Repo/minix` checkout read-only.
MINIX/NetBSD supplies behavior and mechanism evidence, not copied source.
BOOT02 #268 is merged; BOOT03 owns B06-F004 and requires COPY01 #191.

## Fixed reference mechanism

| Local MINIX reference | Evidence |
| --- | --- |
| `minix/kernel/main.c:127-145,274-275` | Boot information, diagnostic history and the boot-process table use standard bulk copies; these calls do not introduce another owner or memory-transfer algorithm. |
| `main.c:236-237,435-439` | Initialization clears complete masks and user-mapped structures before populating fields. |
| `minix/servers/rs/manager.c:1500-1538` | Exact service/privilege policy precedes copying kernel-call and VM-call masks; the copy primitive does not authorize the policy. |
| `minix/servers/rs/exec.c:60-87` | A local executable descriptor is cleared before assigning process, image, stack and VM-backed loading callbacks. |
| `common/lib/libc/string/bcopy.c:61-158` | Generic word copying selects alignment-compatible complete words, bounded byte edges and the differing-residue byte path. Its broader overlap/backward domain is not imported. |
| `common/lib/libc/string/memset.c:59-164` | Generic fill retains the three-word short threshold, one replicated word, alignment prefix, word loop and tail. |

[COPY01's fixed study](minix-runtime-memory-primitives.md) owns the deeper
generic/ARM/compiler comparison. Accepted ADR-0090 already specializes
the generic family to eight-byte LP64 copy and four-byte fill with exact
C object-representation accesses. BOOT03 changes callers to that existing
provider, not the reference selection.

Reference transfers use scalar cursors/counts and fixed auxiliary storage.
Their best/average/worst and amortized work is linear in byte count, with
fewer primitive units on the word path and no retained state across calls.
Boot/service policy, VM allocation and publication remain separate.

## Current helper and call census

The fixed current source has seven byte-loop definitions:
`bootstrap_manifest_common.c` owns copy/clear; `bootstrap_control_core.c`
owns private copy/clear; `bootstrap_runtime_state.c` owns copy/clear;
`bootstrap_image.c` owns private clear. These are three copy helpers and
four zero-fill helpers, not seven independent memory algorithms to retain.

Their 28 callers form 38 distinct caller/callee rows and **46 direct calls**:
19 copy and 27 clear. A unique-callee list must not be mistaken for call-site
multiplicity.

| Caller owner under `kernel/boot/` | Calls | Existing responsibility |
| --- | ---: | --- |
| `bootstrap_control_core.c` | 13 | State preparation, controller publication, ready/complete preparation and plan invalidation |
| `bootstrap_image.c` | 1 | Clear the complete image-info candidate |
| `bootstrap_manifest.c` | 3 | Clear validation candidate/diagnostic and publish the derived plan |
| `bootstrap_transitions.c` | 8 | Initialize runtime and prepare/commit selected transition plans |
| `bootstrap_runtime.c` | 3 | Diagnostic and controller-preflight initialization, publication-failure state clear |
| `bootstrap_preparation.c` | 3 | User context, VM snapshot candidate and failed control-state initialization |
| `bootstrap_preparation_image.c` | 10 | Image/stack/manifest pages, translated write/readback, service table and configuration |
| `bootstrap_preparation_rollback.c` | 5 | Snapshot reset plus exact record/binding/order/snapshot cleanup |

There are no external kernel callers of the exported manifest/runtime byte
helpers. The private control/image helpers remain local.
No change to the call count or copied/cleared extent is required.

## Actual size and alignment classes

The current headers and existing RV64 bootstrap-launcher ELF's DWARF types
agree on the following relevant sizes. Inspecting that existing artifact
did not create an executable design prototype.

| Transferred object or extent | Bytes / shape |
| --- | --- |
| Bootstrap diagnostic | 24 |
| Runtime entry / selected plan | 48 / 64 |
| Manifest plan / validation candidate | 68 / 152 |
| Manifest | 1,408 |
| Control state / runtime state | 2,416 / 448 |
| Ready plan / complete plan | 136 / 200 |
| Image-info candidate | Seven 56-byte entries: 392 |
| Binding prefix / complete binding array | `56 * count` / 392 |
| Preparation-record array / order array | 392 / 14 |
| VM snapshot-result descriptor | 64 |
| User context | 264 |
| Service table / configuration | 56 / 128 |
| Image, stack and manifest-view page | 4,096, with the original bounded file prefix |
| VM boot-information payload | 364,704 total, moved through existing translated chunks, never assumed physically contiguous |

The 3,816-byte target context is a persistent storage cap, not a new
whole-context copy. Ordinary scalar/structure assignments are outside
the seven-helper replacement.

Page destinations are page-aligned. Typed candidates retain their natural
alignment. `tools/generate_bootstrap_fixture.py:187-194` emits ordinary
`static const unsigned char` segment arrays without an eight-byte
alignment promise. Manifest-plan pointers require their actual type's
alignment, not an invented wider public requirement.

Thus a valid caller may take the provider's byte fallback. Tests must use
known input residues and the actual requested extent, not assume every
image/configuration copy is a whole number of aligned words.

## Authority, alias and failure trace

`bootstrap_preparation_image.c` retains one translation/allocation owner
per page. It clears an allocated page before copying its file-backed
prefix, independently checks every file byte/zero tail, and separately
verifies stack and manifest contents.

Embedded bytes are immutable kernel storage excluded from the allocator's
user frames. Manifest/configuration and VM snapshot sources are distinct
from their newly prepared user storage. Readback uses a separate automatic
object. Control/transition publication copies an automatic candidate to
its final owner. All inventoried nonzero copy pairs are disjoint.

`write_user_range` and private `read_user_range` check the required access
on each real translated chunk. The VM snapshot call in
`bootstrap_preparation.c:523-540` preserves the full payload and independent
readback. Configuration writing follows the same mechanism.
If a later chunk fails, earlier copied bytes may already exist; the
current caller's preparation rollback, not a new memcpy error/transaction,
owns cleanup.

`bootstrap_preparation_rollback.c` releases resources in the existing
reverse order before clearing its exact record, binding, order and
snapshot storage. Controller publication failure retains its existing
control clear. None of those operations is permission to batch mappings,
skip generations or discard full-capacity zeroing.

Byte comparisons, fixed-name/zero predicates and image/stack verification
are deliberately retained. They still contribute `O(z)` work, so reducing
movement units is not a claim that complete boot time improves by the same
factor.

## Current evidence and consumer implications

The indexed-bootstrap pair currently compiles and observes four of the
seven helpers: manifest copy/clear and control copy/clear.
Its runtime-state subset contains only the real context getter; the
runtime copy/clear and image-catalog clear need additional real-definition
coverage. Private helpers should be reached through their existing owning
APIs, not exported or replaced with a successful test implementation.

The kernel already links `lib/runtime/memory.c` through the corrected
grant path. The native manifest/model, control/catalog, TTY and BOOT
products need their directly dependent link/source closure kept coherent.
Other current subsets that only use selected bindings do not acquire a
new memory consumer merely because they share a source file.

COPY01's current `wide_data_transfers` and linked-object proof engines
already distinguish real non-stack memory transfers from incidental stack
spills. The existing grant kernel consumer demonstrates a finite
caller/provider artifact binding; it is not a new runtime prerequisite
or permission to borrow that guest as bootstrap evidence.

The bootstrap-launcher kernel has BOOT02's command collector by explicit
test scope. Provider-only ordinary-code checks must distinguish that from
enabling RUNTIME counters in the provider. Existing BOOT02 records and
the strict terminal stack/user authentication ordering remain independent
acceptance obligations.

## Difference classification

| Dimension | Classification |
| --- | --- |
| Exact bytes, complete zeroing and standard bulk movement | Baseline parity |
| Existing generic word family and differing-residue fallback | Baseline parity through COPY01 |
| RISC-V C17 inline-copy/aliasing adaptation | Existing required adaptation under ADR-0090 |
| Generation/permission checks and held user-service preparation | Existing required isolation/bootstrap adaptation |
| Independent readback, canaries and exact access footprint | Compatible safety extension |
| B06 source-level byte-only movement | B06-F004 divergence requiring this separate implementation |

This study is design evidence only. No replacement code, model, fixture,
observation harness, build scaffold or executable prototype was authored.

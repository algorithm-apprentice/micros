# Bootstrap Memory Transfers

## Status and scope

This is the independently reviewed BOOT03 contract for B06-F004 under
[ADR-0134](../adr/0134-shared-bootstrap-memory-transfers.md).
COPY01 #191 supplies the actual dependency. BOOT02 #268 is the completed
preferred predecessor; no new DAG edge is introduced.
Design review is clean and the contract is Accepted.
Implementation #270 follows separately merged design #269 and closes
B06-F004. The implementation PR owns the exact native, actual target,
full stack, independent review and publication evidence.

The [fixed study](../research/minix-bootstrap-memory-transfers.md) owns
the seven-helper, 28-caller, 38-caller/callee-row and 46-call census,
reference provenance and current size/alignment evidence.
This outcome changes the movement primitive, not bootstrap policy or
the amount of data an operation must initialize.

## Existing owners and interfaces

Preserve these seven internal interfaces at their existing definitions:

| Owner | Helper | Provider |
| --- | --- | --- |
| `bootstrap_manifest_common.c` | `bootstrap_manifest_copy_bytes` | `micros_runtime_memcpy` |
| `bootstrap_manifest_common.c` | `bootstrap_manifest_clear_bytes` | `micros_runtime_memset(..., 0, ...)` |
| `bootstrap_control_core.c` | private `copy_bytes` | `micros_runtime_memcpy` |
| `bootstrap_control_core.c` | private `clear_bytes` | `micros_runtime_memset(..., 0, ...)` |
| `bootstrap_runtime_state.c` | `bootstrap_runtime_copy_bytes` | `micros_runtime_memcpy` |
| `bootstrap_runtime_state.c` | `bootstrap_runtime_clear_bytes` | `micros_runtime_memset(..., 0, ...)` |
| `bootstrap_image.c` | private `clear_bytes` | `micros_runtime_memset(..., 0, ...)` |

All four files remain under `kernel/boot/`. Each helper delegates once
with its unchanged arguments and discards the provider's destination
return. No function is exported only to make a test convenient.
The private runtime/context owner retains its original state and accessors.

Include the existing `lib/runtime/memory.h` declaration where consumed.
Keep a single `lib/runtime/memory.c` object in each affected executable;
the production kernel already has it. Direct native consumers include
the manifest/model, control/image-catalog, TTY and indexed-bootstrap
products. Current extracted subsets that do not compile movement helpers
need no invented runtime implementation or success stub.

Do not link `memory_symbols.c` into the kernel/native consumers or replace
host libc. User/service compiler forwarders and COPY01's implementation
remain unchanged. Private dependency direction is bootstrap-to-runtime,
with no reverse dependency or new state owner.

## Memory and authority contract

The [existing provider](runtime-memory-primitives.md) owns the exact copy/
fill algorithms, access footprint, C object-representation adaptation
and independent unit formulas. Reuse that contract without another copy.

Each nonzero source/destination copy is disjoint and accessible for its
entire requested extent. The fixed caller census establishes that:
automatic candidates and plans are separate from their destinations;
immutable embedded bytes are outside allocated user frames; kernel
manifest/configuration/snapshot sources are separate from prepared user
storage; readback uses an independent automatic destination.

The provider adds no authority, range admission, permission check,
allocation or recoverable failure. It consumes only already admitted RAM.
Keep zero-length no-access behavior, including null private-helper
arguments. Do not add a self-copy/overlap contract or new alias detector.

Retain natural object alignment and the current arbitrary alignment of
embedded file bytes. Matching copy residues use the accepted prefix/
eight-byte-word/tail path; differing residues retain the byte fallback.
Fill retains four-byte words and the existing short-fill threshold.
Do not change a public layout or force source-array alignment merely to
make a work assertion pass.

## Caller lifetimes and ordering

| Path | Unchanged behavior |
| --- | --- |
| Image catalog | Clear the complete seven-entry local candidate; validate all requested images before the existing output assignments. |
| Manifest/control construction | Preserve argument/alignment/state checks, local candidate initialization, exact copied extents and success-only output publication. |
| Portable transitions | Preserve BOOT01's selected after-plan and the original prepare/commit positions; do not restore a complete runtime snapshot. |
| Ready/complete control | Preserve BOOT02 relation admission, reply rollback, final membership/revocation barriers, plan invalidation and one-shot notifications. |
| Image pages | Allocate one admitted page, zero all 4,096 bytes, install only its file-backed prefix, then retain independent verification of file bytes and zero tail. |
| Stack/manifest-view pages | Keep complete-page zeroing, exact manifest copy and current independent readback. |
| Service configuration | Keep the zeroed 128-byte configuration, capacity-seven service table, exact write/readback and comparisons. |
| VM boot information | Keep the 364,704-byte payload, existing page-local translation chunks and complete independent readback; snapshot allocation/lifetime stays with VM. |
| Reset/rollback/publication failure | Retain exact field/capacity zeroing, reverse cleanup, resource counts, consumed generations and fatal boundaries. |

`bootstrap_preparation_write_user_range` and the private readback helper
still translate and check the required permission before every chunk,
reject zero contiguous length, and move `min(remaining, contiguous)` bytes.
Do not cache mappings, combine pages, borrow a grant span or skip an
existing translation.

These helpers are not whole-range failure-atomic transactions. A later
translation/permission failure returns false after any earlier completed
prefix; preparation's existing rollback owns cleanup. Keep that ordering,
including which diagnostic and resource state is visible on failure.
Local candidate/plan APIs retain their own success-only output guarantees.

Byte-equality, zero predicates, image/stack verification, fixed-name checks,
topology, scalar writes and ordinary structure assignments remain unchanged.
Do not count comparisons as transfers or remove them to claim a faster
whole bootstrap.

## Work and storage

Transferred bytes remain exactly the existing requested bytes. Word paths
reduce executed transfer units, not the semantic byte count.
For nonzero aligned calls, copy uses eight-byte units plus byte tails;
fill at or above twelve bytes uses four-byte units plus byte tails.
Other residues follow COPY01's existing formulas.

The current selected-plan calls supply useful independent aligned checks:

| Existing operation | Copied bytes / copy units | Cleared bytes / clear units |
| --- | --- | --- |
| Pure release/ready/complete or control release | 160 / 20 | 64 / 16 |
| Control ready prepare | 248 / 31 | 200 / 50 |
| Control ready commit | 48 / 6 | 136 / 34 |
| Control complete prepare | 312 / 39 | 264 / 66 |
| Control complete commit | 48 / 6 | 200 / 50 |

These rows describe successful existing movement in aligned native
fixtures, not every command's total work or failure path. Retain other
owners' necessary work. The constructor/image/runtime cases use their
actual input extents and residues, including the 68-byte manifest plan,
short 14-byte order array, page tails and capacity-sized candidates.

Best/average/worst and amortized transfer work remain linear in requested
bytes; there is no cross-call amortization. Retained comparison work also
remains linear. No elapsed-time threshold or whole-boot speedup substitutes
for primitive evidence.

The provider uses fixed scalar auxiliary state. Add no BSS, cache, VLA,
recursion, capacity-sized automatic object or retained pointer.
Runtime/control/target-context caps stay 448/2,416/3,816 bytes;
selected/ready/complete plan caps stay 64/136/200 bytes.
Actual linked call chains must fit every existing stack and image budget.

## Native acceptance

Extend the existing ordinary/observed indexed-bootstrap pair and collector,
not the workflow count. Execute real production definitions:

- preserve all 48 current cases, model seeds and 4,096 transitions;
- retain every diagnostic, malformed selected relation, deadline,
  permutation, reply/seal and output-preservation assertion;
- exercise the image clear through its real catalog API, control helpers
  through real owning operations, and runtime helpers through their actual
  definitions, preserving the original context storage;
- cover the fixed size classes and valid unaligned caller inputs,
  zero-length no access, canaries and exact bytes/zero tails; and
- preserve ordinary/observed agreement and collector absence in the
  ordinary native product.

Establish genuine unchanged-production Red with byte/state agreement
before accepting its excess copy/fill units. At least the successful
aligned selected-plan movement and each newly covered helper family must
discriminate the byte loop from the required provider.
Missing APIs, compile failures, source-qualification failures or a smaller
fixture are not algorithmic Red.

Count at the executing primitive owner. Old B06 byte stores count as B06;
the unchanged provider's existing annotations count as RUNTIME after
delegation. Compose those owners for transfer-unit/byte assertions and
do not count a wrapper again. In particular, migrate the existing BOOT-only
byte checks so moving work to RUNTIME cannot satisfy them with zero.
Qualify all actual compiled bodies, subsets, headers, sources and sites.

The independent oracle uses fixture input geometry and the Accepted
provider formulas. Candidate counters, helper traces or generated output
cannot define expected sizes, number of operations or work limits.
Retain COPY01's existing complete residue/access matrix; caller-focused
cases need not repeat that entire Cartesian matrix for every large object.

## Actual target and complete acceptance

The existing bootstrap-launcher workflow must bind its actual compiled and
linked seven helper-to-provider edges and both provider bodies using the
existing source/object/relocation/linked-transfer proof engines.
Prove one actual provider, the expected reachable calls, non-stack wide
copy and fill transfers, and no new standard-memory forwarder or recursive
memory call. Close source, compiler, object, ELF/map and recipe identities
through the check. No stand-in object or second guest supplies acceptance.

That kernel already contains BOOT02's explicitly scoped command collector.
Do not mistake it for an ordinary-provider observer or claim the whole
test kernel is collector-free. The provider remains uninstrumented there;
do not globally enable its native counters or change BOOT02's target owner
mask. Preserve all existing five/ten command captures, zero complete-audit
requirements and terminal stack/report composition.

Keep every image-content/zero-tail/configuration check and all current
native, actual QEMU, failure, VM/TTY/PM, composed spawn/rollback/resource,
standalone-image and stack requirements. The complete gate still owns
44 static configurations, 113 ELFs and ten observed scopes; B13 is blocked.
One sufficient final author plan and fresh independent implementation
review precede the first permanent commit and exact-tree publication.

Current qualification/recipe maintenance belongs in this task under
ADR-0123. No historical catalog, metadata-only prerequisite or duplicate
recertification execution is introduced.

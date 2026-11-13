# MINIX Checked-Copy Span and Movement Study

## Fixed inputs

```text
micros source: abafb49ea5f7b126363a0d0cf3de15f1feeeb55d
micros tree:   e031b94ac1321c52d188872dcd0394250c3bcb31
MINIX source:  4db99f4012570a577414fe2a43697b2f239b699e
```

Both repositories were checked clean at these revisions. The MINIX source
was read from the existing local `/Users/alephling/Repo/minix` checkout.
It is reference evidence, not a source donor.

GRANT03 owns B05-F005 after merged GRANT02 #262. Its actual GRANT01,
COPY01 and MEM04 prerequisites are already merged. The
[canonical contract](../architecture/grant-copy-spans.md) owns the
Accepted correction; this study is not executable Red/Green evidence.

## Fixed MINIX trace

| Owner | Observed behavior and mechanism |
| --- | --- |
| `minix/kernel/system/do_safecopy.c:42-267`, `verify_grant` | Selects exact endpoint/table/index/sequence, direction and grant-relative range before returning the resolved grantor address. Indirection, magic grants and `CPF_TRY` are separate branches. |
| `do_safecopy.c:272-375`, `safecopy` | Chooses source/destination from READ/WRITE, verifies authority, then delegates the resolved ranges to ordinary or VM-checking virtual copy. |
| `do_safecopy.c:380-404` | The scalar to/from entries use the actual caller endpoint and fixed access direction. Vectored copying is a distinct later routine. |
| `minix/kernel/arch/earm/memory.c:69-133`, `createpde` | Selects an accessible current mapping or one of two kernel mapping windows and clips the transfer to the available one-megabyte window. |
| `memory.c:154-239`, `lin_lin_copy` | Obtains both current spans, takes the common bounded length, checks end arithmetic, calls `PHYS_COPY_CATCH`, then advances both addresses and remaining bytes. Faults can terminate after earlier movement. |
| `memory.c:497-568`, `virtual_copy_f` | Resolves both endpoints and handles resumed VM work; a source/destination fault may suspend the caller through VM. Zero bytes returns `EDOM` here. |
| `minix/kernel/arch/earm/phys_copy.S:78-198` | ARM copy saves a fixed four-register MINIX frame, aligns operands and moves 32/16/12/4-byte groups with byte tails. |
| `phys_copy.S:200-250` | The differing-residue path rounds the source down and reconstructs words with shifts. Its wider read footprint is explicitly recognized by `lin_lin_copy` fault classification. |
| `common/lib/libc/string/bcopy.c:63-174` | The generic wide family uses `long` words when residues match, a byte prefix/suffix and a byte fallback when both operands cannot be aligned together. |

For `n` bytes across `s` mapping spans, the reference has `O(n+s)`
movement/control work and constant auxiliary storage. Word/block grouping
changes actual primitive counts, not the linear asymptotic class.
There is no cross-copy amortized state. The ARM entry has a fixed
16-byte register-save frame rather than length-sized scratch.

## Current micros trace

| Owner | Current responsibility and remaining defect |
| --- | --- |
| `kernel/grant/grant_copy.c:55-108,110-223` | Selected authority, phase and zero-length gates; both range plans before invariant/phase/fault classification; direction, physical validation and one SIE-clear commit. |
| `kernel/grant/grant_copy.c:233-350` | Operation 15 has separate remote-only planning, no byte copy and no retained lease. |
| `kernel/grant/grant_copy_range.c:26-150` | At most two translated leaves per participant, exact permissions, device-span exclusion and complete output-plan publication. |
| `kernel/grant/grant_copy_core.c:6-95` | Validates chunk counts, positive lengths, total/physical overflow and every source/destination interval pair. |
| `grant_copy_core.c:97-148` | Already retains and zips physical spans; only its inner byte assignment is the remaining movement divergence. It never retranslates during commit. |
| `lib/runtime/memory.c:13-67` | Existing COPY01 eight-byte, alignment-compatible, alias-safe prefixed copy with exact-range access and byte fallback; no authority or persistent state. |
| `CMakeLists.txt`, endpoint/model/kernel and GRANT01 targets | Native grant core and kernel do not yet link that provider. Its existing user-runtime consumers remain independent. |
| `tests/host/selected_grant_backend.c:49-75` | Existing native backend has real fixture storage and aborts on more than two translations per participant. |
| `kernel/tests/grant/grant_test.c:553-665`, syscall copy commands | Existing actual RV64 cases include page-local and unequal-boundary cross-page copies in both directions, with exact bytes and canaries. |

The current plans are already the right structure. With at most two
chunks on each side, their cursor zipper has at most three nonempty
spans. MEM04 supplies selected translation; GRANT01 supplies selected
authority. Neither needs another cache or planning rewrite.

The shared provider's exact arithmetic, C aliasing adaptation and complete
residue/access matrix are already fixed by
[COPY01](../architecture/runtime-memory-primitives.md). Reusing that
generic reference family is the RV64 adaptation; translating ARM assembly
or importing its adjacent-word reads would create a different obligation.

## Difference classification

| Dimension | Classification | Mapping |
| --- | --- | --- |
| Verify participant/direction/range before movement | Baseline parity | Keep current selected grant admission. |
| Pair bounded source/destination spans and advance cursors | Baseline parity | Preserve the existing two-plan zipper; no additional plan. |
| Whole-operation preflight instead of faulting incremental movement | Compatible extension | Preserve all-or-error bytes and complete physical nonoverlap. |
| Exact resident process-owned/wired physical frames | Required adaptation | Keep RV64 direct-map access, no SUM or cross-process pointer authority. |
| ARM block instructions to existing generic LP64 word family | Required adaptation | Reuse COPY01, including its differing-residue byte fallback; no new assembly or primitive. |
| Exact access footprint rather than aligned-down reads | Compatible extension | Every load/store stays within its current validated span. |
| Zero-length success after authority and phase | Compatible extension | Preserve existing micros behavior, not MINIX's `EDOM`. |
| VM fault suspension and broader grant forms | Staged substitution | Existing SOM-04/SOM-25 obligations remain; no new retry or delegation. |
| Unconditional byte commit | Divergence requiring correction | Replace only the inner transfer with the shared prefixed provider. |

## Evidence and integration implications

The current outer grant `COPY_UNIT` observation counts spans, while its
inner loop counts copied bytes. It is not yet a byte-versus-word oracle.
The Accepted contract separates span visits from actual transfer units before
establishing unchanged-production Red.

The provider is already annotated. Its actual COPY01 primitive admission
must remain authoritative: compile it unchanged, qualify its complete
current functions and include its actual bytes/headers/products in the
grant pair without inserting duplicate observations. The only required
non-kernel native-input exception is this named provider and private header.

Existing COPY01 linked-image proof targets a user-runtime ELF, not the
kernel. GRANT03 therefore needs the bounded kernel post-guest artifact
check specified by the canonical contract, using existing byte-link and
wide-transfer proof primitives. Native discovery must not depend on a
target image that has not yet been built.

No executable prototype, new fixture, harness, model, observation record
or claimed measurement was authored in this design phase. The later
implementation must earn genuine old-byte Red, independent span/work
Green, real target/composed evidence and unchanged full stack bounds.

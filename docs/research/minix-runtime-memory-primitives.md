# MINIX Runtime Copy and Fill Baseline

## Scope and fixed references

This study supports `COPY01`, the B05-F006 correction after merged EV04
#189. It does not include privileged grant transfers (`GRANT03`), bootstrap
copy-site replacement (`BOOT03`), complete-validator cadence or the blocked
hosted CI outcome.

The local MINIX reference is commit
`4db99f4012570a577414fe2a43697b2f239b699e` in
`/Users/alephling/Repo/minix`. The current `micros` baseline is
`a5b121cdc805b53a201d9859db0af9958f3efeaa`.
The following observations came from those actual sources, not a copied
third-party implementation or a newly authored executable prototype.

| Fixed MINIX owner | Observed responsibility |
| --- | --- |
| `common/lib/libc/string/memcpy.c` | Selects the `MEMCOPY` specialization of `bcopy.c` |
| `common/lib/libc/string/bcopy.c` | Generic alignment, word transfers, byte remainder and original-destination return |
| `common/lib/libc/string/memset.c` | Generic short fill, replicated `u_int` pattern, destination alignment, word loop and byte remainder |
| `common/lib/libc/arch/arm/string/memcpy.S` | Selects the ordinary ARM or XScale implementation |
| `common/lib/libc/arch/arm/string/memcpy_arm.S` | Architecture-specific aligned blocks and shifted unaligned-source reconstruction |
| `common/lib/libc/arch/arm/string/memset.S` | Architecture-specific alignment, repeated words/blocks and byte remainder |
| `common/lib/libc/arch/riscv/string/` | Contains `ffs.c`, not a specialized copy/fill implementation at this revision |

## Generic copy mechanism

The generic wide path uses a `long`-sized word. Its state is two cursors, a
remaining length and scalar alignment/count values. It handles zero length
without copying and returns the original destination.

It uses words only when source and destination have matching low alignment
bits and the range is long enough. A byte prefix reaches a shared word
boundary, the middle moves whole words, and a byte suffix finishes the range.
If the residues differ, advancing both cursors cannot align both; this generic
reference path copies the range with bytes. That fallback is part of the
selected reference algorithm, not an omitted fast path.

The shared `bcopy` body also chooses backward traversal and handles overlap.
COPY01 retains only the existing `restrict`, nonoverlapping `memcpy` domain.
Forward traversal preserves the current `micros` mutation direction and has
the same complete aligned intervals and work count for that domain. No
`memmove` or overlap guarantee is added.

The `__OPTIMIZE_SIZE__` specialization is separately byte-oriented. COPY01
selects the generic wide algorithm required by B05-F006, not that size-only
alternative. Existing application `-Os` flags remain unchanged; they do not
silently select a different source algorithm.

## Generic fill mechanism

The generic wide fill uses `sizeof(u_int)` and initially chooses bytes when
the length is less than three words. Otherwise it converts the fill value to
one unsigned byte, replicates that byte into one scalar word, aligns the
destination with a byte prefix, fills complete words and finishes with bytes.
The replicated pattern is prepared once, not rebuilt at every iteration.

On the selected LP64/8-bit-byte target, the copy word is eight bytes and the
generic fill word remains four bytes. Widening fill to eight bytes or adding
an architecture-specific multiword unroll is not required for this first
baseline outcome.

## Architecture-specific alternative

The ARM copy implementation moves 32-, 16-, 12- and four-byte groups on its
aligned path. Its unaligned-source path rounds the source down, loads aligned
words and merges shifted pieces. The ARM fill has larger repeated-store
groups as well as a scalar-word tail.

Those routines establish that byte-only iteration is not the only MINIX
algorithm, but COPY01 does not translate their instruction spelling or import
their wider read footprint. The generic alignment-compatible word path
already supplies the RISC-V fallback family. Every `micros` access must stay
inside the caller's requested range; no rounded-down load or speculative
trailing word may borrow adjacent object or mapping storage.

## Current micros owners and deficit

`lib/runtime/memory.c` defines `micros_runtime_memcpy` and
`micros_runtime_memset`. Both use a volatile byte assignment for every byte,
regardless of alignment or length. `lib/runtime/memory.h` supplies their
private declarations; `memory_symbols.c` preserves the compiler-visible
`memcpy`/`memset` forwarding symbols.

The current native `runtime_test` links the prefixed bodies without replacing
host libc. Its `test_memory_support` checks lengths zero, one and 257,
unaligned addresses, fill conversion, return values and surrounding bytes.
It does not distinguish an aligned word path from the audited byte loop.

The bodies are linked into the standalone runtime probe, bootstrap users and
dynamic applications. They are not the kernel's checked-grant commit loop.
The correction therefore does not remove the repeated complete ownership
and address-space validation sampled during the failed CI investigation.

The exact historical identity that must transition is:

```text
tests/host/test_trap_syscall_source_layout.py:data-entry:STABLE_HASHES/lib/runtime/memory.c
```

Its independently retained expected value is
`780f152e6a26cfcc8d01ee3760662d5bfcd941154051396f8d696c9039171323`,
from reviewed source stage `09271b80de386870b7ecadcf0368576e46da56ae`.
The current inventory classifies it as `TRANSPORT_IDENTITY`, primary B05,
with B05/B14 consumers. The header, forwarding symbols, other 22 stable
artifacts and the structural family's unrelated identities are not retired.

## C object representation and compiler boundary

A cast followed by a wide typed load/store is not a portable implementation
of copying arbitrary C object representations. Alignment alone does not
resolve effective-type and aliasing rules. Reusing the public memory symbol
for an internal word transfer can also recurse after compiler lowering.

The supported Clang implementation provides a narrower primitive:
[`__builtin_memcpy_inline`](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.4/clang/docs/LanguageExtensions.md).
Its documented semantics match `__builtin_memcpy`, its size is a compile-time
constant, and it guarantees no external call. The fixed
[Clang lowering](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.4/clang/lib/CodeGen/CGBuiltin.cpp)
obtains aligned addresses and emits `CreateMemCpyInline`.
`__builtin_assume_aligned` returns the supplied pointer with an alignment
assumption; it does not align or validate an address.

The Accepted adaptation uses only constant eight-byte copy and four-byte
pattern transfers after explicit alignment/range checks. It does not delegate
the variable-size algorithm to a library, promise an unaligned wide access,
disable alias analysis globally or change optimization flags. Byte paths
retain volatile character accesses to prevent loop-to-library recursion.
Actual IR/object/linked evidence must confirm the admitted lowering.

## Complexity, storage and hot paths

Let `n` be the requested byte count and `q` the number of complete aligned
words selected by the reference path. A word transfers exactly `w` bytes:

```text
byte assignments = n - w*q
transfer units   = q + n - w*q
transferred bytes = n
```

Copy uses `w=8`, with `q=0` when the two residues differ or no complete aligned
word is available. Fill uses `w=4`, with `q=0` below the reference 12-byte
threshold. The complete contract fixes the prefix/word/tail boundaries.

Best, average and worst work remain `O(n)`; there is no distinct amortized
state. The meaningful difference is the exact byte/word primitive count,
not an elapsed-time threshold. Auxiliary and automatic storage remain
`O(1)`: scalar cursors, counters and one fill pattern; no array, VLA, heap,
global workspace or retained caller pointer is introduced.

Hot consumers include compiler-emitted aggregate copies/initialization and
explicit service/application memory operations. Their call chains retain
the existing 4 KiB/16 KiB stack and image limits. Runtime performance or
hosted CI success is not claimed before the implementation's actual gates.

## Difference classification

| Dimension | Accepted mapping | Classification |
| --- | --- | --- |
| Byte result, fill conversion and destination return | Preserve the current C ABI and reference observable result | Baseline parity |
| Alignment-compatible words and byte remainder | Reproduce the generic wide copy/fill family | Baseline parity |
| Differing copy residues | Preserve the generic byte fallback | Baseline parity |
| Forward nonoverlap specialization | Preserve the existing `restrict` domain and direction without importing `bcopy`/`memmove` breadth | Required adaptation |
| Word sizes | Eight-byte LP64 copy and four-byte generic fill | Required adaptation |
| Wide access to arbitrary object representations | Closed Clang inline-copy primitives after verified alignment and bounds | Required adaptation |
| Access footprint | No read or write outside the requested range | Compatible extension |
| Primitive measurements and retirement | EV02 native observations plus one exact EV01 transition | Compatible extension |
| Volatile byte iteration for aligned bulk work | Replaced only by the separately reviewed implementation | Divergence requiring correction |

MINIX/NetBSD source remains reference evidence, not a source donor. Optional
multiword unrolling, vector instructions and other algorithm variants remain
later measured work.

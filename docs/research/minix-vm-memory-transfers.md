# MINIX VM Memory-Transfer Study

## Fixed inputs and scope

```text
micros source: d6bc939a4f3a4a5ab0a9a6df6722dbe6b244d645
micros tree:   d44b2855aad48478f21607c6a738465de19ae976
MINIX source: 4db99f4012570a577414fe2a43697b2f239b699e
```

VM04 #278 is the preceding merged outcome. VM05's actual predecessors
are COPY01 #191, GRANT03 #264, BOOT03 #270 and VM03 #276.
The local fixed reference is evidence, not source to copy.

VM05 owns B07-F005, not allocation or snapshot lifetime. The
[Accepted contract](../architecture/vm-memory-transfers.md) contains
the exact fifteen-helper inventory and preserved effect boundaries.

## Fixed reference and existing adaptation

| Fixed MINIX source | Relevant mechanism |
| --- | --- |
| `common/lib/libc/string/bcopy.c:60-164` | Generic word copy, residue-compatible alignment, byte prefix/remainder and zero-length handling. The broader overlap/backward and size-only alternatives are not the chosen `micros` contract. |
| `common/lib/libc/string/memset.c:60-170` | Three-word short threshold, replicated scalar pattern, alignment, complete words and remainder. |
| `minix/lib/libexec/exec_general.c:21-79` | VM/loader callbacks keep allocation/process authority separate from selected copy/clear spans and delegate local movement. |
| `minix/lib/libexec/exec_elf.c:175-318` | Selected segment extents drive map/allocate/copy/zero and separate stack construction, rather than an independent byte engine per state owner. |

Accepted COPY01 independently implements the generic family in
`lib/runtime/memory.c`: eight-byte aligned-compatible copies, four-byte
fills, exact range access and a genuine differing-residue byte fallback.
It uses the already reviewed constant Clang inline-copy/alignment
builtins, not typed alias punning, rounded-down overreads, imported
assembly or host libc.

GRANT03 already zips retained physical spans before calling the same
provider. BOOT03 already delegates its seven bootstrap helper boundaries.
VM05 needs the analogous caller integration, not another algorithm.

## Current movement ledger

Current-source inspection found fifteen copy/clear helpers across nine
owners, with 103 direct production calls. The canonical contract lists
each owner/helper/count. The meaningful classes are:

- VM state, transaction, frame-array and action publication;
- full executable/request snapshot clear/copy and selected scratch fill;
- stack-page clearing and request-string copies;
- service object/reply initialization;
- kernel VM decode/load/freeze/abort/exit candidates and state;
- fixed operation-12 user-object chunk transfers;
- runtime handoff reset;
- boot database construction/failure clear and summary publication; and
- the separate kernel snapshot and its result/failure cleanup.

The VM core and boot validator have eight explicitly unrolled byte
assignments per main iteration; other helpers use ordinary byte loops.
An iteration counter is therefore not yet a correct transfer-unit oracle.
The Red instrumentation must cover all actual byte assignments.

FNV, byte equality/zero predicates, scalar wire encoding, structure
assignments and VM key-array algorithms are not these movement helpers.
The B07 audit explicitly distinguishes them from general storage movement.

## Extent and nonoverlap evidence

Kernel and service preflights build local candidates, then publish into
distinct persistent/output storage. Preparation copies its own independent
local frame arrays into a separate candidate. Clear operations retain
the complete original object or capacity, not only a populated prefix.

`publish_load_input` handles exact staged-buffer identity separately and
performs no self-copy. External input must pass the existing overlap
preflight. Image scratch fill rejects overlap with VM state/boot storage,
checks the selected source intersection, clears the full page and copies
only admitted file bytes.

Stack build checks request/page nonoverlap and retains validation before
zeroing. Its small string copies may have incompatible residues; the
provider's byte fallback is part of parity, not a failed optimization.

Operation-12 fixed-object preparation translates and validates all chunks
before movement. Read/write consumes that exact at-most-two-chunk plan.
User-owned wired frames cannot also be kernel candidate/stack storage
under the retained ownership rules. No extra translation or authority
acquisition belongs in the transfer loop.

Boot/snapshot helpers copy local completed summaries/results and clear
their owned storage. None transfers snapshot ownership or frees it.
That remains VM06.

## Evidence integration

The existing VM native family directly includes several movement owners.
MEM03 also compiles kernel handoff core; MEM04/MEM05 compile handoff core
and runtime. These are the current additional provider admissions; do not
broaden all native families indiscriminately.

Native provider observations need exact channel ownership. MEM03's
aggregate is distinct from its focused frame/geometry views. MEM04/MEM05
require their context component plus aggregate to preserve exact
disjoint composition. The provider already has primitive annotations;
duplicating them through the generic source inserter is invalid.

The real ELF-spawn command collector remains armed across separate
command-active intervals. Enabling unconditional runtime-provider
annotations there would collect unrelated runtime/compiler movement.
Keep that provider uninstrumented in this guest and instead reuse the
actual object/linked edge and wide-transfer proof used by prior transfer
outcomes. Native unit counts plus the same guest's actual linked
algorithm and contents establish the required integration without a
new telemetry framework.

## Complexity and resources

For each unchanged requested span, work remains `O(n)` and storage `O(1)`.
Native expected units follow the existing COPY01 prefix/word/tail formulas,
not a candidate observation. Page and 128-KiB bulk cases discriminate
actual bytes from eight-/four-byte transfers; differing residues and
short lengths retain their specified byte work.

VM04's accepted current artifacts establish 160 resident pages for the
observed VM and a 3,744-byte bound/high-water use against 4,096.
The shared provider is already in both kernel and VM images.
Delegation adds no backing storage, new provider or capacity buffer.
Actual implementation compiler/linked/target evidence still decides fit.

## Adaptation ledger

| Difference | Classification | Reason |
| --- | --- | --- |
| Delegate selected copy/clear extents to a shared word family | Baseline parity | Follow reference separation of loader/state ownership and movement. |
| Existing RV64 generic COPY01 provider and exact footprint | Required adaptation | Preserve the Accepted platform, aliasing and isolation constraints. |
| Existing disjoint local/kernel-user plans | Required adaptation | Retain privilege and pointer authority rather than copying through arbitrary user pointers. |
| Direct wrapper names and unchanged caller order | Compatible extension | Preserve reviewable failure/publication boundaries without changing complexity. |
| Native units plus same-guest linked algorithm/contents | Compatible extension | Measure the current implementation without unsafely widening target observer scope. |
| Retained duplicate snapshot storage | Staged substitution | VM06 owns its explicit lifetime correction; faster clearing does not close it. |

This study authors no executable code, tests, harnesses or prototypes.
Independent design review is clean; the separate design merge still
precedes implementation.

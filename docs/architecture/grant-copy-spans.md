# Retained Grant Copy Spans

## Status and scope

This is the independently reviewed GRANT03 design Accepted by
[ADR-0131](../adr/0131-retained-grant-copy-spans.md), owning B05-F005.
GRANT01 #259, COPY01 #191 and MEM04 #228 are merged; the preferred
serialized predecessor GRANT02 #262 is complete.
Design #263 is separately merged. Its paired implementation proceeds from
that main; the design alone closes no GRANT03 finding.

The [fixed study](../research/minix-grant-copy-spans.md) owns reference
provenance and difference classification. This contract changes only
movement over already validated physical spans and the evidence/build
integration needed to prove that movement.

## Existing preflight remains authoritative

Keep the current interfaces, token packing, exact participant generations,
grant direction, grant-relative bounds and public 4,096-byte limit.
Neither the stateless copy provider nor a physical address grants authority.

The current operation order remains:

1. Reject malformed arguments and nonzero local-range shape before the
   protected operation.
2. Admit the selected grant and participants under ADR-0128, with its
   existing error precedence.
3. Require the authoritative ledger and BOOTSTRAP or HANDED_OFF phase.
   Zero length succeeds only here, without device inspection,
   translation or data-buffer access.
4. Inspect the existing denial-only committed TTY binding.
5. Prepare both complete remote and local plans, requesting the existing
   direction-specific permissions. Classify their results in invariant,
   phase, then fault order; a first-side fault cannot hide second-side
   structural corruption.
6. Select source/destination by direction. Validate both retained plans
   and every source/destination physical interval before the first store.
7. Commit the exact bytes and restore the original IRQ state through the
   existing common exit.

The range planner remains unchanged: at most two translated user pages
per participant; exact process-generation ownership; no UART, VM scratch
or transferable leaf as copy storage. Its device-span advancement and
deferred fault remain, including later structural-error precedence.

The physical-plan validator retains its current positive-length/chunk
count, sum and overflow rules and cross-source/destination nonoverlap.
Do not add intra-source or intra-destination disjointness requirements,
reinterpret unused chunk slots, or change the helper's accepted inputs.
The public length limit remains owned by the public operation.

Operation 15 keeps its distinct remote-only preflight and copies nothing.
A successful validation is not a lease; later copy revalidates current
authority. Captured syscall shape/results/registers, panic reasons,
selected lifetime/index checks and complete diagnostic boundaries remain.

## Commit and provider ownership

`kernel/grant/grant_copy_core.c` continues to own physical-plan validation
and the zipper. `grant_copy.c` owns authority/phase orchestration;
`grant_copy_range.c` owns translation. No owner is split or relocated.

The zipper keeps source/destination chunk indices, offsets and copied
length. Each iteration selects the minimum remaining length of its
current chunks, invokes `micros_runtime_memcpy` exactly once for that
nonzero span, advances both offsets by that length and advances exhausted
chunk indices. It stops after the validated total length.

Keep the provider in `lib/runtime/memory.c` with its existing private
`memory.h` declarations. Its body, ABI and COPY01 algorithm are unchanged.
Only the prefixed implementation is linked into the kernel; do not link
`memory_symbols.c`, raw syscalls, startup or service wrappers.
No standard `memcpy` symbol may silently make an unexpected kernel
compiler libcall succeed.

Each call receives accessible kernel physical storage confined to the
retained source and destination span. Complete cross-plan nonoverlap
establishes its existing `restrict` precondition. Physical address
conversion remains the existing RV64/LP64 direct-map operation, never a
user virtual-pointer dereference.

The [COPY01 contract](runtime-memory-primitives.md#copy-algorithm) owns
alignment, eight-byte transfers, byte prefixes/tails, differing-residue
fallback, alias-safe inline builtins and exact access limits. Reuse it;
do not introduce another copy algorithm or access an adjacent byte to
complete a word. Read and write ranges stay inside each individual span,
including a page-end span shorter than one word.

SIE stays clear across the existing complete preflight and commit.
Commit performs no mapping, grant or endpoint lookup, allocation, error
classification or recoverable failure. It does not publish an intermediate
result. No physical plan, provider pointer or validation state is retained
after return. All failures before commit preserve both buffers/canaries,
outputs, registries, mappings and IRQ state as before.

## Work and storage

Let `n` be the requested bytes, `cs`/`cd` the nonempty source/destination
chunk counts and `s` the number of paired spans. The two retained plans
already exist; no additional span array is required.

```text
0 <= n <= 4096 on the public path
cs <= 2, cd <= 2
n == 0: s == 0, no translation or transfer
n > 0: s <= cs + cd - 1 <= 3
translations <= 4, with no translation during commit
cross-plan overlap comparisons <= cs * cd <= 4
```

For each paired span of length `m`, count complete aligned intervals from
its actual input addresses using COPY01's formula. Equal residues modulo
eight have a bounded byte prefix, whole words and a byte suffix; different
residues use `m` byte units. Sum those independently derived counts over
the span boundaries. Copied bytes equal `n` exactly.

Average and worst transfer work remain `O(n+s)`, with no cross-call
amortization. Aligned spans use `m/8` units rather than `m` byte units.
The existing two 40-byte LP64 range plans and scalar cursors remain;
provider auxiliary storage is constant and contains no array or mutable
global. No heap, VLA, recursion, new registry, third plan or enlarged
stack/image budget is permitted. Actual compiler/machine stack closure,
not a guessed frame size, remains the bound.

## Native observation integration

Extend the existing selected-grant ordinary/observed pair, not a second
collector, workflow or replay of historical implementations.

The old outer `KERNEL_GRANT/COPY_UNIT` observation counts zipper spans,
not byte/word primitives. Reclassify that one owned observation as
`KERNEL_GRANT/RANGE_VISIT`. For the initial unchanged-commit Red, observe
each original byte assignment as one `KERNEL_GRANT/COPY_UNIT` and one
`COPIED_BYTE`. The aligned 64-byte case must therefore report 64 transfer
units, not the required eight.

After replacement, the span observation remains at the executing outer
loop. Only COPY01's existing `RUNTIME` sites count actual transfer units
and bytes. The old byte-loop observations disappear with that loop.
Do not count requested length as executed work or double-charge a caller
estimate and its callee. Assess span work and transfer work separately.
For a pure commit capture, range visits equal `s`, copy units equal the
independent per-span sum and copied bytes equal `n`.

The sole additional non-kernel provider in this native family is
`lib/runtime/memory.c`, with `lib/runtime/memory.h` in its closed headers:

- keep its complete current function/call/loop/storage qualification;
- use the existing `runtime_memory_evidence.validate_green_source`
  admission for its actual COPY01 sites and placement;
- materialize its unchanged body in both channels, without inserting a
  second copy of its already present observations;
- include the `RUNTIME` owner in the existing capture mask and bind the
  real compiled provider in source/product/collector closure;
- retain ordinary observer absence, complete input identities and closing
  checks; and
- admit no arbitrary library path, raw unqualified helper, hidden header
  code, fake memcpy backend or generalized observation exemption.

Update the current GRANT01 qualification and precise source membership
with this outcome under ADR-0123. Other families and the existing COPY01
algorithm/primitive declarations do not change.

## Acceptance evidence

Before changing the production commit, establish actual old-byte work Red
while both native channels agree on independently expected bytes. A
compiler, harness, missing-provider or malformed-record failure is not Red.

Keep all 86 existing grant cases and their purposes, the original eight
deterministic cases and the full 4,096-step grant model, seed
`0x38a110c5`, forced prefix, draw schedule, coverage and trace.
All index/state and failure-byte comparisons remain exact.

Add focused composition cases for:

- zero, one-byte, 7/8/9-byte, aligned 64-byte and full-page transfers;
- matching nonzero residues, differing residues and reversed physical
  address order;
- source-only, destination-only, coincident and unequal two-page
  boundaries, including three paired spans and short page-end pieces;
- exact source immutability, complete destination contents and canaries;
- both public copy directions through real authority and range planning,
  including maximum-length cross-page copies;
- late second-page permission/structural failure and physical overlap
  before any data movement; and
- zero transfer for rejected/zero-length requests and operation 15,
  retaining the existing no-lease, phase and dual-invalid controls.

The existing native backend already owns aligned two-page storage and
rejects more than two translations per participant. Preserve those
checks; do not replace authority or validators with success stubs.
Compute expected span boundaries and word intervals from fixture input,
not candidate counters or a copy of the production zipper.
COPY01's complete residue/access/sanitizer matrix remains in the full
native suite; do not duplicate it merely to increase the case count.

Keep missing/duplicate/misplaced primitive, unknown source/header,
wrong-owner/capture, ordinary observer, source/product mismatch and
hidden byte-work negative controls.

### Actual target and artifact ownership

Add the unchanged prefixed provider to the real kernel, `endpoint_test`,
`ipc_model_test` and both generated grant executables. Keep existing
optimization, ISA, ABI, warning, sanitizer, linker and observer policies.
Do not add a compiler-visible memory forwarder to the kernel.

The existing `test-qemu-grant` command gains one host post-guest check in
`tools/check_selected_grant_validation.py`. Its target mode is closed to
that workflow's `build/riscv64-grant-test/kernel/micros.elf`, map and
actual Ninja compiler graph. Using the existing object/link and
`runtime_memory_evidence.wide_data_transfers` primitives, prove:

- one actual linked `lib/runtime/memory.c` object owns the retained
  `micros_runtime_memcpy`, reached from the grant commit;
- actual compiler operands, object/linked bytes and machine transfers
  agree, with the admitted eight-byte data path and no recursive/external
  memory helper;
- the ordinary image contains no work observer; and
- source, compiler, image, object, map and command identities remain
  unchanged through publication.

This consumes the same accepted guest/build; it launches no second guest.
It requires only the copy symbol, not retention of unused fill code.
Failure is a workflow failure. Native validation must not require this
target artifact before the target workflow builds it.

Existing mechanism/syscall cases already execute aligned page-local and
unequal-boundary cross-page transfers through the real RV64 path. Retain
them, handed-off copies, device exclusions, PM/TTY/VFS and full ELF-spawn
rollback/exit/wait/resource evidence without changing their markers or
deadlines. Keep all ordinary/metadata/observed stack configurations and
the explicit B13 block.

One sufficient current final author plan and fresh independent
implementation review precede the first permanent commit and exact-tree
merge. Current metadata/recipe maintenance is part of this outcome, not
another prerequisite or duplicate recertification/receipt ceremony.

## Phase barrier and non-goals

This design authors no executable artifact. Implementation starts on a
fresh branch only after separate design merge. It must not change the
shared primitive, range/overlap/error semantics, grant indexing, quiescent
resource predicate, VM/TTY policy or public ABI to make its evidence pass.
No new cache, coalescing, arbitrary-size operation, assembly, vector,
retry/recovery, concurrency or production node-Q scope is included.

# ADR-0131: Retained Grant Copy Spans

- Status: Accepted
- Date: 2026-11-13
- Refines: ADR-0005, ADR-0039, ADR-0040, ADR-0041, ADR-0047, ADR-0049,
  ADR-0069, ADR-0080, ADR-0083, ADR-0084, ADR-0090, ADR-0107, ADR-0123,
  ADR-0128, ADR-0129 and ADR-0130
- Supersedes after Acceptance and separate design merge: unconditional
  byte-at-a-time grant commit; the kernel-only native-source admission
  restriction solely for the existing stateless COPY01 provider in GRANT01

## Context

GRANT02 #262 is independently reviewed and merged. The next preferred
outcome, GRANT03, owns B05-F005 and has merged GRANT01, COPY01 and MEM04
prerequisites.

The [fixed local study](../research/minix-grant-copy-spans.md) distinguishes
the remaining defect from work already completed. Copy already retains
both complete translated plans and advances their cursors without
retranslation. Its inner transfer still moves every byte separately.
Introducing another span table, mapping cache or validation phase would
not address a missing contract.

MINIX maps bounded source/destination spans and delegates movement to its
architecture copy primitive. The existing independently implemented
COPY01 provider supplies the reviewed RV64-compatible generic word family,
including its differing-residue byte fallback and exact access footprint.

## Decision

After independent review, Acceptance and separate merge, implement the
[retained-span contract](../architecture/grant-copy-spans.md).

### Change only movement after complete preflight

Keep the existing two-chunk plans, their complete physical nonoverlap
validation and monotonic cursor pairing. For each minimum remaining
source/destination span, call the existing `micros_runtime_memcpy` once
with exactly that span's kernel-accessible physical addresses and length.
Then advance the existing cursors.

Do not change the provider's source algorithm, private declarations or
compiler adaptations. Link its prefixed implementation into the actual
kernel and native grant consumers, not the compiler-visible forwarding
symbols or the rest of the user runtime. The provider owns no grant,
mapping, process, syscall or recovery authority.

The generic alignment-compatible eight-byte family is the required
platform adaptation of MINIX's architecture-specific movement. It does
not import ARM instructions, rounded-down reads or shifted overreads.
Different residues still use bytes; this outcome does not claim that every
unaligned copy becomes a word transfer.

### Retain authority, lifetime and failure boundaries

Preserve selected grant/participant admission, exact direction and bounds,
the one-page public limit, both supported ownership phases and all
device/scratch/transferable exclusions. Prepare both participant plans
before invariant/phase/fault classification. All fallible checks and
every cross-source/destination interval comparison precede movement.

Zero length still follows authority and phase validation but performs no
translation or memory access. Operation 15 remains remote-only, non-copying
and non-leasing. Captured decode, results, registers and fatal boundaries
remain unchanged.

The existing SIE-clear epoch covers preparation through commit. Commit
does not validate, translate, reacquire authority or return an error after
its first store. Plans and physical pointers do not survive the operation.
GRANT02 indexing and ADR-0130's quiescent resource witness are unchanged.

### Measure spans and actual transfers separately

For two nonempty plans, the zipper executes at most the sum of their
chunk counts minus one spans, hence at most three. It allocates no new
plan or workspace. Transfer work is the sum of COPY01's exact per-span
alignment/word/tail counts; average and worst work remain linear in the
requested bytes, with constant auxiliary storage.

Extend the existing ordinary/observed grant pair. Give the outer span
iteration a `KERNEL_GRANT/RANGE_VISIT` observation, distinct from actual
copy primitives. Before changing the old commit body, observe each old
byte assignment as one copy unit/byte and demonstrate real aligned-work
Red. Green counts transfers only at the shared provider's existing
`RUNTIME` sites; it does not estimate them at the caller.

Admit exactly `lib/runtime/memory.c` and its private header into this
pair's closed current inputs. Retain complete function qualification and
the existing COPY01 annotation/primitive validation. Do not double-insert
observations into its already annotated body or grant arbitrary library
sources an exemption. All other families keep their existing admission.

### Use existing evidence, not another workflow

Keep all 86 existing grant cases, the original eight deterministic cases
and the complete 4,096-step model with unchanged seed/draw/trace rules.
Add focused retained-span composition cases rather than duplicating
COPY01's complete residue/access matrix.

The existing `test-qemu-grant` workflow gains one post-guest artifact check
in the existing grant evidence driver: prove the actual kernel's unique
prefixed copy provider, linked object/bytes, actual compiler command and
wide transfers using the existing COPY01 artifact primitives. This is a
host check of that same accepted guest/build, not another QEMU invocation
or new workflow. Keep ordinary observer absence and closing identity checks.

Existing actual grant/syscall/handoff, PM/TTY/VFS, spawn/resource and all
stack configurations remain required. Preserve every budget and blocked
B13 admission. One current final author plan supplies affected-unit
evidence; no separate metadata prerequisite or receipt ceremony is added.

## Phase and non-goals

This phase is documentation only. No executable code, test, model,
harness, build scaffold or prototype is authorized before separate merge.
Fresh independent design review precedes the first design commit; fresh
independent implementation review precedes the first implementation commit.

No new memory API or primitive algorithm, assembly/vector/unroll, span
coalescing, arbitrary-size copy, incremental validation, user-pointer
dereference, SUM change, cache, persistent plan, mapping policy, grant
index change, delegation, VM retry, concurrency, migration, recovery,
larger budget, hosted-CI acceptance or node-Q/POSIX completion.

Design Acceptance alone does not close B05-F005.

## Alternatives

- Keep byte iteration and rely on unspecified optimization: does not prove
  the required movement family or exact work.
- Add another private word-copy implementation: duplicates the already
  reviewed provider and its aliasing/compiler obligations.
- Build a second span plan or retranslate during commit: unnecessary
  storage/work, and risks the existing all-or-error boundary.
- Copy ARM assembly or its wider reads: incompatible with RV64 and the
  exact requested-range access contract.
- Skip composition or machine proof because COPY01 passed: does not prove
  this caller's physical-span integration or its actual linked kernel.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
nine-document proposal, fixed local reference/current source, provider and
evidence integration boundaries and exact four-command author evidence.
It reported no substantive design issue remaining, with High confidence.
Targeted reproduction: None.

Acceptance records that design result, not an implemented movement change
or B05-F005 closure. The separate design merge still precedes implementation.

## References

- [Canonical retained-span contract](../architecture/grant-copy-spans.md)
- [Fixed local study](../research/minix-grant-copy-spans.md)
- [Existing memory primitive](../architecture/runtime-memory-primitives.md)
- [Grant source ownership](../architecture/grant-data-plane-source-organization.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)

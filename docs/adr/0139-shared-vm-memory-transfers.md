# ADR-0139: Shared VM Memory Transfers

- Status: Accepted
- Date: 2026-11-15
- Refines: ADR-0045, ADR-0050, ADR-0055, ADR-0057, ADR-0077, ADR-0080,
  ADR-0083, ADR-0084, ADR-0087, ADR-0090, ADR-0123, ADR-0131, ADR-0134,
  ADR-0135, ADR-0136, ADR-0137 and ADR-0138
- Supersedes only after Acceptance and separate design merge: the fifteen
  B07-owned byte copy/clear helper implementations named below

## Context

VM04 #278 is independently reviewed and merged. VM05 owns B07-F005.
Its actual predecessors are COPY01 #191, GRANT03 #264, BOOT03 #270 and
VM03 #276, not an additional VM04 edge.

The [fixed study](../research/minix-vm-memory-transfers.md) identifies
fifteen movement helpers in nine owners, with 103 current direct
production calls. These include unrolled byte assignments, not just
one-byte loops. The executable/request snapshots, scratch page and
boot snapshot still use those helpers despite the existing shared
MINIX-compatible word provider.

No new transfer algorithm, span table or snapshot ownership scheme is
needed. The current observed VM image already occupies 160 of 160
resident pages, so another provider or buffer is inappropriate.

## Decision

After independent review, Acceptance and separate merge, implement
[the VM transfer contract](../architecture/vm-memory-transfers.md).
This phase contains documentation only.

### Delegate at the existing helper boundaries

Keep all fifteen helper names, signatures, visibility and callers.
Replace each copy loop with one call to `micros_runtime_memcpy`, and
each clear loop with one zero-valued `micros_runtime_memset`.

Use the existing `lib/runtime/memory.c` and private header without
changing its algorithm, compiler adaptations or ABI. Preserve its
alignment-compatible eight-byte copies, four-byte fills, twelve-byte
short-fill threshold, differing-residue byte fallback and exact access
footprint. Zero length remains no-access, including null private-helper
arguments.

Every inventoried nonzero copy has disjoint source and destination
storage. Preserve that caller contract. Do not add `memmove`, overlap
recovery, a new alignment requirement or a validation branch to a
prevalidated commit.

### Keep the existing plans and effects

Operation-12 fixed objects still use their existing complete translated
plans. Move each retained chunk with the same physical address, kernel
buffer offset and size, without retranslation or plan coalescing.
Keep every owner/permission/token check before the first store.

Scratch pages still receive their complete zero before exact file bytes;
stack construction retains both pre-write validation boundaries.
Input publication/clearing, complete object zeroing, snapshot construction,
result publication and rollback retain their original sizes and order.

No allocator, local frame-class, cache/origin, mapping, prepared-generation,
fence or process lifecycle rule changes. VM06 continues to own snapshot
lifetime and duplicate-state reclamation.

Byte comparisons, zero predicates, FNV, little-endian encoding/decoding,
scalar/structure assignments and key-workspace algorithms are not movement
helpers to rewrite here.

### Keep one provider and meaningful evidence

Kernel and VM images already own the provider. Keep one actual linked
provider and add its prefixed implementation to native consumers that now
need it. Do not add compiler-visible forwarding symbols or the rest of
the runtime to kernel/native targets.

Native Red counts every old byte assignment, including all eight stores
in unrolled loops, as one unit and one byte. Existing loop-only counts
are not sufficient movement evidence. Green counts the provider's
actual executing units/bytes; no wrapper estimate or duplicate insertion
is allowed.

VM native and affected MEM03/MEM04/MEM05 consumers explicitly admit the
provider and RUNTIME owner. Preserve channel separation: MEM03 observes
it only in aggregate; MEM04/MEM05 in aggregate and their single context
component, not every disjoint component. Ordinary and unrelated channels
remain uninstrumented for that provider.

The existing ELF-spawn guest retains its scoped VM command collection.
Its shared runtime provider remains uninstrumented there, avoiding
unrelated compiler/runtime movement outside command-active intervals.
Instead, the same accepted guest/build supplies actual object/linked
proof of every helper/provider edge, both wide primitives, unique
provider ownership and exact compiler/input identity. No second guest,
new telemetry protocol or global observer mode is introduced.

### Preserve resource and review barriers

Require genuine current-runtime native movement Red and the original
guest's old linked-helper algorithm Red before correction. A missing API,
compiler failure, panic or timeout is not that Red.

Retain every existing semantic case, model seed/count, malformed/authority
control, resource witness and hardware scenario. Add focused page,
128-KiB snapshot, boot-object, alignment, canary and zero-length
composition evidence rather than repeating COPY01's complete Cartesian
matrix for every wrapper.

No new production backing storage or capacity-sized temporary is allowed.
Every current image, stack and deadline bound remains, including the
160-page VM image, four-KiB VM stack and complete 44-configuration /
113-ELF / ten-observed-scope gate. B13 remains blocked.

Current source, recipe, callback, primitive and native-consumer upkeep
belongs here under ADR-0123. One sufficient final author plan and fresh
independent review precede the first permanent implementation commit.

## Phase and non-goals

Independent design review is clean and this decision is Accepted.
Implementation remains pending the separate design merge; design closes no
finding.

No new memory algorithm/API, `memmove`, assembly/vector/unroll, larger
alignment, provider implementation copy, bulk authority, incremental
validation, span cache, allocator change, snapshot reclamation, recovery,
concurrency, hosted-CI acceptance, node Q or POSIX completion.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
eight-document proposal, relevant Accepted contracts, fixed local
MINIX/current source and exact four-command author evidence at staged tree
`13be3ada210f1f74ec94ee8b3782eb96d5cb3b31`.
It reported no substantive design issue remaining, with High confidence.
Targeted reproduction: None.

Acceptance records that design result, not an implemented transfer change
or B07-F005 closure. The separate design merge still precedes implementation.

## References

- [VM transfer contract](../architecture/vm-memory-transfers.md)
- [Fixed MINIX/current study](../research/minix-vm-memory-transfers.md)
- [Shared provider](../architecture/runtime-memory-primitives.md)
- [Grant spans](../architecture/grant-copy-spans.md)
- [Bootstrap transfers](../architecture/bootstrap-memory-transfers.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)

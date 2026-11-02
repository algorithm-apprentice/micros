# ADR-0090: MINIX Word-Based Runtime Copy and Fill

- Status: Accepted
- Date: 2026-11-02
- Refines: ADR-0002, ADR-0042, ADR-0080, ADR-0082, and ADR-0083
- Supersedes in part: ADR-0042's byte-loop implementation requirement, only
  for its two existing runtime memory bodies

## Context

EV04 acceptance #189 is merged. COPY01 is the next preferred dependency-ready
correction and owns only B05-F006. The existing prefixed runtime copy/fill
bodies use volatile byte assignments for every length. Semantic tests cannot
distinguish them from the required MINIX word family.

The [fixed-reference study](../research/minix-runtime-memory-primitives.md)
traces the actual generic and ARM implementations, alignment and access
footprints, primitive work, C aliasing constraints and existing source
authority. The generic reference supplies a suitable independently written
fallback for RV64; this task does not import ARM assembly or later memcpy
optimization.

The first real source correction also needs a concrete consumer of EV01.
One retained whole-file transport hash cannot approve a changed algorithm,
but removing it before independent Red/Green evidence is equally invalid.

## Decision

After separate design review, Acceptance and merge, implement the bounded
[runtime memory contract](../architecture/runtime-memory-primitives.md).

### Preserve the ABI and caller boundary

Keep the two prefixed functions, their existing declarations and exact
compiler-visible forwarding symbols. Preserve byte results, unsigned-byte
fill conversion, zero-length no-access behavior, original-destination
return, arbitrary caller alignment and the nonoverlap/restrict copy contract.

The functions own no process, mapping, grant, allocator, syscall or recovery
authority. Invalid nonzero ranges remain caller errors; no validation or
new error result is invented.

### Reproduce the selected word family

Copy uses the generic reference's alignment-compatible eight-byte words,
with a bounded byte prefix and suffix. Differing residues retain the
reference byte fallback. Fill retains four-byte words and the generic
three-word short-fill threshold, constructing one replicated pattern before
its word loop.

No size-optimization conditional replaces the wide algorithm. Existing
production flags, including application `-Os`, remain unchanged. Exact work
is defined over actual primitives and requested bytes, not benchmark time.

### One closed compiler adaptation

Permit only `__builtin_memcpy_inline` with constant sizes eight or four and
`__builtin_assume_aligned` with the corresponding alignment for these word
transfers. Explicit guards establish the assumption and the complete access
range before invocation. Fill copies the representation of one aligned
four-byte local pattern.

This retains C object-representation semantics without typed alias punning,
out-of-range merged loads or recursive library calls. Missing builtin support
fails compilation; there is no byte-only fallback or alternate compiler
framework. Native tests use an admitted Clang/Apple Clang implementation.

Keep volatile character accesses only on the genuine byte paths. No new
assembly, vector ISA, global compiler flag, host libc dependency or writable
production state is added. Compiler and linked artifacts must prove the
constant aligned transfers and absence of recursive/external memory calls.

### Primitive evidence before source retirement

Use the existing test-only RUNTIME owner and COPY/CLEAR unit/byte dimensions.
One byte assignment records one unit/one byte; an eight-byte copy records one
unit/eight bytes; a four-byte fill records one unit/four bytes. Disabled-mode
annotations remain passive and absent from ordinary artifacts.

Independent native expectations count complete aligned intervals from input
geometry and compare bytes against a separate reference. Old byte loops must
fail both aligned-work oracles while retaining semantic correctness. Tests
also cover every residue, short and boundary lengths, exact access limits,
typed objects and production-mode absence.

The sole planned retirement is the existing whole-file
`STABLE_HASHES/lib/runtime/memory.c` identity. Preserve its original literal
and reviewed source pin, all unrelated identities and permanent invariants.
The exact declaration, recorded real-execution sequence and provisional
assertion boundary are part of the canonical contract, not implementation
choices.

### A bounded real EV01 consumer

Reuse EV01's existing types and validator without changing its state machine.
The task-specific author driver accepts only Red, Green and recertification
intents, derives all source/command authority from the merged design and
current clean candidate, and records actual compiler/test/unit outcomes.
It accepts no caller-supplied success, command, source path or retirement set.

Newly staged retirement claims cannot bypass ordinary assertions. Before the
first reviewed commit, only a valid current terminal journal authorizes the
one retirement; the author-recertification transaction may provisionally
suppress exactly that identity after actual Green. A failed/interrupted
transaction restores ordinary assertion policy and publishes no terminal
event. Committed reviewed registry entries remain prior retirement authority
for ordinary no-plan checks, as in ADR-0082; they are not new execution
receipts or cross-candidate Green events.

This is one consumer integration, not a general command runner, remote
evidence importer or replacement for independent review.

## Non-goals

No `memmove`, string/libc breadth, new public memory API, heap, DMA/MMIO copy,
fault recovery, privileged grant commit, bootstrap call-site replacement,
validation-cadence correction, CI waiver or node-Q functionality is added.
In particular, this outcome does not claim to resolve the hosted PM/ELF
timeouts caused by still-audited runtime work.

## Acceptance

The implementation must establish:

- independent discriminating Red for both aligned copy and fill work;
- exact boundary/residue work and semantic/access-footprint Green;
- unchanged signatures, symbols, zero-length and nonoverlap behavior;
- fixed storage, ordinary observer absence and no recursive/external memory
  calls under the retained compiler modes;
- one valid granular EV01 transition, with tamper/failure controls and all
  other identities still active;
- actual affected-unit recertification, then the existing complete fresh
  native/QEMU/image/stack author gate;
- unchanged image/stack/time budgets and explicit future B13 status; and
- clean independent implementation review before merge.

## Phase barrier

This Accepted design contains documentation only. No C edit, test, model,
harness, record writer, build scaffold, dependency or executable prototype is
authored in this phase. `copy01-implementation` remains pending until this
design is independently reviewed, Accepted and separately merged.

## Design review disposition

Independent GPT-6.1 Sol review inspected the complete eight-file design,
fixed-reference trace, current interfaces and exact author evidence. It
reported no substantive design issue and required no targeted reproduction.
Acceptance records that design result, not an implemented primitive,
retirement event or hosted CI pass.

## References

- [Runtime memory contract](../architecture/runtime-memory-primitives.md)
- [Fixed MINIX study](../research/minix-runtime-memory-primitives.md)
- [B05 audit](../research/minix-complete-baseline-b05-grants-user-execution.md)
- [EV04 acceptance](../research/evidence-foundations-recertification.md)

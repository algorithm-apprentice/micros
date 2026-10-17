# ADR-0077: Bounded Complete VM Boot-Database Validation

- Status: Accepted
- Date: 2026-10-17
- Refines: ADR-0025, ADR-0043, ADR-0045, ADR-0050, ADR-0057, and ADR-0076

## Context

ADR-0045 requires VM to import and validate one complete fixed-capacity boot
database before the one-way handoff and ordinary launcher readiness. ADR-0050
retains complete read-only VM-state validation before production control
transitions. ADR-0057 assigns that runtime validation solely to
`servers/vm/vm_validation.c`.

The CPython 3.15 implementation gate first exposed the separate kernel
frame-ownership hot path corrected by ADR-0076. After that correction merged,
the exact same-candidate producer passed all 47 commands, but its bounded reuse
consumer failed in `test-qemu-elf-spawn` with:

```text
MICROS_BOOTSTRAP_FAILURE reason=ready-timeout
service=0x0000000000000002
state=starting
detail=0x0000000021fd2cb4
```

The unchanged VM ELF was still executing
`vm_core_boot_database_is_immutable()` at the branch after
`vm_core_initial_frame_is_free()`.

The complete boot object is 364,704 bytes. Its frame-state array has a fixed
262,144-byte capacity, while the observed image manages 28,904 frames. Runtime
immutability validation therefore performs one out-of-line bitmap helper call
and one FNV update for each of 233,240 inactive tail bytes before VM may report
ready. Debug target code places that deterministic work near the accepted
10-second guest readiness deadline.

The fixed evidence and Five-Whys analysis are recorded in
[the VM validation timeout study](../research/elf-spawn-vm-validation-timeout.md).

The canonical MINIX VM study classifies `micros`' fixed object, zero tails,
typed frame states, exact generations, and independent digest as a compatible
safety extension. MINIX constructs private VM state from bounded imported boot
data; it does not continuously rehash a fixed transport object before ordinary
VM transitions.

## Decision

Preserve complete VM runtime validation and the exact boot digest while
replacing the measured fixed-capacity helper/zero-byte work with one segmented,
word-aware implementation.

### Unchanged authority and semantics

`micros_vm_state_validate()` and its private
`vm_core_validate_state()` remain the complete read-only VM-state validators.
They continue to verify:

- initialization magic, reserved fields, boot-info identity, and disjoint
  storage;
- exact imported header/summary agreement;
- complete boot-database immutability;
- every active local frame state and its agreement with the initial-free
  bitmap;
- every inactive frame-state byte and initial-free bitmap bit;
- mapping-record identity, ordering, frame roles, and zero tails;
- transaction buffers, plans, tokens, phases, and aggregate counts; and
- every existing public error and failure-atomic output contract.

The public VM boot ABI, `micros_vm_state` layout, fixed capacities, initial
digest value, FNV prime, digest result, frame-state encodings, readiness
deadline, handoff protocol, service protocol, and call sites do not change.

Validation remains complete on every current transition. It gains no cache,
epoch, dirty bit, trust in mutation APIs, or success-shaped shortcut.

### Segmented canonical digest

`vm_core_boot_database_is_immutable()` computes the same canonical byte stream
in these phases:

1. require `managed_frame_count` to be nonzero and no greater than
   `MICROS_VM_MAX_MANAGED_FRAMES`;
2. hash bytes before `header.digest`;
3. advance across exactly `sizeof(header.digest)` zero bytes;
4. hash bytes after the digest through the start of `frame_states`;
5. hash active frame-state bytes in initial-free bitmap-word order;
6. prove the inactive frame-state tail is zero;
7. prove every inactive initial-free bitmap bit is zero;
8. advance the digest across the proven zero frame-state tail; and
9. compare with `state->boot_summary.digest`.

No field is omitted from the logical digest. Segmenting the implementation
changes only how the identical byte sequence is processed.

The count guard dominates every count-derived frame-state pointer, bitmap
index, loop bound, boundary mask, and tail-length subtraction. It uses the
already matched header/summary value but does not trust equality as proof of
range: coherent corruption of both copies must return
`MICROS_VM_CORE_ERROR_INVARIANT` without an out-of-bounds access.

### Immutable prefix

One file-local byte-digest helper processes the immutable prefix in explicit
bounded chunks. It may unroll a small fixed number of byte updates, matching
the existing boot-digest implementation, but introduces no platform-specific
load, alignment assumption, vector extension, or compiler-optimization
dependency.

The digest field itself remains logically eight zero bytes exactly as in
`micros_vm_boot_digest()`.

### Active frame-state scan

The active range
`[0, state->boot_summary.managed_frame_count)` is processed in 64-frame bitmap
chunks.

For each chunk:

1. load `state->initial_free_bitmap[word_index]` once;
2. visit only active frame indices in that chunk;
3. derive the initial-free bit from the loaded scalar word;
4. hash `MICROS_VM_FRAME_FREE` for an initially free frame; and
5. otherwise hash the exact current boot-info frame-state byte.

The active loop does not call `vm_core_initial_frame_is_free()` and does not
reload or re-index the bitmap from each raw frame index.

This preserves the accepted rule: later VM-local lifecycle changes to
originally free frames do not alter the boot-database digest, while any change
to an originally nonfree frame state remains detectable.

### Inactive tails

The inactive frame-state range
`[managed_frame_count, MICROS_VM_MAX_MANAGED_FRAMES)` remains completely
validated:

1. `vm_core_bytes_are_zero()` checks every inactive frame-state byte;
2. when `managed_frame_count` is not word-aligned, one boundary mask rejects
   every high initial-free bit in the partial word; and
3. the full-word scan begins at the exact first fully inactive word, advances
   contiguously by one, and checks through
   `MICROS_VM_INITIAL_FREE_BITMAP_WORDS - 1`.

The exact start is:

```text
first_word = managed_frame_count / 64
if managed_frame_count % 64 != 0:
    check high bits in first_word
    first_word += 1
scan [first_word, MICROS_VM_INITIAL_FREE_BITMAP_WORDS)
```

For an aligned count such as 64, word 1 is the first fully inactive word. For
a partial count such as 65, word 1 receives the boundary mask and word 2 is the
first fully inactive word. Maximum capacity produces an empty full-word scan.

The bitmap check is required to preserve current semantics. A set inactive bit
would otherwise canonicalize a zero `MICROS_VM_FRAME_UNUSED` byte as
`MICROS_VM_FRAME_FREE` and change the imported digest.

The existing later local-frame validator may retain its independent zero-tail
guard. That duplicated cheap semantic guard is not the measured defect and
need not be removed.

### Logarithmic zero-tail advance

For one zero byte, the existing FNV-1a update is:

```text
next = (digest XOR 0) * prime = digest * prime mod 2^64
```

For `n` proven zero bytes:

```text
next = digest * prime^n mod 2^64
```

One file-local helper computes that power by exponentiation by squaring. Its
loop halves the remaining byte count each iteration, uses only `uint64_t`
unsigned arithmetic, and therefore has standard-defined modulo-2^64 behavior.

The helper performs logarithmic work. It must not decrement once per zero byte,
call a byte-update helper once per zero byte, allocate storage, use
floating-point arithmetic, or precompute a mutable table.

### Failure ordering and outputs

The existing top-level order remains:

1. state pointer and initialization checks;
2. boot header/summary agreement;
3. complete boot-database immutability;
4. phase-specific transaction checks;
5. active/tail frame-state validation;
6. mapping-record validation; and
7. loader-phase/count agreement.

Within immutability validation, an inactive byte or bitmap corruption may be
rejected before the final digest comparison. Both paths already produce the
same private `false` result, public
`MICROS_VM_CORE_ERROR_INVARIANT`, no output, and no mutation. This does not
change caller-visible precedence.

### Ownership and source boundaries

`servers/vm/vm_validation.c` remains the sole owner of the complete runtime
immutability check. File-local helpers may own:

- bounded byte digestion;
- logarithmic zero-byte advancement;
- active frame-state canonicalization; and
- inactive initial-free bitmap validation.

`servers/vm/vm_core.c` retains the shared bitmap query used by unrelated frame
selection and lifecycle code. The correction does not move public functions,
change `vm_internal.h`, create another validation owner, or alter
`lib/vm/vm_boot_validate.c`.

Test implementation may extract only generic fixed-target preprocessing and
Clang JSON-AST utilities from the existing frame hot-path gate into one shared
host-only helper. Both structural gates must retain their existing fail-closed
identities.

## Test-first acceptance

Implementation begins with one deterministic structural Red for
`vm_core_boot_database_is_immutable()` and its file-local helpers. Using fixed
RISC-V freestanding preprocessing plus Clang JSON AST, it rejects:

1. a canonical digest loop bounded by `sizeof(*state->boot_info)` across the
   full object;
2. absence of a nonzero/capacity guard that dominates every
   `managed_frame_count`-derived traversal, pointer, index, mask, and
   subtraction;
3. any call to `vm_core_initial_frame_is_free()` from the immutability digest
   path;
4. active bitmap access derived separately from each raw frame index;
5. absence of a bitmap-word outer loop with one scalar word load supplying all
   active bits in its chunk;
6. absence of one partial inactive bitmap mask and a full-word scan that
   starts at the exact first fully inactive word, advances contiguously by one,
   and ends at `MICROS_VM_INITIAL_FREE_BITMAP_WORDS`;
7. hashing the inactive frame-state tail through a byte-linear digest loop;
8. absence of an independent inactive frame-state zero check; and
9. a zero-run helper whose loop decrements by bytes rather than halving the
   remaining count.

The gate is a bounded source/work-shape contract, not a general malicious-C
verifier. It must include negative mutations for realistic helper-call,
per-frame bitmap, missing-tail, shifted full-word start, skipped-word,
early-end, linear-zero-loop, and control-flow regressions.

Existing semantic tests are preservation guards, not Reds. Native
sanitizer-backed cases must remain green and add:

- managed counts `1`, `63`, `64`, `65`, the observed `28,904`,
  `MICROS_VM_MAX_MANAGED_FRAMES - 1`, and maximum capacity;
- coherent header/summary managed counts of zero and
  `MICROS_VM_MAX_MANAGED_FRAMES + 1`, with the imported digest left unchanged,
  requiring invariant rejection without mutation or sanitizer failure;
- exact digest equivalence for zero-tail lengths at those boundaries;
- immutable prefix and originally nonfree active frame-state corruption;
- allowed originally free local-state lifecycle behavior;
- inactive frame-state corruption at the first inactive byte and final byte;
- a partial boundary-word initial-free bit;
- an initial-free bit in word 1 when `managed_frame_count == 64`;
- an initial-free bit in word 2 when `managed_frame_count == 65`;
- an inactive bit in the final bitmap word; and
- active initial-free bitmap corruption.

Randomized evidence is unnecessary. If property coverage is added, it must use
fixed replayable seeds and print the complete operation/count trace on failure.

The exact `test-qemu-vfs` then `test-qemu-elf-spawn` sequence is final target
acceptance, not a deterministic Red: the old implementation can finish before
the timing threshold. It must pass once without retry, sleep, readiness
deadline change, host-timeout change, compiler-optimization change, or runner
relaxation.

Before implementation review:

- the new structural Red is green;
- the complete `vm-core` native CTest passes under existing sanitizers;
- the VM handoff and ELF-spawn QEMU gates pass;
- the exact no-retry VFS-to-ELF sequence passes;
- one complete current producer succeeds;
- exact selected receipt reuse succeeds;
- source, staged, worktree, and branch-range checks pass; and
- independent review reports no substantive issue.

## Design review disposition

Initial independent design review found that "every later bitmap word" did not
prove the exact start of the full inactive-word scan. An implementation could
skip word 1 for aligned managed count 64, or word 2 after the partial word for
count 65, while still satisfying the listed partial/final corruption cases.

The corrected contract defines the exact half-open full-word range, contiguous
increment, and fixed end. Structural negative evidence shifts, skips, and
truncates that range; native corruption cases cover the first fully inactive
word for both aligned and partial boundaries.

Correction review then found that segmented traversal would consume
`managed_frame_count` before proving it was within the fixed capacity. The old
fixed-size loop did not need that precondition, so coherent corruption of both
the header and stored summary could otherwise produce an out-of-bounds access
or tail-length underflow.

The final contract requires a dominating nonzero/capacity guard before every
count-derived pointer, index, mask, loop, or subtraction. Sanitizer-backed
native cases coherently corrupt both copies to zero and maximum plus one while
leaving the imported digest unchanged.

Final independent read-only review concluded:

> No significant issues found in the reviewed changes. No substantive issue
> remains; the design is ready to mark Accepted.

Reviewers reran no broad suite, model, build, or QEMU workflow. Both findings
were established statically from the proposed algorithm and current source.
Acceptance records an implementation-ready design, not optimized production
code or a passing target gate.

## Non-goals

This decision does not:

- change the boot-information ABI, state layout, digest algorithm, digest
  value, fixed capacity, or frame-state encoding;
- remove complete validation or trust mutation APIs;
- cache a prior successful validation or add validation epochs;
- change VM handoff, PM/VFS control, load, abort, commit, exit, mapping,
  allocation, or fault behavior;
- change the 10-second guest readiness deadline or 180-second host timeout;
- add retries, sleeps, performance markers, production counters, or a new
  workflow;
- depend on target compiler optimization;
- optimize the separate initial `micros_vm_boot_validate()` digest without its
  own measurement and design;
- reopen ADR-0076 or alter kernel frame-ownership validation; or
- add generalized digest, bitmap, or validation frameworks.

## Alternatives considered

### Retry the reuse consumer

Rejected. The exact binary already demonstrated both outcomes. A passing retry
would hide deterministic work behind host scheduling variance.

### Increase the VM readiness deadline

Rejected. It preserves the fixed-capacity helper and byte-linear zero-tail work
and merely moves the guest threshold.

### Build the target with optimization

Rejected. Debug target evidence remains required, and compiler inlining is not
an architectural work bound.

### Validate the boot database only once

Rejected. Complete runtime validation deliberately detects silent boot-object,
bitmap, record, and transaction corruption before every accepted transition.

### Copy the original boot object and use `memcmp`

Rejected. It duplicates a 364,704-byte authority object, increases the wired VM
working set, still performs linear fixed-capacity comparison, and complicates
the allowed mutable frame-state projection.

### Change or rebaseline the digest

Rejected. Existing boot construction, summaries, native fixtures, handoff
proof, and runtime immutability all use the current exact digest.

### Inline only `vm_core_initial_frame_is_free`

Rejected as incomplete. It removes call overhead but retains linear hashing of
233,240 proven zero tail bytes and provides no source-level bound independent
of compiler choices.

### Preserve the digest with segmented active and zero-tail processing

Accepted. It retains the identical canonical byte stream and complete
corruption detection while removing the measured fixed-capacity execution
shape.

## References

- [Root-cause study](../research/elf-spawn-vm-validation-timeout.md)
- [VM service source contract](../architecture/vm-service-source-organization.md)
- [Static VM handoff decision](0045-static-vm-bootstrap-and-handoff.md)
- [Post-handoff ELF spawn decision](0050-post-handoff-elf-spawn.md)
- [MINIX VM baseline](../research/minix-vm-bootstrap-and-handoff.md)
- [Testing strategy](../testing-strategy.md)

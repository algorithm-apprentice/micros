# ADR-0076: Bounded Complete Frame-Ownership Validation

- Status: Accepted
- Date: 2026-10-17
- Refines: ADR-0020, ADR-0025, ADR-0040, and ADR-0050

## Context

ADR-0020 requires `micros_frame_ownership_validate()` to check every active
and inactive ledger entry, every allocator bit, aggregate counts, phase, and
immutable geometry. ADR-0040 deliberately places that complete validation
before each user-address lookup or translation and records that the cost is
accepted for v0.1 until measured after integration.

The CPython 3.15 implementation gate supplied that measurement. The unchanged
`test-qemu-elf-spawn` image alternated between success and a 180-second host
timeout. Failures stopped after different UART markers, so retrying could
misclassify the symptom as a TTY, VFS, spawn, shutdown, or QEMU-runner race.

Two independent QEMU monitor captures froze the hart during a timeout. Both
showed:

```text
priv:  S
scause: user ecall
pc:    micros_frame_ownership_allocator_bit_is_set
ra:    micros_frame_ownership_validate
```

The recovered frame-pointer chain was:

```text
micros_frame_ownership_validate
micros_frame_ownership_runtime_validate
load_validation_context
micros_user_address_space_validate_locked
micros_user_address_space_translate
```

The active QEMU memory map manages 28,904 frames. The fixed ledger capacity is
262,144 frames, so every full validation also inspects 233,240 inactive
entries: 88.97% of the table. Captured inactive indices included 108,764 and
219,920. Target Debug assembly performs an out-of-line owner comparison and an
out-of-line allocator-bit query for every inactive frame.

The guest was therefore making forward progress inside a read-only invariant
check, not deadlocked. Host performance determines whether the same exact
binary crosses the 180-second harness limit, which explains the apparently
probabilistic result. Retrying until one run passes hides deterministic
algorithmic work behind a wall-clock threshold and is not acceptable
evidence.

The fixed MINIX baseline does not perform a global physical-ownership audit
before every copy. Its safe-copy path validates the selected grant, then
`virtual_copy_f()` calls `lin_lin_copy()`, which maps selected source and
destination chunks with `createpde()` and copies them through
`PHYS_COPY_CATCH`. Separate `vm_lookup()` and `vm_lookup_range()` entry points
likewise inspect only selected mappings/ranges, but are not part of that
safe-copy call chain. `micros` keeps its stronger complete-ledger validation
as a compatible safety extension, but that extension does not require
per-frame helper calls or per-frame bitmap reads over the inactive tail.

## Decision

Preserve the complete validation contract and error semantics while replacing
the measured instruction-heavy scan shape with one bounded, word-aware
implementation.

### Unchanged authority and semantics

`micros_frame_ownership_validate()` remains the sole complete
allocator-ledger validator. It continues to check:

- initialization, phase, allocator identity, and immutable geometry;
- every active owner, handoff target, and allocator bit;
- exact allocated, owned, and per-kind counts;
- handed-off phase restrictions;
- every inactive owner and handoff target through the fixed maximum capacity;
- every allocator bitmap bit beyond `managed_frame_count`; and
- every final aggregate equality.

Validation remains read-only, repairs nothing, returns the existing error
values, and preserves ADR-0040's complete-validation precedence before
`NOT_MAPPED`.

The public structures, fixed 262,144-frame capacity, BSS layout, ABI, owner
encodings, handoff phases, mutation APIs, and one-hart serialization contract
do not change.

### Active-frame scan

The active range `[0, managed_frame_count)` is processed in allocator bitmap
word order.

For each 64-frame bitmap word:

1. load that word once;
2. visit only the active frame indices covered by the word;
3. derive each allocated bit from the loaded word;
4. validate the exact owner and handoff semantics in current frame order; and
5. accumulate the same allocated, owned, and per-kind counts.

The active loop does not call
`micros_frame_ownership_allocator_bit_is_set()` once per frame. Existing
owner/handoff predicates may remain shared only where target disassembly
proves they do not recreate the rejected allocator-bit call shape.

### Inactive-tail scan

The inactive range `[managed_frame_count,
MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES)` remains completely checked.

It is split by representation:

1. one direct owner/handoff pass checks every inactive owner field and target
   byte for zero;
2. the partial bitmap word containing `managed_frame_count`, when present,
   masks and rejects every inactive high bit; and
3. each later allocator bitmap word is checked once for zero.

The inactive owner/handoff loop contains no out-of-line function call. The
inactive bitmap is never queried once per frame.

This preserves every accepted corruption check while reducing inactive
bitmap work from at most 233,240 repeated bit queries in the observed image
to at most 4,096 word checks for the complete maximum bitmap.

### Failure ordering and outputs

Header and geometry checks remain first. Active-frame validation and count
construction remain before inactive-tail and aggregate checks. All failures
in those phases still return `MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT`; no
caller-visible output is published by validation.

Because inactive owner, target, and bitmap corruption share the same public
error and validation is read-only, separating their representation-specific
passes changes neither error classification nor state.

### Ownership and source boundaries

`kernel/memory/frame_ownership_validation.c` owns the optimized complete scan.
Low-level owner encoding and allocator mutation remain in
`kernel/memory/frame_ownership.c`. Address-space translation continues to call
the same complete validator through the same public and private interfaces.

Implementation may split header, active-scan, inactive-scan, and aggregate
checks into cohesive file-local helpers. It must not introduce a generalized
validation framework, mutable cache, validation epoch, callback, dynamic
allocation, or another production owner.

## Test-first acceptance

Implementation begins with one deterministic structural Red. It must reject:

1. an inactive owner/handoff loop containing any out-of-line call;
2. an active or inactive frame loop calling the allocator-bit helper;
3. active or inactive bitmap access indexed from each raw frame index,
   including per-frame division or shift to recover a word index;
4. absence of a word-indexed active outer loop whose one loaded bitmap word
   supplies every covered active-frame bit; and
5. absence of one explicit partial-boundary mask plus a later
   bitmap-word-indexed zero scan.

Semantic tests are preservation guards, not Reds: the current implementation
already returns the required results. They must remain green throughout:

- native tests cover managed counts at `1`, `63`, `64`, `65`, the observed
  28,904-frame shape,
  `MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES - 1`, and maximum capacity;
- native corruption cases independently flip an active owner, active target,
  active bitmap bit, each inactive owner field (`generation`, `slot`, `kind`,
  and `reserved`) with every other field zero, an inactive target byte, a
  partial boundary-word bit, frame 262,143's inactive bit in the final bitmap
  word, and a later inactive bitmap word; and
- the existing replayable frame-ownership model and every existing expected
  result remain unchanged.

The exact `test-qemu-vfs` then `test-qemu-elf-spawn` sequence is final target
acceptance, not a deterministic Red: the threshold defect can pass before the
correction. It must pass once without retry, sleep, timeout increase, or runner
relaxation after the structural Red and semantic guards are green.

Before implementation review:

- the complete native host gate passes;
- the frame-ownership QEMU component passes;
- `test-qemu-elf-spawn` reaches the exact final report and clean SBI shutdown;
- one complete current producer succeeds without retrying a failed command;
- exact selected receipt reuse succeeds;
- source, staged, worktree, and branch-range checks pass; and
- independent review reports no substantive issue.

## Design review disposition

Complete independent read-only review found three gaps:

- the initial MINIX note incorrectly placed `vm_lookup()` and
  `vm_lookup_range()` on the safe-copy call chain and mixed ADR-0025
  classifications;
- the acceptance section incorrectly labeled already-green semantic guards
  and a threshold-sensitive QEMU scenario as deterministic Reds, while its
  structural gate did not forbid direct per-frame inactive bitmap access; and
- the boundary matrix omitted the final partial bitmap word at maximum
  capacity minus one.

The corrected design records the exact
`verify_grant -> virtual_copy_f -> lin_lin_copy -> createpde /
PHYS_COPY_CATCH` path, assigns one classification per difference, and keeps
development retry policy outside runtime parity. Only the work-shape gate is a
Red; semantic guards remain green and target execution is final acceptance.
The structural gate now requires a boundary mask and word-indexed tail scan,
and the native matrix includes frame 262,143's inactive bit.

Acceptance-state review then identified a stale scratch-acquisition return
address in the recorded live chain, an active-word requirement not enforced by
the structural Red, incomplete per-field inactive-owner corruption, and one
stale Proposed status in the research note. The corrected design removes that
frame, requires one loaded word per active chunk, covers generation, slot,
kind, and reserved independently, and records ADR-0076 as Accepted
consistently.

Final wording review then corrected the roadmap to retain and pass, rather
than remove, the reproducer and clarified that existing semantic guards remain
green while the new cases are added. Final independent review concluded:

> No substantive issue remains. Both prior Medium findings are closed, and
> the staged design is ready to commit.

Targeted reproduction was the author's QEMU monitor/register/stack capture
recorded in the research note; the reviewer reran no broad suite. Acceptance
records an implementation-ready design. It does not claim optimized production
code or a passing ELF-spawn gate.

## Non-goals

This decision does not:

- reduce or sample complete ledger validation;
- cache a prior successful validation;
- trust mutation APIs as a substitute for corruption detection;
- change ADR-0040 error precedence;
- lower the fixed frame capacity or QEMU memory size;
- raise the ELF-spawn timeout;
- retry QEMU, accept a marker from a timed-out run, or weaken clean-shutdown
  evidence;
- change TTY, VFS, PM, VM, IPC, grant, scheduler, or application behavior;
- add SMP, dynamic storage, recovery, or performance counters to production;
  or
- claim a broader optimization beyond this measured validator.

## Alternatives considered

### Retry until `test-qemu-elf-spawn` passes

Rejected. It converts a deterministic work budget into probabilistic
acceptance and wastes validation time without correcting the cause.

### Increase the QEMU timeout

Rejected. The same validator remains on every translation, so a larger timeout
only moves the threshold and makes the gate slower.

### Accept the final pass marker even when QEMU times out

Rejected. Some failures stop before the child or final marker, and a marker
does not prove the drain barrier, final resource check, or SBI shutdown.

### Cache complete-validation success

Rejected. Silent owner, handoff, allocator, or aggregate corruption must still
be detected by every complete validation call.

### Reduce the fixed frame capacity

Rejected for this correction. The measured implementation shape can be fixed
without changing ADR-0020's public capacity or VM boot structures.

### Optimize the complete scan without changing semantics

Accepted. It preserves the stronger `micros` safety extension and the MINIX
behavioral boundary while removing the measured redundant execution shape.

## References

- [Root-cause study](../research/elf-spawn-frame-validation-timeout.md)
- [Frame-ownership source contract](../architecture/frame-ownership-source-organization.md)
- [Post-handoff address-space decision](0040-post-handoff-wired-address-space-resolution.md)
- [MINIX baseline rule](0025-minix-behavioral-baseline-before-optimization.md)
- [Testing strategy](../testing-strategy.md)

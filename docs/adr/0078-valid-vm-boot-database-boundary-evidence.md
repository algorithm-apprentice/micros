# ADR-0078: Valid VM Boot-Database Boundary Evidence

- Status: Accepted
- Date: 2026-10-17
- Refines: ADR-0057 and ADR-0077
- Supersedes in part: ADR-0077's native semantic case requiring a valid
  `managed_frame_count == 1` boot database

## Context

Accepted ADR-0077 defines a segmented, word-aware implementation of complete
VM boot-database immutability validation. Its native semantic matrix requires
valid managed counts:

```text
1, 63, 64, 65, 28,904, maximum - 1, maximum
```

Implementation planning found that count 1 is not valid under the current
public boot-validator contract.

Every accepted boot database has at least one address space and one mapping.
The address-space root must resolve to a `MICROS_VM_FRAME_KERNEL` byte, while
the self mapping must resolve to a distinct
`MICROS_VM_FRAME_SELF_WIRED` byte and contribute a nonzero self-wired count.
One frame-state byte cannot satisfy both roles.

The existing complete VM-core fixture has a larger valid minimum:

The fixture owns one exact boot topology:

- two address spaces;
- three mappings;
- two distinct kernel root frames;
- two VM-self-wired mapping frames;
- one service-wired mapping frame;
- one additional kernel frame; and
- free frames beginning at `TEST_FIRST_FREE_FRAME == 6`.

`vm_core_test_initialize_boot_info()` therefore rejects any count below six.
This is not an arbitrary test limit. The boot validator requires each address
space root to resolve to a `MICROS_VM_FRAME_KERNEL` frame, each mapping to
resolve to its exact self/service wired state, and mapped physical frames to be
unique. The fixture's six nonfree typed frames must all lie inside
`managed_frame_count`; otherwise its nonzero bytes fall into the required zero
tail or a referenced frame falls outside the managed range.

A different minimal topology could be authored solely to admit count 2, or a
private digest helper could be exposed to tests. Neither is needed to verify
ADR-0077's algorithm:

- count 6 exercises a small partial active bitmap word and a large inactive
  tail;
- counts 63, 64, and 65 exercise the exact partial/aligned/next-word
  boundaries;
- 28,904 exercises the measured QEMU shape;
- maximum minus one and maximum exercise the final partial/no-tail shapes; and
- coherent zero and maximum-plus-one corruption exercise the required
  dominating range guard.

The implementation task correctly stopped before code rather than inventing
an unreviewed fixture, test-only production export, or misleading invalid-state
"equivalence" case.

## Decision

Supersede only ADR-0077's valid count-1 native semantic case.

The valid managed-count matrix is:

```text
TEST_FIRST_FREE_FRAME == 6
63
64
65
28,904
MICROS_VM_MAX_MANAGED_FRAMES - 1
MICROS_VM_MAX_MANAGED_FRAMES
```

The implementation uses the existing `vm_core_test_context`, boot topology,
initialization helpers, native executable, CTest, sanitizer configuration, and
case order. It does not create a second boot fixture or expose a private
production digest helper.

### Preserved structural evidence

ADR-0077's structural Red remains unchanged. It proves:

- the nonzero/capacity guard dominates every count-derived access;
- the active scan is word/chunk bounded;
- the inactive bitmap scan starts at the exact aligned or post-partial first
  full word and remains contiguous;
- inactive frame-state bytes are checked independently;
- zero-tail digest advancement is logarithmic; and
- the rejected fixed-capacity/per-frame helper work cannot return.

The structural gate, rather than a fabricated count-1 complete VM topology,
proves the implementation shape for every admitted count.

### Preserved invalid-count evidence

Native sanitizer-backed tests still corrupt both matched count copies to:

```text
0
MICROS_VM_MAX_MANAGED_FRAMES + 1
```

They leave the imported digest unchanged and require
`MICROS_VM_CORE_ERROR_INVARIANT` without mutation, pointer underflow,
out-of-bounds access, or sanitizer failure.

These are invalid-state guards, not valid digest-equivalence cases.

### Preserved bitmap boundaries

The corrected valid matrix retains the exact cases required by the two design
review findings:

- count 63: partial word with one inactive bit;
- count 64: aligned boundary whose first fully inactive word is word 1;
- count 65: partial word 1 and first fully inactive word 2;
- maximum minus one: partial final word;
- maximum: no inactive frame-state or bitmap tail.

Native corruption still covers:

- the first and final inactive frame-state bytes;
- the partial boundary bit;
- word 1 at count 64;
- word 2 at count 65;
- the final inactive bitmap word; and
- an active initial-free bit.

### Unchanged production contract

Every production decision in ADR-0077 remains authoritative:

- exact boot ABI, state layout, capacity, FNV constants, and digest result;
- complete validation and corruption detection;
- active initially-free/nonfree semantics;
- inactive frame-state and initial-free bitmap tails;
- failure ordering, result, read-only behavior, and call sites;
- readiness and host timeouts;
- source ownership and non-goals; and
- exact no-retry target, producer, reuse, and independent-review gates.

## Test-first acceptance

The dependent implementation may resume only after this corrective design is
Accepted and merged.

Its Red/Green sequence remains:

1. add the deterministic ADR-0077 structural Red;
2. add the semantic boundary/corruption cases using the corrected valid and
   invalid count sets;
3. implement the minimum segmented canonical digest;
4. keep every permanent commit green;
5. run the focused native VM-core gate;
6. run VM handoff and the exact no-retry VFS-to-ELF sequence;
7. run one complete producer and exact reuse consumer; and
8. obtain clean independent implementation review.

## Design review disposition

Initial independent review found that the proposed text treated count 1 as a
shape another topology might admit. The current public validator makes that
impossible: every valid boot database needs at least one kernel root frame and
one distinct self-wired mapping frame.

The corrected design distinguishes:

- count 1: architecturally invalid under the current validator;
- count 2: theoretically admissible through a different topology; and
- count 6: the smallest valid existing complete fixture shape.

It also clarifies that a required count-1 invalid-state case would exercise
existing topology/frame-role validation rather than ADR-0077's new range guard
or valid digest-equivalence boundaries.

Final independent read-only review concluded:

> No significant issues found in the reviewed changes. The retained boundary
> matrix is sufficient, ADR-0078 preserves ADR history without changing
> production contracts, and the prior Medium finding is closed. No substantive
> issue remains; the design is ready to mark Accepted.

The reviewer ran no build, suite, model, or QEMU workflow. Acceptance records a
corrected implementation evidence contract only.

## Consequences

- Every native "valid" case now constructs a boot database accepted by the
  existing public validator.
- Word-boundary, observed-shape, capacity, and invalid-count evidence remain
  complete.
- Implementation does not invent test-only production access or another
  topology.
- ADR-0077's production semantics and root-cause correction remain unchanged.

## Alternatives considered

### Keep count 1 as a valid complete-state case

Rejected. The current fixture cannot represent its required roots, mappings,
and six typed nonfree frames within one managed frame. More fundamentally, the
public validator requires distinct kernel-root and self-wired mapping states,
so no complete topology can be valid with one managed frame.

### Add a second minimal boot topology

Rejected for this bounded correction. It would duplicate substantial boot
construction and validation evidence solely to test a digest helper below the
smallest current complete fixture shape.

### Export the private segmented digest helper

Rejected. Test convenience does not justify a new production interface or
second validation authority.

### Treat count 1 as an invalid-state case

Rejected as a required case for this correction. Count 1 exercises existing
boot topology and frame-role validation, not ADR-0077's new count guard or
word boundaries, and it cannot provide the valid digest-equivalence evidence
the original matrix intended. Existing boot-import tests retain those topology
rules.

### Use count 6 as the smallest valid existing fixture shape

Accepted. It is already authoritative, exercises the partial-word/tail path,
and preserves the exact task scope.

## References

- [Bounded VM validation decision](0077-bounded-complete-vm-boot-database-validation.md)
- [VM service source contract](../architecture/vm-service-source-organization.md)
- [Testing strategy](../testing-strategy.md)

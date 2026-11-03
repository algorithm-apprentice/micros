# ADR-0097: Panic-Trace Link Rejection Precedence

- Status: Accepted
- Date: 2026-11-03
- Refines: ADR-0080, ADR-0082, and ADR-0096

## Context

PLAT03 design #202 is independently reviewed and merged as
`2c0b6c8ae0c0fb3ec6bcfa9473766b9c334db1be`, tree
`0d62f80ba2c512763a2a49f3da181b70162cbb43`.

Its cursor order is explicit: after a valid record is emitted, check the
authenticated root, null predecessor and non-increasing predecessor
before checking the next record's alignment/range. This retains the
fixed MINIX progress-check order.

The target table's case 8, however, says "Same explicit rejection and
no user-memory read" immediately after the misaligned case's
`invalid-frame` result. The first uncommitted implementation declaration
incorrectly interpreted that shorthand as the same stop code.

Static existing evidence establishes the actual result:

- `include/micros/user_address_space.h:19-20` fixes the nonzero user
  window to `[0x40000000, 0x80000000)`;
- `arch/riscv64/kernel.ld:6-9` starts the kernel at `0x80200000`, with
  all admitted kernel stack allocations later in that image;
- a successor in that user window is therefore below any admitted
  current kernel frame; and
- the earlier non-increasing guard wins before the range guard.

Case 8 must stop `nonmonotonic`. No user-memory read occurs under
either label, but exact failure precedence is acceptance evidence;
it cannot be left to an implementation-dependent interpretation.

The implementation stopped during independent declaration/decoder work,
before any production edit, real trace fixture or actual runtime Red.
Its five-file host scaffold and original frozen authority were preserved
as reference, and its branch was abandoned. Passing scaffold tests
were not runtime acceptance.

## Decision

Clarify only the fixture expectations in the
[canonical trace contract](../architecture/panic-stack-trace.md):

- malformed-successor cases use a valid current PC and an interior
  current frame, strictly below the authenticated root;
- case 7 uses a successor greater than the current frame but
  misaligned, so its first failed successor guard is `invalid-frame`;
- case 8 uses a nonzero successor in the fixed user window and must
  stop `nonmonotonic`, without loading that successor;
- case 9's equal successor likewise stops `nonmonotonic`; and
- an actual root frame still stops `boundary` before following or
  diagnosing its predecessor.

Do not move the range check before the progress check to accommodate
the incorrect declaration. Do not combine stop codes, weaken exact
target expectations, change the reference algorithm, increase a limit
or invent an exception to the frozen-input rule.

## Preserved scope

Keep all ADR-0096 owner/generation/root admission, 32-record depth,
two-word read order, PC validation, constant storage, output grammar,
fault/recursive/UART precedence, eighteen scenarios, 192-byte
observation, four oracles, nine-fact family, finite-proof obligations
and complete author/stack gates.

Keep the fixed runtime reference at merged PLAT02 #201. No new runtime
finding, feature, correction-DAG edge or authority retirement is added.
This is the sole active design prerequisite for blocked PLAT03.

## Acceptance and restart

The correction is documentation only. Its evidence is the existing
numeric address geometry, fixed MINIX/current guard order, complete
documentation/structure checks and three-layer diff checks. No new
executable reproduction or prototype is needed to establish the
ordering contradiction in the derived declaration.

Independent review must verify that the clarification matches the
already Accepted algorithm and changes no unrelated contract.
Only after Acceptance and separate merge may PLAT03 restart from
current `main`.

Before creating new implementation owners/extractors, independently
declare the corrected nine facts against both merged designs. Preserve
all 3,929 committed predecessor records and thirteen fingerprints.
Do not regenerate or overwrite the abandoned independent declaration
after its source owners existed. It remains evidence of the rejected
attempt, not authority for the restarted candidate.

The restarted native/model and actual target work must distinguish
null, backward/equal, increasing-misaligned, increasing-out-of-range,
root-boundary and depth-limit precedence. Actual fixed-runtime Red,
matching current Green, complete author gates and independent
implementation review remain required.

## Design review disposition

Independent read-only GPT-6.1 Sol review inspected the complete
five-file clarification, the Accepted cursor order, fixed address
geometry and exact current author evidence. It found no substantive
issue. Static evidence was sufficient; no reproduction or routine
validation rerun was needed, and no files were modified.

Acceptance records that design result. Implementation remains blocked
until the correction separately merges and a fresh implementation
branch independently declares its authority before owner edits.

## References

- [Accepted base design](0096-bounded-kernel-panic-stack-trace.md)
- [Canonical trace contract](../architecture/panic-stack-trace.md)
- [Fixed MINIX/RV64 study](../research/minix-panic-stack-trace.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)

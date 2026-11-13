# Quiescent Grant Resource Evidence

## Fixed inputs and phase

```text
merged main/design: 9340502012b5ba0d570bebe58a8cb3107d12a0b1 (#260)
merged tree:        4307f01aa51e13818d726ee091c246d950800794
MINIX reference:    4db99f4012570a577414fe2a43697b2f239b699e
failed local tree:  3690c3df05dcdc6fe2bef2744377e8bba47c8f53
```

The failed tree was an uncommitted GRANT02 implementation candidate, not
accepted or merged code. The corrective design uses a separate clean
worktree from #260; the original work is preserved and blocked. No
executable artifact is authored in this phase.

## Existing source and authority

| Source | Established boundary |
| --- | --- |
| ADR-0050, production integration/resource baseline | Require no leaked live resource after post-freeze rollback and normal child exit/wait. |
| `docs/architecture/elf-spawn-integration.md`, complete resource evidence | Compare live identities rather than totals; consumed token/free-generation history must not be reset, and every exception requires a lifetime rationale. |
| ADR-0129 and its fixed MINIX study | Initially ascending free slots are allocated by head pop; retirement advances generation and pushes LIFO. |
| Local MINIX `minix/lib/libsys/safecopies.c:90-171,229-272` | The same free-list initialization/pop/revoke-push mechanism; source remains evidence, not copied implementation. |
| `kernel/tests/spawn/elf_spawn_test_capture.c` | Copy the actual complete grant registry into both resource snapshots after runtime validation. |
| `kernel/tests/spawn/elf_spawn_test_compare.c:153-184` at merged main | Require zero active counts and equal per-slot state, normalize only `generation`, then compare whole records. The future index fields have no separate lifetime policy. |
| `kernel/tests/spawn/elf_spawn_test_report.c:75-96` | The real rollback checkpoint checks runtime idleness, complete kernel resources and complete service resources. |
| `tests/host/elf_spawn_resource_test.c` | Existing native mutation controls cover live leaks and permitted generation/counter history. |

The full index/state model required by GRANT02 and the cross-checkpoint
resource oracle answer different questions. The former verifies the exact
transition and resulting LIFO order. The latter must prove that a
quiescent system has the same available/live resources after a different
sequence of valid operations.

## Counterexample derived from the Accepted algorithm

Consider three free slots with initial order `0 -> 1 -> 2`.
Allocate slots 0 and 1, then retire 0 and 1 in that order. LIFO produces
`1 -> 0 -> 2`; generations of slots 0 and 1 advance.

Both states have the same three available slots, no grant authority and no
participant memberships. The list is valid in each state. Literal head/
next equality nevertheless fails. Resetting that order would contradict
the accepted reuse policy; comparing only the number three could hide
missing/duplicate slots.

This counterexample follows from the reference mechanism, not captured
candidate output or a new executable prototype.

## Actual discovery

The complete fresh author plan for the local candidate stopped at
`test-qemu-elf-spawn`, exit 2, after the complete native gate and preceding
target/service gates passed. The guest reported rollback stage 2/detail 2
and `elf-spawn-checkpoint`, not a timeout.

A narrow generation-tag adaptation passed native resource-oracle controls
but did not fix the actual target checkpoint. Bounded failure-only
diagnostics then reported:

```text
runtime validation:                 true
service resource equality:          true
baseline/current active grants:     0 / 0
baseline/current free head:         0x201 / 0x241
record-0 baseline/current next:     0x43 / 0x83
changed next-target slot mask:      0xE
other normalized grant-record mask: 0
```

The displayed head/record-0 changes are coherent generation advances;
other next-target slots also changed. This proves an additional order
disagreement in the grant witness, not a successful complete integration
or a waiver of other resource checks. The corrected full gate must still
establish every predicate.

The temporary diagnostics were removed. No runtime was reset, no pass
condition was bypassed, and no implementation commit or PR was published.
Raw local evidence remains attached to the corrective task/PR record,
including the failed source tree and exact command outcomes.

## Classification and bounded correction

LIFO reuse is baseline parity. Exact kernel-owned generations and two-party
reclamation remain the already Accepted adaptations/extensions. The new
decision is a narrowly scoped compatible evidence correction: validate
each complete idle index, then compare the represented available slots,
authority and lifetime rather than allocation-order history.

Each snapshot needs `O(G+P)` cold work and one fixed membership mask, not a
new runtime algorithm, persistent history, normalization cache or sort.
All nonquiescent, transition/model, failure-atomic and hardware evidence
retains exact head/link checking.

[ADR-0130](../adr/0130-quiescent-grant-resource-equivalence.md) and
[the canonical predicate](../architecture/elf-spawn-integration.md#quiescent-grant-resource-equivalence)
own the Accepted decision, separately merged as #261. The restarted GRANT02
implementation must earn its own complete acceptance; the failed candidate
and diagnostics above remain discovery evidence, not Green.

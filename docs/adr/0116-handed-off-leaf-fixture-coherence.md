# ADR-0116: Handed-Off Leaf Fixture Coherence

- Status: Accepted
- Date: 2026-11-09
- Refines: ADR-0040, ADR-0082, ADR-0114 and ADR-0115
- Supersedes only after separate Acceptance and merge: MEM05's
  twenty-nine-path candidate envelope and the unchanged construction of
  the handed-off transferable-leaf corruption fixture
- Preserves: runtime algorithms, complete index validation, every
  retirement key, historical facts, expected errors and final gates

## Context

Corrective design #234 is separately merged as
`7b5aa73cd42a2742cea014c8773879845f78ccd4`, tree
`3b2bacb362577a1538cc4e0fc3c89bef31df6e27`.
The fresh MEM05 candidate reached actual native execution and the
user-address-space QEMU gate, then the handoff component failed at stage 8.
No Green event, recertification, retirement or implementation publication
occurred.

The [fixed-source investigation](../research/minix-selected-release-and-wired-handoff.md#mem05-c02-handed-off-corruption-fixture-conflict)
identifies an incompatible test-state construction.
`reject_corrupt_frame_owners` changes one mapped `VM_WIRED` owner to
ownerless `VM_TRANSFERABLE`, adjusts two counts, then requires complete
frame validation to succeed while address-space validation rejects the
leaf. That was a valid ledger before MEM05. It leaves the new process
index inconsistent.

ADR-0114 requires complete index/owner equality. Accepting that stale
index would weaken the runtime contract. The target fixture is outside
the current closed implementation envelope, so silently editing it would
also violate the Accepted evidence contract.

## Decision

After separate review, Acceptance and merge, apply the exact
[MEM05-C02 fixture and source contract](../architecture/selected-release-and-wired-handoff.md#mem05-c02-handed-off-corruption-fixture-coherence).
This is a test-input adaptation, not another runtime algorithm.

### Preserve the valid-ledger, invalid-leaf distinction

Keep the first foreign-process-owner corruption case unchanged.
Replace only the later transferable-leaf setup with a legitimate existing
VM ownership claim and a deliberately invalid PTE:

- reuse `stale_snapshot_frame`, already allocated and released by the
  fixture's earlier wired-snapshot control, and recheck that it is `FREE`;
- claim that one frame as `VM_TRANSFERABLE` through the existing
  privileged runtime preflight/commit;
- move the original grantee data PTE to its already empty
  `TEST_UNMAPPED_ADDRESS` slot and prove the resulting root remains valid;
- put the transferable-frame PTE in the original data slot, retaining the
  existing user read/write permissions;
- require complete runtime ownership validation to return `OK` and
  complete grantee address-space validation to return `OWNERSHIP`; and
- restore both exact PTE words, release the temporary transferable frame
  through the existing runtime preflight/commit, and prove complete
  original-state equality.

The original mapped wired frame and its index are never reclassified.
Its temporary relocation prevents an orphan from becoming a second
reason for the negative result. The positive complete-root check precedes
the single invalid transferable-leaf insertion.
There is no raw index repair, manually duplicated link algorithm,
candidate-derived expected result or exemption from complete validation.
The same adapted fixture uses existing interfaces in the fixed #232 and
actual current builds; no baseline-layout branch or production
compatibility macro is needed.

### Keep the fixture's authority and failure boundaries

The component already runs with SIE clear and returns to the kernel root
before its corruption checks. Require that protected, inactive-user-root
state explicitly. Never activate or access data through the poisoned PTE.

Prepare both PTE locations and the replacement encoding, then take the
existing complete fixture snapshot before authoritative setup.
After the temporary frame is claimed,
only its matching release may clean it up. Restore both injected PTE words
before returning from the diagnostic case, including an unexpected
diagnostic result, and report a cleanup failure rather than manufacture
success.

Successful cleanup must restore allocator, complete ownership/index,
objects, endpoints, grants, existing page tables and user data, plus the
unchanged root and interrupt state. Reuse existing fixture storage and
bounded local plans; do not add frame-capacity storage or raise a stack,
image, timeout or frame limit.

### Add one frozen dependent target owner

Add exactly:

```text
kernel/tests/memory/address_space_handoff_test_runtime.c
```

Its historical whole-file key is already retired by committed MEM04.
Do not retire it again. Its existing fast/PR/full classification
identities and all unrelated predicates remain active.

The direct 28-path closure and 82-key partition do not change.
Two dependent targets now give thirty candidate paths. Only the original
seventeen production subjects may change after Red; the other thirteen
candidate files are independently frozen through
`@mem05/frozen-candidates`. The seven assertion/generator adapters,
four unchanged literal modules, twenty-three noncandidate adapters and
eight new evidence/tool owners remain the same.

All B01-B12/B14 projections use the same thirty-path domain.
The additional target is frozen before Red and executes in the existing
handoff workflow in both phases. Keep its markers, `MEM04_HANDOFF`
version-one record, nominal/error cases, real user return and grant
syscalls, scenario ordering, complete-state comparisons and 20-second
guest deadline.

### Renew complete evidence

Preserve all 5,379 predecessor facts, twenty-two fingerprints, six claims
and 103 retirements. The 41/21/20 replacement partition, eight-fact
addition, five native products, 128 common rows and current-only guard
families remain unchanged.

Archive the unaccepted implementation and restart from the corrected
main after this design merges. Independently freeze that actual base and
every immutable input, then renew genuine native/target Red before
production changes. Neither an archived execution nor the stage-8 failure
is replacement Green.

Require current Green, B01-B12/B14 recertification, all fifty author
commands, 45 CTests, complete Python discovery, forty QEMU workflows,
four standalone images, same-run receipts and complete 44/113/ten-scope
stack coverage. B13 remains blocked. Independent implementation review
still precedes the first permanent implementation commit.

## Phase boundary

This correction contains documentation only. No runtime, test, model,
fixture, generator, build scaffold or reusable executable prototype is
authored in its pull request.
Documentation/structure and all three diff checks precede independent
design review. Clean review, Acceptance and separate merge precede the
fresh implementation.

No runtime finding, hosted-CI result or later POSIX task closes here.

## Design review disposition

Independent read-only review inspected the complete nine-document proposal,
the actual fixed target and ownership interfaces, the relevant Accepted
contracts and the current author evidence at staged tree
`dcfb694666b139d149c4583b9ce04fb734142b1e`.
All four documentation/diff commands were FRESH/exit0; implementation
native/build/QEMU/integration/stack gates were correctly N/A.
The reviewer also inspected the archived stage-8 failure as diagnosis,
not acceptance.

The reviewer reported: "No significant issues found in the reviewed
changes. No substantive design issue remains." Confidence was high.
No targeted reproduction or author-gate rerun was needed.
Acceptance records this design result; the separate merge still precedes
the fresh implementation and its complete runtime evidence.

## Alternatives

- Accept a stale index in complete validation: violates the Accepted
  independent owner/index equality.
- Change the old `OK` assertion to `INVARIANT`: loses the distinction
  between a valid ledger and an unauthorized live mapping.
- Reconstruct the index with test-local layout-dependent stores: adds
  unnecessary baseline/current layout machinery and duplicates the
  mechanism under test.
- Skip the corruption case or its full validator: removes existing
  authority coverage.
- Change the target without extending the frozen source envelope:
  violates the same closed transition that constrains production edits.
- Reuse a previous attempt: borrows evidence across changed immutable
  inputs.

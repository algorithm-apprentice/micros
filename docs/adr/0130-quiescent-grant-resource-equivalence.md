# ADR-0130: Quiescent Grant Resource Equivalence

- Status: Accepted
- Date: 2026-11-13
- Refines: ADR-0038, ADR-0050, ADR-0069, ADR-0080, ADR-0123 and ADR-0129
- Supersedes after Acceptance and separate design merge: literal free-head
  and next-free order/tag equality in the quiescent ELF-spawn resource
  witness; not live authority, ordinary index/model equality or validation

## Context

GRANT02's separately merged design #260 requires MINIX-family LIFO reuse.
Its uncommitted implementation passed native/model and direct target
checks, but complete acceptance stopped at the real post-freeze spawn
rollback resource checkpoint.

The [fixed investigation](../research/quiescent-grant-resource-evidence.md)
records the failure. Runtime validation succeeded, both snapshots had zero
active grants, and service resources agreed. Free generations and valid
free-chain order had advanced. The inherited resource oracle normalized
only the old generation field and then compared complete records literally.
Normalizing the new generation tags alone still left the legitimate order
change rejected.

ADR-0050 requires the same live resources and no leak, not rollback of
consumed identities. ADR-0129 requires exact list/model evidence, but did
not explicitly distinguish that transition evidence from equality of
quiescent resources across different valid allocation histories.
Blanket masking of the new fields would hide missing slots or stale links.

Implementation is blocked and its uncommitted branch is abandoned with the
work preserved. Correct this acceptance boundary through documentation
before resuming, rather than weakening a failing executable oracle.

## Decision

After independent review, Acceptance and separate merge, apply the
[canonical quiescent grant predicate](../architecture/elf-spawn-integration.md#quiescent-grant-resource-equivalence)
inside the existing shared ELF-spawn resource oracle.

### Validate both complete idle inventories

The exception is available only when both grant registries have zero active
grants. Each snapshot must independently prove:

- every slot is canonical FREE or QUARANTINED, with zero authority and null
  participant links;
- every free token is packable and every quarantine generation obeys the
  existing terminal rule;
- every participant root is exactly empty;
- the free head and every next-free token resolve the exact current
  generation of a FREE record;
- one bounded acyclic traversal visits every FREE slot exactly once and no
  quarantined slot.

No count, hash, copied candidate state or production validation flag
substitutes for this proof. Retain the raw captured heads/links; compare
their validated inventory meaning, rather than discarding them.

### Compare resource identity, not free-stack history

Registry initialization identity and each slot's FREE/QUARANTINED state
must agree. Available slot identities are therefore identical, not merely
equal in number. All authority remains zero and all participant indices
remain empty.

FREE generations must be valid and nonregressing. QUARANTINED generations
must be unchanged because those slots cannot be reused. Neither producer
state nor generation may be reset to manufacture equality.

After both complete inventories are validated, free-head/next-free order
and their coherent generation tags may differ. They describe the LIFO
history used to issue a later opaque token, not an outstanding grant or
lost capacity. Do not sort, rewrite or restore the live allocator to match
the checkpoint.

This is not permission to accept live grants, new quarantine, stale links,
cycles, duplicate membership, omitted free slots, changed authority, dirty
participant roots or regressing generations.

### Keep all other comparisons exact

Do not use this predicate for a live or nonquiescent grant registry.
Ordinary/observed semantic records, failure-atomic snapshots, deterministic
LIFO cases and the independent mixed model still compare every new head,
link, generation and authority field as required by ADR-0129.

All process/thread/endpoint, mapping, frame, PM, VFS, VM, TTY, filesystem,
descriptor, IPC, reply, counter and output-drain obligations remain
unchanged. This decision adds no broad mask or count-only baseline.

### Keep the implementation and proof bounded

The owner remains the existing shared kernel-resource comparator and its
native fixture. A cold `O(G+P)` walk with a fixed 64-bit membership mask
needs constant auxiliary storage. No new runtime index, epoch, history,
service operation, memory mutation, general normalization framework or
validation workflow is introduced.

The preserved blocked implementation is not accepted by this design.
After this separate merge, restart GRANT02 from current main with both
design references and the normal implementation evidence/review gates.

## Acceptance and phase

This phase is documentation only. No executable fix, harness, model,
normalizer prototype or new test belongs here.

The restarted implementation must retain a native counterexample for two
different valid LIFO histories with the same idle slot inventory. It must
also reject stale head/next generations, cycles, duplicate/missing free
membership, linked quarantine, changed FREE/QUARANTINED state, nonzero
authority, dirty participant links/roots, live grants and generation
regression. Preserve all existing resource-oracle negative controls.

Actual `test-qemu-elf-spawn` must pass its existing post-freeze rollback,
normal exit/wait and final complete resource checkpoints without altering
the scenario, parent/child, deadlines, allocator behavior or pass marker.
Its observed stack scope and the unchanged complete author plan remain
required. Native resource equality does not replace target composition.

Independent corrective-design review precedes the first design commit.
Fresh independent implementation review still precedes the first
permanent restarted GRANT02 implementation commit.

## Alternatives

- Ignore all grant metadata: can hide an orphan free slot or stale root.
- Require the same free-chain order: rejects the newly required legitimate
  LIFO history without identifying a leak.
- Restore the producer's old order/generations: changes the runtime
  mechanism or resurrects consumed identities to satisfy a witness.
- Record a complete allocation history or epoch: unnecessary persistent
  state for proving an idle resource inventory.
- Skip the composed rollback or accept equal counts: loses required
  cross-service resource evidence.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
seven-document corrective proposal, relevant Accepted authority, fixed
source/failure evidence and exact documentation-phase author results in
the isolated design worktree. It reported no substantive design issue
remaining, with High confidence. Targeted reproduction: None.

Acceptance records that design result, not a passing implementation or
composed checkpoint. The separate corrective design merge still precedes
the restarted GRANT02 implementation.

## References

- [Canonical resource predicate](../architecture/elf-spawn-integration.md#quiescent-grant-resource-equivalence)
- [Fixed investigation](../research/quiescent-grant-resource-evidence.md)
- [Grant index/lifetime contract](../architecture/grant-free-lists-and-targeted-cancellation.md)
- [Current validation](../architecture/current-contract-validation.md)

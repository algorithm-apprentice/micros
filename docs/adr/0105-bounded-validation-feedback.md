# ADR-0105: Bounded Validation Feedback

- Status: Accepted
- Date: 2026-11-06
- Refines: ADR-0031, ADR-0032, ADR-0034, ADR-0064, ADR-0074, ADR-0075,
  ADR-0080, ADR-0082, ADR-0103 and ADR-0104
- Supersedes in part, only after separate Acceptance and merge:
  the identical full Python discovery in both native tiers; per-question
  reconstruction of already verified source/evidence authority inside an
  explicitly bounded validation run; and end-only workflow log replay.
- Preserves: final acceptance coverage, source/oracle independence, actual
  Red/Green, granular retirement, tool/input closure and target evidence

## Context

MEM03's uncommitted author gate passed 43 CTests in 214.22 seconds, then spent
hours in Python source/evidence checks. A preceding complete Python run took
18,841.945 seconds. The
[measured investigation](../research/validation-feedback-cost.md) identifies
a concrete defect rather than inferring a deadlock from missing terminal
output.

One selected-file identity query took approximately 86 seconds. It captured
the complete author context twice and validated the complete journal twice,
including six execution-log replays. A single scratch-owner test makes
fifteen such affected queries while checking three declarations in nineteen
source files.

Both native tiers currently run the same full Python discovery. The
controller also withholds workflow output until completion. More CPU time,
longer timeouts or additional agents would not remove this repeated work.

The user prioritized correcting validation before more runtime development.
The exact MEM03 candidate and evidence are preserved; its interrupted full
run is not acceptance.

## Decision

After separate review, Acceptance and merge, implement the
[bounded validation feedback contract](../architecture/validation-feedback.md).

### Keep the appropriate proof layer

Portable behavior starts with unit/property/model tests. Source checks remain
narrow structural, ABI, artifact or historical transport evidence; source
equality is not functional or MINIX algorithmic correctness.

Hardware behavior still requires QEMU component evidence. Composed VM, PM,
VFS, filesystem and terminal protocols still require focused integration
evidence. The fast loop is not a reason to remove either layer from the
appropriate PR or milestone gate.

### Verify one bounded source view, not one proof per lookup

Add one explicitly owned, process-local checked source view for a host
validation invocation or fixture-generation invocation.

Its owner establishes the exact source snapshot, complete current inventory,
committed authority and any required actual terminal author journal before
making verified source decisions available. The same journal is semantically
validated once, not again merely because its reader already validated it.

Within the view, source consumers use the immutable captured bytes and
verified per-key decision. They do not reconstruct the inventory, probe the
toolchain or replay native records for every lookup.

This is not a persistent cache or a live authority lease. It cannot authorize
an author event, execute an operation, approve a merge or publish a receipt.
The owning invocation must recheck the complete source, tool, environment,
selector and record/log identities before reporting overall success.
Mutation invalidates the run; no previously verified result may hide it.

Outside an owned view, the ordinary cold authority APIs retain their full
checks. Provisional author transactions remain separate. Test fixtures that
model altered authority use explicit isolated/cold contexts, not the live
repository's already verified decision.

### Make fast and complete native feedback different

Keep all existing native CTests and complete Python discovery in `test-unit`.
The complete gate must still execute the entire current discovered set.

`test-unit-fast` keeps its nonslow native CTests and executes a small closed
set of cheap Python unit suites. Expensive compilation, source-history,
complete-authority and stack-artifact work remains in the full tier or a
focused explicit invocation, not the ordinary fast loop.

The pending MEM03 six-product orchestration belongs to the slow/full tier.
Its small launcher regression remains directly runnable. This amends only
its placement in both native tiers, not its required full acceptance.

No test is relabeled as passed because it was not selected. Fast execution is
development feedback, never a substitute for the minimum PR gate.
ADR-0032's required PR/full selection and unknown/shared-input escalation
remain unchanged.

### Show actual progress without weakening log publication

The Python runner reports each selected test's start, completion and observed
duration. It retains standard unittest outcomes and a bounded slow-test
summary. These timings are diagnostics, not flaky pass/fail thresholds.

The workflow executor streams output while writing the same raw bytes to its
owned attempt log. It does not replay those bytes again afterward.
Only actual exit zero, complete log finalization and unchanged postflight can
publish a successful receipt or final candidate.

### Preserve historical facts through bounded build admission

The two native Python invocations receive exact independently specified
runner replacements. A finite inverse recovers only those two original
invocations before existing historical recipe comparisons.

That inverse is not proof of the new runner's behavior. Independent unit and
actual selection evidence must prove the full discovered set, fast subset,
runtime prefix, failure propagation and complete log semantics. Every
outside CMake byte, native artifact declaration and unrelated historical
fact remains subject to its existing contract.

No old hash is recomputed from the candidate. No runtime source identity
retires in this tooling outcome.

## Phase and restart boundary

This design contains documentation only. Its paired implementation remains
pending separate Acceptance and merge.

The implementation operates on the committed validation foundations before
MEM03 restarts. The archived MEM03 tree is diagnostic reference only.
Its fresh restart must adopt the new checked source-view interface and slow
orchestration placement, independently bind the actual new base membership,
and renew its actual Red/Green, recertification and final acceptance.
The fixed #214 runtime, forty retirement keys, six frozen adapters and
independent work expectations remain unchanged.

## Acceptance

Require deterministic evidence that repeated local queries perform no repeated
full proof inside one successfully opened view, while every changed source,
tool, environment, journal and scope boundary rejects final success.
Cold queries and journal-free committed authority must remain valid.

Prove exact full-suite membership, explicit fast selection, no silent skip,
failure/exception propagation, pre-exec Python guards, live progress,
byte-identical retained logs and unchanged atomic receipt publication.
Record actual before/after costs without substituting timing for work or
correctness evidence.

Run the applicable complete native, planner, recipe and full author gates.
Preserve all current QEMU/image/stack coverage and blocked B13 admission.
Independent implementation review precedes the first permanent commit.
Publication and completed-ref cleanup follow the existing exact-tree rules.

## Non-goals

No kernel or service behavior, new runtime policy, lower capacity, larger
timeout/stack/image budget, test-count reduction, broad source-gate deletion,
cross-candidate/process/worktree reuse, unchecked global success flag,
parallel product development, hosted-CI acceptance or node-Q implementation.

## Design review disposition

Independent read-only GPT-6.1 Sol review inspected the complete fifteen-file
design, relevant canonical/source context and the exact author evidence at
staged tree `693974c6284abaf5c9bd2e3f7a4091d5e8b1ff66`.
All four documentation-phase commands were FRESH/exit0. Implementation
native, QEMU, build and stack gates were correctly N/A.

The reviewer reported: "No significant issues found in the reviewed changes.
No substantive design issue remains." Confidence was high; no targeted
reproduction or author-gate rerun was needed.

Acceptance records this design result, not an implemented source view,
runner, measured speedup or completed MEM03 correction. The separate design
merge still precedes the dependent implementation.

## Alternatives

- Accept five-hour feedback: contradicts a usable development loop.
- Add workers first: distributes redundant work without correcting it.
- Trust an unchecked cached boolean: can conceal changed or invalid evidence.
- Remove all source checks: loses useful structural and provenance boundaries.
- Use only host unit tests: cannot prove privilege, MMU or cross-service behavior.
- Run all Python checks in the fast tier: preserves the measured feedback defect.
- Show provisional output as final success: confuses progress with acceptance.

## References

- [Measured cost and test-layer study](../research/validation-feedback-cost.md)
- [Exact bounded feedback contract](../architecture/validation-feedback.md)
- [Tiered native validation](0031-tiered-native-validation.md)
- [Authority transition](0082-evidence-authority-transition.md)
- [Exact command receipts](0064-fail-closed-validation-evidence-reuse.md)
- [Author evidence before review](0034-author-validation-before-review.md)

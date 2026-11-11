# ADR-0123: Current-Contract Validation

- Status: Accepted
- Date: 2026-11-11
- Refines: ADR-0031, ADR-0032, ADR-0033, ADR-0034, ADR-0051, ADR-0064,
  ADR-0065, ADR-0074, ADR-0080, ADR-0082, ADR-0083, ADR-0084 and ADR-0105
- Supersedes after the phase barriers below: historical source-retirement
  machinery as an ordinary development prerequisite; mandatory metadata-only
  prerequisite pairs; repeated correction recertification before the complete
  author plan; and automatic receipt eligibility as a condition of ordinary
  fresh validation

## Context

OBJ02 is independently reviewed and merged as #246. Its bounded scheduler
plan and protected context reuse are complete. The user next prioritized
removing historical source-freeze chains, metadata prerequisite PRs and
duplicate validation, while retaining necessary behavioral, hardware and
cross-service evidence.

The [fixed-current investigation](../research/current-validation-overhead.md)
records the remaining cost. Five native evidence orchestrators account for
950.72 of 1,100.46 native-test seconds. Complete Python discovery takes
944.539 seconds. These are aggregate measurements, not an attribution of
every second to bookkeeping.

The implementation explains a concrete avoidable component: ordinary source
reads establish a cumulative nine-provider retirement proof; current tests
also reconstruct old source, execute historical products, and repeatedly
prove earlier claims. The catalog has 6,789 facts in twenty-nine families.
Naming omitted assertions has itself required nine metadata prerequisite
design/implementation pairs.

OBJ02 also passed native tests and its smoke guest in an earlier final run,
then failed optional-receipt postflight. Its mandatory same-producer rule
required another complete fresh run without a repository-content change.
That is not another independent experiment.

ADR-0082 correctly prohibited using historical source equality as MINIX
correctness and prohibited candidate-derived behavioral expectations. Its
retirement machinery nevertheless became a permanent dependency on those
same historical implementations. The appropriate replacement is current
contracts and independent tests, not a faster way to bless every old hash.

## Decision

Implement the
[current-contract validation contract](../architecture/current-contract-validation.md)
as one sequential tooling outcome. It changes no operating-system algorithm,
authority, ABI, resource budget or runtime finding ownership.

### Finish historical transport at its original outcome

Completed decomposition source/body/AST/token identities and their inverse
chains remain historical evidence in Git and their original PRs. They cease
to be ordinary current acceptance requirements.

Remove their live retirement catalogs, cumulative predecessor proofs,
per-assertion binding tables, author-event journals and obsolete author
controllers. Do not retain a permissive compatibility shim, blanket
`identity_required = false`, skipped historical test class, or alternate
success-shaped gate.

Mixed tests must be separated by what they prove. Preserve current ownership,
private dependencies, ABI/artifact policy, independent reference models,
primitive-observation integrity and actual stack/source-machine contracts.
A source check that qualifies an actual indirect edge or an observation
inside a loop is not obsolete merely because it reads source.

Current consumers read actual current source. They do not reconstruct an
ancestor or substitute an old body. A second implementation spelled in
Python strings is not an ordinary behavioral oracle. Narrow proof-critical
bindings required by current observation or machine contracts remain valid.

### Keep independent current evidence

Retain every current native semantic/work case, model transition count,
ordinary/observed agreement check, malformed-input control and required
target scenario. Current expected results and bounds continue to derive
from Accepted decisions, independent models or fixed reviewed reference
data, never edited-candidate output.

Regular validation does not compile or execute old implementations to
rediscover their already documented divergence. If an existing current
oracle consumes fixed-reference results, preserve the necessary reference
data with its provenance instead of retaining the historical execution
pipeline. This is not permission to loosen a bound or replace a missing
model with captured candidate output.

Retain current generated-source, executable, collector-linkage and input-drift
checks. A lightweight invocation-owned source snapshot replaces the
retirement-authority view. It owns stable current bytes and closing checks,
not proof that earlier PRs were correct.

### Keep metadata with its actual change

Updating test ownership, case coverage, primitive sites, source sets, recipe
dependencies or equivalent closed-profile bindings belongs in the design
and implementation of the outcome that needs it. It does not create another
prerequisite pair solely because a literal or assertion lacks a catalog key.

A genuine change to authority, behavior, lifecycle, work bound, ABI or
acceptance still requires an Accepted design. A discovered design defect
still follows ADR-0033's corrective-design barrier. This decision removes
bookkeeping prerequisites, not design review.

### Execute one sufficient final plan

Actual Red and focused Green remain mandatory at the correct layer. The
current planner remains the authority for the final PR or milestone gate.
The same native or QEMU command is not rerun merely to populate a separate
B-unit recertification transaction. Its actual current result supplies the
corresponding affected-unit evidence.

Required coverage, unknown-input escalation, distinct target configurations,
complete native discovery and blocked future admissions remain unchanged.
After a correction, ADR-0034's explicit ownership-based carry-forward remains
available; uncertain ownership requires rerunning the affected check.

Ordinary default and explicit `--fresh` execution run tests and retain exact
source/log results without attempting reusable-profile collection or
publishing reusable receipts. They still invalidate an older receipt before
a fresh attempt and reject execution, source-drift or log-publication failure.

Explicit `--reuse`, `--producer` and `--record-workflow` retain their closed
input, tool, environment, artifact and failure rules. They remain optional
features, not universal runtime acceptance ceremonies. Testing those features
requires actual profile evidence when their implementation changes.

### Preserve the important boundaries

Do not remove QEMU evidence for traps, privilege, MMU, fences, interrupts or
context switching. Do not remove integration evidence for VM/PM/VFS/TTY/RAMFS
composition, grants, rollback, exit/wait or resource release.

Retain compiler-derived stack closure, finite fatal/indirect qualifications,
metadata passivity, observed producer ownership, existing budgets and the
explicit B13 block. Unit tests do not substitute for these requirements.

## Supersession boundary

This decision replaces the operational catalog/retirement/event protocol of
ADR-0082, the historical-transport portion of ADR-0083, and the cumulative
authority-proof portion of ADR-0105. Their independence, observation,
snapshot-consistency and failure principles remain.

Historical transport requirements introduced by completed source-decomposition
and evidence-binding outcomes no longer freeze future source spelling.
Their current architectural, behavioral and artifact obligations remain.
The detailed disposition and explicit retained families are in the canonical
contract; hashes used for artifact/input identity or a genuinely closed
execution profile are not blanket-deleted.

Completed correction records under ADR-0090 through ADR-0122 remain true
historical records. Their runtime contracts and finding closures are not
reopened. Their old per-correction author ceremonies are not inherited by
each new task.

ADR-0064/0074's explicit receipt modes remain intact. Only ordinary fresh
execution is separated from opportunistic receipt production. ADR-0032's
planner and ADR-0033/0034's phase, author-evidence and independent-review
barriers remain authoritative.

## Phase and acceptance

This Accepted design contains documentation only. Complete independent design
review is clean. Its paired implementation stays pending the separate
design merge.

After that design merge, this decision directly authorizes its implementation
to remove the superseded machinery. Requiring an EV01 retirement transaction
to remove EV01 would recreate the circular prerequisite being corrected.
No runtime finding is closed by that authorization.

Implementation must preserve the complete current evidence mapping, prove
the new source-snapshot and fresh-mode failure boundaries, remove historical
proof/execution dependencies from ordinary validation, and report actual
before/after cost. It must not hide unported tests through runner selection.

Because this implementation changes shared validation and receipt dispatch,
its final author gate still covers all current workflows and verifies the
three supported explicit receipt profiles. One complete producer may supply
that final gate; a second full run is not required merely to exercise fresh
dispatch already covered by focused orchestration tests.

Independent implementation review precedes the first permanent outcome
commit. The exact reviewed tree is published and completed task refs are
cleaned. No implementation or speedup is claimed by this design.

## Non-goals

No runtime audit or correction, new kernel/service behavior, shortened model,
merged QEMU scenario, compiler cache, remote cache, parallel task development,
general C/CMake verifier, migration framework, larger timeout/budget, hosted-CI
acceptance, node Q or POSIX-conformance claim.

## Design review disposition

Independent read-only GPT-6.1 Sol review inspected all eleven proposed
documents, the relevant current implementation and Accepted contracts, the
fixed OBJ02 records and the exact author evidence at staged tree
`3ad3801d0872be10b56115ecd8110c218dd4aa1d`.
All four documentation-phase commands were FRESH/exit0; executable gates
were correctly N/A.

The reviewer reported: "No significant issues found in the reviewed changes.
No substantive design issue remains." No targeted reproduction, author-gate
rerun, artifact modification or delegated review was performed.
Acceptance records that design result, not an implemented replacement or
speedup. The separate design merge still precedes implementation.

## Alternatives

- Cache the cumulative proof more aggressively: preserves the metadata and
  historical-source coupling that blocks later changes.
- Keep all old and new implementations in every current gate: repeats
  historical evidence and requires ABI compatibility machinery.
- Recompute old hashes from every candidate: provides no independent
  behavioral oracle and retains the bookkeeping cost.
- Delete all source or integration checks: discards real current contracts.
- Require only a fast unit run before merge: loses necessary native-model,
  hardware, cross-service and stack coverage.
- Add another per-assertion registry for the replacement: recreates the
  original problem under a different name.

## References

- [Current-contract validation](../architecture/current-contract-validation.md)
- [Measured current overhead](../research/current-validation-overhead.md)
- [Testing strategy](../testing-strategy.md)
- [AI-native workflow](../development/ai-native-workflow.md)

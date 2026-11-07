# ADR-0108: QEMU Accepted Transcript Reporting

- Status: Accepted
- Date: 2026-11-07
- Refines: ADR-0064, ADR-0072, ADR-0075, ADR-0080, ADR-0082, ADR-0105
  and ADR-0107
- Supersedes only: ADR-0072's no-new-option/output restriction for the
  explicitly requested, single-run diagnostic output below

## Context

MEM04 design #222 is merged. Its uncommitted implementation obtained real
native and target Red, but could not complete Green through the accepted
target-output boundary. The canonical user-address-space workflow passed
without exposing its successful guest observations to the independent host
oracle.

`run_qemu()` already captures the complete decoded result. On accepted
success, `print_tap_result()` returns after four TAP summary lines and does
not print that result's output. The failure branch prints diagnostic lines.
An aggregate pass therefore cannot supply MEM04's independent return-value,
SATP and state witnesses.

The [fixed-source investigation](../research/minix-qemu-runner-source-organization.md#mem04-successful-output-boundary)
also identifies the original runner/interpreter requirement in stack
evidence and the existing source/recipe projection chain. A new wrapper,
second guest execution, relaxed witness parser or candidate-derived hash
would not repair the accepted boundary.

## Decision

Implement a separate evidence prerequisite, `MEM04-C01`, according to the
[QEMU reporting contract](../architecture/qemu-runner-source-organization.md#mem04-c01-accepted-transcript-reporting).
It has its own sequential design and implementation pull requests.
MEM04 remains blocked until both are separately reviewed and merged.

### Opt-in output from the already accepted result

Add `--report-transcript`, default false, to the existing runner.
It is restricted to exactly one resolved memory size, including the default
`128M`; reject its use with multiple sizes before any QEMU execution.
Existing invocations without the option keep their output, acceptance,
multi-memory behavior and exit status unchanged.

For an accepted single run, print the existing TAP success first, then the
captured decoded guest text with one `# ` prefix per line. Preserve line
endings and an unterminated final fragment; reporting must not manufacture a
terminated observation. Empty output adds nothing.

This is diagnostic text, not a byte-for-byte raw serial archive or a new
acceptance certificate. It can describe an accepted expected panic as well
as an accepted pass. Classification, complete legacy acceptance, serial-input
gating and failure output remain unchanged. A rejected run follows only its
original reporting path, without duplicate output.

Only the existing `test-qemu-user-address-space` and
`test-qemu-address-space-handoff` recipes enable the option. Their original
runner path, `-E -S -B` interpreter prefix, guest argv, markers, requirements,
dependencies and respective 8/20-second deadlines remain unchanged.

### Preserve independent historical authority

Use an independently specified finite source/option/recipe inverse for the
exact addition. Apply it before the existing diagnostic-history source
projection and before MEM03's complete predecessor-recipe comparison.
Its separate direct forward-comparison test projects only the new recipe
addition from the observed current text; its fixed expected recipe and
raw-current inverse/corruption checks remain unchanged.
Keep all original expected hashes, source-node/import inventories, parser
order expectations and complete recipe comparisons.

The inverse is not evidence that the option works. Independent observable
unit tests and both actual canonical QEMU workflows establish that behavior.
No historical identity is retired, no new authority family is needed, and
all 4,774 predecessor facts, nineteen fingerprints, five claims and seventy
retirements remain unchanged.

The exact ten-member production runner family and its loader/default-input
closure remain intact. The new projection helper is test/evidence code,
never a dependency of the executing QEMU runner.

### Restart MEM04, do not resume its old evidence

The archived candidate is reference material only. After this prerequisite
merges, restart MEM04 from current `main`, independently freeze the new base
membership and reporting inputs, then renew actual Red, Green,
recertification and final acceptance.

Its sixteen production subjects, 33-key retirement plan, eight evidence
owners and runtime semantics do not expand. Its target oracle must inspect
the fresh successful diagnostic lines, not merely the TAP pass. Within each
phase, the same owned workflow execution supplies both its oracle and
matching B02/B05 result; do not execute QEMU again for a second consumer.

## Acceptance and phase barrier

Design is documentation only. No executable prototype, test, harness or
build edit belongs to this phase.

Implementation starts with the missing-output regression against the
unchanged CLI orchestration, not a failure caused only by an unknown option.
Prove one execution, unchanged default/failed output, complete legacy
acceptance, parser failure before execution for the unsupported multi-memory
form, and exact decoded-line publication including a partial final line.

Prove complete old source/recipe reconstruction, malformed-extension
rejection, unchanged current inventory and stack recipe admission. Run the
two actual target workflows and the unchanged complete fresh author plan
with current-candidate receipts. Independent implementation review precedes
the first permanent commit; exact-tree publication and protected completed-ref
cleanup remain required.

## Non-goals

No runtime, target-fixture, guest-acceptance/parser-rule, subprocess, serial-input,
timeout, capacity, stack budget, receipt, source-view, generic logging,
transcript-file, multi-run export, hosted-CI or node-Q change. No wrapper,
second execution, automatic rebaseline or reuse of the archived MEM04
journal.

## Design review disposition

Initial independent read-only review found one substantive omission: the
MEM03 test's direct forward recipe comparison runs before the proposed
inverse entry point. The contract now includes that one test adapter and
preserves its independently pinned expected recipe plus the original
raw-current inverse/corruption checks. No executable source changed.
Complete independent correction review inspected all nine documents and the
current author evidence at staged tree
`3e91b7813d89e409ea34759651de4b94522189cc`.
All four documentation/diff commands were FRESH/exit0, bound to that exact
candidate with authoritative/final-candidate results.
The reviewer reported: "No substantive design issue remains. The previous
Medium finding is resolved." No targeted reproduction or gate rerun was
needed.

Acceptance records that design result, not an implemented reporting option
or completed MEM04 evidence. The separate design merge still precedes the
pending prerequisite implementation.

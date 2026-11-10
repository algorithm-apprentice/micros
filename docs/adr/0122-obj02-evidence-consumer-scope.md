# ADR-0122: Complete OBJ02 Evidence Consumer Scope

- Status: Accepted
- Date: 2026-11-11
- Refines: ADR-0082, ADR-0083, ADR-0084, ADR-0105, ADR-0108 and ADR-0121
- Supersedes only after separate Acceptance and merge: ADR-0121's incomplete
  immutable historical-consumer list and ADR-0108's two-recipe enrollment
  boundary for the one explicitly owned scheduler reporting use below

## Context

OBJ02 design #244 is separately merged. Its uncommitted implementation
executed genuine native and target Red, then current Green. It measured a
220-byte return plan instead of 1,344 bytes, no static workspace instead of
4,224 bytes, no queue shifts, and the required target validation counts
`1/2/1`. Those results are not final implementation acceptance.

The complete B14 gate retained the earlier memory/object executions, then
rejected historical source and recipe consumers. The
[fixed-source investigation](../research/minix-common-scheduler-return.md#obj02-c01-observed-consumer-scope-conflicts)
records the exact failures. Three required existing test owners are absent
from the Accepted immutable-adapter list. Editing them silently, deleting
their checks, hiding the new reporting token, or automatically treating old
code as current would bypass the design boundary.

The candidate is preserved without an implementation commit, pull request,
terminal recertification event or retirement. OBJ02 implementation is blocked;
this documentation-only correction is the sole active outcome.

## Decision

After independent review, Acceptance and separate merge, restart OBJ02 under
the [corrected consumer contract](../architecture/common-scheduler-return.md#obj02-c01-complete-evidence-consumer-scope).
The scheduler algorithm, context proof, fixed runtime/MINIX references,
finding ownership, 58-key selection and 28-path direct closure do not change.

### Report the existing scheduler execution

Enable the existing `--report-transcript` option only in the current
`test-qemu-scheduler` recipe, immediately after its original runner path.
Its default single memory size satisfies ADR-0108's existing parser contract.
The same accepted workflow execution supplies the complete guest captures
and original scheduler/IPC-return/TTY-trap result.

Preserve the runner, interpreter prefix, guest arguments, markers,
requirements, dependencies and eight-second deadline. Do not execute a
second guest for reporting, add an output wrapper, change shared parser or
collector code, or substitute TAP success for actual work observations.

### Keep the original two-recipe tests historical

Add these exact immutable test adapters:

- `tests/host/test_selected_ownership.py`;
- `tests/host/test_qemu_transcript.py`; and
- `tests/host/test_selected_release_handoff.py`.

The first two project only the independently declared complete OBJ02
recipe extension from observed current CMake before the original historical
comparisons. The old MEM03 forward builder and its fixed expected recipe
remain unchanged. The original two reporting targets, exact token count,
source values, ordering and malformed-extension controls remain unchanged
within that historical view.

The OBJ02-owned inverse must first validate its exact native/target/reporting
addition. Missing, duplicate, relocated or changed additions reject rather
than becoming an absent extension. Preserve every outside byte for the
retained comparison. Independently test the current scheduler enrollment and
actual successful output; the inverse alone does not establish reporting.

Keep `tests/host/qemu_transcript_source.py` unchanged. Its two-token inverse
continues to govern its original boundary; do not broaden it globally to
accept arbitrary reporting consumers.

### Keep historical and current source contexts distinct

The third adapter extends only MEM05's existing explicit cold historical
fixture context so it cannot borrow OBJ02's newer-current authority.
Retain the complete original input files, definition/site sets, ordinary
coverage and observation round trip.

Do not infer historical mode from matching old bytes inside a live current
API. Current admission must still reject the old planner, apply and
selected-context loader when a current contract is required. The fixture
override is test-local, unwinds on every exit and supplies no author event
or production permission.

### Preserve the existing observation and protocol boundaries

The scheduler finish adapter must leave the existing stack-observer include
in its exact original second-line slot and leave the original hook/primitive
placement intact. Place the new report include after it; keep the work report
after semantic/cleanup checks and before original success reporting.
Neither stack-source projection nor a stack budget changes.

Already authorized adapters also keep their complete obligations:

- current-catalog assertions account for independently declared new families
  while preserving all 6,327 predecessor records and twenty-seven fingerprints;
- the isolated OBJ01 replay model explicitly carries its plan side-record
  member, while actual acceptance requires every real case observation;
- the complete affected-source B14 selection retains the historical reporting
  and MEM05 fixture tests rather than narrowing selection to hide failures.

These are corrected input/consumer bindings, not additional runtime behavior,
new source identities to retire, or permission to normalize an observed result.

## Restart and acceptance

The fresh implementation starts from the separately merged correction and
independently freezes its actual new base membership. Retain the fixed #243
runtime and all original oracle expectations; do not derive expected source
contracts from the archived edited runtime.

The archived code and logs are reference material only. Renew actual native
and QEMU Red, current Green and all thirteen recertification units with the
corrected immutable owners. Prove ordinary and observed native agreement,
original target scenarios, strict raw plan observations, current/historical
source isolation, unchanged stack hooks, exact recipe inverses and complete
predecessor conservation.

Retain terminal source-view construction, native no-work preparation, all
fifty fresh author commands, three same-run profiles, complete current stack
coverage, independent implementation review before the first completed-claim
commit, exact publication trees and protected completed-ref cleanup.

## Non-goals

No runtime algorithm or authority change, additional retirement, shared
reporting/parser/collector modification, timeout or capacity increase,
historical rebaseline, omitted gate, hosted-CI acceptance or node-Q work.
The user-requested broader workflow simplification remains after OBJ02;
this correction does not start it.

This design contains documentation only. No executable fixture, model,
adapter, helper, prototype or build change precedes its independent review,
Acceptance and separate merge.

## Design review disposition

Independent read-only review inspected the complete nine-document proposal,
the relevant Accepted contracts, the actual historical consumers and the
author's evidence at staged tree
`d6def32e212adb8723db2b7fe927597e98f9dd3f`.
All four documentation-phase commands were FRESH/exit0; implementation
native, build, QEMU, integration and stack gates were correctly N/A.

The reviewer reported: "No significant issues found in the reviewed changes.
No substantive design issue remains."
Acceptance records that design result, not a runtime finding closure or
permission to reuse the archived candidate's execution. The separate design
merge still precedes the fresh implementation and its renewed full evidence.

## References

- [OBJ02 runtime decision](0121-bounded-common-scheduler-return.md)
- [Exact corrected consumer contract](../architecture/common-scheduler-return.md#obj02-c01-complete-evidence-consumer-scope)
- [Observed B14 conflicts](../research/minix-common-scheduler-return.md#obj02-c01-observed-consumer-scope-conflicts)
- [Accepted transcript reporting](0108-qemu-accepted-transcript-reporting.md)
- [Checked stack evidence](0084-checked-stack-bound-evidence.md)

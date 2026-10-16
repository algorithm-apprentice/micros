# ADR-0071: TTY Model Validation Reference Dependency

- Status: Accepted
- Date: 2026-10-16
- Refines: ADR-0033, ADR-0047, and ADR-0070
- Supersedes: ADR-0070's source-contract graph only where it omits the
  model-validation dependency on the reference owner.

## Context

TTY design #141 is separately merged as
`0cef6e82fa855af4448ce6aeee29c987e6567eed`. Implementation #142 exposed a
contradiction between its exact original-definition partition and its private
dependency graph. The implementation is blocked, its pull request is closed,
and its patch is retained only as non-authoritative reference under ADR-0033.

At the unchanged reviewed source base `ac94a211`, the complete
`model_validate` function ends with
`!model->bus.expect_lf || reference_output_pending(reference)`
(`tests/host/tty_model_test.c:1544-1547`). The callee is the original pure
reference-output predicate at lines 248-253. ADR-0070 places these functions
in the validation and reference owners respectively, but its graph lists
only validation -> common.

The complete-body contract and the exact owner partition therefore require
an edge omitted by the graph. A strict graph check rejected the actual
extraction even though its immutable body/type checks and native model trace
remained unchanged. Passing the behavioral model cannot waive the design
contradiction.

## Decision

After independent review, acceptance and separate merge, the private model
graph permits:

```text
model validation -> common, reference
```

The reference edge exists solely for the original final
`reference_output_pending` observation. The predicate remains in the
reference owner, with its complete original type and body and its already
required private declaration. Validation does not call production-private
helpers to predict reference state. Reference does not depend on validation;
the graph remains acyclic.

This is the only changed dependency permission. Every other ADR-0070 owner,
private direction, state/lifetime rule, required declaration and acceptance
contract remains authoritative. The 26 C owners, five private headers/sets,
eight operands, 156 original definitions, 116 moves, fifty original private
exports and eight ordered C/Python dispositions do not change.

The fixed MINIX trace and all ADR-0047 adaptations remain unchanged. This is
a source-organization correction, not a terminal, device or model-behavior
change and not another runtime-DAG node.

## Implementation restart and acceptance

The corrective design is the only active task. It contains documentation
only; the paired implementation remains blocked until independent design
review, acceptance and separate merge.

After merge, create a fresh implementation branch from current `main`.
Reintroduce still-valid behavior through structural Red, exact immutable
body/type/data restoration and narrow green slices, not by wholesale
cherry-picking the abandoned branch. Add the necessary reference edge to the
actual graph check without weakening any other direction.

Retain the one automatic model, callback/self-pointer bindings, seed,
twenty scripted steps without outer draws, 8,192-step loop, per-write draws,
final drain, trace/hash/counters and complete first-failure comparison order.
The exact final output-pending observation must remain after the existing
CR/LF check.

All original native/model/real-target, full current author, actual bounded
original-producer reuse, independent implementation review and exact
merge/tree/branch-cleanup gates remain required. A historical result from
the abandoned candidate is not acceptance evidence for the restarted one.

## Non-goals and rejected alternatives

Do not move or duplicate the predicate, replace it with an inline expression,
skip the final comparison, add a production dependency, change state or
random draws, relax the graph generally, alter validation/reuse algorithms
or add executable design artifacts. Each would evade the exact original
behavior or widen a correction that needs only one existing private edge.

## Design review disposition

Independent complete read-only design review inspected the six-document
proposal, original predicate and final validator call, relevant Accepted
authority, unchanged acceptance contracts and exact current four-command
author evidence. It reported: "No significant issues found in the reviewed
changes." Static evidence establishes the necessary acyclic edge; no targeted
reproduction or validation rerun was needed. No substantive issue remains.

Acceptance records that clean review result only. The implementation remains
blocked until this corrective design separately merges.

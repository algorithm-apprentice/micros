# ADR-0074: Fail-Fast Validation Producer Readiness

- Status: Accepted
- Date: 2026-10-17
- Refines: ADR-0032, ADR-0034, ADR-0064, ADR-0065, and ADR-0073

## Context

ADR-0064 deliberately requires a complete eligible profile before and after a
fresh workflow may publish a reusable receipt. If preflight is unavailable,
the workflow may still run fresh, but `_postflight` returns no receipt because
there is no closed baseline against which to prove unchanged context and
inputs. That fail-closed rule is correct.

The final kernel-directory candidate exposed a separate orchestration defect.
Its first complete exact-candidate author run passed all 47 commands in
996.79 seconds, but smoke and the standalone TTY image lacked complete output
identities before execution. The run therefore published only the native
receipt. A later ten-command consumer spent another 446.81 seconds discovering
that only one of the three required workflows could be reused. After a
35.77-second read-only readiness probe confirmed 869 native, 193 smoke, and
22 TTY-image outputs, an unchanged second complete producer and bounded
consumer supplied the required evidence.

The avoidable discovery cost was 1,443.60 seconds, or 24 minutes 3.60 seconds.
No additional source change or independent behavioral claim justified that
delay. The planner knew every required command before execution, and the
context collector could already determine profile eligibility, but no
interface expressed the stronger intent: this fresh run must be capable of
publishing every selected reusable receipt.

The fixed MINIX validation-driver baseline keeps named tests, explicit
selection, per-test results, and failure when selected evidence is
unsatisfied. It provides no receipt mechanism. Refusing an impossible
producer before launching its tests is therefore a compatible
development-only extension, not a MINIX behavior change or permission to
skip a selected test.

## Decision

Extend the existing planner/execution interface with one explicit
fail-fast producer mode. Preserve all coverage, receipt identity, preflight,
postflight, fresh, reuse, gate-only, candidate, and review rules.

### Interface and authority

The dependent implementation adds `--producer`, valid only with
authoritative `--execute`. It is mutually exclusive with `--fresh`,
`--reuse`, and `--record-workflow`.

The planner still:

1. captures the candidate before discovery;
2. validates the canonical inventories;
3. unions committed, staged, worktree, untracked, and explicit paths;
4. computes the complete required command sequence; and
5. rechecks that the candidate did not change.

Producer mode cannot add, remove, reorder, replace, or satisfy a command. Its
selected reusable set is the ordered intersection of that already computed
plan with the reviewed reusable-profile declarations. At least one reusable
workflow must be selected; otherwise producer mode is an error rather than a
vacuous success.

Default fresh execution, explicit `--fresh`, opt-in `--reuse`,
`--record-workflow`, plan-only output, and explicit full-tier behavior retain
their existing meanings.

### Readiness barrier

Before opening the evidence publisher or launching any canonical validation
command, producer mode:

1. requires the exact clean staged candidate and no untracked task input;
2. collects every selected reusable profile against that same candidate;
3. records the complete eligible profile in memory; and
4. rechecks the candidate after all profile collection.

Profile collection may execute only the existing bounded metadata probes
declared by ADR-0064 and ADR-0065. It does not execute a CMake workflow,
native test, model, QEMU guest, image build, documentation check, diff check,
arbitrary shell command, or preparation command.

Readiness uses the following closed outcomes and reason codes:

| Outcome | `reason_code` | Exact condition | Preparation argv |
| --- | --- | --- | --- |
| `READY` | `ready` | The complete reviewed profile exists. | `null` |
| `PREPARE` | `selected-output-absent` | After valid cache, tool, graph, command, rule, and currently observable non-generated source/input closure, an exact selected non-phony output below the configured build directory is absent. | Canonical workflow argv |
| `PREPARE` | `declared-output-absent` | After exact CTest/image declaration validation, an exact declared output below the configured build directory is absent. | Canonical workflow argv |
| `PREPARE` | `selected-dependency-stale` | An existing selected output has a Ninja dependency record explicitly marked `STALE`. | Canonical workflow argv |
| `UNSUPPORTED` | `profile-unsupported` | Every other profile-collection failure. | `null` |

The context/build-manifest layer owns distinct typed exceptions for the three
`PREPARE` codes. Producer execution must never infer an outcome by matching
exception text.

The classification precedence is normative:

1. candidate/source, workflow declaration, preset, recipe, environment,
   platform, runner/source family, and root checks;
2. an existing `CMakeCache.txt` with exact grammar/configuration, selected
   tools/default-configuration closure, and existing `build.ninja`;
3. Ninja graph, command, rule, generator-include, CTest/image declaration,
   and all currently observable non-generated source/input closure;
4. any ordinary unsupported failure, which overrides every accumulated
   preparation condition;
5. `selected-output-absent`;
6. `declared-output-absent`; then
7. `selected-dependency-stale`.

If several preparation conditions remain, the first code in that precedence
is reported. Missing or malformed `CMakeCache.txt`, `build.ninja`, compiler
metadata, generator includes, external/repository inputs, command tools,
CTest identity, or ambiguous artifact/input ownership is `UNSUPPORTED`, not
`PREPARE`. A missing path outside the configured build directory is always
`UNSUPPORTED`.

The readiness collector may continue checks that do not depend on an absent
generated output so that a later unsupported condition overrides preparation.
It does not claim that preparation will pass; it states only that the fixed
canonical workflow is the sole admitted next action for the exact generated
condition. A later readiness attempt may still become `UNSUPPORTED`.

If any selected profile is not `READY`, producer mode exits nonzero before
opening the evidence store. Every planned command remains `NOT_RUN`, no
receipt is invalidated or published, no attempt log is created, and the local
ledger remains byte-for-byte unchanged.

For each `PREPARE` result, diagnostics print only the fixed canonical command:

```text
cmake --workflow --preset <reviewed-name>
```

The tool never executes that suggestion automatically. `UNSUPPORTED` results
print their bounded reason but no success-shaped preparation advice.
Environment values and credentials remain absent from diagnostics and
reports.

### Producer execution

After the barrier succeeds, producer mode executes the complete plan fresh.
It never consults an existing receipt as command success.

Immediately before each selected reusable workflow, execution recollects its
profile. The profile must remain eligible and its context and inputs must
match the readiness snapshot; otherwise the workflow is not launched.
Existing postflight then proves unchanged source, context, and inputs and
records the actual post-execution outputs.

Every selected reusable workflow must:

- execute fresh with exit zero;
- publish a successful receipt for the exact candidate;
- retain its complete log identity; and
- name the one evidence-store run ID shared by the producer invocation.

A fresh workflow success without a receipt is a producer failure, not a
producer success. Later commands remain `NOT_RUN` under the existing
stop-on-failure rule.

Non-producer reports omit the `producer` field entirely. Producer mode adds
this exact top-level object:

```json
{
  "producer": {
    "selected_workflows": ["test-unit"],
    "readiness": [
      {
        "workflow": "test-unit",
        "outcome": "READY",
        "reason_code": "ready",
        "preparation_argv": null
      }
    ],
    "barrier_passed": true,
    "run_id": "32-lowercase-hex-or-null",
    "complete": true,
    "failure_code": null
  }
}
```

`selected_workflows` and `readiness` have the same nonzero length and plan
order. `workflow` is one reviewed reusable name. `outcome` is exactly
`READY`, `PREPARE`, or `UNSUPPORTED`. `reason_code` is one code from the
closed table above. `preparation_argv` is `null` except for `PREPARE`, where it
is exactly:

```json
["cmake", "--workflow", "--preset", "<same-workflow>"]
```

No free-form reason, environment value, path, or credential enters the JSON.
Human stderr may print the existing bounded collector diagnostic after the
fixed workflow and reason code.

Argument, inventory, path-discovery, untracked-file, or dirty-candidate
refusal occurs in the existing planning/execution-readiness phase before
`execute_plan` starts and therefore retains the current no-report behavior.
Once producer execution starts, the candidate is exact and clean and the
`readiness` array is complete.

`barrier_passed` is true only when every readiness entry is `READY`.
`run_id` is `null` until the evidence store opens, otherwise it is that
invocation's existing 32-lowercase-hex run ID. `complete` is true only when:

- the barrier passed;
- the complete plan returned success;
- `run_id` is non-null; and
- every selected workflow has exactly one `FRESH`, exit-zero command entry
  with the current candidate, complete log, and the same `run_id`.

`failure_code` is `null` on complete success and otherwise exactly one of:

- `readiness-blocked`;
- `profile-changed-before-launch`;
- `receipt-not-published`; or
- `plan-failed`.

If the initial barrier is blocked, every command entry remains `NOT_RUN`.
If a selected profile becomes ineligible or its context/inputs differ
immediately before launch, that workflow entry becomes `FAILED` without an
`exit_code`, log, or subprocess; later entries remain `NOT_RUN`. If its subprocess exits zero but postflight fails, that entry is `FAILED`
with `exit_code: 0`, any successfully retained attempt-log provenance, and
`plan-failed`; later entries remain `NOT_RUN`. If subprocess execution,
postflight identity, and complete-log finalization all succeed but receipt
publication returns no record or fails, the entry is `FAILED` with
`exit_code: 0`, its retained complete log, no receipt candidate, and
`receipt-not-published`; later entries remain `NOT_RUN`.

Failure-code precedence after a passed barrier is normative:

1. `profile-changed-before-launch` before any workflow subprocess;
2. `receipt-not-published` only after exit zero, successful postflight, and
   successful complete-log finalization;
3. `plan-failed` for subprocess failure/interruption, source mutation,
   postflight ineligibility/disagreement, store lock/start failure, attempt-log
   finalization failure, non-receipt command failure, or any other execution
   error.

Implementation uses typed phase outcomes or exceptions; it must not infer this
precedence from error strings.

The exact formula becomes:

```text
final_candidate =
    candidate exists
    and candidate is clean
    and not gate_only
    and plan success
    and (producer field absent or producer.complete)
```

Existing non-producer top-level fields and command-entry meanings remain
unchanged.

### Failure and mutation boundaries

Readiness never weakens the per-command preflight or postflight. A source,
index, environment, tool, context, input, or output change after the initial
barrier is still detected at the existing boundary.

Producer readiness does not preserve an older receipt through a new fresh
attempt. Once a ready producer begins a workflow, the existing store
invalidates that workflow's older receipt before launch. Failure,
interruption, publication error, or postflight disagreement cannot resurrect
it.

Lock contention, malformed profile data, unknown workflow declarations,
candidate mutation, typed-outcome disagreement, and report-construction
failure are explicit errors. No exception starts validation optimistically.

### Performance evidence

The kernel-directory incident records the reference comparison:

```text
readiness probe:                   35.77 seconds
avoidable first full producer:    996.79 seconds
avoidable failed consumer proof:  446.81 seconds
avoidable total:                1,443.60 seconds
```

These observations motivate the barrier but are not permanent timing budgets.
The implementation acceptance record measures readiness separately and proves
that a blocked producer launches zero canonical commands.

## Preserved invariants and non-goals

This decision does not:

- reduce or reorder the required validation plan;
- publish a receipt from postflight alone when preflight was unavailable;
- make an unsupported profile eligible;
- automatically run preparation commands;
- infer a build-only shortcut from CMake or Ninja;
- add another reusable workflow or broaden an existing profile;
- reuse across candidates, worktrees, repositories, machines, or contexts;
- change receipt invalidation, locking, log publication, or provenance;
- parallelize validation commands;
- add CI, remote caching, compiler caching, or dependency installation;
- change reviewer behavior or allow reviewers to rerun the matrix routinely;
- change the Python interpreter policy; or
- add a runtime-DAG node or operating-system behavior.

## Acceptance and phase barrier

This design task is `validation-producer-preflight-design`; its dependent
implementation task is `validation-producer-preflight-implementation`.

The design pull request contains documentation only. It adds no CLI flag,
production code, executable test, model, harness, fixture, build target,
dependency, or reusable prototype.

Before implementation starts, independent design review must verify:

- producer mode cannot change required coverage;
- all selected reusable profiles are checked before any command or ledger
  mutation;
- readiness outcomes are typed rather than inferred from messages;
- blocked plans retain all commands as `NOT_RUN`;
- only supported missing/stale artifacts receive a fixed preparation command;
- producer execution remains fresh and requires same-run receipts;
- source/context/input mutation remains fail closed;
- default fresh, reuse, gate-only, and plan-only interfaces remain unchanged;
- reports cannot claim producer completeness without published receipts; and
- the measured incident and non-goals are recorded accurately.

### Required implementation regression matrix

The implementation begins with the smallest Red proving that the current
execution path launches a workflow despite missing producer readiness. Before
merge, deterministic host tests must also prove:

- `--producer` requires `--execute`, conflicts with fresh/reuse/recording, and
  remains valid with explicit full tier;
- a plan with no selected reusable workflow is rejected before execution;
- producer mode passes the exact original command tuples in their original
  order and checks only reusable workflows already selected by that plan;
- each fixed `READY`, `PREPARE`, and `UNSUPPORTED` code follows the normative
  precedence, including unsupported-over-prepare combined cases;
- missing cache/build files, missing external inputs, malformed metadata, and
  ambiguous paths never receive preparation advice;
- each preparation argv is the exact four-element canonical workflow argv and
  is never executed automatically;
- barrier failure leaves every command `NOT_RUN`, launches zero planned
  canonical validation-command subprocesses, does not construct/open the
  evidence store, creates no attempt log, and leaves pre-existing
  receipt/store bytes unchanged; bounded readiness metadata probes remain
  permitted and are asserted separately from canonical commands;
- candidate mutation during the barrier blocks execution;
- readiness success followed by profile/context/input drift marks the selected
  workflow `FAILED` before subprocess launch and leaves later commands
  `NOT_RUN`;
- producer mode never calls receipt lookup and every selected workflow runs
  fresh;
- older receipt invalidation begins only at the existing `store.start` after
  the complete barrier and final workflow preflight;
- a successful producer publishes one receipt per selected workflow with one
  shared run ID and exact candidate/log provenance;
- postflight failure after exit zero is `FAILED`, cannot set
  `final_candidate`, and uses `plan-failed`;
- receipt publication failure after successful postflight and complete-log
  finalization is `FAILED`, retains that complete log, sets
  `receipt-not-published`, and cannot set `final_candidate`;
- subprocess, interruption, source mutation, lock/start, attempt-log
  finalization, non-receipt command, and other execution failures retain
  existing stop/invalidation behavior and use `plan-failed`;
- producer and non-producer reports match the exact optional schema and
  `final_candidate` formula above; and
- all existing validation-context, build-manifest, evidence-store, execution,
  planner, source-layout, fresh, reuse, and gate-only tests remain green.

## Design review disposition

Complete independent read-only design review found three initial gaps:
`PREPARE` lacked a closed classification, the producer report schema was
underspecified, and the implementation regression requirements did not cover
all failure boundaries. Correction review then identified an imprecise
subprocess boundary and overlapping postflight/publication failure codes.

The final design fixes exact reason codes and precedence, the optional JSON
schema and `final_candidate` formula, the complete host regression matrix, the
metadata-versus-canonical subprocess boundary, and typed failure-code
precedence. Final independent review concluded:

> The design is implementation-ready and no substantive issue remains.

Targeted reproduction: `None`. Acceptance records that clean design result;
it does not claim the dependent CLI, code, tests, or producer behavior exists.

## Separate implementation disposition

The dependent implementation adds the exact `--producer` interface, readiness
outcomes, report schema, failure ordering, and regression matrix above without
changing required coverage or ordinary fresh/reuse behavior.

Its context collector represents the three preparation conditions with
distinct typed failures, accumulates only generated output/dependency
conditions, and lets every ordinary closure failure override preparation.
Execution completes the full barrier before opening the evidence store,
rechecks context/inputs before each selected workflow, executes the unchanged
plan fresh, and requires one shared run ID across every selected receipt.

Historical validation source oracles retain their prior ASTs through bounded
exact-current-source inverses; separately Accepted validation helper bodies
receive new complete identities rather than weakening the older checks.
Merge remains conditional on the implementation Red/Green evidence, one
current exact-candidate producer, bounded actual reuse, complete independent
implementation review, identical squash tree, main fast-forward, and branch
cleanup.

## Alternatives considered

### Publish a receipt after successful postflight without preflight

Rejected. A postflight-only identity cannot prove that context and inputs
remained unchanged during execution.

### Automatically prepare missing profiles

Rejected. Hidden commands would change execution order, invalidate receipts,
and claim that one workflow is always a sufficient preparation for every
ineligibility reason.

### Keep the current process and inspect reuse afterward

Rejected. That is the exact 24-minute avoidable failure mode motivating this
decision.

### Make every fresh execution require producer readiness

Rejected. Ordinary fresh iteration must continue to run on unsupported or
dirty tracked contexts without publishing reusable evidence.

### Lower the validation tier or carry the earlier full result forward

Rejected. Coverage and exact producer provenance are separate requirements;
neither can substitute for the other.

### Infer build-only preparation commands

Rejected. General build dependency inference is outside the reviewed
validation model. The initial correction prints only existing canonical
workflow commands.

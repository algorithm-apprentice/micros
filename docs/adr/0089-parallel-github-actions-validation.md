# ADR-0089: Parallel GitHub Actions Validation

- Status: Accepted
- Date: 2026-11-02
- Refines: ADR-0032, ADR-0033, ADR-0034, ADR-0064, ADR-0084, and ADR-0088
- Supersedes in part: ADR-0088's single-job execution and report boundary;
  ADR-0084's all-configurations-before-observation ordering only for the
  isolated CI producers defined below

## Context

Design #185 is merged. The serial implementation #186 passed its complete
local author matrix and independent publication review, then started its
first hosted run. The user requested smaller parallel CI jobs rather than
one long serial validation step.

The hosted evidence confirms the problem: native validation took 549.963
seconds, ordinary QEMU/image workflows then took another 697.302 seconds,
and stack evidence was still incomplete after more than thirteen additional
minutes. The existing execution controller buffers a workflow's output until
that workflow ends, hiding progress during the largest gate.

The run was cancelled and #186 closed without merge. Its exact reviewed
source is preserved; none of its partial hosted results is final acceptance.
The [research record](../research/parallel-ci-validation-evidence.md) contains
the identities, measured costs and source trace.

Serial development means one implementation outcome at a time. It does not
require independent validation jobs to execute serially.

## Decision

Implement the bounded
[parallel CI contract](../development/parallel-ci-validation.md) after this
corrective design is independently reviewed, Accepted and separately merged.
Restart CI implementation from the resulting current `main`.

### Independent work and retained dependencies

Use a closed six-member producer matrix on standard `macos-26`:
one `native` producer and five target producers. Limit simultaneous matrix
jobs to five and keep `fail-fast: false`; failure in one producer does not
hide diagnostics from independent producers.

The native producer executes the existing complete native workflow,
documentation and three diff commands. Its independent CTests may use
`CTEST_PARALLEL_LEVEL=3`, matching the standard runner's three vCPUs.
No native test/model is removed, shortened or replaced.

Partition the existing 42 target configurations exactly once by the fixed
algorithm in the contract. A target producer owns each assigned configuration's
ordinary canonical QEMU/image workflow, ordinary/metadata stack proof, and
observed scenario where declared.

Within a configuration, ordinary artifacts precede metadata pairing.
The complete metadata proof and the actual observed image's static proof
must pass before that image's guest runs. Independent configurations may
advance concurrently on isolated machines. Their partial success cannot
approve the complete CI outcome.

Do not run the complete 48-command plan in every matrix job. Preserve all
47 directly executed non-stack commands exactly once and fulfill the one
stack intent through the complete distributed proof.

### A distinct CI evidence boundary

The local complete planner and `test-stack-evidence` command remain unchanged.
They still execute and certify one local candidate with their original
source/tool/artifact checks.

A CI producer emits only a versioned `PART_COMPLETE` record for its exact
owned scope. It never publishes a local full-stack certificate, a reusable
receipt or a final author-validation report for a subset.

The final `validation` job consumes only the complete current workflow
run/attempt's producer artifacts. It requires every producer to have
succeeded and checks exact source, scope, command, image, observation,
file-digest and tool identities before publishing a distinct
`CI_VALIDATION_REPORT`.

That report truthfully distinguishes 47 fresh canonical command executions
from the one distributed stack intent. It does not forge a local
`VALIDATION_REPORT` or claim that the aggregate runner built remote objects.

The producer remains responsible for actual compiler/link/source proofs and
pre/post build-seal validation. Artifact transport preserves those results;
the collector does not repeat all compilers, guests or graph decoding.
The trusted channel is the reviewed current-run workflow, successful complete
producer set and pinned digest-verifying artifact action, not arbitrary
caller-supplied success files.

### Source, artifacts and failure

Every job binds the same event commit, actual commit tree, source digest,
resolved comparison base, repository, workflow identity, run ID and attempt.
For CI, a clean staged tree must also equal the checked-out HEAD's tree;
staged edits cannot borrow the event commit's name.

Keep each producer's own runner/repository/tool context. Do not normalize
another checkout's inode, path or receipt identity into local authority.
Required target tool and firmware identities must agree across producers;
unsupported drift fails rather than silently mixing toolsets.

Artifacts have exact run/attempt/part names, no overwrite, fixed file
inventories and seven-day retention. Download only current-run artifacts
with the pinned official action and `digest-mismatch: error`. No executable,
cache or command is loaded from their contents.

Missing, duplicate, extra, stale, failed, cancelled, skipped, malformed or
unattributable parts prevent aggregate success. Partial re-runs cannot reuse
prior-attempt parts; rerun the complete workflow when a new complete attempt
is required.

### Visibility and resource use

Stream each executed command's output while retaining its exact complete
log. Emit per-part, profile and phase start/completion records and elapsed
durations. Timing is diagnostic scheduling evidence, never a test oracle
or permission to change a guest deadline.

Retain ADR-0088's read-only permissions, hosted machine family, official
action pins, supported runtime/tool families, pinned firmware, job limits,
secret handling, no-cache policy and publication/final-review sequence.
Only the collector's tool requirement is narrower: it needs the admitted
Python/source boundary, not unused target compilers or QEMU.

## Acceptance

The restarted implementation must prove:

- complete deterministic partition and unknown/missing/duplicate rejection;
- unchanged local full-plan and stack-gate behavior;
- exact fresh command routing and preserved within-profile ordering;
- source/event/index-tree binding and per-producer pre/post identity checks;
- streaming output and failure propagation without retries;
- complete native execution with its CI-only CTest concurrency;
- strict current-run/attempt artifact and aggregate completeness controls;
- rejection of a missing producer even when other producers passed;
- no local receipt or complete-certificate authority from a partial record;
- the unchanged 42 configurations, 111 static ELF outputs, ten observed
  scopes and explicit blocked B13 admission;
- actual overlapping hosted producer jobs and an accepted aggregate result;
- full local author evidence, independent publication review, hosted Green,
  final independent review and successful main-run verification.

Record actual hosted phase durations and queue effects. Do not claim a fixed
speedup or rely on a higher paid concurrency quota.

## Phase barrier

This Accepted correction contains documentation only. No workflow, shard
runner, collector, test, build scaffold or executable prototype is authored.
The old implementation remains abandoned reference. Only separate design
merge permits the dependent implementation to restart.

## Design review disposition

Independent GPT-6.1 Sol review inspected the complete seven-file design and
exact author evidence and reported no substantive issue. No targeted
reproduction or validation rerun was needed. Acceptance records the design
result, not a parallel implementation or hosted pass.

## References

- [Parallel execution and evidence contract](../development/parallel-ci-validation.md)
- [Measured and fixed-source evidence](../research/parallel-ci-validation-evidence.md)
- [Original hosted-CI design](0088-github-actions-validation.md)
- [Current stack contract](0084-checked-stack-bound-evidence.md)

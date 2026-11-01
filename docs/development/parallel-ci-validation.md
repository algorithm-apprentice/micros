# Parallel CI Validation

## Status and boundary

This Accepted contract corrects the serial CI design under
[ADR-0089](../adr/0089-parallel-github-actions-validation.md).
The original implementation #186 is closed without merge and preserved at
`f09f7852d354bbc03957096ce658cd16ef13a1b8`.
Implementation is blocked until separate corrective-design merge.

The [original CI contract](github-actions-ci.md) still supplies supported
host/runtime/tool/firmware authority, read-only permissions, source history,
publication review before commit and final remote acceptance before merge.
This document defines the replacement for execution placement,
current-run transport and aggregate completion.

## Job graph

One producer matrix has exactly these part IDs:

```text
native
target-1
target-2
target-3
target-4
target-5
```

All six jobs use standard `macos-26`. The matrix has `max-parallel: 5`,
`fail-fast: false`, and no `continue-on-error`, excludes, optional members
or caller-selected matrix values. There are no dependency edges between
independent producers.

One stable job named `validation` depends on the complete producer matrix.
Its always-evaluated completion guard explicitly rejects any upstream
result other than success. A failed, skipped or cancelled dependency cannot
be accepted as a green skipped aggregate. It performs no target build or
guest run. If platform-wide cancellation prevents the collector from
executing, the run remains cancelled with no complete result. Hosted
acceptance requires both the workflow and aggregate job to conclude success.

The existing per-PR/ref concurrency group still cancels superseded workflow
runs. Each job retains the 90-minute safety limit. Five is a bounded job
limit, not a claim that five runners will always be available: GitHub's
account-wide limits and platform queues still apply.

## Exact work partition

The version-controlled planner and stack scope remain the authorities.
The initial union contains one complete native workflow, 38 ordinary QEMU
workflows, four image workflows, documentation, three diff commands, and
the complete stack intent.

The native part executes:

- the canonical `test-unit` workflow with `CTEST_PARALLEL_LEVEL=3`;
- the canonical documentation command using the selected Python prefix; and
- the three canonical staged/worktree/resolved-base diff checks.

The CTest programs are independent processes; their only external fixture
file reads are read-only and the current declarations impose no shared
resource or fixture-order dependency. Python discovery remains complete.
Concurrency changes neither test membership nor model transition counts.
Local validation keeps its existing default unless the caller explicitly
selects this environment.

The five target parts use this deterministic partition:

1. materialize the closed current target `Profile` and ELF-output inventory;
2. require exact equality with the canonical QEMU/image workflow set;
3. assign each profile weight equal to its declared ELF-output count,
   doubled when that profile has an observed scenario;
4. order profiles by descending weight, then ascending configure name;
5. place each profile in the lowest-weight part, breaking ties by part number;
   and
6. execute each part's profiles in ascending configure-name order.

Weight determines placement only. It is not a runtime complexity, performance
or stack oracle. The five-way placement is `O(C log C)` time and `O(C)`
storage for `C` configurations; the part count is fixed.

The current independently materialized inventory is:

| Part | Configurations | Static ELF identities | Observed scopes | Placement weight |
| --- | ---: | ---: | ---: | ---: |
| target-1 | 8 | 22 | 1 | 32 |
| target-2 | 9 | 24 | 1 | 32 |
| target-3 | 9 | 20 | 4 | 32 |
| target-4 | 8 | 20 | 3 | 32 |
| target-5 | 8 | 25 | 1 | 31 |
| Total | 42 | 111 | 10 | 159 |

Unknown configurations, an omitted/duplicate workflow, empty or overlapping
ownership, or disagreement with the current registry fail before execution.
A future gate must join the versioned declaration and full aggregate
coverage; a CI path filter cannot make it disappear.

## Within-profile ordering

For every owned target configuration:

1. execute its exact ordinary canonical QEMU/image workflow;
2. build its separate fresh ordinary stack counterpart;
3. build the same-source metadata counterpart after the ordinary pair seal;
4. prove complete compile/link/generated-input equality, byte passivity and
   every declared static image/root;
5. if observed, build that configuration's separate observed image set;
6. prove all those observed images before starting the exact guest scenario;
7. require the complete legacy transcript and every declared live producer;
   and
8. retain the original seals and source/tool identities through part
   publication.

Reuse the existing artifact, callback, assembly, fatal-context, observer and
report owners. Do not replace them with a CI estimate, copied maximum or
hosted smoke proxy. No unrelated profile is a prerequisite for a safe guest
whose own complete proof passed on its isolated producer.

This per-profile ordering is the sole CI-only refinement to the earlier
global static/observation phase barrier. All profiles remain mandatory before
aggregate success. The local full stack command retains its original global
ordering and complete publication.

## Producer execution and records

The prospective CI part entry point accepts only one of the six declared
part IDs. It derives commands and profile membership from the current
checked-in declarations; it accepts no arbitrary command, path subset,
firmware, expected result or success file.

Execute the exact canonical command argv, not the full plan once per part.
The CI-specific recorder streams stdout/stderr while retaining the complete
raw log and actual exit code. Start/end records name the part, workflow or
profile phase and diagnostic elapsed time. No shell pipeline may mask a
failure. Stop that part on failure; other independent parts may finish.

A complete part record contains:

- format/version and `PART_COMPLETE`, never overall `PASS`;
- exact part ID and declared membership;
- repository, workflow ref/SHA, event commit, run ID and attempt;
- actual HEAD, HEAD tree, staged tree, source digest and resolved base;
- original before/after host/runtime/tool/firmware context;
- ordered exact command argv, actual fresh result, exit code and log identity;
- every owned static profile/image proof and original build-seal identity;
- every owned observed image/proof/transcript/producer result; and
- the complete normalized file inventory with byte length and SHA-256.

Every producer requires the index tree to equal HEAD's committed tree as
well as a clean worktree and matching event commit. Complete source history
and the fixed source-stage owners remain required.

Before and after atomic record publication, recheck source and the original
per-profile build seals and relevant tool/firmware identities. A newer valid
seal cannot replace the build that produced the proof. An interrupted or
failed part has diagnostics only; it cannot leave `PART_COMPLETE`.

Keep each runner's original repository, path and tool context in its record.
Only code/source identity is shared across machines. Another producer's
filesystem identity is not rewritten to match the collector.

The producer output root is `build/ci/parts/<part-id>/`, with `result.json`,
named logs and referenced seal/metadata data. The part record enumerates
every other uploaded file; it does not contain a self-referential file hash.

## Current-run artifact transport

A producer uploads one immutable, non-overwriting artifact named:

```text
micros-ci-<run-id>-<attempt>-part-<part-id>
```

Its root contains the complete part record, named command/phase logs,
preflight metadata and referenced build-seal JSON. Only this fixed
project-owned output namespace is eligible. There are no object/image
execution caches, secrets, checkout files, `.git` directories or arbitrary
host paths. Retain artifacts for seven days.

Add only the official download action to the existing action set:

| Action | Release | Verified commit |
| --- | --- | --- |
| `actions/download-artifact` | v8.0.2 | `9000827ccba6bdab643e8b6fd33ac0654aef8333` |

The collector downloads the exact current run/attempt part-name pattern,
uses `merge-multiple: false` and `digest-mismatch: error`, and specifies no
alternate repository, run ID or token. Distinct artifact directories prevent
one producer's files from overwriting another's.

The download action's permission normalization is artifact transport, not
proof of a runtime object's mode. The collector accepts only regular,
nonlinked data below those exact directories and validates content digests;
original runtime artifact modes remain in their producer seals. It never
executes downloaded files or restores a producer build directory.

The trust boundary is the reviewed current workflow, all successful expected
jobs, and the official digest-verified current-run artifacts. Caller-created
directories, previous successful runs, prior attempts or arbitrary URLs are
not an alternative admission path. CI JSON by itself is not a GitHub success
check or a reusable local receipt.

## Aggregate acceptance

The collector first requires the complete matrix's success. It then checks:

- exactly one native part and all five target parts, with no extra artifact;
- strict supported JSON schemas, no duplicate keys or malformed numeric data;
- identical repository/workflow/run/attempt/event/source/base identities;
- committed tree equality with its own exact event checkout;
- exact declared command ownership and 47 fresh successful executions;
- compatible and unchanged per-producer contexts, plus identical required
  target compiler/linker/companion-tool and firmware identities;
- all 42 static configurations and 111 qualified image identities once;
- all ten observed scopes and their complete source-declared producer
  identities, including lifecycle generations and optional-owner policy;
- no borrowed variant/image/proof identity, missing root, excess bound,
  malformed observation, failed transcript or incomplete seal reference; and
- the complete referenced file inventory, byte lengths and digests.

Actual binary decoding and physical ELF/stack-range binding remain the
owning producer's checked execution, not new work claimed by the collector.
The aggregate validates complete protocol/source/coverage consistency without
rebuilding objects, re-running guests or repeating all graph decoding.
It preserves the complete checked proof payload and provenance for inspection.

Only then atomically publish one `build/ci/complete.json` containing the
distinct CI validation report and complete CI stack bundle. Recheck the
source and original input-file identities before and after publication;
failure leaves no new complete result. Emit a `CI_VALIDATION_REPORT`
summary only after success. The report maps every intent in the current
canonical full plan to
one of:

- an exact freshly executed command and its owning producer; or
- the one explicitly distributed stack intent, with all required producer
  references and complete static/observed scope.

The current count is 47 executed commands plus one distributed stack intent,
not a falsely claimed 48-command local execution. Emit the single current
B13-E007 `BLOCKED / production-node-q-absent / STACK13` record only at full
aggregate completion.

Do not create a local `VALIDATION_REPORT`, reusable receipt or
`build/stack-evidence/current.json` from imported parts. The local complete
commands retain their existing authority and artifact contracts.

Any failed, cancelled, skipped, missing, duplicate, extra, stale, malformed or
drifted part prevents a successful aggregate. Failures retain diagnostics but
publish no complete result. A partial workflow rerun cannot combine earlier
attempt artifacts with new ones; complete acceptance requires one complete
attempt.

## Tool and workflow boundary

Producer jobs retain ADR-0088's full admitted macOS/tool preflight and pinned
firmware. The collector needs only admitted macOS/CPython plus exact
repository/event/source checks; it does not pretend to have executed target
tools whose producer contexts it reads.

Source, role and data-shape checks apply before executing or consuming
anything. Role selectors are the closed producer set and collector mode,
not a generalized workflow engine. Keep all existing action/security,
no-cache, cancellation and prepublication/final-review requirements.

## Acceptance evidence

Native tests start Red for missing partition/record/collector behavior and
cover omitted or duplicate coverage, unknown parts/commands, malformed
records, foreign source/run/attempt, replaced files, linked paths, failures
and false aggregate success. Prove streamed output and preserved nonzero
exit without sleeps.

Run the unchanged full local author gate and the explicit native parallel
mode before publication review:

```text
CTEST_PARALLEL_LEVEL=3 cmake --workflow --preset test-unit
```

Actual hosted acceptance must show overlapping producers, complete
same-attempt aggregate coverage, all unchanged stack/resource/semantic gates,
and retained per-phase timing. Record queue effects rather than promising
an unavailable macOS concurrency quota or a fixed runtime threshold.

ADR-0088's publication review still precedes every implementation commit;
actual hosted Green and final independent review precede merge. Verify the
main run before reporting the parallel CI operational.

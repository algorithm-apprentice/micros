# Parallel CI Validation Evidence

## Fixed source and abandoned candidate

The corrective design starts at merged CI design #185:
`a373f9651e73e5f0b97782a5db20d291a8081d5d`.
It follows the user's request to divide the long validation step into
parallel jobs rather than serial steps.

The reviewed serial candidate is preserved at
`f09f7852d354bbc03957096ce658cd16ef13a1b8`, tree
`e5644825b2f258bfcb36836f0553f8c99b4d6135`, on
`archive/github-actions-ci-serial`. PR #186 is closed without merge.
Its local 48-command author run and publication review passed, but those
results do not certify the requested parallel implementation.

Hosted run
[41333377480](https://github.com/algorithm-apprentice/micros/actions/runs/41333377480),
attempt 1, job `124927981067`, was explicitly cancelled. The job lasted
35 minutes 21 seconds and did not complete stack validation or artifact
publication. It is timing/diagnostic evidence, not hosted Green.

## Measured serial path

GitHub's completed job log records:

| Phase | Start/end evidence | Measured duration |
| --- | --- | ---: |
| Tool installation | 17:48:59 to 17:50:06 UTC, 2026-11-01 | 67 seconds |
| Complete native workflow | 17:50:10.847275 to 17:59:20.810650 | 549.963 seconds |
| Ordinary 38 QEMU plus four image workflows | 17:59:20.810650 to 18:10:58.112405 | 697.302 seconds |
| Stack evidence | Started 18:10:58.112405; incomplete at cancellation around 18:24:14 | More than 13 minutes, not a complete cost |

The native result contained 37 passing CTests and 1,217 passing Python
cases. CTest reported 333.19 seconds; Python reported 190.796 seconds.
The largest native model costs were ELF spawn 184.43 seconds, IPC 53.83,
VFS 44.26 and RAMFS 31.94.

Representative ordinary workflow durations, including their builds, were
PM 34.714 seconds, TTY 41.879, RAMFS 51.958, VFS 81.303 and ELF spawn
147.548. These are whole workflow measurements, not isolated guest times.

The observed lower bound on total serial duration is not an estimate for
the uncompleted stack gate. No fixed speedup is derived from a cancelled run.

## Source explanation

`tools/validation_execution.py:225-265` executes each canonical workflow
with output directed to its attempt log and replays it only after process
completion. This explains why the UI had no incremental stack-phase output
despite the stack dispatcher emitting progress records.

`tools/check_stack_evidence.py:79-104` serializes all 42 ordinary/metadata
pairs and then all ten observed scenarios. Each pair uses fresh separate
build directories and actual compiler/link/source proof. The aggregate
therefore adds 84 paired build configurations plus ten observed configurations
after the ordinary acceptance matrix.

These repeated required modes cannot be removed by relabeling a step.
Independent configurations can execute on separate runners, while the
ordinary-to-metadata dependency and static-before-guest checks remain local
to each producer.

## Native independence and bounded placement

The current CTest declarations in `CMakeLists.txt:2774-2806` assign slow
labels but no shared resource lock, fixture dependency or run-serial property.
The native programs keep their mutable state per process. The only direct
external fixture opens found in `tests/host/*.c` are read-only:
`ramfs_model_fixture.c:31`, `ramfs_test_fixture.c:91`, and
`ramfs_seed_test_fixture.c:39`.

This supports CI-only CTest concurrency on the runner's three vCPUs without
changing membership, model size or shared ownership. It does not authorize
running separate development tasks concurrently.

A naive lexical five-way split placed 36 static ELF identities in one part
and only 13 in another. The bounded image/observation-weight placement in
the Accepted contract yields weights 32, 32, 32, 32 and 31, with exactly
42 configurations, 111 static images and ten observed scopes retained.
Placement weights are scheduling heuristics, not correctness expectations.

The fixed
[MINIX validation-driver study](minix-validation-driver-baseline.md)
remains the behavioral/structural baseline: named tests, explicit selection
and per-test results. The local reference stays at
`4db99f4012570a577414fe2a43697b2f239b699e`.
CI changes only execution placement and evidence transport, not the
runtime algorithms under test.

## Platform and artifact evidence

The official [matrix guide](https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/run-job-variations)
defines independent matrix jobs, `max-parallel`, failure handling and
artifact-based fan-in.

The official [Actions limits](https://docs.github.com/en/actions/reference/limits#job-concurrency-limits-for-github-hosted-runners)
list five concurrent standard macOS jobs for Free, Pro and Team, and fifty
for Enterprise. The cap is shared across the account/organization; this
design does not infer the current user's plan or depend on a larger quota.
The cancelled run also reported a macOS arm64 capacity/queue warning.

GitHub API inspection verified the official
[download-artifact v8.0.2](https://github.com/actions/download-artifact/tree/9000827ccba6bdab643e8b6fd33ac0654aef8333)
commit `9000827ccba6bdab643e8b6fd33ac0654aef8333`.
Its documented inputs provide current-run defaults, separate artifact
directories and `digest-mismatch: error`. No cross-run token, repository
override or latest-success lookup is required.

The action normalizes downloaded file permissions. This is why CI transports
only data/log/seal files and preserves runtime-mode evidence in the original
producer record, rather than restoring another runner's executable build
directory and treating it as a local artifact.

## Required distinction

Successful parts are not a full local validation report. The parallel
collector must preserve complete current-run source/scope/provenance while
truthfully distinguishing executed commands from the distributed stack
intent. Imported JSON cannot become a reusable local receipt.

The exact requirements and failure boundaries are in Accepted
[ADR-0089](../adr/0089-parallel-github-actions-validation.md) and
[the parallel contract](../development/parallel-ci-validation.md).
No executable shard runner or collector is supplied by this research.

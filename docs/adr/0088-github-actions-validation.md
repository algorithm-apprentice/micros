# ADR-0088: GitHub Actions Validation

- Status: Accepted
- Date: 2026-11-01
- Refines: ADR-0013, ADR-0032, ADR-0033, ADR-0034, ADR-0064, ADR-0065,
  ADR-0075, and ADR-0084
- Supersedes in part: ADR-0034's timing of remote evidence before publication,
  solely for the CI-bootstrap boundary below

## Context

EV03 is separately merged as #184. Its complete author matrix includes native
tests, 38 QEMU workflows, four image workflows, the all-current stack gate,
documentation and three diff checks. The repository has no Actions workflow.
The user requested GitHub Actions hosting for CI.

The current complete stack proof intentionally binds the admitted Darwin
arm64/Homebrew QEMU configuration and exact OpenSBI artifact. A Linux package
with a different default firmware is not equivalent merely because it can
boot the kernel. Initial CI should host the existing contract rather than
silently weaken it or introduce another host-platform implementation.

The fixed MINIX driver comparison, current tool constraints and official
GitHub evidence are in
[the CI research record](../research/github-actions-ci-baseline.md).

A new workflow also has a publication dependency: GitHub cannot execute its
unpublished working-tree contents. Requiring its first hosted result before
any reviewed publication would make CI bootstrap impossible. That dependency
does not justify committing unreviewed code or merging an untested workflow.

## Decision

Implement the bounded
[GitHub Actions contract](../development/github-actions-ci.md) after this
design is independently reviewed, Accepted and separately merged.

### One hosted execution boundary

Use one standard GitHub-hosted `macos-26` arm64 job for pull requests to
`main`, pushes to `main`, and explicit manual dispatch. Run the existing
complete canonical validation plan fresh, without path filters, command
subsets, receipt reuse or cross-run build caches.

The hosted job changes execution location, not the runtime DAG, test
algorithms, compiler optimization, QEMU arguments, guest deadlines, image
limits or stack budgets. It does not open EV04 or node Q by itself.

Retain the existing host families: GIL-enabled final CPython 3.15, LLVM
Clang/LLD major 23, QEMU major 11, the documented CMake/Ninja boundary, and
the exact Accepted OpenSBI bytes. Homebrew supplies supported patch releases;
their actual identities are recorded per run. This is not a claim that a
moving runner image or package-manager index is an immutable machine image.
An unsupported update fails explicitly instead of selecting a substitute.

Use `actionlint` for the new workflow's syntax and shell controls. This
CI-specific gate is additional to, not a replacement for, the canonical
native/QEMU/image/stack plan.

Pin the two official JavaScript actions to verified full commit IDs. Do not
add reusable/composite actions, a private runner, deployment credentials,
artifact-derived execution or an unreviewed installer framework.

### Source and authority

Checkout the event's exact commit with complete history and without retained
Git credentials. Bind the hosted record to actual HEAD, staged tree, source
digest, event commit, resolved comparison base, run ID and attempt.

The ordinary candidate checks still reject dirty, missing, unmerged or
untracked task input. Full history is mandatory for the fixed source-stage
oracles; a shallow checkout is not an acceptable faster substitute.

Grant only `contents: read`. Do not use `pull_request_target`, `workflow_run`,
secrets, write tokens, deployments or self-hosted machines. Untrusted event
text must not become shell program text. Existing contributor approvals and
repository rules remain unchanged.

### Completion and evidence

The complete planner must exit zero with an authoritative final-candidate
report and every selected command freshly passed. The stack result must
belong to that same candidate. Missing tools, unsupported firmware, failed
tests, timeout, cancellation, absent records or artifact publication failure
cannot yield a successful CI job.

Retain bounded-duration diagnostic artifacts: tool/environment metadata,
the complete planner log and per-workflow logs, the stack bundle and its
build seals. These are CI evidence, not reusable local receipts. Do not
upload repository credentials, a complete environment dump or unrelated
workspace contents.

CI supplements local Red evidence and independent analysis. It does not
approve a design, retire an audit finding, automatically merge a PR or
replace the author's existing validation obligations.

### Reviewed publication before remote acceptance

For this CI implementation, and subsequent changes to its hosted execution
boundary that inherently require publication to run:

1. complete local Red/Green controls, all locally executable required gates,
   clean staging and exact HEAD/tree evidence;
2. obtain an independent **publication review** of the complete change and
   those results, recording hosted acceptance as `NOT RUN: unpublished`;
3. only after that review is clean, commit and publish the candidate to its
   implementation PR;
4. obtain and inspect the actual hosted run for that exact candidate;
5. after hosted acceptance passes, obtain the final independent
   implementation review of the complete change and updated evidence; and
6. merge only when both local and hosted requirements and final review pass.

Publication review is not merge approval or a completed implementation
review. Failure keeps the PR open and unaccepted. Each correction again
passes local controls and independent review before a new commit; no amend,
unreviewed push, automatic retry or skipped remote gate is authorized.

This exception does not apply to ordinary runtime development. Design PRs
remain documentation-only, and implementation starts on a fresh branch only
after design merge.

## Acceptance

The separate implementation must demonstrate:

- the existing absence of a hosted workflow before implementation;
- native positive/negative controls for host, runtime, tool, firmware,
  history, source and event identity preflight;
- the exact selected interpreter and `-E -S -B` prefix at every Python site;
- full, fresh planner coverage and unchanged target/stack contracts;
- secretless read-only execution and pinned official actions;
- clean local author evidence and publication review before the first commit;
- one actual successful Actions run on the implementation PR, including its
  native, QEMU, image and stack results and retained evidence;
- exact hosted commit/tree/run/attempt provenance and final independent
  review; and
- a successful main-branch run after merge before reporting CI operational.

## Phase barrier

This Accepted design contains no workflow, preflight helper, tests, harness,
installer, build scaffold or executable prototype. No hosted success is
claimed. `github-actions-implementation` remains pending until separate
design merge.

## Design review disposition

Independent GPT-6.1 Sol review covered all seven design files and the exact
author evidence and reported no substantive issue. It confirmed the CI-only
publication sequence is complete and consistent; no targeted reproduction
was needed. Acceptance records this design result, not a hosted workflow
or implementation pass.

## Alternatives considered

- Start on Linux and omit the stack gate: violates current mandatory coverage
  and the admitted firmware/default-configuration boundary.
- Use the developer's machine as a self-hosted runner: unnecessarily exposes
  a persistent shared environment to pull-request code.
- Rely on CI instead of independent review: does not establish correctness
  analysis or local test-first evidence.
- Commit first and review later to bootstrap CI: violates the required
  independent review before publication.
- Replace the current package/runtime closures with a container, source-build
  or package-snapshot framework: unnecessary for the initial hosted contract.

## References

- [CI execution contract](../development/github-actions-ci.md)
- [Baseline and platform evidence](../research/github-actions-ci-baseline.md)
- [Author validation](0034-author-validation-before-review.md)
- [Stack evidence](0084-checked-stack-bound-evidence.md)

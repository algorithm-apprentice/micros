# GitHub Actions Validation

## Status and outcome

This is the Accepted contract for `github-actions-design`, following merged
EV03 #184. [ADR-0088](../adr/0088-github-actions-validation.md) governs the
separate implementation and its reviewed-publication bootstrap.

The outcome is one hosted CI job that executes the repository's complete
current validation and retains attributable results. No workflow or hosted
result is claimed by this design.

The unmerged serial implementation #186 has been abandoned after the user's
parallelism request and measured hosted latency. Accepted
[ADR-0089](../adr/0089-parallel-github-actions-validation.md) and the
[parallel CI contract](parallel-ci-validation.md) define its replacement.
Implementation must restart after separate corrective-design merge.
The execution and report shape below records the original serial design;
the parallel contract is authoritative for its replacement. The common
host/security and publication-review rules below remain required, and a
distributed success claim requires that complete current-run protocol.

## Original execution declaration

| Dimension | Initial contract |
| --- | --- |
| Workflow | `.github/workflows/ci.yml`, named `CI` |
| Job | One job named `validation` |
| Runner | Standard GitHub-hosted `macos-26`, actual Darwin/arm64 |
| Events | Pull request to `main`, push to `main`, manual dispatch without inputs |
| Pull-request activity | Opened, synchronized, reopened and ready for review |
| Permissions | `contents: read` only |
| Checkout | Exact event commit, `fetch-depth: 0`, `persist-credentials: false` |
| Concurrency | One active run per workflow and PR/ref; cancel superseded runs |
| Job limit | 90 minutes; expiration is failure, not target success |
| Validation | Complete canonical full plan, fresh execution |
| Artifact retention | Seven days; diagnostics are not reusable receipts |

There are no path filters, draft-only bypasses, user-supplied commands,
matrix subsets, automatic retries, `continue-on-error`, build/receipt cache
restoration, deployment or automatic merge.

The job limit bounds hosted resource use; it changes no existing guest
readiness, serial, QEMU, model or stack limit. A slow runner does not
authorize a larger target timeout.

## Action and tool authority

Only these official JavaScript actions are needed initially:

| Action | Verified release | Full commit |
| --- | --- | --- |
| `actions/checkout` | v7.0.1 | `3d3c42e5aac5ba805825da76410c181273ba90b1` |
| `actions/upload-artifact` | v7.0.2 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |

The source/action records and current runner declaration are linked in
[the research evidence](../research/github-actions-ci-baseline.md).
No composite or reusable workflow introduces another transitive action.
Updating an action pin requires ordinary reviewed source changes.

The runner installs the documented tools through Homebrew, using the
versioned `python@3.15`, `llvm@23` and `lld@23` names and the `cmake`, `ninja`
and `qemu` formulae. Install `actionlint` for the CI-specific workflow check.
Prefixes come from the selected Homebrew installation,
not a copied developer home path. The versioned LLVM/LLD names currently
alias the major-23 formulae.

Preflight requires:

- actual macOS major 26 and arm64;
- ordinary GIL-enabled CPython 3.15 final, using the existing runtime checker;
- LLVM Clang and LLD major 23 and the target `llvm-nm`/`llvm-readelf` tools;
- CMake 3.25 or newer and Ninja;
- QEMU major 11 in the existing admitted Homebrew layout; and
- the existing default-firmware resolver's complete configuration and
  byte-pinned OpenSBI proof.

Do not duplicate or rebaseline the firmware expectation. Call its existing
owner. A different firmware, QEMU layout, missing bottle/tool, unsupported
version or inherited configuration is an explicit failure before the
complete test plan. No source-build fallback, automatic downgrade,
configuration deletion or broader host admission belongs to this task.

Supported patch updates are not cross-run cache identities. Record the
actual versions and resolved executable identities for each run; the
existing validation and stack manifests retain their exact byte/input
closure. A future package or runner update outside the admitted families
requires a separate reviewed maintenance change.

## Environment and source

Package installation runs before the project's validation environment is
selected, so disabling the project's SSL trust lookup does not affect
Homebrew downloads.

The validation steps explicitly select absolute `MICROS_PYTHON`,
`MICROS_CLANG`, and `MICROS_LD_LLD` paths, set
`CLANG_NO_DEFAULT_CONFIG=1`, and use the existing three `/dev/null` OpenSSL
variables. QEMU discovery is made unambiguous through its selected Homebrew
binary directory. The native compiler remains the runner's Xcode compiler;
the target compiler is selected explicitly rather than prepending LLVM to
all native commands.

Every project Python process uses `<MICROS_PYTHON> -E -S -B`. Existing
forbidden inherited-variable checks remain active. CI does not silently
unset `PYTHONEXECUTABLE`, `__PYVENV_LAUNCHER__`, compiler injection controls,
QEMU configuration or unsupported tool settings.

The prospective `tools/ci_environment.py` has one responsibility: validate
and report this hosted preflight. It accepts no command, scope, replacement
firmware or arbitrary source-directory argument. Its record includes actual
host/tool/runtime/firmware identities, run ID and attempt, event commit,
HEAD, index tree, source digest and resolved comparison-base commit.
It writes no source file and does not execute a guest.

The actual clean candidate must match `GITHUB_SHA`. A pull-request checkout
uses GitHub's event merge commit, not an independently inferred branch tip.
All fixed historical source-stage commits must be available. A detached
HEAD is supported by the existing candidate reader.

`CI_BASE` comes only from the pull request's base SHA, the push's before SHA,
or `origin/main` for manual dispatch. Preflight accepts only the literal
`origin/main` or a complete supported Git object ID and resolves it to a
commit. Missing history and an all-zero/nonexistent base fail. Pass the
resolved commit, not a moving ref, to the full planner.

Event values enter commands through quoted environment variables, never
interpolation into shell program text. Do not expose a full environment dump
or retain checkout credentials.

## Ordered job phases

1. Checkout the exact event source and complete history.
2. Install the selected tools using the runner's package manager.
3. Run `actionlint .github/workflows/ci.yml` without suppressing syntax,
   expression, runner or shell findings.
4. Establish the explicit validation environment, check hosted identity and
   tool/firmware eligibility, and record preflight metadata.
5. Run the existing canonical command:

   ```text
   <MICROS_PYTHON> -E -S -B tools/validation_plan.py --tier full --execute --fresh --base <resolved-base>
   ```

6. Retain the full output with pipeline failure propagation.
7. Upload the fixed diagnostic/evidence paths when the run is not cancelled.

The current plan has 48 commands. The planner, not a copied CI command list,
remains the coverage authority as later reviewed tasks add gates.

The shell uses fail-fast and pipeline-failure semantics. Logging with `tee`
cannot turn a failed planner into success. Preflight failure prevents the
validation phase. Diagnostic upload after failure does not change the
earlier failure. Missing expected upload input is an explicit upload error.

## Evidence and outcomes

Keep project-owned CI diagnostics under ignored `build/ci/`. Upload only:

- bootstrap/preflight metadata and complete validation output there;
- `build/validation-evidence/v1/logs/`;
- `build/stack-evidence/current.json`, when a complete run produced it; and
- the fixed per-build `stack-build-seal.json` paths below stack evidence.

Do not upload the checkout, `.git`, global configuration, credentials,
package-manager caches, unrelated host files or executable build caches.
Use a run-ID/attempt-specific artifact name without overwrite. The artifact
is a diagnostic snapshot, not permission to import remote receipts into a
local evidence store.

A passing hosted acceptance requires all of:

- the actual Actions job succeeds;
- its checkout commit and source tree match the event and inspected PR;
- the final `VALIDATION_REPORT` is authoritative and final-candidate;
- every command in the canonical complete plan is freshly successful;
- the stack bundle belongs to the same candidate, contains all current
  static/observed scopes and preserves the explicit future B13 status; and
- the expected diagnostic artifact is retained and attributable to this run.

The author inspects the actual run and artifact, recording URL, run ID,
attempt, tested commit/tree, exact command and result in the PR.
Cancelled, failed, skipped, timed-out or incomplete runs remain unaccepted.
An artifact's presence alone is not success.

## Publication and review

Follow ADR-0088's bounded sequence:

```text
DESIGN_MERGED
-> LOCAL_GREEN
-> PUBLICATION_REVIEW_CLEAN
-> PUBLISHED
-> REMOTE_GREEN
-> FINAL_REVIEW_CLEAN
-> MERGED
```

Local Green includes native preflight controls, workflow/script validation,
the existing full author matrix, clean staging and exact evidence. Publication
review explicitly records remote CI as not yet runnable and authorizes only
the reviewed publication. It is not completed implementation acceptance.
Record `actionlint .github/workflows/ci.yml` separately from the unchanged
canonical planner results.

After publication, inspect the real PR run before final review. Corrections
repeat the same local and independent-publication checks before another
commit. Final review reads the complete change and remote evidence, without
routine reruns. A failed remote run never permits merge.

After merge, verify the main-branch run before reporting CI operational.
Existing repository rules and independent review remain in force; configuring
a new required-check ruleset is outside this outcome.

## Implementation scope and tests

The separate implementation owns the one workflow, the bounded preflight
helper, focused native preflight controls, Python process-site registration
where required, and directly related documentation.

Fresh Red first records that the hosted workflow does not exist. Native
controls then reject unsupported host/runtime/tool families, changed or
unbound firmware, missing history, dirty/mismatched source and malformed
event/base identity before launching the complete plan. Existing runtime,
firmware and planner tests remain active rather than being duplicated.

The first actual PR run is the remote integration Green. Source parsing or
local success alone does not prove that GitHub can provision and execute
the job. No runtime correction, Linux closure, cache/reuse extension,
installer framework, secrets or deployment belongs to this implementation.

# Validation Evidence and Owned Build Changes

## Status and outcome

This is the design contract for Proposed
[ADR-0064](../adr/0064-fail-closed-validation-evidence-reuse.md).
It is not implemented. Existing fresh planner behavior remains authoritative
until the separately reviewed design is Accepted and merged.

The paired tasks are `validation-plan-dedup-design` and
`validation-plan-dedup-implementation`. The design is active; implementation
is pending. No executable prototype or build change belongs to design.

The goal is to avoid re-executing an already successful exact current-context
command, and avoid unrelated guests for a strictly recognizable existing
workflow-command edit. Required coverage, independent review and fail-closed
fallback do not become optional.

## Preserved authority

The planner remains repository-rooted. It discovers the branch range, index,
tracked worktree and untracked paths separately and unions explicit paths.
Deleted and renamed paths keep both identities.

It computes required commands before consulting evidence. A receipt cannot
select paths, change ownership, lower a tier, cover a different command,
remove a diff check, approve a review or authorize merge.

An explicit full tier always executes fresh. Existing invocations without
reuse execute fresh. Documentation and the three staged/worktree/branch
checks always execute fresh in every mode.

The existing manual, ownership-disjoint cross-tree carry-forward rule in
ADR-0034 remains separate. This design does not automate that rule or reuse
across HEADs or staged trees.

## Supported interfaces

The existing `--tier`, `--base`, `--path` and `--execute` interfaces retain
their meaning. The implementation adds:

- `--reuse` with authoritative `--execute`: permit eligible exact receipts
  after computing the complete required plan;
- `--fresh` with `--execute`: explicitly prohibit receipt reuse;
- `--record-workflow NAME`: execute one canonical workflow fresh and record
  its result, clearly labeled **gate-only, not final author validation**.

`--reuse` and `--fresh` are mutually exclusive. Explicit `--tier full` rejects
`--reuse`. Recording is mutually exclusive with authoritative `--execute`,
explicit paths and a nondefault tier. It accepts only an exact current
inventory name, never a shell string, arbitrary executable or custom argv.
Argument disagreement is an error, not an ignored option.

Plan-only output stays nonexecuting and shows all required commands.
Reuse-enabled execution additionally reports each command as `FRESH`,
`REUSED`, `FAILED` or `NOT_RUN`; it never labels a reused command as a new
subprocess execution.

## Local evidence identity

The ignored store is `build/validation-evidence/v1/` under the planner's
resolved repository root. No global user cache, remote cache or
cross-worktree lookup is added.

A successful receipt identifies:

| Field | Required meaning |
| --- | --- |
| Schema | Exact supported version, no unknown fields |
| Repository | Resolved worktree and Git-common-directory identity |
| Candidate | Exact HEAD object and exact `git write-tree` object |
| Command | Canonical workflow name and exact fixed argv tuple |
| Producer | Unique run ID, executing tool version and fresh-attempt ID |
| Context | Host/platform, tool/environment and configured-input digests |
| Inputs | Exact source identity and declared external-input closure |
| Outputs | Declared artifact paths, kinds, modes and content digests |
| Result | Actual subprocess exit zero and completed postflight |
| Record | Complete stdout/stderr log path, size and SHA-256 |

HEAD equality alone never qualifies. Execute/record preflight requires a
clean tracked worktree relative to the index and no untracked task input.
Staged changes are allowed only because their exact tree is recorded.
Preflight and postflight must agree on HEAD, staged tree, tracked cleanliness,
tool/context fingerprints and absence of untracked task inputs.

A changed candidate invalidates the whole receipt, even when the edit seems
unrelated. Branch-range diff checks still execute against the current
selected base; a cached command never satisfies those checks.

## Closed input and artifact manifests

Eligibility is explicit per canonical workflow. The reviewed declaration
lists its configured build directory, produced artifacts and external input
classes. Source membership alone never establishes behavioral coverage.

The context fingerprint includes the resolved executable bytes and version
of every tool used by that workflow, the complete inherited environment
digest, host OS/architecture identity, relevant CMake configuration and
generator files, compiler resource/sysroot inputs, Python runtime/import
inputs, and QEMU/firmware inputs when applicable.

Environment values are hashed in memory and never written to the receipt or
log. The ledger must not record credentials, environment values or arbitrary
caller commands. Tool paths are resolved before use; symlink targets and
content, not only timestamps or version strings, determine identity.

The producer must close the manifest before issuing an eligible receipt.
Missing tools, unsupported configuration, unknown import/tool/resource
resolution, external includes, unenumerated runtime dependencies or
unrecognized input classes make that workflow ineligible. It still runs
fresh. A partial manifest cannot publish a reusable success.

Native declarations cover their executable set, CTest inventory and model
inclusion, build configuration and runtime dependencies. QEMU declarations
cover the actual kernel, checked images/seeds and firmware, not just a
successful marker string. Image declarations cover checked link/runtime
artifacts and exact seed payloads. Content-derived fingerprints include
regular-file contents and modes, resolved link targets and directory
membership so additions or removals cannot evade invalidation.

On lookup, every declared output and complete log must still exist with the
recorded identity. A removed or edited artifact, changed configuration,
unclosed manifest or digest disagreement is a diagnosed miss followed by
fresh execution.

The initial implementation supports only configurations whose dependency
closure it can enumerate using repository/CMake metadata and installed tool
introspection. It adds no dependency installation or general package
resolver. Unsupported platforms/configurations retain fresh validation.

## Receipt lifecycle

One OS-backed exclusive lock protects the resolved repository-local store.
Lock contention reports an error; correctness never depends on a sleep,
unbounded retry or stale-lock recovery mechanism.

For each required workflow:

1. capture the current candidate/context and validate any matching receipt;
2. if reuse is enabled and every field matches, record `REUSED` with the
   original producer, HEAD/tree, command and log digest;
3. otherwise explain the miss and invalidate a previous success for this
   fresh attempt before launching the fixed command;
4. execute once, retaining complete output and the actual exit status;
5. on success, verify unchanged source/context and complete output closure;
6. atomically publish the complete log and then its successful receipt.

An in-progress attempt is not success. A failed fresh rerun, interruption,
missing output, changed input, truncated log or publication error cannot
leave an older success eligible for the current command. A failure stops
authoritative execution and leaves later required commands `NOT_RUN`.

Temporary publication files stay within the owned store. Receipt/log paths
are fixed relative paths; traversal, external paths and symlink substitution
are rejected. Malformed JSON, unknown schema, unsupported fields and
verification failures are explicit diagnostics, never silent success.
Ordinary absence is a cache miss, not a fabricated pass.

The final report binds the required sequence and every fresh/reused result
to the current HEAD/tree. It includes source provenance for reused evidence.
Any failed, not-run or unverified requirement blocks review. An independent
reviewer still reads the current diff and exact producer evidence.

## Bounded CMake exception

The exception applies only to content discovered for the exact repository
`CMakeLists.txt`. It never applies to a caller-supplied impact override.
An explicit CMake path without a verified before/after change stays shared.

The first implementation recognizes only existing terminal intent commands:
reviewed `add_custom_target` definitions for known native/model, QEMU and
image workflows. The reviewed owner table explicitly binds each recognized
definition to workflow/model/image ownership. It does not derive ownership
from target names, source lists or dependency reachability.

Every discovery layer is checked separately: selected-base range, HEAD to
index and index to tracked worktree. Each changed CMake snapshot must satisfy
all of these conditions:

- the same uniquely identified command exists before and after;
- target name and command kind are unchanged;
- enclosing literal guards, nesting and surrounding statements are unchanged;
- the body uses only the reviewed workflow-command grammar and known
  arguments/variables;
- every changed byte is inside recognized owned bodies; and
- all other CMake bytes match exactly.

The bounded scanner must distinguish quoted strings, escapes and comments
from syntax. Bracket arguments/comments, unsupported generator expressions,
unknown variable references, macros, includes, function calls, global
setters, control-flow changes, malformed tokens or ambiguous boundaries
escalate to full validation. Merely adding region comments is not proof.

The command grammar permits only the known workflow target's fixed runner,
its canonical arguments, literal markers/timeouts and declared dependencies.
It cannot introduce arbitrary commands, mutate another target, change a
runner path, add an include or execute a new tool under narrower ownership.
Changing such structure remains full.

Added/removed targets, new workflows, renamed targets, changed enclosing
guards, factories, source bundles, compile/link options, generated rules,
presets or toolchain files remain full. This deliberately does not optimize
the broad node-P addition itself.

Impacts from every valid owned edit are unioned with normal path ownership.
One unsupported/shared layer or mixed unknown path selects full coverage.
A later net revert cannot conceal an earlier unsupported discovery layer.
Code PRs still require complete native and smoke; slow/model/image ownership
remains exact. Full remains coverage-equivalent to the documented matrix.

## Responsibility owners

| Planned owner | Responsibility |
| --- | --- |
| `tools/validation_plan.py` | Existing root/discovery/inventory/selection facade and CLI |
| `tools/validation_evidence.py` | Canonical execution, candidate/context manifests, ledger lifecycle and provenance |
| `tools/validation_build_impact.py` | Bounded before/after CMake recognition and explicit impact result |
| Corresponding focused host tests | Independent receipt/classifier negative cases and fake subprocess counts |
| Existing planner/source-layout tests | Preserve fail-closed path coverage and independent expected-value oracles |

No production or operating-system interface changes. No test success is
inferred from an artifact without an actual successful producer execution.
The independent source-layout expectations must not import production owner
tables simply to remove duplication.

## Acceptance evidence

Implementation starts with failing host regressions, then preserves existing
planner tests. Required deterministic cases include:

- a valid same-HEAD/tree/context receipt, exact producer provenance and zero
  duplicate subprocess executions on the second reuse-enabled plan;
- changed HEAD, index, worktree, untracked input, environment, tool, recipe,
  configuration, import/resource input, artifact or log forcing fresh work;
- malformed schema, missing/truncated data, wrong command, unknown workflow,
  wrong repository/worktree and symlink/traversal rejection;
- failed or interrupted fresh rerun invalidating an old success; atomic
  publication and competing publisher rejection;
- fresh default/full/documentation/diff behavior and gate-only recording
  that cannot report final author success;
- exact required-sequence equality between fresh execution and
  fresh-plus-verified-reuse, with every unsatisfied requirement blocking;
- one valid existing-workflow CMake marker/timeout edit selecting only its
  explicit owners plus required native/smoke;
- multiple owned edits unioning owners; unknown syntax, changed guards,
  factories, source sets, presets, added/removed commands and mixed layers
  selecting full;
- quoted/comment delimiter adversaries, malformed nesting, generated
  expressions, hidden extra commands and later-reverted shared edits;
- all current inventory, rename, explicit-path-union, root/subdirectory and
  slow-model inclusion regressions.

Before review, the author runs focused host checks, complete native
validation and a fresh full current milestone plan. The PR records exact
HEAD/tree/commands/results, clean tracked/index state, no untracked task
files, actual producer output and independent read-only review.
On a documented supported development configuration, the author must also
demonstrate actual successful recording and subsequent valid reuse for
complete native, one QEMU workflow and one image workflow. A design that
always declares every real workflow ineligible does not meet the outcome.
The second plan must name the original producer/log and perform no duplicate
workflow subprocess while still running its fresh documentation/diff checks.

Design validation is documentation/structure and the three diff checks only.
Design commits no implementation, executable tests, models, harnesses,
scaffolding, dependencies or reusable prototypes.

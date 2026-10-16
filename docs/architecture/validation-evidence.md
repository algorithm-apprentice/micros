# Validation Evidence and Owned Build Changes

## Status and outcome

This is the design contract for Accepted
[ADR-0064](../adr/0064-fail-closed-validation-evidence-reuse.md).
The implementation source owners and command interfaces are listed below.
Fresh planner execution remains the default; opt-in receipts do not approve
review or merge.

The paired tasks are `validation-plan-dedup-design` and
`validation-plan-dedup-implementation`. Original design #129 and corrective
design #131 are merged; their independent reviews are clean. The fresh
implementation follows both contracts. No executable prototype or build
change belonged to either design.

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
Executing modes capture the candidate before inventory/path discovery, verify
the same identity after selection and pass that identity into execution.
Execution may not establish a newer baseline for an already selected plan.
Plan-only mode remains nonexecuting and does not capture a staged tree.

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

HEAD equality alone never qualifies. Reuse and gate-only recording require
a clean tracked worktree relative to the index and no untracked task input.
Staged changes are allowed only because their exact tree is recorded.
Eligible producer preflight and postflight must agree on HEAD, staged tree,
tracked cleanliness, tool/context fingerprints and absence of untracked
task inputs.

Legacy fresh iteration, including `--tier fast --execute`, continues to
allow unstaged tracked edits. It retains the existing untracked-file refusal.
A missing indexed path has an explicit absent marker in dirty source
identity; an unstaged deletion is not a preflight filesystem failure.
A dirty fresh execution cannot publish reusable evidence or become final
author evidence, but receipt eligibility does not make that iteration fail.
Fresh execution still invalidates any previous success before attempting a
canonical command. Clean fresh executions may publish eligible receipts;
`--reuse` never weakens the final author-evidence cleanliness requirement.

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

### Initial supported profile: Darwin arm64 with Ninja

The v1 reusable inventory is exactly the following three rows. Other
canonical workflows may still be recorded as fresh, ineligible executions;
they cannot publish reusable receipts. Expanding eligibility requires a
separately reviewed declaration and matching independent closure tests.

| Workflow | Configure/build identity | Required output closure |
| --- | --- | --- |
| `test-unit` | `host-debug`, `build/host-debug`, build target `test-unit` | Every executable and file argument in the complete CTest inventory, including all slow labels; generated seed payloads; native dependent dylibs; the actual Python discovery command and complete success log |
| `test-qemu-smoke` | `riscv64-debug`, `build/riscv64-debug`, target `test-qemu-smoke` | `kernel/micros.elf`, `kernel/micros.map`, checked link artifacts and actual successful guest log |
| `build-tty-service-image` | `riscv64-tty-service-image`, `build/riscv64-tty-service-image`, target `micros_tty_service` | `bootstrap/micros-tty-service.elf`, `bootstrap/micros-tty-service.map` and checked link artifacts |

The resolved committed preset must contain exactly configure-then-build
steps, Ninja, Debug and the row's configured directory/target. The native row
has host tests enabled; the target rows use
`cmake/toolchains/riscv64-clang.cmake`, the RV64 triple and only their
documented image option. User presets, injected launchers, extra project
includes, external source/include directories, custom Python import roots
and unlisted workflow steps are ineligible, never guessed equivalent.

The supported host is Darwin arm64 with authenticated root enabled.
Fingerprint the OS version/build including supplemental response identity,
kernel identity, mounted root's sealed APFS snapshot UUID and the active
dyld shared-cache UUID returned by `_dyld_get_shared_cache_uuid`.
System tools/frameworks within `/usr/bin`, `/usr/lib` and `/System/Library`
belong to that sealed runtime tuple; an unavailable, unsealed or unresolved
identity makes reuse ineligible. Non-system tool/library bytes are never
covered by an OS version string alone.

For this profile, these are the authoritative resolution rules:

- CMake/CTest and Ninja are resolved executables; hash their versioned
  installation trees, including CMake modules. Only resolved Homebrew Cellar
  installations are supported for these tools and the target LLVM/LLD/QEMU.
  Resolve native Clang, the active developer directory and SDK using `xcrun`;
  hash the actual selected toolchain `usr` tree, SDK tree and Xcode Python
  framework tree. Hash Python's resolved executable, standard-library tree,
  extension modules and import-search directory identities. Nonstandard
  site/import customization is ineligible.
- Target execution requires explicit `MICROS_CLANG` and `MICROS_LD_LLD`
  selections, matching the CMake cache. This closes the toolchain's existing
  selection path without executing Homebrew discovery during the workflow.
  Hash the selected LLVM/LLD installation trees, including compiler resources
  and subsidiary tools. Native cache compiler/shim resolution must agree
  with the active `xcrun` toolchain. Unknown compiler, linker, launcher,
  archiver or post-link executable resolution is ineligible.
- Recursively resolve non-system Mach-O dylibs for tools and native test
  executables with `otool -L` and load-command/rpath metadata. Hash each
  resolved dependency's versioned installation tree or the declared Xcode
  toolchain/Python tree. System dependencies use the sealed runtime above.
  Unresolved `@rpath`, missing files or an external dependency outside these
  admitted roots make the workflow ineligible. This is a bounded Mach-O
  resolver, not a general package manager.
- QEMU's ordered data roots come from its actual `-L help` output. Hash their
  complete trees and resolve `opensbi-riscv64-generic-fw_dynamic.bin` by that
  order, including the first existing file's link target and bytes. Thus
  `-bios default` is not treated as an unknown or version-only firmware.
  An override, unresolved default, unrecognized data root or firmware outside
  the admitted QEMU installation is ineligible.
- Hash `CMakeCache.txt`, `build.ninja`, included generator/CMake inputs and
  configured compiler metadata. Enumerate every selected target's regular
  outputs and generated inputs using Ninja/CMake metadata, including
  dependency records; missing or unrecognized metadata is ineligible.
  CTest JSON discovery supplies the native executable/file-argument set and
  labels, checked against the complete native target and committed tests.
  Repository inputs bind to HEAD/tree; ignored generated inputs are hashed
  explicitly. Temporary test-result/timing logs and Ninja execution counters
  are not inputs, but the producer's complete log remains a required output.
- The inherited environment is hashed, never stored as plaintext. Tool
  selection, import, compiler, loader and CMake override variables must
  resolve within the rules above; injection/preload, nonempty compiler flag
  overrides, nonstandard package/import paths and user-site loading are
  ineligible. The profile requires `PYTHONNOUSERSITE=1`; absent ordinary
  standard-library zip/search locations have explicit absent identities.
  Native Python test subprocesses are the current reviewed
  Python CLI and CMake configuration probes; their runtime/tool closure is
  included, not omitted because they are children of unittest.
  Nonempty sanitizer override/symbolizer variables, including `ASAN_OPTIONS`,
  `UBSAN_OPTIONS`, `LSAN_OPTIONS` and `LLVM_SYMBOLIZER_OPTS`, make this initial
  profile ineligible: option includes, suppressions and external executables
  are not admitted by an environment-string digest. Fresh execution preserves
  those variables unchanged.

Tree fingerprints include relative names, kinds, modes, regular-file bytes,
link spelling/target and directory membership. Deduplicating resolved files
within one fingerprint is allowed; metadata-only persistent digest caches
are not. An unreadable tree, escaping/unknown link or dependency makes reuse
ineligible. Runtime-discovered installation paths/digests are data, not an
opportunity to extend these admitted input classes.

### Accepted default-configuration correction

Accepted
[ADR-0065](../adr/0065-closed-tool-default-configuration-for-validation.md)
corrects the concrete initial-profile omission found in abandoned
implementation #130. Independent correction review is clean. Implementation
may restart only after this separate design merges.
Unknown roots continue to be rejected; no workaround was merged.

The corrected initial profile requires an inherited
`CLANG_NO_DEFAULT_CONFIG=1` and excludes explicit compiler configuration,
response files and unresolved argument-injection environment. Only the
selected LLVM Cellar installation's `etc/clang` alias may add the derived
same-prefix `etc/clang` directory as opaque, completely fingerprinted data.
No other escaping package link is admitted. Raw config bytes are not logged;
disabled automatic loading is not inferred merely from a successful marker.

The QEMU profile additionally requires both its versioned-installation and
same-prefix `etc/qemu/qemu.conf` locations to remain absent, with explicit
checked absence identities, and likewise requires `qemu-bundle` beside the
resolved executable to be absent. All three checks precede any QEMU metadata
query, including `-L help`, which may otherwise load default configuration.
An unknown layout, config option or present/unreadable file/directory/link
makes the workflow ineligible, not silently reconfigured.
The guest command and all required gates remain unchanged.

Accepted
[ADR-0072](../adr/0072-qemu-runner-source-decomposition.md) defines a
separate, bounded source-organization transport for this reviewed argument
builder. Its [exact source contract](qemu-runner-source-organization.md)
requires complete original runner byte/AST restoration before the single
file declaration becomes a fixed ten-member family with narrowly closed
loader inputs. This is not implemented by the design alone: complete independent design/
correction and acceptance-only reviews are clean, and separate design #145
is merged. Its fresh implementation transports the declaration only after
the required actual-source proof; design status alone is not execution or
reuse evidence. All other profile, default-config, coverage,
provenance and failure rules stay unchanged.

The already required native shim/import closure also distinguishes declared
SDK paths from arbitrary include/library overrides. Active SDK/toolchain
paths are fully hashed. Standard `/usr/local/include` and `/usr/local/lib`
fallback locations may have explicit absent identities only; their presence
is not permission to admit another input root. `PYTHONNOUSERSITE=1` remains
mandatory. The inactive standard Xcode Python startup declaration
`Versions/3.9/lib/python3.9/site-packages/distutils-precedence.pth` may be
recognized only at SHA-256
`7ea7ffef3fe2a117ee12c68ed6553617f0d7fd2f0590257c25c484959a3b7373`
with `SETUPTOOLS_USE_DISTUTILS` absent or exactly `stdlib`.
Any other startup file, activation or import customization is ineligible.
The complete framework tree is still fingerprinted.

Driver invocation spelling remains part of the command identity. Resolving
`ld.lld` to its executable bytes must not replace it with the multicall name
`lld` for execution or version queries. Runtime metadata refers to the active
arm64 slice, actual CTest programs rather than data/intermediates, and Python
extension loaders in the actual interpreter's rpath context.
Ninja's unselected sibling-output edges cannot hide inputs: missing input
identities remain unsupported, and complete configured regular-output
inventory covers sibling artifacts. These are bounded resolution rules,
not permission to infer behavioral ownership from build membership.

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

The v1 exception recognizes exactly three existing terminal QEMU commands:

| Target | Exact enclosing guard | Complete workflow owners |
| --- | --- | --- |
| `test-qemu-smoke` | None | `test-qemu-smoke` |
| `test-qemu-panic` | `MICROS_BUILD_PANIC_TEST` | `test-qemu-panic` |
| `test-qemu-scheduler-invalid-context` | `MICROS_BUILD_SCHEDULER_INVALID_OUTGOING_TEST OR MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST` | Both `test-qemu-scheduler-invalid-outgoing` and `test-qemu-scheduler-invalid-next` |

These rows own no model or image workflows: they edit a terminal runner
timeout, not a product build. PR-tier complete native and smoke remain
mandatory. In particular, the shared scheduler-invalid target does not
authorize selecting only one of its two configured workflows.
All other native/model/image/QEMU definitions remain shared in v1.
Ownership is never inferred from target names or dependency reachability.

The fixed template for each row is its complete target command in
`CMakeLists.txt` at `ee6be77c1d786eea0cae96b175a9923eba3e0b55`.
The sole mutable token is the one decimal argument immediately following its
unique `--timeout`: an unquoted integer from 1 through 600, without leading
zeroes. Both snapshots must match every other byte of that reviewed target
template. Reindentation, marker edits, new comments or any additional token
are not exceptions. The implementation retains independent literal owner
and template expectations in tests, not imports of production tables.

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

The immutable template permits only its existing
`"${MICROS_PYTHON}" -B` runner,
`"${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"`,
`"${MICROS_QEMU}"`, `"$<TARGET_FILE:micros>"`, existing literal arguments,
`${PROJECT_VERSION}` and, only in the scheduler row,
`${MICROS_SCHEDULER_INVALID_STATE}`. Its sole `DEPENDS micros`,
`WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"` and `USES_TERMINAL` are
immutable. The existing scheduler state selector also remains outside the
mutable body and byte-identical. No other variable/expression is admitted.

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

Implementation follows merged design #129 at
`0f61a2b2ff2b80aa34f55c51352407090f96b5d0` and correction #131 at
`b3f042c00c660c8ad69156e83f1a23c692582a30`, in these green slices:
bounded CMake/Git-layer classification; receipt identity/publication and
failure tests; supported context/artifact closure; canonical CLI execution
and provenance; then actual recording/reuse and the fresh full milestone
gate. No slice may relax the Accepted contract or its independent oracles.

| Planned owner | Responsibility |
| --- | --- |
| `tools/validation_plan.py` | Existing root/discovery/inventory/selection facade and CLI |
| `tools/validation_evidence.py` | Ledger ownership, strict receipts, complete logs and atomic invalidation/publication |
| `tools/validation_context.py` | Exact staged/source identity, fixed tool/runtime profile and complete-manifest orchestration |
| `tools/validation_inputs.py` | Shared content/mode/membership identities and bounded read-only metadata I/O |
| `tools/validation_build_manifest.py` | Selected Ninja/CTest recipes, dependency records and complete artifact inventories |
| `tools/validation_execution.py` | Canonical execution, source/context postflight, receipt consumption and attributed outcomes |
| `tools/validation_build_impact.py` | Bounded before/after CMake recognition and explicit impact result |
| Corresponding focused host tests | Independent receipt/classifier negative cases and fake subprocess counts |
| Existing planner/source-layout tests | Preserve fail-closed path coverage and independent expected-value oracles |

No production or operating-system interface changes. No test success is
inferred from an artifact without an actual successful producer execution.
The independent source-layout expectations must not import production owner
tables simply to remove duplication.

The private dependency direction is planner -> execution -> context ->
selected-build manifest -> input identities. Context also reads the input
identity owner directly; neither leaf imports the facade, execution or
planner. Frozen function-body evidence preserves the extracted algorithms,
and explicit import/ownership tests protect the narrow shared contract.

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
- one valid panic timeout edit selecting exactly complete native, smoke and
  panic plus the three diffs; one scheduler-invalid timeout edit selecting
  both invalid-context workflows plus native/smoke and the three diffs;
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

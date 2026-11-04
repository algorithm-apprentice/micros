# Panic-Trace Author and Current Execution Scopes

## Status and scope

This is the Accepted PLAT03-C02 contract under
[ADR-0099](../adr/0099-panic-trace-execution-scopes.md).
The [fixed-source study](../research/panic-trace-current-scope-blocker.md)
records the actual failed complete EV01-C03 author run.

The fixed current source is design #205:

```text
commit: 8d4fd8c0dad354a7840a70d1505fa0cfebd2888f
tree:   b2a444d27e0e6cc43d5f5d1e6a5926c52b520dc2
```

EV01-C03 implementation is preserved as reference, not merged or accepted.
Only this corrective design is active. Its implementation follows separate
review, Acceptance and merge; EV01-C03 restarts afterward from current main.
MEM01 and hosted CI do not proceed in parallel.

## Two different authority lifetimes

| Boundary | Historical author/baseline proof | Ordinary current regression |
| --- | --- | --- |
| Source membership | Exact fixed design members plus declared PLAT03 additions | Complete current tracked/index membership |
| Production outside six trace candidates | Exact fixed runtime blobs, with only declared observation inverses | Actual current bytes, stable for the entire run |
| New trace production | Typed absence may be admitted before production implementation | All six declared members must be present and captured |
| Evidence owners | Eight exact declared owners | Same eight required owners, plus all other current tracked inputs |
| Tool/firmware/artifact binding | Exact and stable; matching full pair | Exact and stable within the current run |
| Baseline/current comparison | Complete immutable equality required for initial author acceptance | No dependence on an old local baseline log |
| Result | Authenticated old-runtime divergence or failure | Actual current eighteen-case/four-oracle pass or failure |

The historical `SOURCE_ROLES` declaration remains unchanged. It identifies
the original task's candidates/evidence owners and remains a permanent
invariant at that boundary. It is not a prohibition on all later repository
development.

Current manifest entries named `immutable` are run-stable current inputs,
not a claim that every such byte equals the original pre-PLAT03 runtime.
This distinction does not let a current-only report substitute for an
author baseline/current pair.

## Historical baseline behavior

Retain the complete existing `require_source_scope` behavior for the
historical baseline path, including:

- the fixed design membership digest and exact admitted union;
- only the existing typed optional new trace files before production;
- exact noncandidate production blobs/modes and observation inverses; and
- checks before any target build and after execution before publication.

Retain `prepare_baseline` and `validate_baseline`: the archived runtime stays
fixed to `e12b2d6c163ac92282d380eff5be419fad7f7a30`; only the existing
observation/fixture integration is admitted. No new production trace owner
is copied into that archive.

At current #205, `--baseline` must still reject the three newer documents
as outside the historical author scope. Do not repair that rejection by
changing the old digest, permitting Markdown or accepting all current files
as historical members.

The baseline/current author comparison still requires complete immutable
source/tool/firmware equality, actual old execution and four individual
expected oracle failures. A later source addition makes an old pair
inapplicable; it does not become an authorized exception to equality.

## Ordinary current behavior

Before creating target-build artifacts:

1. Require an actual Boolean execution mode; integers, strings and `None`
   are not alternate mode spellings.
2. Obtain the existing complete current manifest from a clean fully staged
   candidate with no untracked inputs.
3. Require its subject membership to contain exactly all six declared trace
   production paths. No missing file is typed absence in current mode.
4. Preserve all eight evidence owners, the checked recipe/presets, frame-chain
   options and closed tool/firmware capture.

Do not invoke the historical whole-repository membership or fixed
noncandidate-production check for ordinary current execution.

The existing manifest captures every tracked path, including additions from
later tasks. Files outside the six trace subjects remain in the run-stable
input map. No extension, suffix, directory, caller label or task ID removes
a file from capture.

Build the real current target with the existing fresh private
configure/clean-first commands. Preserve all actual ELF/stack/observation
bindings, eighteen cases, four independent oracles, original acceptance,
packet/read/write restrictions and physical completion/reset requirements.

Before publishing success, repeat the full current source/tool/firmware and
actual artifact capture. Require complete equality with the initial
manifest/artifact identities. This also preserves the required subject and
evidence membership after execution. Added, deleted or modified inputs
cannot be hidden by a narrower final comparison.

Missing current inputs, dirty/untracked state, recipe drift, tool/firmware/
source/artifact changes, build or execution failure remain exit 2 with no
new successful report. An older report must never be treated as evidence
for a failed invocation. Current exit 0 still requires all eighteen cases
and four oracles. Baseline exit 1 retains its existing precise meaning.

## Implementation boundary

Change only the mode dispatch/current subject precondition in
`tools/check_panic_traces.py`, focused controller regressions in the existing
`tests/host/test_panic_trace.py`, and directly related documentation.

Keep the source-manifest capture mechanism, historical scope helper,
baseline archive/validation, report schema, source/recipe/effect declarations,
oracles, runtime and target fixture behavior unchanged. No new module,
dependency, workflow, CLI option, generic current-scope registry or
per-task allowlist is needed.

For `n` current inputs and `b` captured bytes, preserve the existing complete
snapshot's `O(n + b)` work and `O(n)` manifest storage. Checking the fixed
six-subject membership adds only bounded host work. There is no kernel
algorithm, stack or hot-path change; the MINIX/RV64 trace baseline is
unchanged.

## Test-first evidence

Native regression must reproduce the real current-mode rejection caused by
the already committed #205 documents, not merely assert a new helper name.
Exercise the controller's public execution path with a controlled build
boundary; this is a source-scope/protocol test, not a mock proof of hardware
trace behavior.

Focused cases must establish:

- current execution can reach its build boundary with additional tracked
  files while retaining those inputs in its complete manifest;
- the same historical baseline scope still rejects extra members and
  changed noncandidate production;
- every one of the six current production subjects is mandatory, including
  complete absence rather than only untracked/ignored replacement;
- current dirty/untracked state and missing evidence ownership still fail;
- whole current subject/other-source/tool/firmware/artifact changes fail
  before publication, not just selected-value drift;
- historical scope checks remain before and after the baseline execution;
  and
- non-Boolean modes and failed captures/builds/oracles cannot publish a
  success-shaped result.

Retain existing source reconstruction, authority, observer and oracle tests.
No native control fixture is accepted as evidence that an actual guest
trace or old-runtime Red passed.

## Renewed fixed-envelope target regression

The corrective controller is one of the original frozen evidence owners.
Do not reuse an old baseline record as if it were produced by the new bytes.

After native Green and before final review, create one owned detached
worktree at fixed PLAT03 #204:

```text
commit: ddca7661d4fa7e13acf0aaa5f0ba55b91f9c9916
tree:   3bf57ef9d12345f7b72e082a5843c97a2476f23e
```

Overlay only the current implementation bytes of
`tools/check_panic_traces.py` and `tests/host/test_panic_trace.py`, preserving
their ordinary modes, and stage them. Require exactly that complete
two-file diff, no untracked inputs and equality with the current
implementation's two file bytes. All other tracked source remains byte/mode
equal to fixed #204. Do not copy the newer design documents or any
EV01-C03 implementation.

The resulting historical-envelope tree still has exactly the original
1,431 admitted members. Run the existing documented baseline and current
commands there with the same explicit tool environment. Require:

1. all eighteen actual baseline executions, original acceptance and positive
   control, four named expected oracle failures and actual exit 1;
2. all eighteen actual current executions/four oracles and exit 0;
3. exact complete immutable source/tool/firmware equality, plus unchanged
   source and actual artifact snapshots before/after both commands; and
4. preserved JSON/log identities, exact overlay and staged-tree evidence
   before removing only that owned validation worktree.

This pair is a controller regression in a fixed historical envelope. It is
not the complete current checkout, an EV01-C03 acceptance run or a new
runtime retirement. Label its source/tree separately and do not mix it
with current-candidate producer receipts.

## Complete current acceptance

Independently preserve all 3,938 complete current facts, fourteen
fingerprints and both prior claims. No inventory metadata exception is
authorized here; the EV01-C03 one-record change remains pending.

On the real complete current checkout, run the unchanged full fresh
50-command author plan, including actual current panic-trace execution,
44 static configurations/113 ELF outputs and ten observed scopes.
B13 remains blocked. Preserve ordinary journal-free prior authority.

Obtain independent implementation review before the first implementation
commit, then verify reviewed/committed/remote/squash tree equality and clean
only the completed task branches. Restart the preserved EV01-C03
implementation from current main afterward; its old incomplete producer is
not reusable as complete acceptance.

## Design phase barrier

This proposal adds documents only. No controller, test, model, fixture,
prototype, build scaffold or dependency is authored before independent
review, Acceptance and separate design merge.

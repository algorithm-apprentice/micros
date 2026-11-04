# ADR-0099: Panic-Trace Author and Current Execution Scopes

- Status: Accepted
- Date: 2026-11-04
- Refines: ADR-0033, ADR-0034, ADR-0080, ADR-0082, ADR-0095, and ADR-0096
- Supersedes in part: applying PLAT03's historical authoring membership and
  production freeze to every later ordinary current execution

## Context

PLAT03 #204 passed its original complete acceptance. EV01-C03 design #205
then added three canonical documents. Its implementation passed native
checks but the complete author plan failed before the ordinary panic-trace
target could execute.

The [fixed-source study](../research/panic-trace-current-scope-blocker.md)
shows that `execute(False)` applies `require_source_scope` to both modes.
That check admits only the original 1,431 PLAT03 members and freezes every
other production blob. The three already committed design documents make
the current 1,434-member checkout fail. Future unrelated runtime corrections
would fail the production comparison too.

This is not a trace-runtime failure or a missing execution receipt.
The historical scope check is necessary for the original authoring proof,
but is not the lifetime contract of an ordinary current regression gate.
The existing complete current manifest already captures all tracked source,
tools, firmware and actual artifacts and rejects changes during execution.

EV01-C03's Accepted scope excludes a controller-policy change. Its complete
uncommitted implementation is preserved and blocked. PLAT03-C02 design is
the sole active prerequisite; MEM01 remains blocked.

## Decision

After separate review, Acceptance and merge, implement the
[execution-scope contract](../architecture/panic-trace-execution-scopes.md).

### Preserve the historical author boundary

Keep the fixed design/runtime pins, original membership digest, six
candidates, eight evidence owners, strict noncandidate production checks,
typed pre-production absence and exact archive reconstruction unchanged.

Baseline execution continues to enforce that historical boundary before and
after execution. It does not admit newer task files or runtime substitutions.
The original first-commit baseline/current comparison still requires exact
complete immutable-input equality.

### Bind ordinary current execution to the complete current tree

Ordinary current execution no longer requires the historical whole-repository
membership or noncandidate production blobs. It requires the complete clean
staged current checkout, all six current trace production members, the eight
evidence owners and the existing exact recipe/options.

Every tracked addition remains in the current manifest; none is ignored or
projected away. All source, tools, firmware and actual artifacts must remain
identical across the run. All eighteen actual target cases and four oracles
must pass. Current execution creates no retirement or replacement-authority
claim.

Preserve the exact Boolean mode requirement and the existing closed CLI,
exit codes, debugger authority, deadlines, observations, stack/effect proof
and report schema. Add no override, task-specific allowlist, alternate source
root, cached-baseline requirement or generic scope framework.

### Preserve evidence and prove both modes

All 3,938 complete facts, fourteen current fingerprints and prior claims
remain unchanged. EV01-C03's separate one-record correction is not applied
in this prerequisite.

Native regression must expose the actual ordinary-current rejection and
verify that historical baseline rejection remains strict. Cover complete
current membership, before/after source/artifact stability, invalid mode
types and no success/publication on failure.

Renew the actual eighteen-case baseline/current comparison in a controlled
fixed #204 worktree containing only the two reviewed controller/test-file
overlays. This preserves the historical admitted membership while executing
the exact new controller bytes in both modes. Keep complete immutable-input
equality and distinguish this historical-envelope regression from acceptance
of the real current checkout.

The actual current checkout must separately pass the unchanged complete
fresh 50-command author gate, including 44 static configurations/113 outputs
and ten observed scopes, then independent implementation review.

## Non-goals

No runtime, source-assertion retirement, inventory/fingerprint rebaseline,
declaration-map change, ignored current file, recipe relaxation, new target
or workflow, timeout/budget change, hosted-CI acceptance or EV01-C03/MEM01
implementation.

## Phase barrier

This proposal contains documents only. No controller, test, model, fixture,
prototype, build scaffold or dependency changes before independent design
review, Acceptance and separate merge.

## Alternatives

- Add each later task's files to PLAT03's old roles: rejected; that changes
  historical authority and still blocks unrelated production corrections.
- Ignore Markdown or selected new paths: rejected; ordinary current input
  capture must remain complete, not file-type dependent.
- Disable all scope/stability checks: rejected; current execution must bind
  actual stable inputs and baseline execution must retain its old boundary.
- Require the author's old local baseline log for every current run:
  rejected; this is neither necessary nor the Accepted ordinary-gate policy.
- Fold the change into EV01-C03: rejected; it exceeds that Accepted
  implementation's scope and would hide the distinct failure boundary.

## References

- [Execution-scope contract](../architecture/panic-trace-execution-scopes.md)
- [Fixed-source blocker](../research/panic-trace-current-scope-blocker.md)
- [Original trace contract](../architecture/panic-stack-trace.md)
- [Standalone binding prerequisite](0098-standalone-frame-validator-evidence-binding.md)

# ADR-0128: Selected Grant Validation

- Status: Accepted
- Date: 2026-11-12
- Refines: ADR-0005, ADR-0038, ADR-0039, ADR-0040, ADR-0041, ADR-0047,
  ADR-0049, ADR-0069, ADR-0080, ADR-0083, ADR-0084, ADR-0107, ADR-0123
  and ADR-0124
- Supersedes after Acceptance and separate design merge: complete grant,
  endpoint/object and TTY audits and unconsumed-corruption precedence at the
  eight ordinary boundaries in the canonical contract; ADR-0107's inspector
  consumer restriction only for three specified grant exclusion functions

## Context

IPC04 #257 is independently reviewed and merged. GRANT01 is the next preferred
correction, owning B05-F001 and B05-F002; its actual prerequisite is IPC01 #251.

The [fixed local study](../research/minix-selected-grant-validation.md)
traces MINIX's selected grant entry and the current complete-audit wrappers.
IPC01 has already made endpoint resolution local, so the current cost is
`O(G + Vep)`, not the original audit's per-record multiplication.
Ordinary captured grant requests still run complete grant validation twice,
an additional complete IPC audit and, where active, full TTY authority.

The prior audits also establish real record, participant, count and device
preconditions. Deleting calls without replacing their consumed guarantees
would permit stale authority, count underflow or unsafe error reordering.

## Decision

After independent review, Acceptance and separate design merge, implement the
[selected-grant contract](../architecture/selected-grant-validation.md).
This proposal authorizes no executable implementation artifacts.

### One selected authority boundary

Use existing scalar readiness and selected endpoint/object leaves.
Inspect/revoke/copy admit one decoded record's complete storage and stored
participant relation, then retain the original caller, generation,
ownership, permission and bounds order. Reuse actual admitted participant
selections within the current serialized operation.

The view is private automatic state, at most 64 bytes, not a persistent cache,
permission bit or cross-operation lease. Keep token packing, kernel-managed
direct authority, revoke-time generation advance, terminal quarantine and
existing value-only outputs.

Create keeps its lowest-free scan until GRANT02. Admit each visited storage
row, preserve participant-before-allocation order and validate the selected
free slot before publication. Keep the existing unpublished create plan and
success-only count/token commit. Add no allocation or cancellation index.

### Preserve local failures, diagnose global state explicitly

Check scalar count bounds and selected/prefix count consistency before
mutation. Exact aggregate equality remains a complete diagnostic rather
than another scan for every indexed operation.
An in-range mismatch or unrelated malformed record may no longer dominate
an otherwise valid selected operation; consumed malformed storage, stored
participant relations, impossible counts or binding state still fail.

The canonical contract specifies the scope and original test adaptations,
including preserving output sentinels and a separate selected/full count
control. No malformed current state is repaired or captured as an oracle.
Full validators, initialization, cancellation, bootstrap/PM audits and
explicit diagnostic callers remain.

### Captured readiness is not a complete audit

Before decode, retain exact authoritative getters, current captured context
and fatal scalar-readiness checks. Do not select a token, active endpoint,
range, phase or mapping ahead of user-shape errors.
Then dispatch one independently checked portable operation.

Preserve `sepc`, `a0`, other registers, nonblocking return, panic reasons,
stable error order and success-only create output. No new IRQ scope,
scheduler guard, fallback or caller-supplied process authority is introduced.

### Extend the existing TTY inspector only for exclusion

Permit the existing bounded committed-device inspector in exactly
`grant_syscall_create_request`, `grant_copy_execute` and
`grant_copy_prepare_remote_plan`, at their existing device-check positions.
This explicitly refines ADR-0107's memory-only consumer boundary.

Grant participants and permissions are admitted independently. The snapshot
only excludes the exact TTY device range; it cannot resolve a grant,
authorize a TTY/bootstrap/IRQ command, install a mapping or certify the
bootstrap transition graph. Preserve its selected live tuple and pure state
checks, actual device-PTE validation in range planning, ordinary same-VA RAM,
MMIO/scratch/transferable rejection and structural-before-fault precedence.

Do not alter TTY command/full-validation providers or add another controller
binding. Complete diagnostics retain publication/transition/profile/queue
invariants not consumed by an exclusion-only read.

### Keep the remaining mechanisms and evidence

Copy and operation 15 retain phase/zero-length ordering, complete mapping
plans, physical nonoverlap, all-or-error bytes and no lease. GRANT03 still
owns copy spans/block movement. Cancellation remains the exact original
one-use/prevalidated transaction until GRANT02.

Actual unchanged-production native Red must expose the redundant audits
while independent semantics agree. The focused pair executes real portable
and captured C logic with only bounded native runtime/IRQ/translation/panic
backends; no fake authority or validator success is allowed.
Existing actual target and composed-service workflows remain mandatory.

Preserve every original grant/model case, 4,096 transitions, seed and draw
schedule, negative precedence/control, current primitive/source/product
checks and actual stack proof. Current metadata/recipe maintenance belongs
in this outcome under ADR-0123, not another prerequisite PR or repeated
recertification execution.

## Phase and non-goals

This phase is documentation only. `grant01-implementation` remains pending
the separate reviewed, Accepted design merge. No code, tests, models,
harnesses, build scaffolding or reusable executable prototypes belong here.
Independent design review precedes the first design commit; fresh independent
implementation review precedes the first permanent implementation commit.

No free-list/cancellation indexing, new copy algorithm, user grant tables,
delegation/wildcards, new token or syscall, VM retry, TTY command-policy
change, concurrency, persistent cache/history/recovery, larger budget,
hosted-CI acceptance or node-Q/POSIX completion.
Design Acceptance alone does not close either finding.

## Alternatives

- Remove audit calls without selected replacements: loses consumed lifetime,
  canonical storage, count and device preconditions.
- Retain complete audits around every selected helper: preserves the
  divergence under a different name.
- Cache whole-registry validation across operations: adds invalidation and
  persistent authority, contrary to current serialized-view contracts.
- Use the TTY snapshot as command or grant authority: exceeds its descriptive,
  exclusion-only role.
- Add bootstrap indexes or rewrite TTY validation here: combines later
  owners rather than reusing the existing bounded inspector.
- Change allocation, cancellation and copying together: combines GRANT02
  and GRANT03 with this validation-boundary outcome.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
nine-document proposal, fixed local MINIX/current source, relevant Accepted
contracts and exact four-command author evidence. It reported no substantive
design issue remaining, with High confidence. Targeted reproduction: None.

Acceptance records that design result, not an implemented validation change
or B05 finding closure. The separate design merge still precedes implementation.

## References

- [Canonical selected-grant contract](../architecture/selected-grant-validation.md)
- [Fixed local research](../research/minix-selected-grant-validation.md)
- [Committed-binding inspection](../architecture/selected-user-address-space.md#bounded-committed-binding-inspection)
- [Grant source ownership](../architecture/grant-data-plane-source-organization.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)

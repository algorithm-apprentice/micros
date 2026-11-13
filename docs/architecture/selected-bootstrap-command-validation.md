# Selected Bootstrap Command Validation

## Status and scope

This is the independently reviewed BOOT02 contract for B06-F002/F003 under
[ADR-0133](../adr/0133-selected-bootstrap-command-validation.md).
BOOT01 #266, MEM04 #228, OBJ01 #243, OBJ02 #246, IPC01 #251 and GRANT01
#259 are merged. Independent design review is clean and the contract is
Accepted. Design #267 is separately merged; its paired implementation
proceeds from that main.

The [fixed study](../research/minix-selected-bootstrap-command-validation.md)
owns the MINIX/current trace. This outcome replaces complete diagnosis
around ordinary B06 commands, not bootstrap policy, cold preparation,
BOOT03 byte primitives or later VM/PM/TTY command owners.

## Goal and retained authority

An ordinary operation-11 command admits its runtime/controller, selected
service and consumed cross-owner resources, prepares its existing bounded
transition values, then commits in the original protected order.
It does not certify every service, frame, root, grant or application.

Keep all of the following:

- pointer-free manifest and existing public syscall/message shapes;
- exact process/thread/endpoint generations and immutable profiles;
- distinct service, manifest, binding, runtime and scheduler domains;
- BOOT01 indices, selected plans and deadline corruption handling;
- one starting service, exact order and named prerequisites;
- VM handoff and TTY console/IRQ gates;
- expected-caller reply ownership and rollback;
- source-only launcher and one-shot PM sealed notification; and
- one hart, checked one-thread policy, SIE-clear lifetime, current
  completion/common-return protocol and every existing resource budget.

Do not grant command authority from an exclusion-only VM/TTY inspector,
an IPC payload pointer, a cached valid bit or another command's plan.

## Selected control relation

Keep the public portable control signatures and complete diagnostic APIs.
Within the existing control owner, use one private automatic selected
service view, at most 64 LP64 bytes. It may borrow the admitted binding,
manifest entry, runtime entry, process, thread and endpoint, with their
bounded row indices. It contains no array or whole-state snapshot.

The view is valid only for the same unchanged input owners in the current
serialized operation. Do not retain it across reply failure/rollback,
user return, scheduling, mapping mutation or an interrupt-enable boundary.
It is not a reusable authorization token.

### Header and controller admission

Before indexed access, admit the consumed header:

- active counts are nonzero, at most seven and agree across the control,
  manifest count, plan and published runtime;
- the runtime phase agrees with the running control phase;
- both index-zero entries are empty;
- controller and any consumed role IDs are within the existing domain;
- the next-order counter is bounded before any ordered-row access; and
- selected order values agree with the corresponding immutable plan
  position before release.

This is not a full index/table, manifest or topology audit. Do not compare
unused entries or all ordered IDs. Cold constructors and complete
diagnostics keep those responsibilities.

The controller's selected relation binds the plan/runtime controller ID,
controller fields, manifest controller role, exact live process/thread,
root, active endpoint and installed immutable profile. The profile's
bootstrap-operation permission remains necessary. The current captured
process/thread must match the controller; a stored binding cannot replace
the kernel-derived caller.

The controller is READY/active/profile-installed/scheduler-assigned with
no readiness deadline before a running command. Check its selected
scheduler/current/stack relationships through existing selected owners,
not a complete object or scheduler audit.

### Selected service admission

Check each selected binding's manifest index before dereferencing it.
Require exact service identity across the binding, selected runtime row
and manifest entry. The consumed prerequisites and timeout agree with
their immutable manifest values; IDs and row positions remain distinct.

Resolve the exact process, thread and endpoint. Retain the current
binding-to-manifest and binding-to-object predicates: process slot/root/
primary endpoint, endpoint owner/generation, thread owner/context and
the phase-appropriate installed profile, inactive/runnable state and
stored scheduler policy.

Use the existing checked endpoint/profile and object leaves. No privilege
profile narrowing precedes its range check. The view publishes only after
all its required checks succeed.

An absent user-requested ID remains ARGUMENT at its existing handler
position. Malformed consumed count/index/row/manifest or live-owner
metadata is INVARIANT, not absence. A required named role or controller
missing its relation is corruption, not permission to skip that role.
Cross-check selected runtime/binding presence; do not scan for an
unindexed orphan or repair either index.

### Selected versus complete corruption

Only consumed records and scalar readiness are ordinary preconditions.
An unrelated foreign index entry, unused service, orphan mapping or grant
may no longer outrank an otherwise valid selected operation.
Complete diagnostics still reject it.

This does not waive the finite completion barriers below. A staged
acknowledgment or grant/IPC reference involving the launcher is consumed
by revocation and must be checked before sealing.

## Captured entry and error order

Preserve the current adapter sequence:

1. retain captured-register/context ownership and argument-pointer panic;
2. decode command, widths and unused-register shape first;
3. obtain bootstrap state, then IPC state, and authorize the exact running
   controller under the existing unauthorized/fatal distinction;
4. replace the old complete runtime call by consumed header/controller,
   selected binding/root/role-relation admission and scalar registry
   readiness;
5. dispatch the original command and preserve its argument, lifecycle,
   role, endpoint/token and range ordering.

The grant registry remains required where the common current contract
requires it. Use `micros_grant_preflight_context` for its scalar readiness
with the actual IPC/object owners, not `micros_grant_runtime_validate`.
Do not duplicate magic values or turn a missing owner into an empty table.

A missing bootstrap/controller authority or valid non-running target
remains unauthorized as in the current adapter. A missing IPC owner still
uses the original fatal authority boundary. Consumed invariants after
authorization use the existing fatal reason at their owning command,
not a new public error enum.

At the former common-audit position, derive consumed scope from the
decoded command without granting authority from its IDs. For a known
selected service, admit stored index/binding/live-owner/root relations
before handlers can dereference them. Admit the TTY bootstrap relation
there when the command consumes it; malformed consumed relationships
that previously failed common preflight still use AUTHORITY.
This relation admission does not require the command's desired next
phase or ready result: legitimate wrong order/state and incomplete
VM/TTY role gates stay at their original command-specific checks.
VM's private handoff readiness remains its original role-gate decision,
not a new common application-state audit.

Portable control APIs retain their own null/output and valid-phase error
contracts. A selected service relation must be checked before a handler
uses its manifest index. Missing-request lookup is retained as missing
until the original argument check; it must not skip the current RELEASE/
ACCEPT_READY deadline check. Malformed consumed metadata cannot be
dereferenced while waiting for that later check.

Keep RELEASE/ACCEPT_READY's single current-time sample and TTY-first
deadline enforcement. COMPLETE, CONSOLE_BEGIN and FAIL do not acquire a
new timer policy merely because they no longer run complete diagnosis.
Malformed failure reason/detail keeps its existing command-specific order.

Failure preserves the same externally owned state/output as before.
In particular, retain the captured-context store and reversible scheduler
guard semantics on failed ready reply; do not promise a new saved-context
rollback that the current path does not provide.

## Cross-owner preparation

### Endpoint and scheduler

Keep `micros_endpoint_preflight_publish` and the existing prevalidated
publication commit. Its selected relation and required owned-thread/
inhibition checks are not replaced with a successful stub or a new
process/thread equivalence.

Expose only the existing scheduler admission store portion as
`micros_thread_scheduler_commit_admit_prevalidated` in the current private
scheduler interface. The checked public admission function still prepares
then uses that store portion. The B06 release caller prepares once before
endpoint publication and consumes that same admitted policy/queue state
after publication without calling the checked wrapper again.

This trusted commit takes the existing exact objects, hart/thread,
priority, quantum and preemptibility values. It adds no scheduler plan
registry, permission, scheduling policy or recoverable operation.
Retain the existing prevalidated tail enqueue and selected queue checks.

The existing current-IPC guard/reply/completion APIs remain. Constant-size
local checks inside those independently owned boundaries are not a reason
to invent a generic cross-owner transaction framework.

### VM bootstrap role

Keep complete `micros_vm_handoff_runtime_validate` and
`micros_vm_handoff_state_validate` for their existing VM/diagnostic owners.
Expose the existing scalar VM identity predicate through its private
owner rather than duplicating its magic or fixed shape.

Refine the bootstrap-only `micros_vm_handoff_runtime_role_ready` path
to admit that identity, HANDED_OFF phase, exact bootstrap VM binding,
live process/thread/root/endpoint/profile and kernel-operation permission,
plus the authoritative ownership phase. It does not enumerate dynamic
application mapping records or run complete frame-owner metadata diagnosis.

Admit the selected VM root through the existing ownership lookup, including
ledger readiness/geometry and exact process-table kind/process generation.
A matching numeric root in two records or a phase enum alone does not
establish physical ownership.

Both ready acceptance and console begin use this selected committed VM
relation. Merely seeing a phase enum or copying the MEM04 scratch
description is not sufficient authority.

### TTY bootstrap relation

Add a narrowly named
`micros_tty_handoff_runtime_validate_bootstrap_binding` in the existing
TTY runtime owner for B06 command use. It retains the full selected
relationship, not merely the denial-only device snapshot:

- exact stored bootstrap-owner pointer;
- bounded binding/manifest/runtime selection and exact service identity;
- fixed image/profile/process/thread/root/endpoint/IRQ/device tuple;
- live process/thread/endpoint owner relations and profile permission;
- scheduler policy and PREPARED/STARTING/READY relation;
- pure handoff-state validity, console/route phase; and
- exact retained deadline agreement and required disarming.

Factor the existing live-binding relation so the complete TTY validator
still executes its complete endpoint/object audit at its own boundary.
The new selected path omits only that audit and uses BOOT01 navigation.
The existing bootstrap-only `*_role_ready` consumes this relation plus
its OWNED and IDLE/IN_SERVICE requirements.

Other TTY/VM/IRQ commands continue using their existing complete APIs.
Do not expand the MEM04/GRANT01 exclusion inspector into a command gate.
The authoritative relation above is checked independently of any
description used by the memory path.

### B06-only UART point inspection

In the existing `user_address_space_tty.c` owner, add the read-only private
`micros_user_address_space_inspect_bootstrap_tty_uart(process, expected_root,
mapped)` query for B06's three console command positions.
`mapped` chooses the two current predicates, not an optional-validation
flag or future generalized mapping mode.

It admits runtime/phase/process/root and the exact fixed UART slot through
MEM04's selected path. Before dereference, retain exact managed/allocated/
generation-owned table checks and scalar ancestor checks. Require the
same scratch-idle boundary without acquiring or clearing complete scratch.

For console begin require the existing inactive root, a complete
three-table path and a canonical absent leaf. Check that the fixed
physical UART is unmanaged. For release/ready require the exact physical
UART leaf and user R/W, non-executable permissions, with the selected
committed TTY tuple and phase.

Preserve explicit ARGUMENT, phase, stale/process, NOT_MAPPED, conflict,
ownership and invariant distinctions needed by the original callers.
Begin maps the same legitimate absent-path/conflict/state failures to
STATE and deadline overflow to RANGE; unexpected selected corruption
remains fatal. Release/ready retain their existing gate/fatal handling.

The query performs no allocation, owner/PTE/scratch store, root activation,
fence, device/IRQ operation or authority publication. It returns no
mutation plan or retained physical pointer. The actual VM mapping
preparation/commit and existing full UART validator are unchanged for
their other consumers.

### Selected execution state

Before release or ready publication, use the
existing inspect plus `micros_user_execution_validate_context` path for
that exact selected service thread. This retains canonical status and selected executable
PC/read-write stack mappings without walking every service root.
Other selected role/revocation relations admit their exact root owner
through existing selected ownership leaves; unrelated roots and leaves
remain diagnostic scope.

Do not borrow a context proof across the existing common-return boundary,
modify root preparation/fences or introduce another context cache.

## Command plans and commit positions

| Command | Consumed scope and order |
| --- | --- |
| Ordinary RELEASE | Controller and selected PREPARED service, exact order and named READY prerequisites; selected context, exact profile/endpoint and scheduler admission. Prepare before any store; endpoint publication -> BOOT01 selected runtime commit -> prevalidated scheduler admission. |
| TTY RELEASE | Original mapped-phase TTY candidate, quiesced UART, disabled PLIC, exact mapped point and clear controller event state. Retain the original absolute deadline; successful control release precedes the existing TTY release commit. |
| ACCEPT_READY | Original deadline -> selected starting/role/endpoint readiness plan -> captured context and reversible guard -> exact expected-caller reply. Reply failure rolls back the guard and leaves bootstrap/TTY unchanged. Success precedes selected runtime/TTY commit, original no-message completion and guard finish. |
| CONSOLE_BEGIN | Exact TTY request, READY VM/PM and next-order/held-TTY identity; selected VM/TTY relation and ownership phase; absent UART point and original deadline range; clear VM destination and prepared notification. Preserve quiesce -> deadline -> console ownership -> TTY phase -> one VM notification. |
| COMPLETE | Selected controller plus the final active-service READY/zero-deadline/live-binding barrier, PM-role uniqueness/readiness, present VM/TTY role gates, staged launcher acknowledgments, grant and IPC reference checks. Prepare notification/source-only state before hold/removal; preserve source-only publication -> selected seal/controller-clear -> one PM notification. |
| FAIL | Original reason/detail, implicated selected binding/state and authoritative detail. Emit the same one-shot FAILED record and nonreturning panic; no recovery, alternate classification or return-shaped success. |

Retain local post-store checks that observe a specific required hardware
or captured-completion fact. Remove only complete post-command audits;
do not replace them with a success boolean.

Console begin checks the owned starting header, selected role/deadline
relations and its TTY state rather than scanning all runtime rows.
Final completion deliberately retains its all-service barrier.

Completion's final service barrier consumes every active service's
binding/runtime/live object relation, not merely a READY enum. It may
share the existing PM-role pass; it does not walk every root or every
application mapping. Present VM/TTY roles retain their selected committed
handoff and console/route gates, including the fixed TTY mapping and
platform checks. These checks add no timer policy or readiness transition.
A broken active service relation is therefore consumed corruption at
seal, even where an earlier unrelated release need not inspect it.

Existing BOOT01 selected, ready and complete plan layouts remain bounded
at 64/136/200 bytes. A role candidate remains local to its command; no
whole runtime, manifest or registry is added to a command plan.

## Diagnostic and scan ownership

Replace the nine B06 caller rows / ten complete bootstrap calls listed
in the fixed study. Keep the ten other rows and both complete APIs.
Initialization, prepared-state publication, explicit diagnostics and
later VM/PM/TTY owners retain their complete coverage.

Replace the additional B06 console VM/TTY/frame-owner and UART full-root
preflights only with the owner-specific relations above.
Relocating the TTY complete endpoint audit into its complete owner is not
permission to remove it. VM application state, memory mutation/inventory
and other command paths remain outside this correction.

Keep completion's service/PM-role, staged-thread, grant-dependency and
endpoint-reference scans. Keep endpoint publication's required owned-
thread search and scheduler queue-prefix checks. These are consumed
lifecycle work, not whole-system diagnostic substitutes.

Cold image/manifest/profile/policy/topology preparation and rollback remain.
BOOT03 owns the surviving byte-copy/fill family. No new dependency on
GRANT02 indexing or new persistent owner index is required here.

## Bounds

Let `d` be named prerequisites, `q` the actually consumed scheduler/IPC
queue prefix, `t`/`p`/`g` the retained lifecycle scan domains and `n`
the final service barrier. Ordinary B06 validation has zero complete
control/runtime/object/endpoint/frame-owner/grant/VM/TTY-runtime/root
audit calls at the replaced boundaries.

Release adds `O(d + q + t + harts)` selected/lifecycle work to fixed
PC/SP walks and existing MEM03 geometry. Ready adds its selected reply/
queue work and at most one fixed UART point. Console begin checks a fixed
set of role records and one fixed point. Complete retains
`O(n + t + p + g + q)` revocation work.
Do not rename those necessary scans to claim constant total work.

There is no managed-frame, unrelated mapped-leaf or application-mapping
table term in ordinary B06 command validation. Do not move a complete
audit into a newly named helper to satisfy an invocation counter.
No cross-call amortization is claimed.

Persistent sizes stay at BOOT01's 448/2,416/3,816 bytes. New selected
views are at most 64 bytes; use existing bounded command, 32-byte TTY,
264-byte context and three-level path storage with ordinary scoped
lifetimes. No arrays, VLA, recursion, new BSS or raised stack/image/time
budget is admitted. Compiler/link/target closure remains mandatory.

## Implementation acceptance

After separate design merge, establish actual unchanged-production Red:
the existing real portable BOOT01 pair exposes complete control calls,
and bounded same-guest observations expose the actual captured runtime/
role/root calls. Both semantic behavior and complete work ownership must
be established before accepting the count failure as Red.

Retain the original 43 BOOT01 cases, all bootstrap/control/model cases and
the 4,096-step seed/draw/dispatch/final-sixteen trace. Keep the original
corrupt inputs, complete diagnosis and byte-preserving failure checks.
Only documented unconsumed-corruption expectations change: in BOOT01's
grouped control-index case, the foreign ID-63 binding/runtime entries
are complete diagnostic scope; selected controller/request/count
corruption remains an invariant failure. Add separate observable
selected-success cases rather than normalizing the candidate's output
or silently repairing those inputs.

Green must cover valid and malformed selected views; absent versus
mismatched index relations; unknown/stale caller/service/profile;
selected versus unrelated root/owner/VM-load/grant corruption;
role/endpoint/token/range precedence; rollback and complete output
canaries; retained maximum/expired TTY deadlines; active IRQ-at-ready;
source-only seal and one-shot notifications.

Measure actual complete-audit invocations and consumed row/PTE/owner/
queue work. Require zero complete audits in the replaced ordinary
boundaries and positive complete diagnostic controls. Keep the existing
required lifecycle scans visibly accounted, including adversarial
prefixes. Extend current source/site/callee/consumer qualifications in
this outcome, not a metadata-only prerequisite.

Use actual production C and the existing collector. Native backends may
provide bounded physical memory, IRQ/time and panic/output effects;
they may not return fake successful authority or validator results.
Target captures belong to the existing bootstrap-launcher and TTY
workflows, include each actual command/role position, and report after
the original safe semantic boundary. Do not add a duplicate guest,
collector, timing-dependent UART print or larger timeout.

Retain every current native/target scenario, VM/TTY failure and handoff,
PM sealed gate, composed ELF-spawn/rollback/resource proof, standalone
image and all 44 static/113-ELF/ten observed stack scopes. B13 remains
blocked. One sufficient current author plan and fresh independent review
precede the first permanent implementation commit and exact-tree merge.

This design phase authors documentation only.

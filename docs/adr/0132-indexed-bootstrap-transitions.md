# ADR-0132: Indexed Bootstrap Transitions

- Status: Accepted
- Date: 2026-11-13
- Refines: ADR-0043, ADR-0044, ADR-0045, ADR-0047, ADR-0049, ADR-0050,
  ADR-0060, ADR-0080, ADR-0083, ADR-0084, ADR-0107 and ADR-0123
- Supersedes after Acceptance and separate design merge: B06-owned
  service-ID scans and capacity-sized release/ready/complete snapshots;
  internal bootstrap runtime/control/plan layouts and their directly
  dependent current native qualifications

## Context

GRANT03 #264 is independently reviewed and merged. BOOT01 is the next
preferred outcome, owns B06-F001 and has its actual EV04 prerequisite.

The [fixed local study](../research/minix-indexed-bootstrap-transitions.md)
traces MINIX's selected `rproc` and endpoint-slot `rproc_ptr`, plus the
current bootstrap lookup and transition callers. The remaining defect is
specific: repeatedly find an already named service and copy a complete
384-byte transition object to change one record.

Service IDs, manifest positions, binding positions, process slots and
topological positions are distinct. PREPARING has valid bindings but an
entirely zero transition object. The standalone portable runtime also has
no owning control object. One shared index would require changing those
lifetimes or imposing an unnecessary row-order equivalence.

## Decision

After independent review, Acceptance and separate merge, implement the
[indexed-bootstrap contract](../architecture/indexed-bootstrap-transitions.md).
This phase authors no executable artifacts.

### One direct index per actual row owner

Keep IDs 1 through 63 and capacity seven. Add a 64-byte ID-to-slot array
to the portable runtime and a separate 64-byte array to the control state
for bindings. Zero means absent; a nonzero byte encodes the row plus one.
Entry zero is always empty.

Build each array only in its existing cold constructor. Preserve manifest
and caller binding order, PREPARING's zero transition object, controller
publication and all current service-generation bindings. Neither array
is permission, a validation cache or an additional service registry.

Selected lookup validates ID, count, encoded slot and the selected row's
exact service ID before dereference/publication. No fallback scan or
repair is allowed. Complete diagnostics independently prove exact index
coverage and unused-zero entries. Both indices remain immutable through
release, readiness, sealing and failure.

### Replace full transition snapshots with one selected plan

Keep transition ownership in `bootstrap_transitions.c`. A declarations-only
private header exposes bounded prepare/commit helpers to the existing
control owner. One 64-byte LP64 plan contains the selected after-record,
its row and the four changed runtime header values; no array or runtime
snapshot is embedded.

Public portable operations prepare that plan and then commit. Control
release/ready/complete prepare the same plan but retain their existing
cross-owner checks and commit positions. Every failure precedes any
runtime mutation or output-plan publication. Commit changes only the
selected record/header and cannot return a recoverable error.

Release checks only explicitly named prerequisites through the runtime
index, in `O(d+1)` work for `d` dependencies. It does not rebuild a ready
bitmap by scanning unrelated services or add a maintained ready cache.
Ready acceptance remains constant work. Final completion retains its
required one-time complete READY/deadline audit, then plans only the
controller's change.

### Preserve bootstrap policy and lifetime

Keep profile/endpoint/scheduler publication order, exact reply ownership,
acknowledgment bytes, retained TTY deadline, VM/TTY role gates, PM sealed
notification and source-only launcher semantics. Controller authority is
cleared at seal; retained service bindings and indices do not restore it.
No plan survives the existing serialized protected operation.

B06-owned deadline/diagnostic ID selection uses the same checked runtime
lookup. An active nonzero starting ID whose selected index/state/deadline
is corrupt fails through the existing authority diagnostic instead of
silently disabling timeout enforcement. Normal timeout ordering, boundary,
reason, endpoint and absolute deadline are unchanged.

The canonical contract specifies argument/phase/lookup/dependency/deadline
precedence and the new index/count-corruption boundary. No new syscall,
message, role, error enum or recovery transition is introduced.

### Keep the next owners separate

BOOT02 still owns whole-system command validation and repeated pre/post
audits. Retain every existing control/runtime, cross-owner, PM-role,
staged-acknowledgment and grant-dependency audit. Do not claim that a
complete controller command becomes constant-time.

BOOT03 still owns byte/word copy migration. Cold initialization and
bounded selected-plan movement retain their existing helpers.
Manifest/image/profile/policy/topological construction and external
VM/TTY role-audit loops are not rewritten here.

### Bound storage and evidence

The two indices add 128 bytes to the existing target context, not another
owner. LP64 bounds are runtime 448 bytes, control state 2,416 bytes,
target context 3,816 bytes, selected plan 64 bytes, ready plan 136 bytes
and complete plan 200 bytes. Existing stack/image budgets remain unchanged.

Actual unchanged-production native Red must expose ID traversal and
whole-runtime movement while independent semantics agree. Extend the
current observation engine with one focused ordinary/observed bootstrap
pair, preserve the existing 4,096-step model and all control cases, and
qualify directly affected header/fixture consumers in this same outcome.
No fake object/endpoint/validator success is admitted.

Keep actual launcher, timeout/failure, VM/TTY, PM/service and full
spawn/resource workflows and every current stack configuration; B13 remains
blocked. One sufficient current author plan and fresh independent review
precede the first implementation commit.

## Phase and non-goals

The paired implementation remains pending independent design review,
Acceptance and separate merge. No code, tests, models, harnesses, build
scaffolds or executable prototypes belong to this phase.

No new service, wire format, capacity, role, cache, ready counter/bitmap,
epoch, concurrency, restart, migration, recovery, compiler primitive,
larger budget, hosted-CI acceptance or node-Q/POSIX completion.
Design Acceptance alone does not close B06-F001.

## Alternatives

- Infer service identity from process or array position: violates the
  existing distinct domains and valid permuted inputs.
- Share one index by changing PREPARING or binding order: unnecessary
  lifecycle/layout coupling for saving 64 bytes.
- Keep full snapshots but copy them faster: leaves capacity-sized work and
  confuses BOOT01 with BOOT03.
- Maintain a ready bitmap/count: adds redundant persistent state when
  named dependency lookups suffice.
- Remove all audits in this outcome: combines BOOT02 and loses its
  separately reviewed authority/error contract.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
eight-document proposal, relevant Accepted authority, fixed local
reference/current source and exact four-command author evidence.
It reported no substantive design issue remaining, with High confidence.
Targeted reproduction: None.

Acceptance records that design result, not an implemented index/transition
change or B06-F001 closure. The separate design merge still precedes
implementation.

## References

- [Canonical contract](../architecture/indexed-bootstrap-transitions.md)
- [Fixed local study](../research/minix-indexed-bootstrap-transitions.md)
- [Bootstrap source owners](../architecture/kernel-bootstrap-source-organization.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)

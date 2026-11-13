# Indexed Bootstrap Transitions

## Status and scope

BOOT01 implementation #266 follows the independently reviewed design #265,
Accepted by [ADR-0132](../adr/0132-indexed-bootstrap-transitions.md),
and closes B06-F001.
EV04 #189 is its actual prerequisite; preferred predecessor GRANT03 #264
is complete. The implementation PR owns the exact native, actual target,
full stack, independent review and publication evidence.

The implementation names the two indices `service_slots` and
`binding_slots`. `bootstrap_transitions_internal.h` declares
`bootstrap_transition_plan`, the checked runtime lookup/index diagnostic
and selected prepare/commit helpers; their definitions remain in
`bootstrap_transitions.c`.

The [fixed study](../research/minix-indexed-bootstrap-transitions.md) owns
reference provenance and adaptations. Only B06-owned service-ID navigation
and selected runtime mutation change. BOOT02 owns command audit cadence;
BOOT03 owns bulk copy/fill algorithms.

## Identity and index representation

Keep service IDs 1 through 63, capacity seven, immutable manifest bytes,
the existing manifest plan and lowest-ready-ID topological order.
The six-service production chain and capacity-seven fixtures remain.
No service ID is a process slot, binding row or topological position.

Add exactly these navigation fields:

| Owner | Field meaning |
| --- | --- |
| `micros_bootstrap_runtime` | 64 `uint8_t` entries mapping a service ID to its runtime row plus one. |
| `micros_bootstrap_control_state` | 64 `uint8_t` entries mapping a service ID to its binding row plus one. |

Zero means absent; index entry zero is always zero. An encoded row must be
between one and `entry_count`, which itself is bounded by the current
capacity. The selected row must carry the queried ID.

The binding's existing `manifest_index` still resolves its immutable
manifest row. Runtime entries retain manifest order; bindings retain the
caller's accepted order. The two orders need not agree. Neither index
stores a pointer, endpoint generation, permission or success flag.

Two owners need two indices: control PREPARING already exposes bindings
while its entire runtime transition object remains zero; standalone
portable runtimes have no control object. Do not normalize binding order,
publish RUNNING early or make the runtime borrow another object's index.

## Cold construction and complete diagnosis

Construct each index in its existing unpublished cold candidate.
Check count bounds before array access; check each ID's 1..63 domain and
uniqueness before indexing or shifting. Reject malformed construction as
`INVARIANT`, with all caller state/output bytes unchanged.

Runtime initialization retains its argument/alignment, already-initialized
and plan/controller-first checks. Its ordered prefix must name exactly the
constructed service set once, with the original controller first. Do not
reimplement manifest/profile/image/topological validation here.
Control preparation keeps its current manifest/binding identity checks
and copies the accepted binding order; the transition object stays zero.

Initialization, prepared-state publication and their whole-object copies
remain cold. No index is filled or repaired on a lookup miss.

Complete control diagnosis retains all current checks and adds:

- count bounds before traversal;
- every populated binding row has exactly its own encoded index entry;
- every nonzero index entry names an in-range row with that exact ID;
- no duplicate, missing, foreign or extra membership; and
- every unused ID entry is zero.

Published runtime diagnosis independently checks the same relation for
runtime rows, including exact count agreement with its control owner.
PREPARING instead retains the complete zero-runtime check. No stronger
unrelated inactive-binding or role policy is silently introduced.

Indices and row identities remain immutable through RUNNING, SEALED and
FAILED. Sealing clears only the existing controller authority fields;
the launcher binding remains for already staged source-only
acknowledgments and diagnostics. There is still no unseal/reset path.

## Selected lookup and error scope

Preserve `micros_bootstrap_control_find_binding_bounded` and its existing
wrapper signature. Replace the loop with checked ID/index/row access.
Null state, invalid count/ID, absence or mismatched selected metadata
returns `NULL`; no complete graph validation or authority is added.

Use the corresponding checked runtime selection inside B06's pure
transitions, control relation/acknowledgment lookups and diagnostics.
A private status-aware selector distinguishes an absent requested ID
(`ARGUMENT`) from malformed consumed count/index/row identity (`INVARIANT`).
A readonly pointer facade returns `NULL` for either failure.

Pure operation ordering remains:

1. original null/zero-ID arguments;
2. original phase check;
3. selected service lookup;
4. operation-specific order/state checks;
5. named dependency or deadline checks; and
6. publication of the complete selected plan.

Thus a nonzero out-of-domain ID does not outrank a prior phase error.
Malformed readiness message shape still outranks source classification;
unknown or malformed selected lookup classifies as FOREIGN, while known
PREPARED/READY sources retain EARLY/DUPLICATE. Do not add a phase gate to
that pure classifier.

An unindexed orphan or corruption in an unrelated index row is complete
diagnostic scope, not a reason to repair or scan on a selected lookup.
Actual control commands still run their existing full audits, so they
continue rejecting unrelated corruption before mutation. The selected
lookup is not a cached grant of bootstrap or device authority.

## One selected transition plan

Introduce one declarations-only private header,
`kernel/boot/bootstrap_transitions_internal.h`. The existing
`bootstrap_transitions.c` implements readonly lookup/diagnosis and
prepare/commit helpers; no new production C owner is needed.

The value-only selected plan has these LP64 fields and ordering:

```text
uint16 entry_index
uint16 next_order_index
bootstrap phase
uint32 controller_service_id
uint32 starting_service_id
one complete micros_bootstrap_runtime_entry after-value
```

Its bound is 64 bytes. It contains no runtime object, array, pointer,
registry identity, epoch or permission cache. Prepare initializes only
this fixed object and publishes it only after every check succeeds.

Commit copies only its selected after-record and installs the four header
values. It never writes entry count, order, index or another service row.
It performs no validation, allocation or recoverable operation.
It requires the successfully prepared plan for the same unchanged runtime
in the existing serialized epoch; it is not a cross-operation lease.

The public portable release/ready/complete APIs retain their signatures
and call these prepare/commit helpers. The control owner's ready and
complete plans replace only their embedded whole-runtime snapshot by this
selected plan, retaining active flags, binding indices, acknowledgment and
PM notification. They still clear/consume their fixed output object after
commit; no new replay protocol is added.

### Release

Keep exact-next-order, no-other-starting, PREPARED/reserved/uninstalled/
unassigned checks. Resolve only set prerequisite IDs through the runtime
index and require their actual state to be READY. Bit 63, missing
dependencies and non-READY dependencies remain STATE at that position;
malformed consumed index metadata is INVARIANT.

Iterate only the set dependency bits, using a bounded fixed-word
selection operation, not a 63-slot or service-capacity scan. Do not add a
ready bitmap/count or use topological position as a substitute for the
actual dependency states.

Preserve zero-timeout/range overflow and retained-console-deadline
ordering. Compute the original absolute deadline, including the exact
`now < retained_deadline` check without extension.
The after-record becomes STARTING/active/profile-installed/scheduler-
assigned and records that deadline; only `starting_service_id` changes
in the runtime header.

### Ready acceptance

Keep exact currently starting ID, STARTING state, nonzero deadline and
`now < deadline` checks. Before incrementing, require
`0 < next_order_index < entry_count`; an impossible selected counter is
INVARIANT rather than wrapping or publishing a corrupt plan.

The selected record becomes READY with deadline zero; clear starting ID
and advance order once. Do not scan unrelated entries or copy the order
or either index.

### Final completion

Keep running phase, no starting service and exact completed order.
Retain the one-time complete READY/zero-deadline audit over active entries.
Then select the controller and retain its active/profile/scheduler checks.
Only that entry becomes SOURCE_ONLY and scheduler-unassigned; phase
becomes SEALED and controller ID becomes zero.

This required final barrier remains `O(n)` read-only work. No extra ready
cache is introduced to pretend that checking every service is constant.
Its mutation and plan movement are constant-size.

## Control, reply and deadline integration

Preserve all existing cross-owner checks and actual commit positions:

- Release prepares the selected runtime plan, endpoint publication and
  scheduler admission before mutation; endpoint publication still
  precedes runtime commit and scheduler admission.
- Ready prepares the canonical acknowledgment and role state without
  mutation. The existing successful expected-caller reply precedes
  selected runtime/TTY commit inside the protected operation; a failed
  reply preserves bootstrap state and rolls back its scheduler guard.
- Complete retains PM role validation, launcher IPC/grant dependency and
  staged-acknowledgment checks. The existing hold/remove/source-only
  transition precedes selected seal and one-shot PM notification.

Keep the existing SIE-clear lifetime, token/generation rules, captured
register results, fatal post-publication disagreement and common return.
Do not move a plan across a callback, user return or intervening bootstrap
mutation.

B06 deadline and diagnostic service-ID lookups use the checked runtime
selector. TTY deadline enforcement remains first. When generic
`starting_service_id` is nonzero, a missing/corrupt selected index,
non-STARTING entry or zero deadline is an authority invariant and uses the
existing nonreturning authority diagnostic with that ID, the bounded
binding's endpoint or NONE, and detail zero.
An ordinary expired deadline keeps the existing ready-timeout reason and
stored deadline. Diagnostic state formatting may still return `none`
when describing unavailable/corrupt state; it is not authority.

## Retained work and ownership

Keep every current complete control/runtime, object, endpoint, scheduler,
memory, grant and TTY audit at its existing owning boundary. Keep the
completion PM-role scan, staged-thread acknowledgment audit and launcher
grant-dependency scan. BOOT02 owns their cadence and selected authority
replacement, not this outcome.

Cold manifest/catalog/profile/policy/topology and preparation loops remain.
External VM/TTY role-audit searches are not rewritten or claimed constant
by BOOT01. Their callers of the existing binding getter receive the indexed
implementation without new authority.

Current partial native/target controller fixtures must initialize the new
index coherently where they already construct the corresponding rows.
Do not force unrelated complete-bootstrap validity into MEM04/MEM05/GRANT01
denial-only inspector fixtures or weaken a new index check for them.
Keep the existing bounded inline-header exception; the new private header
contains declarations only.

## Work and storage bounds

Let `n` be active services, `d` the selected prerequisite count and `D=64`
the fixed ID-index extent.

| Boundary | Owned average/worst work |
| --- | --- |
| Selected binding/runtime lookup and classification | `O(1)` checked ID/index/row access. |
| Pure release planning | `O(d+1)` named dependencies and fixed plan movement. |
| Pure ready planning/commit | `O(1)`. |
| Final completion planning | `O(n)` required READY/deadline barrier, then fixed selected plan. |
| Any selected transition commit | One record and fixed header, `O(1)`. |
| Cold construction/index diagnosis | `O(D+n)`, plus the existing separately owned validation. |

No cross-call amortization is claimed. Whole control/syscall cost still
includes the retained audits.

Preserve the 48-byte runtime-entry representation. Enforced LP64 bounds
are runtime 448 bytes, control state 2,416 bytes and existing target context
3,816 bytes, exactly 128 additional persistent bytes for the two indices.
Selected plans are at most 64 bytes; outer ready/complete plans at most
136/200 bytes, including their unchanged IPC values. Cold candidates may
grow by their owning index; no new global workspace or enlarged stack,
service image or deadline budget is allowed.

## Acceptance evidence and phase

After separate design merge, establish real unchanged-production Red for
selected-ID traversal and capacity-sized transition copying. Compile real
portable transitions/control and their actual object/endpoint/IPC/scheduler
dependencies in one ordinary/observed bootstrap pair using the existing
engine and collector. No validator or authority-success stub is allowed.
Keep cold/full audit counts separate from selected lookup/plan work.

Green includes sparse IDs through 63, ID 0/64/maximum rejection, permuted
manifest/binding/order domains, capacity one/seven, wrong/missing/extra
index rows and independent complete diagnosis, selected/unvisited
corruption, named dependencies, normal/retained/expired/overflow deadlines,
selected counter bounds, unchanged failure bytes, one-record mutation,
immutable indices/order and sealed controller/source-only semantics.
Exercise actual deadline selection with bounded native output/panic
backends, including malformed active selection and normal timeout.

Retain every existing manifest/control case, the 4,096-step model with
seed `0x42535452504c4159`, draws, dispatch schedule and final-sixteen trace.
Its independent reference remains a flat semantic model; derive expected
index bytes by enumerating that reference, not production helpers or
captured candidate state.

Measure actual lookups and plan copy/clear work, including empty/sparse
cases and control-plan publication/consumption. Reject hidden whole-runtime
or table copying in ordinary transition paths. Do not count setup/reference
work as production work.

Maintain current primitive/source/product qualification, including the
existing MEM04, MEM05 and GRANT01 inline-header consumers, in this same
outcome. Preserve ordinary observer absence, complete input/collector
identity and independent work limits. One focused native pair may join
complete `test-unit`; no new QEMU workflow or collector is required.

The indexed inline getter also appears as a header-origin function in
stack catalogs. Apply ADR-0123's current-source/type/machine checks to
callback-free header helpers just as to C-owner helpers, rather than
freezing their old bodies. Callback-typed/using functions retain their
anchors; actual machine indirect calls still require complete binding,
even when omitted from a supplied pending list. No callback target, frame,
direct edge, epoch or budget is removed.

Retain actual launcher, deadline/failure, VM/TTY role and service,
PM sealed notification and complete ELF-spawn resource scenarios,
standalone images, all stack configurations and B13's explicit block.
One sufficient current final author plan and fresh independent
implementation review precede the first permanent commit and exact-tree
merge. No executable artifact is authored during this design phase.

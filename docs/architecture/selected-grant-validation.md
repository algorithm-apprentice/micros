# Selected Direct-Grant Validation

## Status and scope

This is the independently reviewed GRANT01 contract Accepted by
[ADR-0128](../adr/0128-selected-grant-validation.md).
Design #258 is separately merged; its paired implementation proceeds from
that main. The design alone closes no B05 finding.
The [fixed study](../research/minix-selected-grant-validation.md)
pins local MINIX and current `micros` after IPC04 #257.

GRANT01 owns B05-F001 and B05-F002. Its actual dependency is merged IPC01 #251;
following IPC04 is sequential execution, not another runtime-DAG edge.

Replace ordinary complete grant/endpoint/object audits with selected
metadata, record and participant admission. Replace the three grant-only
full TTY device reads with the existing bounded committed-binding inspector.
Preserve all authority, result, failure-atomic, serialization and hardware
contracts not explicitly refined below.

GRANT02 still owns the lowest-free allocation and all-grant cancellation
mechanisms. GRANT03 still owns checked-copy spans/block movement.
No new token format, record/index/cache, cancellation algorithm, syscall,
user grant table, delegation, VM fault/retry, TTY command policy, SMP,
compatibility/history/recovery state or larger budget belongs here.

## Finite boundary

The eight replaced direct call sites are the three ordinary
`micros_grant_registry_validate` calls in `micros_grant_prepare_create`,
`resolve_owned_record` and `micros_grant_prepare_copy_authority`; the two
runtime audits before captured grant decode; and the full TTY device getter
in `grant_syscall_create_request`, `grant_copy_execute` and
`grant_copy_prepare_remote_plan`.

Keep complete grant/runtime validators, initialization, cancellation prepare
and commit, bootstrap/PM audits and explicit evidence callers.
Do not delete those checks through shared-helper changes or move another
subsystem's validator cadence into this task.

## Scalar readiness and selected view

Use the existing authoritative runtime getters. Add only a grant-owned
private scalar preflight shared by the adapter and independently callable
portable operations. It checks:

- required registry/object pointers;
- the current grant initialization magic and `active_count <= G`;
- existing selected endpoint metadata, including profile count and reserved
  zero-profile shape, through `endpoint_preflight_registry`;
- existing object scalar bounds through `scheduler_core_preflight_registry`.

Those existing lower-layer helpers perform no table audit. The grant helper
does not inspect a grant slot, current endpoint, user token or user range.
Keep null-argument errors and grant `NOT_INITIALIZED` behavior; copy authority
still maps grant `NOT_INITIALIZED` to `INVARIANT`, and the target guard treats
unavailable/malformed runtime metadata as its existing fatal invariant.

A private selected-record view contains only one admitted record and the
two existing endpoint/process selections for its stored participants.
It is automatic, at most 64 bytes, and never a validation flag, persistent
permission or cross-operation cache. Reuse those concrete selections when
the supplied participant is the same exact identity; do not resolve that
same participant again merely to switch from stored to supplied authority.

Admit a supplied process with the existing selected process-metadata guard
before interpreting its profile or primary endpoint. If a primary is present,
admit its exact endpoint relation even when it is inactive. Only canonical
unbound/reserved/source-only participants use the existing inactive result;
malformed consumed process, primary-endpoint or profile relations remain
`INVARIANT`, not a convenient inactive fallback.

Independently checked public entries may repeat constant scalar readiness
checks. They must not repeat complete audits, rewalk grant tables for an
indexed lookup or validate all participants before decode.

## Record admission

Decode and range-check an opaque token before indexing. At the operation's
existing registry-preflight position:

1. perform scalar readiness;
2. validate the selected slot with the existing storage rules: recognized
   state, generation/packability, permissions/range where active, and exact
   zero authority plus free/quarantine generation rules where inactive;
3. for an active slot, require a nonzero declared active count;
4. resolve its stored grantor endpoint/process once and require exact equality
   with the stored grantor handle and endpoint;
5. resolve its stored grantee endpoint/process once; require a distinct
   endpoint and the existing `ACTIVE`/`SOURCE_ONLY` stored-lifetime allowance.

Malformed selected storage or stored relations are `INVARIANT`, including
when the supplied generation is stale. An inactive slot still has to be
canonical; this does not authorize an inactive record.
Only after that admission do the operation's caller, token-generation,
ownership and active-participant checks run in their existing order.

Stored `SOURCE_ONLY` validity supports prevalidated teardown, not ordinary
copy or new grant permission. A supplied participant still has to pass its
original active-endpoint condition when that step is reached.
Keep the existing operation-specific `STATE`, `DEAD_ENDPOINT`,
`STALE_GRANT` and `UNAUTHORIZED` mappings.

Exact global count equality remains a complete diagnostic. A selected
operation checks bounds and what it can prove locally; it does not scan
unvisited rows to recompute the count. Revoke cannot decrement zero, and
creation cannot increment a declared full count after finding a free slot.
Legitimate lifetime writers continue maintaining the exact count.

## Operation order and publication

### Create

Keep argument/permission/special-endpoint and virtual-range rejection before
metadata. Then admit the supplied grantor and grantee in the current order,
require active distinct endpoints, and only then perform the existing
lowest-free allocation search.

Walk from slot zero to the first free slot. Validate the storage of each
visited row before interpreting its state; do not validate skipped active
rows' endpoint relations. Track the visited active count locally and reject
when it exceeds the declared total. At a selected free slot require a
packable token, zero authority and room to increment the count.
If the whole array has no free slot, its observed active count must equal
the declared count before returning `CAPACITY`; quarantined rows are not
free capacity.

No second table pass, suffix audit or free-list/index change is added.
Bad consumed prefix storage is an invariant, not a skipped slot or repair.
Errors discovered only during allocation remain after participant admission;
the former full-audit precedence for unvisited or not-yet-consumed records
is explicitly removed.

Keep the existing unpublished create plan. Target MMIO exclusion occurs
after successful preparation and before its non-failing commit.
Only successful commit changes the record/count, publishes the token and
consumes the plan. Portable create retains its range-shape-only contract;
this task does not add mapping residency to grant creation.

### Inspect and revoke

Keep inspect's null-output and malformed-token prefix. Admit metadata and
the decoded slot, then the supplied grantor's active identity, then token
state/generation, then exact ownership. A stale caller still precedes a
well-shaped stale token; malformed token packing still precedes registry
inspection.

When the supplied grantor equals the admitted stored handle, reuse its
selection and apply the active condition at this stage. Otherwise admit the
supplied process/endpoint before classifying wrong ownership.
No alternative owner search is permitted.

Inspect copies its complete value snapshot only on success. Revoke keeps
the existing clear, generation advance/quarantine and count-decrement order.
Every failure preserves outputs and all actual state bytes.

### Copy authority, copy and operation 15

Keep the existing null/permission/special-grantor, size/local-range/offset,
and malformed-token prefixes. After selected admission, retain this order:
active current grantee; token state/generation; stored grantee equality;
supplied active grantor; grantor/direction equality; relative/remote bounds.

If the supplied grantor value equals the stored admitted endpoint, reuse its
selection but apply the active-state requirement only at the original step.
If it differs, resolve that supplied endpoint there: a stale supplied grantor
must remain `DEAD_ENDPOINT`, not an early wrong-owner `UNAUTHORIZED`.
Do not move this resolution before stale-token or wrong-grantee rejection.

The authority result remains value-only and success-only. Copy still checks
the ownership phase after authority/bounds; zero length succeeds only in a
supported phase and performs no translation/device read or byte access.
Nonzero copy obtains both complete remote and local plans before classifying
invariant, phase and ordinary fault precedence. Preserve device-span
advancement/deferred fault, physical overflow/nonoverlap and the existing
non-failing retained-plan byte commit.

Operation 15 validates only the remote plan, transfers no byte and creates
no lease or pin. Actual later copy repeats its selected authority and mapping
checks; a prior successful validation is never cached authority.

## Captured target adapter

Keep existing current-context capture, one `sepc` advancement, argument
pointer checks, authoritative getters and panic reasons.
Before decode, require present authoritative grant/endpoint runtimes and
valid scalar metadata, not complete grant/endpoint/object relations.
The existing captured context retains exact current-process authority under
SIE clear; do not add a new IPC guard, scheduler transition or user-supplied
process identity.

Decode still rejects upper bits, unknown operations and nonzero unused
registers before portable dispatch. The guard must not hoist token, active
endpoint, permission, range, phase, device or mapping checks over decode.
Unrelated malformed grant/endpoint records therefore no longer turn an
otherwise invalid user shape into a global-audit panic. Broken readiness or
consumed scalar metadata remains fatal before decode.

Dispatch exactly one existing selected portable path. Create device denial
still precedes token publication; errors and fatal portable results retain
their existing mapping. Only `a0` and the already advanced `sepc` change.
All grant operations remain nonblocking and return through ordinary selection.

## Denial-only committed TTY binding

Explicitly extend ADR-0107's memory-only inspector consumer set to these
three grant-owned functions and their evidence:

- `grant_syscall_create_request`, after successful create preparation;
- `grant_copy_execute`, after authority/phase and only for nonzero length;
- `grant_copy_prepare_remote_plan`, at the current nonzero validation stage.

They may consume the unchanged
`micros_tty_handoff_runtime_inspect_device_binding(objects, binding)`.
It does not resolve a grant or authorize a TTY, bootstrap, IRQ or mapping
command. The grant participants were independently admitted already; the
binding is used only to exclude the exact TTY process/root's reserved device
range. An unrelated process's ordinary RAM at the same VA remains legal.

Retain the inspector's fixed controller identity, pure console/route/deadline
shape, live exact process/thread/root/primary-endpoint tuple and applicable
prepared/active profile/scheduler-assignment checks. Malformed consumed
binding remains invariant; `NONE` does not authorize an exceptional leaf.

Keep `micros_tty_device_range_intersects` and actual
`micros_user_address_space_validate_tty_uart_mapping` in the existing range
planner. Device spans still produce no copy chunk, actual malformed device
mapping outranks ordinary fault, and normal translation retains its MMIO,
transferable, scratch and foreign-owner rejection.

The full TTY providers and every TTY command/publication validator remain
unchanged. Bootstrap binding/transition agreement, unrelated endpoint/queue
state and unused scheduling-policy fields are not reauthorized by an
exclusion-only read. Complete diagnostics retain them. This is the only
new inspector consumer permission; the VM inspector and all other callers
retain their existing boundary.

## Corruption scope and retained diagnostics

Selected success is not a certificate for unvisited grants, endpoint queues,
profile rows, exact global active-count equality or a complete bootstrap
transition graph. Preserve the existing complete validators and their
corruption cases, but remove their unrelated-error precedence at the eight
ordinary call sites.

In particular, an in-range count of two with one active row may still permit
read-only inspection of that valid row; full diagnosis must reject the
count mismatch. Keep this explicit selected/full case. The old inspect
output-preservation test must use a genuinely malformed selected/header
condition, such as a count above capacity, rather than silently dropping its
sentinel check.

Selected malformed storage/relations, impossible consumed count bounds and
bad device bindings remain failures before mutation. Keep lower/upper token
boundaries, inactive zero bytes, terminal slot-63 quarantine, dual-invalid
precedence and failed output/source/destination/canary preservation.
No lazy repair, hidden complete audit or success-shaped fallback is allowed.

## Ownership and bounds

Keep coherent grant representation, selected record/participant admission
and lifecycle in `grant.c`, with the narrow scalar preflight in its existing
private interface for captured admission. Use existing selected endpoint and
object-metadata leaves downward; add no reverse dependency into IPC mutation.
Runtime publication/full diagnostics, copy range/commit, syscall decode and
TTY binding retain their current owners.

| Boundary | Required work |
| --- | --- |
| Scalar runtime/portable readiness | Constant checks; no grant/endpoint/object table audit. |
| Inspect/revoke/indexed authority | One selected grant row, two stored participant selections and at most one different supplied participant; no capacity scan. |
| Create | One lowest-free prefix of `a <= G` storage admissions, two supplied participants, constant scratch; allocation remains GRANT02 work. |
| Grant-only device description | One constant-size selected binding; zero full TTY/bootstrap/endpoint validators. |
| Copy/remote validation | Selected authority plus unchanged at-most-four/two translations and existing byte/plan work. |
| Explicit diagnostics/cancellation | Existing complete scope and capacity work, not claimed constant. |

Average and worst selected identity/authority work are constant; create is
`O(a)` with `O(G)` worst case and no new amortized allocation claim.
Do not charge work to fixture setup, another owner or an unobserved callback.
Retain complete primitive coverage and source/product identity.

No persistent layout changes, new allocation or capacity-sized temporary.
The selected-record view is at most 64 bytes; existing create/copy/cancel
plans, physical bounds, stack budgets, finite machine qualifications and
blocked B13 admission remain unchanged.

## Test-first and phase acceptance

After separate design merge, add one focused ordinary/observed native pair
using the existing current-source observation engine. Execute actual
production grant authority, captured C decode/dispatch, copy orchestration
and TTY committed-binding code. Native backends may supply runtime fixtures,
IRQ state, controlled translations and panic capture; they may not replace
grant/participant/device decisions, actual complete validators or copy logic
with expected successes. No new collector ABI or QEMU workflow is required.

First prove unchanged-production semantic agreement while real complete
audits/table work fail independently required zero ordinary audits. Cover
empty/dense/sparse/high-slot state so a fixed small population cannot hide
global work. Both valid and malformed-decode captured paths must be measured.
Keep create's allocation prefix and cold cancellation/diagnostic work
separately visible rather than requiring zero work for those algorithms.

Focused Green includes:

- first/last/sparse token slots; exact grantee, grantor and direction; inactive,
  stale, wrong-owner, terminal generation and quarantine boundaries;
- malformed selected storage/relation versus unrelated corruption, bounded
  and mismatched counts, malformed caller/profile/primary metadata and
  malformed allocation prefix versus unvisited suffix;
- lowest-free choice, full/quarantined capacity, no generation/count/output
  change on failed create and success-only prevalidated publication;
- all existing dual-invalid orderings, particularly stale caller/token,
  wrong grantee/stale supplied grantor, range/authority/phase and zero length;
- actual captured readiness/decode/fatal/result behavior with only `a0` and
  `sepc` effects, and create failure before token publication;
- inactive/prepared/starting/owned device bindings, malformed consumed tuples,
  MMIO denial, ordinary same-VA RAM, and preserved structural-before-fault
  range classification;
- genuine byte/canary/state preservation, remote-only operation 15 and
  revalidation at actual later copy.

Retain the original eight deterministic grant cases, 4,096-step grant model,
seed, forced prefix, draw schedule, coverage and trace; no candidate-derived
oracle. Keep cancellation's selected-generation/one-use/foreign-plan and
failed-IPC-close cases. Existing native/target source ownership and
independence controls remain; update only directly affected current bindings.

Run the existing actual endpoint/grant/grant-syscall, wired handoff, TTY
device, VFS and composed spawn/exit/resource evidence selected by the
authoritative planner, plus all required image/static/observed stack scopes.
Native work evidence does not replace hardware or cross-service behavior.
One sufficient final author plan supplies affected-unit evidence under
ADR-0123; no historical catalog or duplicate recertification ceremony.

Independent design review precedes the design commit. Fresh independent
implementation review precedes its first permanent commit. Only accepted
implementation and exact-tree merge close B05-F001/F002.

### Current executable evidence

The [focused native pair](../development/building.md#native-unit-tests) uses
the current-source observation engine. Its portable, selected-corruption and
captured-runtime cases live in `selected_grant_validation_test.c`,
`selected_grant_scope_test.c` and `selected_grant_runtime_test.c` under
`tests/host/`. The retained original grant cases and model are unchanged in
count, seed and draw schedule.

Both variants compile the real grant/IPC runtime publication and complete
validators, grant adapter/codec, copy orchestration/range/commit and TTY
binding providers. The native bootstrap finder is the actual current
definition and its observed bounded header helper. Test-only backends supply
the object-runtime fixture, IRQ state, controlled memory-boundary results and
panic capture; no grant, participant, TTY or complete-validator success is
stubbed. Fixture restoration changes only test-owned inputs and the existing
authoritative runtime storage, not a new production reset API.

The native cases establish C orchestration, not RISC-V IRQ or physical-PTE
correctness. Those remain the actual target gates above. The source ledger
in `tests/host/selected_grant_validation_source.py` also preserves the cold
grant, cancellation, bootstrap and PM audit boundaries and the three exact
denial-only inspector consumers.

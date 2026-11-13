# Grant Free Lists and Targeted Cancellation

## Status and scope

This is the independently reviewed GRANT02 design Accepted by
[ADR-0129](../adr/0129-grant-free-lists-and-targeted-cancellation.md).
Its actual prerequisites, GRANT01 #259 and IPC03 #255, are merged.
Design #260 and the
[quiescent resource-witness correction](../adr/0130-quiescent-grant-resource-equivalence.md)
#261 are separately merged. Independently reviewed implementation #262
implements both contracts; its exact acceptance and publication are
recorded in that PR.

GRANT02 owns B05-F003/F004. The
[fixed local study](../research/minix-grant-free-lists-and-targeted-cancellation.md)
traces the reference, current callers and storage/work costs.
This proposal changes allocation/reclamation structures, not direct grant
authority or the checked-copy algorithm.

Preserve token packing, 64-slot capacity, exact participants, range/direction,
revoke-time generation advance, terminal quarantine, one-hart SIE-clear
serialization, failure-atomic publication and the existing user ABI.
GRANT03 owns copy-span/block movement. No user tables, delegation, wildcard,
fault history, heap growth, VM retry, new IPC algorithm, epoch/cache,
migration, recovery, concurrency or larger budget is included.

## Representation and ownership

All index state belongs to the existing grant registry. It is not endpoint,
process, thread, user-memory or cross-process pointer authority.

| Object | Defined state |
| --- | --- |
| Registry header | Existing initialization magic and active count; one `micros_grant_t` free head. |
| Each grant record | Existing state/generation/authority; one next-free token and two membership nodes, each with previous/next grant tokens. |
| Endpoint-root array | One row per process slot: exact endpoint value and one incident-list head token. |
| Cancellation plan | Existing active flag, registry identity, exact endpoint, 64-bit selected mask and expected-generation array; only selected entries are meaningful. |

Use `MICROS_GRANT_NONE` for null grant links, not zero or an untagged slot.
Every non-null reference is a complete slot/generation token. Empty endpoint
roots have endpoint `MICROS_ENDPOINT_NONE` and head `MICROS_GRANT_NONE`.
Nonempty roots are bound to the exact endpoint whose decoded slot selects
that row. There are no additional free counts, per-list counts, epochs,
cached validation flags or persistent plans.

An active record belongs to exactly one list for its grantor and one for its
grantee. The root's combined list may contain either role. Choose a node
only when exactly one stored participant equals the root endpoint; the
two roles cannot name the same process slot. The original selected
participant checks remain authoritative for live permission.

The same neighboring record may occur in both participant lists, using its
different role nodes. Do not reject this legitimate alias or update the
wrong node through a record-only neighbor cache.

## Complete invariants

- FREE records have packable nonzero generations, zero authority, null
  membership links and exactly one membership in the free chain.
- ACTIVE records have a null free link, valid original authority and exactly
  one membership in each participant's incident list.
- QUARANTINED records retain the original terminal generation, zero authority
  and null links; they appear in no list.
- Every link's token resolves the exact current record generation in the
  required role. No self link, cycle, duplicate membership or orphan exists.
- Each incident head has null previous; adjacent nodes have reciprocal
  previous/next tokens and the same exact endpoint role.
- A nonempty root has the exact endpoint tag; an empty root has both null
  fields. Removing its final node clears the tag before endpoint-slot reuse.
- The scalar active count equals the number of active records. All records
  are accounted for once as free, active or quarantined.
- A live token is never changed by allocation or relinking. Only retirement
  advances it, or quarantines its terminal value.

Complete validation retains the existing complete endpoint/object relation
and adds exact index coverage. Derive a free membership mask and the two
active-role membership masks with bounded walks; compare them with record
states. Use global per-role seen sets and budgets so duplicate/cyclic roots
fail without multiplying a full traversal by every endpoint.

Initialization builds the free chain in increasing slot order and leaves
every participant root empty before publishing readiness. Its full work and
the two existing authoritative/scratch registry objects remain cold.

## Local admission, not global recertification

Extend the grant-owned scalar header check only with free-head token shape.
It does not dereference a free or participant head before captured decode.
Keep existing null/uninitialized/error mappings and object/endpoint scalar
guards. Read-only inspect/copy retain ADR-0128's selected record/participant
scope; they do not walk allocation or incident lists.

Record storage admission includes recognized link shapes and role-specific
null fields. A non-null link is shape-checked before any indexed access.
When a path follows it, additionally check exact generation, slot state,
required endpoint role and consumed reciprocal fields. Internal stale,
foreign or malformed links are `INVARIANT`, never ordinary user staleness
or a reason for a repair scan.

Selected removal admits both endpoint roots and the required previous/next
neighbors. A null previous requires the exact root head; otherwise the
previous node's next must name this token. A non-null next must point back.
Reject equal non-null previous/next and every consumed self link.
Validate only the neighbor's required role/storage, not its unrelated
participant's complete lifecycle.

Insertion admits the selected roots and existing heads, including their
exact role and null previous. It performs no tail search. Follow no
unrelated capacity prefix to locate a slot or certify membership.

These are local topology guarantees, not proofs of every list's global
reachability. An unvisited free orphan or disconnected peer-list cycle
remains complete-diagnostic corruption. No ordinary operation silently
repairs it. An empty free head may report capacity without searching for an
unindexed free record.

Keep the existing in-range active-count mismatch example: one valid active
row with declared count two can still be inspected, while full diagnosis
fails. No redundant count is introduced to turn that example into an
impossible header. A selected active retirement requires count room, and a
batch requires at least its exact selected count before decrement.

## Create: prepare without taking the free slot

Preserve the current argument/permission/special-endpoint/range prefix,
scalar readiness, active grantor then active grantee, and self-grant
rejection. Allocation and index mutation are not hoisted over those checks.

1. If the free head is null, return `CAPACITY` with no mutation.
2. Admit that exact FREE head and its non-null successor, if any. Require
   canonical free storage/generation, no self link, and room in the active
   count. A stale/free-to-active link fails; do not skip it.
   The admitted successor may not link back to the selected head. Longer
   cycles in an unvisited suffix remain complete-diagnostic scope.
3. Admit the two participant roots and current insertion heads. Empty roots
   may bind to the already admitted current endpoint; a nonempty different
   generation is an invariant, not a rebinding opportunity.
4. Build the constant-sized unpublished create plan. Retain the selected
   head/successor and prospective participant links; do not pop, reserve,
   mutate a neighbor or change any count yet.
5. Keep the existing target TTY exclusion after successful prepare.
6. Only the non-failing commit pops the free head, installs the record and
   two membership nodes, updates affected old-head previous links and root
   tags/heads, increments the active count and publishes the token.

The whole operation remains in the same protected, non-mutating epoch.
Commit consumes the admitted identities without a second search or fallible
validation. Every denial preserves the registry and output token completely.
Allocation never advances a generation.

## Indexed revoke and shared retirement

Keep ADR-0128's malformed-token, selected storage/participants, supplied
active caller, token-generation and grantor-ownership order. Only an
authorized current record reaches unlink preflight.

Admit both local membership splices, count decrement and any existing free
head before mutation. The retirement operation then:

1. unlinks each exact role and repairs its current immediate neighbors/root;
2. clears an endpoint tag when that root becomes empty;
3. clears authority and obsolete membership links;
4. advances to the next packable token and pushes it at the free head, or
   retains the original terminal generation in unlinked quarantine;
5. decrements the active count exactly once.

A quarantined slot is never pushed. Preserve slot 63's reserved-token edge.
No fallible condition remains after mutation begins. Other authority,
generations and unrelated link fields remain unchanged.

## Cancellation prepare

Keep null/special-endpoint argument rejection, then scalar readiness.
Resolve the exact closing endpoint and require its existing ACTIVE or
SOURCE_ONLY lifecycle allowance before consuming its grant root.

Walk only that endpoint's incident list. Use a fixed 64-bit seen mask, an
at-most-64 step bound, exact token generations and expected previous links.
For each node:

- admit original record storage and its exact stored participant relation;
  reuse the already admitted closing selection and resolve its other peer;
- require exactly the selected endpoint role;
- admit both prospective unlink splices, including required peer-list
  neighbors and root identities;
- add its slot/generation to the private candidate plan.

Validate all prospective free-head/count writes before publication. Empty
cancellation does not inspect every grant or endpoint. Active-count
underflow, cycle, malformed/stale link or consumed peer corruption fails
with unchanged registry and output plan.

After the complete preflight succeeds, publish scalar identity/mask and
only selected generation entries, then set `active` last. Do not clear the
candidate/output array, copy the complete plan or inspect unselected entries.
Unselected entries and padding retain their previous caller bytes.
Success does not certify, initialize or expose authority through those bytes.
Failure preserves every output byte, as before.

## Cancellation commit and IPC-close lifetime

Keep the original null argument, inactive/consumed/foreign-registry
precedence. Validate the plan's endpoint shape, grant-only scalar metadata,
selected endpoint root, exact incident set/generations, local unlink
neighborhoods, free head and sufficient active count before any mutation.

Do not resolve the closed endpoint/process relation or call a complete
registry validator. IPC close intentionally invalidates that relation.
Rewalk the current incident list, require each visited slot to be selected
at its prepared generation, and require the final seen mask to equal the
plan mask. This rejects missing/extra selected nodes, stale generations and
foreign roles without scanning all records. Unselected generation entries
are neither read nor required to be zero.

After all checks pass, consume `active` and retire exactly the selected
slots in increasing slot order through mask-driven iteration. This retains
the old cancellation order without a capacity loop. Consequently, the last
reusable retired slot becomes the new free head; quarantine never does.

The preflight proves all affected neighborhoods in the original state.
The mutation pass uses links updated by its own earlier splices. It must not
replay stale original neighbors after removing adjacent selected records.
Every newly formed link joins previously admitted nodes or nodes just
published by this same non-failing mutation. No further validation failure
or endpoint lookup is allowed after consumption.

Preserve the production sequence in `handle_exit_detach`:

```text
grant prepare -> IPC close -> grant commit -> process/thread detach/release
```

SIE remains clear throughout. IPC close does not modify grant state. Its
failure preserves grants and the unconsumed plan; success is followed by
the already-prevalidated, non-failing grant commit. Unexpected mismatch is
still invariant misuse/corruption, not recoverable partial teardown.
Standalone prepare/commit without IPC close remains valid.

No other grant mutation may intervene in this epoch. Do not introduce a
cross-operation lease, nonce, hash, lock or persistent transaction to make
plans reusable outside that contract.

## Diagnostic and unchanged boundaries

Only cancellation prepare's complete relation audit and cancellation
commit's complete storage audit leave their ordinary paths. Full registry
validation, runtime initialization/full validation, BOOT/PM audits and
explicit diagnostic callers remain.

All permission, generation, quarantine, shape/phase/bounds, TTY exclusion,
copy/operation-15, user-register and scheduler contracts remain except
the explicit allocation/index/cancellation footprint above.
Public function signatures and user wire/token ABI remain; internal
record/registry layout changes are intentional.

The complete parent PM/IPC/BOOT paths may retain other capacity-dependent
work. This correction neither moves new work into them nor claims they
have become constant. Keep their actual composed evidence visible.

## Work and storage bounds

| Boundary | Average and worst work, excluding separately owned peer mechanisms |
| --- | --- |
| Create, including full/quarantined capacity | `O(1)`; one free head/successor and two insertion heads, no allocation-prefix scan. |
| Revoke | `O(1)`; one selected record, at most four role neighbors and a free head. |
| Inspect/copy authority | Existing constant selected scope; no index traversal. |
| Cancellation prepare/publication | `O(k+1)` incident/neighbor work and selected witness writes. |
| Cancellation commit | `O(k+1)` preflight and selected retirement; no full-plan transfer. |
| Initialization/full diagnostics | `O(G+P)` owned index work plus existing complete endpoint/object validation. |

Immediate neighbors on other participants' lists can be unrelated to the
closing endpoint but are necessary splice operands. Count them; do not
mislabel the bound as zero access to every nonincident record. The required
distinction is selected/neighborhood work versus capacity enumeration.

The fixed-capacity implementation has no growth and no amortized allocation
claim. Worst cancellation is linear when all grants are incident, not worse
when unrelated population grows. Empty cancellation touches only bounded
metadata, not 64 records or witness entries.

Order link metadata before the authority payload so FREE/QUARANTINED zero
authority does not accidentally include a valid free link. On LP64, bound
each record by 72 bytes, each endpoint root by 8 and the registry by 5,144.
The existing authoritative and initialization-scratch objects therefore
reserve at most 10,288 bytes, with no third registry.

Retain the cancellation plan's 288-byte bound, including its `O(G)` reserved
generation array; only its `O(k)` meaningful entries are accessed.
Bound create plans and local mutation views by 128 bytes each. Do not add a
capacity-sized snapshot, VLA, recursion, dynamic allocation or new persistent
work area. Prove actual native/RV64 layouts and full compiler/target stack
closure within unchanged budgets.

## Evidence and explicit oracle adaptations

After separate design merge, extend the existing ordinary/observed grant
pair and current observation engine; no new collector ABI or QEMU workflow
is needed. Before production changes, establish real common-case semantic
agreement and actual allocation/cancellation work Red. A separate
multiple-release example must distinguish MINIX LIFO reuse from the old
lowest-slot result. Compiler, harness or source-admission failure is not Red.

Green must cover initial ascending allocation; head pop after adversarial
release order; full/all-quarantined capacity; terminal slot 63; no mutation
after failed create/device exclusion; both-role insertion/removal; the same
neighbor record used through different roles; head/middle/tail removal;
empty/sparse/dense/full incidence; adjacent selected nodes; unrelated
population growth; stale/wrong-role/cyclic/reciprocal/root corruption;
selected-count underflow; exact-set/generation/foreign/consumed plans;
poisoned unselected witnesses; failed IPC close; post-close commit and
endpoint-generation reuse.

Preserve the eight original deterministic cases, 4,096 model transitions,
seed `0x38a110c5`, forced thirteen-operation prefix, draw schedule, coverage
and replay trace. Extend the independent model with its own abstract free
stack and ordered incident membership, not production link helpers or
captured candidate state. Compare all new heads/links and original fields.

This exact transition/model comparison is distinct from equality of idle
resources across different valid histories. The
[quiescent ELF-spawn resource predicate](elf-spawn-integration.md#quiescent-grant-resource-equivalence)
has a separate corrective design barrier; it must validate every index
field before applying only its documented cross-history equivalence.

Retain the purposes of all 64 GRANT01 cases. Change only expectations owned
by this decision:

- allocation counts become head/neighbor bounds, not retained prefix scans;
- multiple-release allocation expects LIFO;
- former prefix corruption is retargeted to the actual selected free or
  insertion neighborhood, with separate truly unvisited corruption cases;
- a formerly skipped active record may now be a consumed insertion head;
  do not preserve success by skipping its required local checks;
- dense cancellation becomes a selected incident path with no complete
  audit, while explicit dense diagnosis remains complete;
- unselected plan generations are untouched/non-authoritative, not zero;
- generation-boundary fixture edits keep their dependent token links
  coherent unless stale links are the deliberate failure input.

Do not drop an old sentinel, stale-owner, failed-close, generation, mask,
canary or error-order purpose. Construct expected state from fixture input
and the independent model. Keep reference scanning outside measured
production; never charge production work to fixture setup or callbacks.
Observe actual links/indexes, selected record visits and plan byte writes.
Reject hidden whole-plan initialization/copy and any empty-case capacity work.

Run the existing real endpoint/grant/syscall, wired handoff, TTY, PM, VFS
and composed spawn/exit/resource gates selected by the planner. Keep all
ordinary/metadata/observed stack configurations and B13's explicit block.
One sufficient final author plan and fresh independent implementation review
precede the first permanent commit and exact-tree merge.

## Phase barrier

This proposal is documentation only. No executable code, model, harness,
build scaffold or reusable prototype belongs to the design task.
Independent design review precedes its first commit. Only the separately
merged Accepted design activates implementation; only accepted
implementation closes B05-F003/F004.

## Current implementation and evidence owners

The implementation keeps registry/index lifecycle in `grant.c`,
representation in `include/micros/grant.h` and constant create planning in
`grant_internal.h`. The existing runtime, copy, adapter and endpoint
authorities do not acquire a second registry or new policy.

The existing [selected-grant native pair](../development/building.md#native-unit-tests)
covers initial/LIFO allocation, exact index state, sparse prepare/commit,
mixed-role neighbors, corrupted links, poisoned unselected witnesses and
commit after actual endpoint close. Its independent model uses a free-slot
stack and publication order, then projects expected membership by flat
reference enumeration rather than production splice helpers. All original
seeds, transition counts and scenario purposes remain.

Current primitive/audit and sparse-plan storage guards remain in
`tests/host/selected_grant_validation_source.py`. They reject whole-plan
initialization/movement, including at the existing production PM caller.
The shared ELF-spawn comparator and its native resource fixture own only
ADR-0130's quiescent inventory equivalence. Actual target and stack gates
remain required before acceptance; these owners do not by themselves close
the findings.

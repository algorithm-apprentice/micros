# Selected-Ownership Observation Authority Gap

## Status and fixed references

This read-only EV01-C07 study supports
[Accepted ADR-0113](../adr/0113-granular-selected-ownership-observation-authority.md)
and its [metadata contract](../architecture/selected-ownership-observation-authority.md).
It is not a MEM05 algorithm, runtime correction, retirement or executable
prototype.

```text
current policy commit: 5748ec20dcdb568a5ccfca2eccfcda09c6dd3f8c (#230)
current policy tree:   21b1b87ca2a5053d2d909d84cfd3507499949fd6
MEM03 source reference:8927f9956eb270962517b19715a12bc93a5fa466 (#214)
MINIX reference:       4db99f4012570a577414fe2a43697b2f239b699e
```

EV01-C06 #230 is separately reviewed and merged. All fifty author commands,
current receipts, complete stack coverage, journal-free committed/main
checks and protected task-ref cleanup passed. It names 477 existing MEM04
clauses without changing any guard.

Resumed MEM05 design traced the fixed MINIX release paths and current
full-capacity release scans. Its source census then exposed the distinct
MEM03 observer boundary below. The design is blocked before selecting a
release/index/handoff algorithm or authoring implementation artifacts.

## Why function keys are insufficient

At this policy, `selected_ownership_source.py:728-773` accepts the current
mutable functions through exact signature, preprocessing and call-set
conditions. It does not fix their complete body text.

The same owner's `current_observations()` at lines 991-1068 constructs the
independently pinned current primitive catalogue. `observation_sites()`
checks its digest; `_observation_bindings()` at lines 1089-1137 resolves
specific primitives and complete enclosing compounds. The coverage check
at lines 1140-1176 independently requires all eligible loop and field
positions exactly once.

Three in-memory mutations were checked against the real existing predicates.
No file was modified and no new algorithm was executed:

| Existing function | Mutation | Function/source guard | Observation rejection |
| --- | --- | --- | --- |
| `frame_ownership_bootstrap.c:collect_process_release_summary` | Parenthesize `frame_index` in its capacity-loop condition | Pass | `missing enclosing primitive region` |
| Same function | Parenthesize `word_index` in `release_bitmap[word_index]` | Pass | `MEM03 primitive occurrence is missing or ambiguous: site-0069` |
| `frame_ownership_validation.c:micros_frame_ownership_validate_metadata` | Add an empty `while (false)` loop | Pass | `MEM03 source has a missing, duplicated or undeclared primitive` |

The unmodified source passed observation admission before each mutation.
The third function currently has no site record; the coverage predicate
nevertheless enforces its empty set. These are independent live comparisons,
not failures of a missing import, new scaffold or earlier function guard.

`tools/generate_selected_ownership_fixture.py:109-143` invokes these checks
for actual current C source and verifies the exact annotation inverse before
its bootstrap-unit projection. Its fixed-source and generated-byte checks
are separate; they are not another owner of the primitive predicates.

## Complete current census

Read-only inspection used the fixed policy's existing function selector,
current catalogue and coverage predicate. All seventeen observed C owners
are members of its 207-input native bundle.

The [canonical census](../architecture/selected-ownership-observation-authority.md#closed-current-census)
has exactly 120 eligible functions: 63 nonempty and 57 empty.
The nine memory owners contribute 55 functions and 151 sites; the eight
object/scheduler owners contribute 65 functions and 28 sites.

All 179 site identifiers are distinct. Their placements are 110
before-primitive, 65 loop-entry and four complete-validator entry records.
The original 173-site catalogue loses eight replaced direct-owner reads
and gains fourteen independently specified current records.
Its current digest remains:

```text
dd940413c0de49a913827ceebee0cc0f6088669ffac343c184efca0dc0bbe71b
```

Coverage checks loops in both owner groups, but watches the thirteen
specific array-field names only in memory owners. The empty
`kernel_object_identity.c` and `kernel_object_stack.c` catalogue entries
must not hide their fifteen real zero-loop function obligations.

The census retained every complete site tuple and function selection in a
session inspection record. It generated no declaration module, executable
fixture, test or prototype.

## Inventory gap and preservation boundary

The complete current catalog has 5,259 records and twenty-one families.
There is no `MEM03/observation/` identity. EV01-C05's 484 records name
current source admission; EV01-C06's 477 records name MEM04, not MEM03,
comparisons.

The existing MEM03 source owner has these aggregate facts:

| Existing scope | Class | Payload paths |
| --- | --- | ---: |
| `source-membership:MEM03` | Permanent invariant | 42 |
| `artifact-contract:MEM03-recipes` | Artifact contract | 67 |
| `state-work-contract:MEM03` | Permanent invariant | 33 |
| `source-projection:MEM03` | Transport identity | 38 |

The permanent and artifact records cannot become transport-retirement
waivers. Retiring the source-projection aggregate would still conflate a
historical literal declaration with many independent current predicates
and pull unrelated/shared policy into the candidate closure.

The full predecessor fact digest is:

```text
f8f3a8b825eb60d8cb1c57d7897ca29d28fefaecb9009fc3224def259c7601e4
```

All six existing claims and 103 retirements remain unchanged. The proposed
120-record addition gives 5,379 facts in twenty-two families.

## Historical and current consumers

The fixed-family comparisons in `test_selected_ownership.py` and the
EV01-C05/EV01-C06 tests already select their original predecessor families
positively. MEM04's production authority first validates the complete
current catalog, then selects the fixed nineteen-family historical view
introduced by ADR-0112.

Those historical expectations and the fixed seven-file scaffold fixture
do not change. Only the two existing complete-current count assertions in
`test_evidence_authority.py` advance. New independent tests must compare
every old and new record, not replace a complete comparison with an
exclusion filter.

## Smallest correction and stop boundary

Declare one literal-only observation-binding family and add closed
materialization plus focused tests in the existing inventory/test owners.
Each function's fact binds only that declaration, its physical C subject
and the real source assertion owner.

Keep every source/observer predicate, catalogue, inverse, native product,
counter dimension, oracle, recipe and prior claim unchanged.
The design and implementation are separately reviewed outcomes.
Only after both merge may MEM05 resume its own algorithm and authority
design. New keys alone neither authorize changed runtime source nor prove
new work bounds.

# Granular Selected-Ownership Observation Authority

## Status and scope

This is the documentation-only EV01-C07 contract defined by Accepted
[ADR-0113](../adr/0113-granular-selected-ownership-observation-authority.md).
Independent design review is clean. The paired implementation remains
pending the separate design merge. MEM05 design is blocked on this
prerequisite; its runtime algorithm is not specified here.

The [fixed-source study](../research/selected-ownership-observation-authority-gap.md)
records independently enforced MEM03 observation predicates that have no
individual inventory identity. This outcome names those predicates without
changing their behavior.

Fixed references:

```text
policy commit: 5748ec20dcdb568a5ccfca2eccfcda09c6dd3f8c
policy tree:   21b1b87ca2a5053d2d909d84cfd3507499949fd6
MEM03 source:  8927f9956eb270962517b19715a12bc93a5fa466
```

The policy is independently reviewed #230. The source reference is MEM03's
original #214 input, not a proposed MEM05 implementation.
The current inventory has 5,259 facts in twenty-one families, six claims
and 103 retired identities.

## Existing assertion ownership

`tests/host/selected_ownership_source.py` owns all three cooperating
predicates:

1. `current_observations()` reconstructs the fixed current catalogue from
   the original catalogue and independently specified replacement/addition
   declarations.
2. `_observation_bindings()` binds every selected site to its exact
   function, primitive and enclosing compound, preserving placement,
   units and field witness.
3. `_validate_observation_coverage()` requires every eligible loop and
   watched memory-field occurrence to be represented exactly once.

`observe_source()` first retains the existing source guard, then renders
those checked observations. `project_observations()` removes only exact
admitted calls and requires rendering to recover identical annotated text.

`tools/generate_selected_ownership_fixture.py` delegates both operations
to that source owner. Annotation occurs before its bounded bootstrap-unit
projection. The existing native closure therefore determines the eligible
bootstrap functions; generated test accessors are not invented observation
subjects.

There is no second generator-owned primitive-binding comparison. Fixed-file
admission, stable input/mode identity, exact inverse rendering, source-stage
selection, compiler/artifact evidence, collector rules and work oracles
remain separate requirements.

## Closed current census

The seventeen observed C owners all occur in the existing 207-input native
bundle. Current eligible functions are exactly:

- the two fixed frame-allocation functions;
- the six fixed ownership-core functions plus its one existing MEM03 helper;
- the fixed bootstrap closure plus its existing MEM03 helpers; and
- every current function in the remaining fourteen observed owners.

Use the fixed policy's `OBSERVED_MEMORY_PATHS`, `OBSERVED_OBJECT_PATHS`,
`OBSERVED_MEMORY_FUNCTIONS`, `native_bootstrap_closure` and `new_helpers`
to reconstruct this finite set. Do not discover future subjects from the
candidate worktree.

| Physical owner | Functions | Nonempty | Empty | Site records |
| --- | ---: | ---: | ---: | ---: |
| `kernel/memory/frame_allocator.c` | 2 | 2 | 0 | 10 |
| `kernel/memory/frame_ownership.c` | 7 | 7 | 0 | 17 |
| `kernel/memory/frame_ownership_validation.c` | 16 | 11 | 5 | 37 |
| `kernel/memory/frame_ownership_bootstrap.c` | 7 | 5 | 2 | 25 |
| `kernel/memory/frame_ownership_handoff.c` | 4 | 3 | 1 | 19 |
| `kernel/memory/frame_ownership_vm_claim.c` | 2 | 2 | 0 | 4 |
| `kernel/memory/frame_ownership_vm_freeze.c` | 2 | 2 | 0 | 13 |
| `kernel/memory/frame_ownership_vm_release.c` | 4 | 4 | 0 | 21 |
| `kernel/memory/bootstrap_memory.c` | 11 | 3 | 8 | 5 |
| `kernel/objects/kernel_objects.c` | 2 | 2 | 0 | 2 |
| `kernel/objects/kernel_object_validation.c` | 10 | 8 | 2 | 11 |
| `kernel/objects/kernel_object_state.c` | 12 | 5 | 7 | 5 |
| `kernel/objects/kernel_object_identity.c` | 12 | 0 | 12 | 0 |
| `kernel/objects/kernel_object_stack.c` | 3 | 0 | 3 | 0 |
| `kernel/objects/kernel_object_process.c` | 6 | 1 | 5 | 1 |
| `kernel/scheduler/scheduler_core.c` | 13 | 3 | 10 | 3 |
| `kernel/scheduler/scheduler_core_validate.c` | 7 | 5 | 2 | 6 |
| **Total** | **120** | **63** | **57** | **179** |

The nine memory owners have 55 function contracts and 151 sites.
The eight object/scheduler owners have 65 contracts and 28 sites.
The two zero-site files remain in the census: their functions still have
actual loop-coverage obligations.

The 179 sites comprise:

- 110 `before` placements, 65 `loop-entry` placements and four `entry`
  placements; and
- 73 `RECORD_VISIT`, ten `RANGE_VISIT`, fifteen `BITMAP_WORD_LOAD`,
  77 `INDEX_PROBE` and four `COMPLETE_VALIDATOR_CALL` records.

Preserve the current catalogue digest:

```text
dd940413c0de49a913827ceebee0cc0f6088669ffac343c184efca0dc0bbe71b
```

The 173-site original catalogue and its digest remain unchanged too.
Neither catalogue is regenerated from an edited runtime body.

## Declaration schema

Add only `tests/host/selected_ownership_observation_bindings.py`, containing
an optional module docstring and exactly two literal assignments:
`BINDING_REFERENCE` and `NATIVE_SOURCE_BINDINGS`.
No imports, calls, functions, comprehensions, executable expressions,
rebinding or other top-level statements are allowed.

`BINDING_REFERENCE` has the existing tagged tuple form:

```text
(("policy", policy_commit, policy_tree), ("MEM03", original_source_commit))
```

The values are the fixed references above. `NATIVE_SOURCE_BINDINGS` is a
sorted tuple of exactly 120 unique four-tuples:

```text
(scope, subject_path, assertion_owners, contract)
```

Every scope is:

```text
MEM03/observation/<physical-C-path>/<function>
```

The function is an ordinary C identifier belonging to that fixed subject.
The exact subject set and per-subject counts are the table above.
`assertion_owners` is always the one-element tuple containing
`tests/host/selected_ownership_source.py`.

Each contract has exactly:

| Field | Required value |
| --- | --- |
| `comparison` | `current-primitive-bindings` |
| `coverage` | `memory-fields-and-loops` for the nine memory owners; `loops-only` for the eight object/scheduler owners |
| `sites` | The ordered tuple of complete current catalogue records for that function, including an empty tuple when required |

A site retains its existing ten-field tuple unchanged:

```text
(function, site_id, owner, category, kind, regions,
 primitive, placement, units, witness)
```

Preserve original tuple order within a function, not a new alphabetical
site ordering. Multiple observations at one insertion position keep their
existing deterministic rendering order.

All sites name the row's function. Their identifiers are globally unique
across the 179 records. Owner, category and kind retain their existing
fixed declarations. `units` is the integer one, never a boolean.
Regions retain their ordered primitive-token strings and nonnegative
occurrence ordinals. Preserve token-normalized primitive text; do not
replace it with a body hash or current-source spelling.

The existing placement relations remain exact:

- `entry`: `COMPLETE_VALIDATOR_CALL`, empty regions/primitive/witness;
- `loop-entry`: `INDEX_PROBE`, nonempty regions with the last primitive
  equal to the loop primitive, and an empty witness; and
- `before`: the existing watched-field witness and exact unique primitive
  occurrence in its declared enclosing region.

## Coverage is part of each comparison

Both coverage modes inspect `for` and `while` tokens followed by `(` in
eligible function bodies. Their positions must equal the unique loop-entry
bindings. Do not widen this to another loop family or change the parser.

For memory owners, the fixed field policy additionally observes:

```text
owners, handoff_targets, owner_counts, managed_ranges, allocated_bitmap,
release_bitmap, selected_bitmap, frame_indices, data_frame_indices,
table_frame_indices, physical_addresses, data_physical_addresses,
table_physical_addresses
```

The first five require a preceding `.` or `->`. At such a member occurrence,
failure to use the required array syntax is rejected as an unobserved alias.
The remaining names are observed at their existing array syntax.
All observed field positions must equal the unique `before` witnesses.
Object/scheduler owners have no such field scan; do not fabricate one.

These are descriptions of fixed predicates, not configurable watch lists.
The source owner and shared lexical/region helpers remain unchanged.
An empty `sites` tuple therefore still forbids new eligible loops and, for
memory owners, new watched-field occurrences. Its contract must not be
omitted merely because rendering currently emits no observation for it.

## Inventory records and conservation

Register one new family, `selected-ownership-observation-admission`.
Materialize every record with:

- owner: the new declaration;
- kind: `native-source-admission`;
- authority class: `TRANSPORT_IDENTITY`;
- primary unit: B02;
- consumer units: B01 through B12, then B14; and
- payload paths: exactly the declaration, its one physical subject and the
  one assertion owner, sorted and deduplicated.

Use the existing value construction:

```text
digest((reference, scope, subject, assertion_owners, contract))
```

Before any implementation declaration/extractor/test edit, independently
freeze all 120 expected rows and complete facts from the fixed policy,
its original runtime source and existing comparison helpers. Independently
declare the complete new-family fingerprint at that point.
The production materializer reads checked AST/literals only and requires
the exact closed schema, subject/function census, counts, uniqueness and
complete independently frozen fingerprint.

Every subject/assertion/declaration path must pass the existing ordinary,
unlinked-file and parent-path checks. Reject unknown or borrowed subjects,
functions, comparison/coverage tags, consumers, references, fields,
placements, witnesses, values, missing/extra records or executable data.

The complete predecessor fact digest is:

```text
f8f3a8b825eb60d8cb1c57d7897ca29d28fefaecb9009fc3224def259c7601e4
```

Preserve all 5,259 complete records and twenty-one family fingerprints.
The complete new inventory is exactly 5,379 facts in twenty-two families.
Preserve all six claims, 103 retired keys and every old authority class.
All 120 new identities remain active.

The old source-membership and state/work records are permanent invariants.
The old recipe record is an artifact contract. The 38-path source-projection
transport record names its original declaration, not these individual
binding predicates. None is retired or rebaselined here.

Current-total assertions in `test_evidence_authority.py` advance consistently.
Its historical family-filtered comparisons retain their original
expectations. MEM04's already corrected complete-current-then-historical
authority path, its fixed nineteen-family register and historical scaffold
fixtures remain unchanged. No new compatibility adapter is required.

## Implementation and acceptance boundary

After separate design merge, change only:

1. the new literal declaration;
2. closed registration/materialization in existing
   `tests/host/evidence_authority_inventory.py`;
3. focused regressions and complete-current totals in existing
   `tests/host/test_evidence_authority.py`; and
4. directly related documentation.

Do not change any source guard, observation catalogue, generator, native
fixture, work oracle, collector, shared parser, transition, checked view,
runtime source, CMake recipe, workflow or budget.

Require genuine coverage Red after the unchanged function/source guard
accepts and the unchanged observer rejects the loop-anchor, bitmap-primitive
and empty-helper mutations in the study. The missing keys, not those
intentional guard rejections, are the failing inventory requirement.

Green independently reconstructs all expected records from fixed Git
policy/runtime and compares every field, including all empty contracts.
It verifies complete predecessor/fingerprint/claim equality, schema and
unsafe-path negatives, borrowed consumers, comparison fidelity, existing
positive/inverse admission and the distinction between the two coverage
modes. Synthetic closure tests use the unchanged EV01 validator: the three
actual payload owners are included, shared policy remains disjoint and
real candidate/immutable overlap still fails.

Focused iteration after implementation uses the existing owned runner:

```bash
"${MICROS_PYTHON}" -E -S -B tools/run_host_tests.py --test test_evidence_authority --test test_selected_ownership --test test_selected_address_space
```

Before independent first-commit review:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier full --execute --producer
```

Retain all fifty commands, 44 CTests, complete Python discovery, forty QEMU
workflows, four standalone-image workflows, 44 static configurations,
113 ELF outputs, ten observed scopes, blocked B13 and three current
same-run reusable profiles. Fast membership is unchanged.

Bind exact HEAD/tree/results to a clean staged candidate with no untracked
task files. Publish only identical reviewed/committed/remote/preview/squash
content, verify all six prior authorities without journals in committed
and actual-main checkouts, and clean exact completed refs.

EV01-C07 closes no runtime finding and supplies no replacement algorithm.
MEM05 still requires its own fixed MINIX/current study, exact authority and
failure contract, actual-source Red/Green, recertification and independent
review after this prerequisite merges.

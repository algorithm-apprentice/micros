# Object and Panic Source Authority

## Status and single outcome

This Accepted EV01-C09 contract follows
[ADR-0118](../adr/0118-precise-object-and-panic-source-authority.md).
Its [fixed-source study](../research/object-and-panic-source-authority-gap.md)
establishes fifty misbound existing object identities and eleven unnamed
existing panic source policies.

```text
policy commit: 89d3b85314754ae225f01fd16f257745bf19ce71
policy tree:   ce73f3875dd354ef6c8452808d53ab2b9e14d522
panic source:  e12b2d6c163ac92282d380eff5be419fad7f7a30
```

Complete independent correction review is clean. Only this design is
active until its separate merge; the dependent implementation remains
pending. OBJ01 design is blocked until that implementation independently
merges. No OBJ01 mechanism, implementation or retirement is authorized here.

## Fifty exact metadata corrections

The only affected old assertion owner is:

```text
tests/host/test_kernel_object_source_layout.py
```

Select exactly its 48 `data-entry:BASELINE_BODIES/0/<name>` records,
`data-entry:BASELINE_SIGNATURES/0` and `data-entry:BASE_CORE_DIGEST`.
The fixed study's nine-owner table and the original 48-name declaration
define the closed set.

Preserve these fields byte-for-byte:

```text
key, family, owner_path, kind, scope, authority_class, evidence_value
```

Only `payload_paths`, `primary_unit` and `consumer_units` change.
The new payload is the sorted unique assertion owner plus:

| Record | Exact source payload |
| --- | --- |
| Each of 48 original bodies | The sole current C owner of that original, including its same-file outlined phases |
| `BASELINE_SIGNATURES/0` | All nine current portable C owners |
| `BASE_CORE_DIGEST` | All nine current portable C owners and `kernel/objects/kernel_object_private.h` |

Thus body records have two payloads, the signature record ten and the
complete-byte record eleven. Apply the unchanged unit-mapping functions:
all fifty now have primary `B03` and consumers `B03`, `B14`.
Do not alter mapping rules to manufacture these results.

The two already corrected scheduler/endpoint complete-byte records retain
their own assertion owners and eleven-path bindings. Other object
families, records and fallback policies remain unchanged.

### Closed ownership decoding

Read `CORE_SOURCES`, `PRIVATE_HEADERS`, `ORIGINAL_FUNCTIONS` and
`BASELINE_BODIES` from the ordinary fixed owner, without importing or
executing that source gate. The portable names are `ORIGINAL_FUNCTIONS[0]`.
Resolve logical C/header paths through the existing checked
`KERNEL_FILE_FACTS` directory map.

Require the exact nine logical C members in the study, their original
order, one ordinary declaration per table, exact integer family/owner
keys, owner indices 0 through 8, 48 unique valid C identifiers and
complete equality with the portable body-name set. The only required
private header is the existing first member,
`kernel/kernel_object_private.h`.

The portable name lists use two existing spellings:

- a literal list of string constants;
- a literal string followed by argument-free, keyword-free `.split()`.

Recognize only those AST shapes at the admitted portable ownership
positions. Do not evaluate a call, import the module, follow an arbitrary
name, accept a comprehension, convert Boolean keys to integers, or accept
another method/receiver/argument. The shared `literal_value` contract
remains unchanged. The `CORE_SOURCES` alias in `SOURCE_SETS` is not a
reason to evaluate that assignment; the admitted direct tuple already
supplies the portable family.

Reject duplicate/missing/rebound declarations, malformed or duplicate
names, unknown owner indices, mismatched body names, unknown directory
members, old flat aliases and missing/linked current sources or parents.
Keep ordinary path validation and canonical ordering from the existing
source helper. Parse the source declarations once per inventory
materialization, not once per body record.

## Eleven panic source identities

After separate design merge add:

```text
owner:  tests/host/panic_trace_source_bindings.py
family: panic-trace-source-admission
```

The new file contains only an optional docstring and two literal
assignments, `BINDING_REFERENCE` and `SOURCE_BINDINGS`. Use the shared
reader's existing strings, integers, tuples and dictionaries; Boolean,
null, list, set, import, call, function, class and executable registration
are not admitted.

The reference tuple is exactly:

```text
("policy", fixed #238 commit, fixed #238 tree)
("PLAT03", fixed panic-source commit)
```

Each of eleven rows, sorted by scope, has:

```text
(scope, subject, assertion_owners, comparison_contract)
```

`scope` is `PLAT03/projected-source/<physical-C-path>`.
`subject` is that exact path from the study.
`assertion_owners` is always the one-element tuple
`("tools/stack_bound_fatal.py",)`.

The comparison contract contains exactly:

```text
comparison:       "projected-file"
projection_owner: "tests/host/panic_trace_source.py"
projection:       "project_trace_runtime"
assertions:       exact tuple from the study, preservation then fault then getter
sha256:           SHA256 of the fixed reference's raw UTF-8 bytes
```

The original predicate compares the actual source *after* the named
projection to that fixed reference. Preserve raw equality, not compact
tokens, an AST approximation or a hash of unprojected current bytes.
Both occurrences of the shared hart and identity policies belong to their
one row. The runtime row has three assertion names:
`trace_preservation`, `trace_fault_artifacts`, `checked_source_record`.
The third refers only to the existing `TRACE_FIXTURE_GETTERS` branch
with `trace_test` enabled, not the other contracts in that function.

| Fact field | Value |
| --- | --- |
| `key` | `tests/host/panic_trace_source_bindings.py:source-admission:<scope>` |
| `family` | `panic-trace-source-admission` |
| `owner_path` | The single literal declaration |
| `kind` | `source-admission` |
| `scope` | Exact row scope |
| `authority_class` | `TRANSPORT_IDENTITY` |
| `primary_unit` | `B01`, the existing fatal/stack proof owner |
| `consumer_units` | `B01` through `B12`, then `B14` |
| `payload_paths` | Sorted declaration, subject and actual assertion owner: three paths |
| `evidence_value` | Existing canonical `digest()` of `(reference, scope, subject, assertion_owners, comparison_contract)` |

The fixed projection provider is comparison provenance and remains an
immutable dependency, not an additional mutable subject. Do not drag its
other PLAT03 declarations, the shared parser, machine evaluator, stack
policy, recipes or documentation into these payloads.

The declaration, subject, enforcer and parents must be ordinary unlinked
files/directories. Values come from the fixed reference, never from current
subject bytes. Mutating a subject may invalidate its real source guard;
it must not silently regenerate its expected fact.

## Complete current and historical conservation

Before any declaration, extractor, bridge or regression edit, independently
freeze:

- all 5,840 old complete records and twenty-four old fingerprints;
- the exact fifty before/after records, derived from the fixed ownership
  declarations and unchanged unit rules;
- all eleven fixed panic rows and complete facts;
- the seven committed claims and 185 retired identities.

The current result is 5,851 records in twenty-five families. Exactly fifty
old metadata tuples change. All 5,840 identities, values and classes
remain; the other 5,790 old records remain complete matches.
Only `kernel-structure` changes among the twenty-four old fingerprints.
The new family adds its own independently derived fingerprint.

### One checked, historical-only inverse

Provide one bounded metadata inverse for the exact fifty keys.
Its old fields derive from the fixed #238 source/membership and original
mapping rules, not current worktree membership. Before replacing anything,
require the supplied records' corrected payload, primary and consumer
fields to match their independently specified current values.

Reject missing, extra or duplicate affected keys and any wrong corrected
metadata. Replace only the three approved fields. Every other field,
including a changed old evidence value, must pass through unchanged and
therefore still fail its original historical fingerprint.
The inverse never changes current inventory records in place.

Earlier conservation tests compose this inverse before their existing
bounded historical projections. They keep all original expected digests.
This is not a new current authorization view.

### MEM04 and MEM05 predecessor comparisons

Both committed providers have a real historical dependency.
MEM04's `historical_predecessor_records` requires nineteen original
family fingerprints and 4,774 records; its `declared_scope` checks their
complete digest. MEM05's corresponding comparison requires twenty-two
original fingerprints and 5,379 records with its complete digest.
MEM05 also constructs MEM04's authority, so adapting only MEM05 would
still break committed authority.

Their sole adaptation is:

1. validate the complete current inventory, including the corrected
   metadata and all new families;
2. apply the checked fifty-key historical inverse;
3. positively select exactly the provider's existing predecessor families;
4. recompute/compare their original fingerprints and require its exact
   record count and original complete digest.

Keep the nineteen/4,774 MEM04 and twenty-two/5,379 MEM05 boundaries
separate. MEM04's existing complete-digest check in `declared_scope`
remains active; no complete digest or family fingerprint is rebaselined.

Do not substitute current fingerprints for old expected values, filter
unknown families before validation, or allow the inverse to approve a
current transition. The fixed selected thirty-three MEM04 and eighty-two
MEM05 identity records, plans, committed claims and retirement sets remain
unchanged.

All current no-plan transition checks use the complete corrected inventory.
Prove all seven claims without an author journal, including ordinary
MEM05 source admission. The historical forty-four-input MEM05 scaffold
continues to use its original complete overlay; it must not borrow this
new live metadata as historical author input.

## Implementation ownership

The eventual implementation is limited to:

| Owner | Permitted change |
| --- | --- |
| `tests/host/panic_trace_source_bindings.py` | New eleven-row literal declaration only |
| `tests/host/evidence_authority_inventory.py` | Fifty exact metadata bindings, closed new-family materialization/registration, fingerprints and checked historical inverse |
| `tests/host/evidence_authority_source.py` | If needed, the dedicated closed portable-owner decoder; no change to the shared literal language |
| `tests/host/selected_address_space_authority.py`, `tests/host/selected_release_handoff_authority.py` | Only their complete-current-first historical predecessor comparisons described above |
| `tests/host/test_evidence_authority.py` | Independent fixed-reference expectations, coverage/schema/path/conservation/bridge tests and discovery reference |
| Existing `test_runtime_memory.py`, `test_terminal_failure.py`, `test_panic_trace.py`, `test_bootstrap_allocation.py`, `test_kernel_mapping.py`, `test_selected_ownership.py`, `test_selected_address_space.py`, `test_selected_release_handoff.py` | Only directly affected historical-record comparisons or existing fixed-input historical fixtures |
| Directly related Markdown | Current phase, contract and exact evidence |

Use the existing test owner and source helpers; no new runner, executable,
CTest, workflow or general registration/parser framework is required.
The old object/scheduler/directory source gates and their declarations
remain byte-identical. So do all runtime subjects, panic source/stack
guards, their machine-effect implementations, native work producers,
transition types/validator, checked-view policy, collectors and recipes.

## Test-first and final evidence

Red must demonstrate the actual gaps:

1. prove positive original object reconstruction and rejecting body/header
   mutations, then show missing physical owners and collateral immutable
   inputs in the existing body/signature/complete-source records;
2. prove the unchanged panic source predicates accept their exact projected
   inputs and reject changed subjects at each existing enforcer, then show
   missing individual names.

For the second item, a focused source-policy fixture may isolate the
unmodified source comparison from later machine-artifact work. A positive
control must reach the declared post-source boundary, and a negative must
fail at the intended equality, not a missing artifact, earlier guard or
unrelated mocked failure. Such a fixture is not machine-proof evidence;
the complete existing stack gate still supplies that independently.

Cover both assertions for a shared unit, one preservation-only unit, one
fault-only unit and all three runtime assertions, including both
trace-fixture getters after their exact mode/origin admission and the
projected runtime guard. Complete row equality must cover all eleven
even when representative failures are grouped.

Green requires complete independently frozen equality, all original
record/value/class/claim conservation, strict closed decoding, path safety
and historical inverse rejection. Include malformed string-call shapes,
duplicate/unknown/missing owners, a bad non-facade reconstruction member,
wrong consumers, altered old values, unknown current families and a
malformed new fact that cannot disappear through historical selection.
The original kernel-object, scheduler, directory and stack source
regressions remain active.

Focused feedback uses the existing owned runner:

```bash
"${MICROS_PYTHON}" -E -S -B tools/run_host_tests.py --test test_evidence_authority --test test_selected_release_handoff
```

Then run the required complete current author plan:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier full --execute --producer
```

Retain all fifty commands, 45 CTests, complete Python discovery, forty
QEMU workflows, four standalone images, 44 static configurations,
113 ELFs, ten observed scopes, blocked B13 and three same-run reusable
profiles. No timeout or budget grows.

Bind exact HEAD, staged tree, commands and results to a clean complete
candidate. Independent implementation review precedes the first permanent
implementation commit. Verify reviewed/committed/remote/preview/squash
tree equality and fresh journal-free committed/main checks, then remove
only completed task refs while preserving purposeful archives.

Only after that separate merge may OBJ01 design resume. This metadata
outcome closes no runtime finding and does not accept hosted CI.

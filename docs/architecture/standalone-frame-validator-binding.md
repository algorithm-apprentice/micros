# Standalone Frame-Validator Evidence Binding

## Status and purpose

This is the Accepted EV01-C03 contract under
[ADR-0098](../adr/0098-standalone-frame-validator-evidence-binding.md).
The [source study](../research/standalone-frame-validator-payload-gap.md)
establishes one concrete collision in the blocked MEM01 design.

The fixed source is PLAT03 #204:

```text
commit: ddca7661d4fa7e13acf0aaa5f0ba55b91f9c9916
tree:   3bf57ef9d12345f7b72e082a5843c97a2476f23e
```

Design #205 is separately reviewed and merged as
`8d4fd8c0dad354a7840a70d1505fa0cfebd2888f`.
The initial implementation is preserved but blocked: its full author run
found that the ordinary panic-trace gate rejects these already committed
design documents as outside PLAT03's historical scope.
[Accepted ADR-0099](../adr/0099-panic-trace-execution-scopes.md) defines the
separate prerequisite; no controller change is folded into this contract.
MEM01 remains blocked and no memory runtime work starts here.

## Exact admission

Recognize exactly:

```text
owner:       tests/host/kernel_layout_source.py
declaration: BOUNDED_FRAME_VALIDATION_SHA256
leaf:        empty scalar leaf
key:         tests/host/kernel_layout_source.py:data-entry:BOUNDED_FRAME_VALIDATION_SHA256
logical:     kernel/frame_ownership_validation.c
physical:    kernel/memory/frame_ownership_validation.c
```

The declaration must retain its one ordinary top-level scalar literal shape.
Use the existing checked literal/directory helpers rather than executing the
source gate or scanning unrelated C bodies. Unknown declarations retain their
current behavior. A malformed occurrence of this admitted declaration must
fail explicitly, not acquire the old fallback.

The actual assertion hashes the selected validator source string before
reconstructing the earlier validator. That consumer establishes its single
C payload. It does not hash CMake, the planner or every file named elsewhere
in the large evidence-owner module.

Resolve the exact logical source through the unchanged `KERNEL_FILE_FACTS`
directory map. Require an ordinary unlinked current file, ordinary unlinked
parent directories, one unambiguous owner and no duplicate old flat owner.
Missing map entries, malformed records, absent files, symlinks, hard links and
ambiguous mappings remain errors.

Do not bind `BASE_FRAME_VALIDATION_SHA256`, another name containing
`FRAME_VALIDATION`, a same-named variable in another owner, or a reconstructed
multi-source property by similarity. No wildcard or task-supplied override
is introduced.

Source binding identifies the bytes whose current identity a later
transition must capture. It does not require the current C bytes to equal the
historical hash inside the inventory resolver; the retained source assertion
and ADR-0082 authorization remain responsible for that comparison.

## Exact corrected record

Preserve:

| Field | Required value |
| --- | --- |
| `key` | `tests/host/kernel_layout_source.py:data-entry:BOUNDED_FRAME_VALIDATION_SHA256` |
| `family` | `authority-core` |
| `owner_path` | `tests/host/kernel_layout_source.py` |
| `kind` | `data-entry` |
| `scope` | `BOUNDED_FRAME_VALIDATION_SHA256` |
| `authority_class` | `TRANSPORT_IDENTITY` |
| `evidence_value` | `448f61074bb3c5d52631660b9537e45a8bb9cca266a645edd503457b6fa03694` |

Change only:

| Field | Corrected value |
| --- | --- |
| `payload_paths` | Sorted tuple of `kernel/memory/frame_ownership_validation.c` and `tests/host/kernel_layout_source.py` |
| `primary_unit` | `B02` |
| `consumer_units` | Sorted tuple `B02`, `B14` |

The primary/consumer values follow the existing `_fact_primary_unit` and
`_consumer_units` rules. Do not edit those rules or retain/remove a unit
merely to avoid a closure conflict.

At the fixed base, the old primary is B14, consumers are
B01/B02/B03/B04/B05/B06/B07/B08/B09/B10/B14, and the payload contains
797 paths. Its actual C owner is already present; the defect is collateral
ownership, not an omitted owner.

## Complete conservation

There remain exactly 3,938 facts and fourteen families.
`authority-core` remains 1,595 facts. There is no new owner, family,
identity, class, historical value or retirement.

Only the one metadata tuple above changes. All 3,937 other complete records
remain identical. In particular preserve:

- all 104 previously corrected ADR-0092 logical-file bindings;
- both complete object-family reconstruction bindings;
- the 177 packed scheduler facts and their strict schema/path behavior;
- the separate original/bounded active/inactive frame-validation authorities;
- every remaining unresolved-schema fallback;
- COPY01 and PLAT01 committed claims;
- the terminal and panic-trace declarations and baseline authority; and
- every source, owner, recipe, ABI and artifact assertion.

The one changed family fingerprint is independently specified:

| Family | Fixed-base fingerprint | Corrected fingerprint |
| --- | --- | --- |
| `authority-core` | `c9f825f5b1488d03ee1c54db9ae3324b312807cd62de6dfa23166331a32e7e5d` | `152ba765d3a332961b59e88a7725f4a823c335bb62b97ba09c662bb84e1098fd` |

Preserve the other thirteen fingerprints exactly. The new value is the
fixed complete family with the single declared record replacement, not a
digest of a modified extractor's observed output.

## Independent declaration before implementation edits

After design merge and before modifying the extractor or tests, materialize
and retain the complete before/after record sets from fixed #204 source,
its literal declarations and directory map, and the unchanged unit rules.

The declaration must prove:

1. the fixed base commit/tree and complete 3,938-record inventory;
2. the exact scalar assertion and actual single-source consumer;
3. the complete old 797-path metadata from fixed source, not a current
   worktree scan;
4. the exact two-path corrected metadata;
5. unchanged identity, owner, class and historical value;
6. exactly one changed record and one changed family fingerprint; and
7. conservation of all other complete records and thirteen fingerprints.

The implementation must match this independent declaration. It may not
repair a failing comparison by regenerating expectations from its candidate.
The design-phase calculation used the unchanged fixed-base extractor and
replaced only the three explicitly declared metadata fields.

## Historical comparison

Earlier conservation tests retain their original expected fingerprints.
Provide one bounded test-side view that:

1. recognizes only the exact corrected key;
2. rejects duplicate occurrences and incorrect current payload/primary/
   consumer metadata;
3. restores only those three metadata fields to their fixed #204 values;
4. passes every other field through unchanged; and
5. leaves unrelated/subset records unchanged.

A changed historical hash, key, family, owner, scope or class must still fail
the original comparison; the inverse does not overwrite that evidence.

Derive the old fallback from the fixed source's literal path declarations
and fixed Git membership, using the existing bounded decoding rules. Do not
read mutable current membership as historical authority or import/execute a
source gate to discover old paths.

Compose this view before any older historical binding projection where the
comparison needs it. Keep the helper inside the already registered
`test_evidence_authority.py` owner and update only directly dependent
conservation call sites. Do not create a new unregistered projection module
or a generated mutable inventory.

This view is never used by current transition authorization. Current
inventory, plans and events always consume the corrected two-path record.

## Unchanged transition semantics

Keep all EV01 types, state transitions, event order, registry rules,
whole-file candidate/immutable closure and overlap rejection unchanged.
Do not alter `evidence_authority_validation.py` to make the example pass.

The positive closure regression supplies:

- this one existing affected identity;
- the evidence owner and physical validator as candidate inputs;
- independently declared command/oracle inputs including CMake and the
  planner; and
- the same current unit/source authority required by the existing validator.

Before correction the fallback intersects those immutable inputs and fails
`PROJECTION`. After correction that false collision disappears. A deliberate
overlap using the actual validator source must still fail. Missing real
candidate bytes and source changes must still fail their existing projection
checks.

This regression is evidence for metadata closure, not a real MEM01
retirement event. No identity is suppressed or claimed retired in EV01-C03.
The later MEM01 design must still close its complete source, command, tool,
oracle and unit scopes on its new base.

## Implementation boundary

The separate implementation changes only:

- the exact qualified source binding and one independently declared
  `authority-core` fingerprint in `evidence_authority_inventory.py`;
- focused binding/closure/path/inverse/conservation tests and the bounded
  historical view in the existing `test_evidence_authority.py`;
- directly dependent prior-family comparisons in current authority tests;
  and
- directly related documentation.

Reuse `evidence_authority_source.py` without weakening its path or literal
rules. Do not modify the kernel-directory map, the historical hash
declaration, source assertions, transition validator/types, any C/header/
assembly, CMake targets/presets or the validation planner.

For source size `s`, map size `m` and one admitted binding, reuse the existing
`O(s)` literal decoding and `O(m)` map validation/storage. The selected
dictionary lookup is expected amortized constant work with `O(m)` worst-case
collision behavior. The result adds one two-path record, not another runtime
structure or kernel hot-path operation.

## Test-first and final acceptance

Native Red must expose the actual 797-path collateral binding and concrete
immutable/candidate collision, not merely a missing new helper import.
Green requires the complete corrected record and unchanged other records,
not a representative count alone.

Focused negatives cover:

- malformed/duplicate scalar declarations or a changed admitted shape;
- similarly named but unadmitted declarations/owners;
- missing, malformed or ambiguous directory bindings;
- missing current source, linked source/ancestor, hard link and old flat
  duplicates;
- incorrect current metadata, duplicate records and evidence-value drift
  through the historical view;
- unchanged rejection of a real immutable/candidate overlap; and
- absent or changed actual source bytes in current projections.

Also prove historical fingerprint recovery, every unchanged current
fingerprint, both prior committed claims without author journals and retained
packed/relocated rejection behavior.

Then run the existing complete fresh author plan under unchanged inputs and
budgets: native, QEMU, integration/image, all 44 static configurations/
113 outputs, ten observed scopes, docs and Git checks. B13 remains blocked.
Independent implementation review and identical reviewed/committed/remote/
squash trees precede branch cleanup and restarting MEM01 design.

## Design phase barrier

This proposal adds documents only. No extractor, test, model, fixture,
historical inverse, executable declaration, build scaffold or dependency is
authored in the design phase. The implementation remains pending separate
review, Acceptance and design merge.

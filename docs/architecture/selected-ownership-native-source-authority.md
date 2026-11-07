# Granular Selected-Ownership Native Source Authority

## Status and outcome

This is the Accepted EV01-C05 contract under
[ADR-0106](../adr/0106-granular-selected-ownership-native-source-authority.md).
The [fixed-source study](../research/selected-ownership-native-source-authority-gap.md)
establishes missing identities for existing MEM03 current-source clauses.

```text
policy commit:  da258293f507c745c8e157cd84bd13b2fb4b5e77
policy tree:    5793c993338c22cf30aa552c9397c8aa234c33cc
MEM03 runtime:  8927f9956eb270962517b19715a12bc93a5fa466
runtime tree:   9f7802dfdfc84034b626068b5c160b90a645dad7
```

Independent read-only design review found no substantive issue. Separate
design #220 merged as `1d9eab05afab16a4caff519a84790541036ee240`, with identical
reviewed/committed/remote/preview/squash tree
`d5c3f3d4adf653f278d47c03b3f1e58a7dc8315e`.
The separate metadata implementation is independently reviewed and merged
as #221, `f28377decc0d20533e05330f2f2211db3f230910`, with identical
reviewed/committed/remote/preview/squash tree
`4b56076479ac53092c4c83d9c9b52829b66139ba`.
Real coverage Red, complete old/new record equality, all fifty fresh author
commands and independent first-commit review passed. Fresh committed and
actual-main checks preserve all five authorities without journals; exact
completed refs and the owned check worktree are removed.

The outcome is precise inventory authority for **484 already defined
current-source clauses**, not 484 new tests or runtime operations. No
assertion is suppressed and no identity is retired. This resumes MEM04
design, not its runtime implementation.

## Preserved authority

Preserve:

- all 4,290 complete predecessor facts and eighteen family fingerprints;
- the original MEM03 aggregate declarations and all Accepted design pins;
- ADR-0102's entire 328-record family and its original values;
- five journal-free committed claims and seventy prior retired identities;
- the exact #214 historical native sources, modes and membership;
- every current source, signature, preprocessing, body, call-set, definition,
  header, skeleton and dependency rejection;
- all native primitive sites, source-stage bindings and passive inverses;
- the 99-case MEM03 matrix, 495 snapshots and 58 current guards per channel;
- complete diagnostics, the retained/migrated audit ledger and helper-token
  assertions;
- the existing collector ABI and absence of target observation hooks; and
- checked source-view behavior, fast/full membership, full author acceptance,
  EV01 transition rules and immutable/candidate overlap rejection.

The aggregate `source-projection:MEM03` remains the exact historical literal
declaration record. New per-clause records name a different obligation.
Their addition neither replaces that aggregate nor claims that any changed
production algorithm satisfies an old work bound.

## One closed declaration

Add exactly this owner and separate family:

```text
owner:  tests/host/selected_ownership_native_source_bindings.py
family: selected-ownership-native-admission
```

Permit only an optional module docstring and the two literal assignments
`BINDING_REFERENCE` and `NATIVE_SOURCE_BINDINGS`. No imports, functions,
classes, calls, comprehensions, runtime discovery, executable registration or
filesystem access belongs in this declaration.

The reference tuple contains exactly:

```text
("policy", fixed #219 commit, fixed #219 tree)
("MEM03", fixed #214 runtime commit)
```

The binding tuple has exactly 484 rows, sorted by unique scope. Each row is
`(scope, subject, assertion_owners, comparison_contract)`. Use only types
already accepted by the checked literal reader; no Boolean/null/expression
extension is needed.

The inventory resolver reads the ordinary file through the existing checked
AST/literal helpers. It never imports or executes the declaration. Focused
tests in the existing `test_evidence_authority.py` make the literal data
visible to existing closed test/import discovery. There is no new CTest,
test executable, workflow, command owner or generalized registration API.

Freeze every expected row and complete resulting fact independently from the
fixed policy/runtime above before any extractor or test edit. Counts and
candidate-derived fingerprints cannot substitute for that declaration.

## Canonical fact shape

| Field | Value |
| --- | --- |
| `key` | `tests/host/selected_ownership_native_source_bindings.py:native-source-admission:<scope>` |
| `family` | `selected-ownership-native-admission` |
| `owner_path` | The single declaration owner above |
| `kind` | `native-source-admission` |
| `scope` | Exact unique scope from the closed census below |
| `authority_class` | `TRANSPORT_IDENTITY` |
| `primary_unit` | `B02` |
| `consumer_units` | `B01` through `B12`, then `B14` |
| `payload_paths` | Sorted unique declaration owner, one subject and its actual assertion owners |
| `evidence_value` | Existing canonical `digest()` of `(reference, scope, subject, assertion_owners, comparison_contract)` |

Keep all consumers because this is shared memory evidence; B13 retains its
existing blocked admission. Ordinary-file checks apply to declaration,
subjects and assertion owners, including checked parent directories. Reject
absolute, parent-relative, noncanonical, missing, symbolic-link, hard-link or
ambiguous paths through the existing mechanism.

Shared C inspectors, operation-count policy, stack tooling, CMake and design
documents are not payloads merely because they help interpret an assertion.
The exact fixed reference is part of every value, not mutable `main` or the
current source text.

## Current function clauses: 251

The scope format is:

```text
MEM03/function/<physical-C-path>/<function-name>
```

The seventeen owners are the exact `.c` members of fixed #219's
`SOURCE_ROLES["production_paths"]`. The study enumerates each owner and its
counts. Their 244 original definitions come from #214. Fixed #219's
`SOURCE_PROJECTION["new_helpers"]` adds exactly seven helpers.

The sole assertion owner is:

```text
tests/host/selected_ownership_source.py
```

Each function contract has exactly `comparison`, `signature`, `guards` and
`content`. `comparison` is `function`. The other fields encode only existing
conditions in fixed `validate_retained_source()`:

- Original functions retain the original `compact(signature)` and exact
  preprocessing-owner tuple. Linkage is already part of that signature.
- The 216 nonmutable original functions use
  `("body-bytes-sha256", SHA256(original_body.encode("utf-8")))`.
  The existing comparison is raw body-string equality: do not replace it
  with compact, lexical, whitespace-insensitive or call-set equality.
- The 28 mutable original functions use `("calls", sorted_call_tuple)`,
  obtained from the fixed `_current_calls(name, original_body)` policy.
  Preserve the existing `_function_calls` semantics and its rejected forms.
- The seven added helpers use the exact compact signature, empty
  preprocessing-owner tuple and closed call set from fixed
  `_helper_contracts()`.

No new helper-body, linkage, signature exception or preprocessing policy is
invented. Expectations derive from fixed source and policy, never the current
production body.

Eight C owners are compiled in the native bundle; nine are not. The existing
author context validates all seventeen. Native observation, inverse and
legacy-admission paths call the same guard where applicable. These are
consumers of the same clause, not separately retireable copies.

## Definition sets and skeletons: 34

For each of those seventeen C owners add:

```text
MEM03/definitions/<physical-C-path>
MEM03/skeleton/<physical-C-path>
```

Both contracts have only `comparison` and `expected`.
The definition comparison is `definition-set`; its expected value is the
sorted tuple of original names plus the exact declared helpers.

The skeleton comparison is `compact-skeleton-sha256`. Its expected value is
the SHA256 of the fixed `_source_skeleton(original, original_records)`
UTF-8 result. That function removes each exact signature/body fragment once
and applies existing `compact()` to the remainder. Preserve its ambiguity
rejection and normalization; do not insert MEM01's separate lexical pass.

The assertion owner is the same source guard. Definition-set authority does
not authorize a different function body, signature, call set, include, type
or storage change.

## Header clauses: three

The exact subjects are:

```text
include/micros/frame_ownership.h
include/micros/frame_ownership_runtime.h
kernel/memory/frame_ownership_internal.h
```

Each scope is `MEM03/header/<physical-path>`. Its sole assertion owner is
the source guard, and its contract contains `comparison` and `expected`.
`comparison` is `compact-header-sha256`.

Construct the expected header from fixed #214 using the exact existing
`header_additions(path)` insertion before its single `\n#endif` boundary.
Hash the UTF-8 result of the existing `compact(expected_header)`.
Retain the original boundary and declaration rules. Do not infer a general
header-extension permission.

## Fixed native inputs: 196

The scope is `MEM03/native-input/<physical-path>`. Membership is fixed #214's
51 declared native C members plus its 156 headers selected under
`include/`, `arch/` and `kernel/` by the unchanged generator. Subtract the
eleven members also in fixed #219's production set: eight C files and the
three headers above. The remaining set is **43 C files and 153 headers**.
It is not a glob over the current worktree.

The contract has `comparison: file-bytes-sha256` and `expected` equal to the
original complete bytes' SHA256. The guard compares contents, not original
file mode; ordinary-file and captured mode/stability requirements remain
separate.

Assertion ownership follows the real paths:

| Input kind | Exact assertion-owner tuple |
| --- | --- |
| 43 C files | `tests/host/selected_ownership_source.py`, `tools/generate_selected_ownership_fixture.py` |
| 153 headers | `tools/generate_selected_ownership_fixture.py` |

The generator checks all 196 dependencies. Instrumented C observation and
inverse paths independently compare noncandidate C bytes through the source
guard. They do not consume those noncandidate headers: the fixed-source
reader's admitted path set excludes them.

The two consumers of a C input share one identity. A future correction must
adapt both consistently, not retire only the generator's check. For headers,
do not manufacture a source-observer comparison that does not exist.

## Conservation, closure and failure

Before implementation edits, retain all 4,290 complete current records,
eighteen fingerprints, five claims and seventy retired keys from #219.
Freeze the independent 484-record addition and its new-family fingerprint.
The complete current catalog then contains **4,774 facts in nineteen
families**, with no new retirement.

Earlier historical comparisons retain their original complete expected
values. Only explicit current-catalog totals/family membership advance.
Do not discard unknown records from a purported complete current comparison
or change any old family fingerprint.

Function, definition, skeleton and header facts have three payloads.
Fixed C inputs have four; fixed header inputs have three. The unchanged EV01
closure builder still adds the owner and every payload. Overlap with a real
subject, declaration or assertion owner must still fail `PROJECTION`.

Reject missing/extra/duplicate assignments or rows, executable declarations,
unknown scopes/owners/subjects, borrowed functions, incomplete comparison
contracts, changed references/values/normalization, malformed paths,
incomplete conservation and identities without a real existing assertion.
Failures are explicit; never fall back to an aggregate, inferred owner,
current filesystem discovery or candidate-derived expected value.

This prerequisite installs no selector and no transition journal. A later
correction must refine the exact contracts, name all affected keys, preserve
unrelated clauses and original values, independently bind real current
helpers/observations and complete actual Red/Green/recertification before
any authorized consumer stops enforcing a clause. Compiling old code in
place of changed current code is not an inverse.

## Implementation boundary and acceptance

After separate design merge, change only:

- the single new literal declaration;
- its closed owner/family materialization and independently frozen new
  fingerprint in `tests/host/evidence_authority_inventory.py`;
- focused regressions and explicit current totals in the existing
  `tests/host/test_evidence_authority.py`; and
- directly related documentation.

Leave source guards, native generators, C/header fixtures, primitive
declarations, the collector, shared inspectors, transition validation,
checked source-view policy, CMake, workflows and production files unchanged.

Start with genuine missing-identity Red for at least:

```text
MEM03/function/kernel/memory/user_address_space_validation.c/micros_user_address_space_validate
MEM03/definitions/kernel/memory/user_address_space_validation.c
MEM03/native-input/include/micros/user_address_space.h
MEM03/native-input/kernel/memory/user_address_space_internal.h
```

Before checking missing inventory keys, establish that the real unchanged
guards reject the corresponding in-memory retained-body, definition-set and
header mutations. No missing import, compiler failure or artificial scaffold
error can stand in for this coverage failure.

Green proves complete independent record equality, all predecessor
conservation, exact membership and consumers, raw-body versus compact
comparison fidelity, schema/path/value negatives and original positive/
negative admission. Exercise the unchanged transition validator with a
synthetic per-clause plan: independent shared inspectors stay disjoint, while
actual subject/declaration overlap fails. This is closure evidence, not an
actual runtime retirement.

Focused iteration uses:

```bash
"${MICROS_PYTHON}" -E -S -B -m unittest tests.host.test_evidence_authority tests.host.test_selected_ownership
```

Before independent implementation review run the complete fresh author plan:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier full --execute --producer
```

All fifty required commands, 43 native CTests, complete Python discovery,
forty QEMU workflows, four standalone images, 44 static configurations,
113 ELF outputs, ten observed scopes and explicit blocked B13 remain.
Preserve the fast tier and current three reusable profiles; do not waive
failures, increase budgets or retry a failed sequence into acceptance.

Stage the complete candidate and record exact `HEAD`, tree, commands and
results with clean tracked worktree and empty untracked task output.
Obtain independent review before the first permanent implementation commit.
Verify all five prior authorities without journals, exact reviewed/committed/
remote/preview/squash tree equality and completed-ref cleanup. Then resume
MEM04 design, not implementation. No runtime finding closes through EV01-C05.

## Implementation map

The separate implementation adds the one literal-only declaration and closed
materialization through the existing inventory owner. It changes no current
source guard, native generator, fixture, shared inspector, collector,
transition rule, checked-view policy, build recipe or production source.

Before any extractor or regression edit, all 4,290 complete predecessor
records, eighteen fingerprints, five claims and seventy retired keys were
retained independently. The 484 expected rows and complete facts were frozen
from the fixed policy/runtime. Their new-family fingerprint is:

```text
4678706adb8ef75162ed57b16821af9262b06ed642a721f482778dbba381d3ff
```

The declaration is read as two checked literal assignments, never executed.
Materialization binds exact current-clause subjects and actual assertion
owners, checks ordinary files and the closed sorted census, and rejects
schema, ownership, reference or value drift against that independent
fingerprint. The complete catalog is 4,774 facts in nineteen families.
Every old complete record and fingerprint remains identical.

Eight focused regressions live in the existing authority test owner.
The real Red first proved that unchanged retained-body, definition-set and
native-header guards reject their inputs, then failed on the four missing
inventory keys. Green also proves exact fixed-policy equality, raw-body
versus compact comparison fidelity, distinct C/header consumers, declaration
and path negatives, unchanged overlap rejection and all five prior claims
without reading author journals.

Focused validation passed 103 tests across the two documented modules.
Final producer `45ae81554fe14d73a1a2fcc431e9fe76` passed all fifty fresh
commands, including 43 CTests, 1,534 Python tests, forty QEMU workflows,
four standalone images and complete stack evidence. Its three same-run
receipts matched current exact profiles before commit.

An earlier complete run's native receipt was invalidated when fast execution
performed a generated-header follow-up and relinked two fixtures. No stale
receipt was used: an actual native no-work build preceded the renewed final
producer. Independent implementation review found no substantive issue
before the first permanent commit. Fresh committed and actual-main
three-case authority checks passed; exact publication and cleanup closed
EV01-C05 without a runtime retirement or hosted-CI success claim.

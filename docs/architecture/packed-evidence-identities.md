# Packed Evidence Identity Coverage

## Status and scope

This Accepted EV01-C01 contract repairs the specific inventory gap recorded
in [the source study](../research/packed-evidence-identity-gap.md).
It refines ADR-0082 through
[ADR-0091](../adr/0091-packed-evidence-identity-coverage.md).

The source is fixed at
`b99848f52d01100a6db2833f610cff7007e1cc6f`, after COPY01 #191.
The design merged separately as #192 and implementation #193 completes
this correction. PLAT01 subsequently identified the separate
[relocated payload prerequisite](relocated-evidence-payloads.md).
Neither evidence correction adds a runtime correction node or reassigns a
finding in the pinned 59-node correction DAG.

Design is documentation-only. No extractor, test, model, fixture, command
runner, dependency or executable prototype is authored before separate
design acceptance and merge.

## Goal and non-goals

Materialize the existing packed scheduler declarations as granular,
independently pinned transport facts, and fail closed when their coverage
or source shape is incomplete.

Do not change scheduler/runtime code, tests' expected source values,
the meaning of a semantic multiset, the line-review limit, existing phase
or ownership assertions, any EV01 state or retirement rule, validation
coverage, stack/image/time budgets or COPY01's completed retirement.
This is not a general Python interpreter, arbitrary embedded-hash scanner,
automatic rebaseline or new runtime-authoring driver.

## Closed declaration admission

The new family is `scheduler-packed-transport`. Its sole owner is
`tests/host/test_scheduler_source_layout.py`.
That module also remains in `kernel-structure`; the two extractors must
produce disjoint canonical keys.

Only these top-level declarations are admitted:

| Declaration | Key form | Value form | Fixed entries |
| --- | --- | --- | ---: |
| `FROZEN_FUNCTIONS` | Literal original source path | Literal multiline function records | 163 records in six source groups |
| `FROZEN_FACTS` | Literal two-string `(original source, function)` tuple | Literal whitespace-separated semantic tags | 14 function entries |

The function-record group sizes, in the existing source-family order, are:

| Original source | Records |
| --- | ---: |
| `kernel/scheduler_core.c` | 60 |
| `kernel/scheduler.c` | 34 |
| `kernel/scheduler_test.c` | 28 |
| `kernel/scheduler_invalid_test.c` | 11 |
| `tests/host/scheduler_test.c` | 13 |
| `tests/host/scheduler_model_test.c` | 17 |

Parse declarations as data from their AST. Do not import or execute the
scheduler test module to obtain them. Require one ordinary single-name
assignment per admitted declaration, a literal dictionary, literal keys
and literal string values. Reject duplicate dictionary keys before literal
decoding could collapse them. Calls, comprehensions, aliases, augmentation,
f-strings and computed replacement containers are not admitted.

Blank lines and separating whitespace carry no new semantic authority.
They may not hide malformed nonblank rows or extra tokens.

## Function records

Each nonblank line has exactly three fields:

```text
<C identifier> <positive decimal original-line-count> <64 lowercase hex digits>
```

Require an ASCII C identifier, a positive unsigned-32-bit line count and
the complete original SHA-256 spelling. A repeated function name within
one original source group is an error, not last-write-wins behavior.
Names in different original source groups remain distinct.

The outer source keys must equal the six `ORIGINAL_FUNCTIONS` source keys
and the matching `SOURCE_SETS` families. The record names in each group
must equal its complete original-function inventory, including retained
definitions. Every function has exactly one declared current logical owner.
Unknown, missing, repeated or multiply owned functions fail.

Each nonempty `SOURCE_SETS` tuple starts with its original source. Its
position binds the corresponding `PRIVATE_HEADERS` entry. The current-owner
keys in `ORIGINAL_FUNCTIONS` must equal that source family, including owners
whose original-function list is empty. Source/header order and ownership
remain fixed declaration data, not something inferred from a current body.

For each record emit one fact:

```text
key:
  <owner>:function-body:FROZEN_FUNCTIONS/<original-source>/<function>
family:
  scheduler-packed-transport
class:
  TRANSPORT_IDENTITY
evidence value:
  compact canonical JSON {"original_lines": <integer>, "sha256": <original hash>}
```

The line count is retained historical metadata, not a replacement for the
separate current line-limit and semantic-disposition checks. Classifying
this fact does not claim that every completed phase root still uses direct
whole-body equality. A later retirement cannot rewrite the original row,
silently disable the 120-line rule or remove retained phase obligations.

## Semantic minimum-multisets

Each value is a nonempty sequence of tokens. A token contains one exact
category letter followed by 24 lowercase hexadecimal digits:

| Tag | Existing meaning |
| --- | --- |
| `C` | Concrete call and arguments |
| `P` | Condition or loop/switch predicate |
| `W` | Concrete write |
| `E` | Return/goto/break/continue exit |

The entry keys must equal the existing twelve
`INITIAL_FUNCTION_MIGRATION` pairs plus the two `SHORT_PHASE_ROOTS`, with
no duplicates in either declaration or their union. Every pair must name
a declared original function.

Materialize exactly one fact per existing dictionary entry:

```text
key:
  <owner>:data-table:FROZEN_FACTS/<original-source>/<function>
family:
  scheduler-packed-transport
class:
  TRANSPORT_IDENTITY
evidence value:
  compact canonical JSON of lexically sorted [token, positive multiplicity] pairs
```

Preserve all 1,080 original token occurrences. Do not use a set, discard
duplicates, reorder production operations to learn expectations, or describe
the data as an ordered trace. The existing assertion requires at least the
frozen multiplicity, not equality with every new operation in a refactored
function.

The fact's granularity is that existing per-function dictionary entry.
Raw digest values are evidence payload, never identity keys.

## Current payload and audit-unit binding

A function record names its current source owner through
`ORIGINAL_FUNCTIONS`. Kernel logical paths resolve through the existing
fixed `KERNEL_FILE_FACTS` directory mapping. Native paths retain their
declared spellings. Unknown, ambiguous, missing or linked current owners
are errors; no scan of production bodies or path-prefix guess substitutes
for this binding.

Its payload set contains the scheduler evidence owner and that exact
current production/native source.

A semantic entry can depend on phases across its original source family.
Its conservative payload set contains the evidence owner, every current
source in the matching `SOURCE_SETS` family and its corresponding private
header. Resolve the kernel members through the same fixed map.
The set is finite and deduplicated; no open glob defines the closure.

Use the existing primary-unit and consumer-unit mapping on these resolved
paths. B03 and B14 remain consumers. Native/target evidence paths retain
their evidence-role primary classification; no policy treats a host test
as a new production owner.

## Discovery and completeness

The current W01/W44 discovery/import closure remains authoritative.
Add recognition of the two supported packed forms to identity-owner
discovery, including a module whose only identity data use those forms.
Recognition does not automatically admit an owner or declaration.

For the finite materialization boundary:

1. Both admitted owner/declaration pairs must be present in their supported
   top-level literal form.
2. Any occurrence of the reserved declaration names outside that admission,
   or a nonliteral replacement of an admitted declaration, fails.
3. Another top-level assignment containing either recognized packed form
   is an undeclared packed authority, not a default-family member.
4. Ordinary source strings used inside negative test fixtures are not
   themselves top-level authority declarations.
5. Unknown formats require a separately reviewed admission; the correction
   does not execute Python or infer an encoding from a candidate result.

Missing/extra family, table, function, phase-root or source membership must
fail rather than return a shorter apparently valid inventory. Materialize
the family atomically after its complete schema and binding checks.

## Independent declaration and conservation

The exact new expectation is derived from the fixed committed scheduler
declarations, their original-owner maps and the existing fixed kernel
directory mapping. Before editing the extractor, implementation separately
records the resulting key/value/payload set and its family fingerprint.
The candidate extractor is not used to compute that expectation.

The positive inventory has:

```text
previous individual facts:          3735, unchanged
new function-record facts:           163
new semantic-multiset facts:          14
current individual facts:           3912
previous closed families:             10, unchanged
current closed families:              11
new runtime retirements:               0
```

Every previous fact retains its key, class, owner, evidence value, payload
and consumer binding. Every previous family fingerprint stays unchanged.
Do not broaden an old fallback payload set merely because the new extractor
needs another helper or a new family registration.

COPY01's exact prior committed claim continues to retire only the original
memory-file assertion in ordinary no-plan validation. Neither the old
author journal nor a fabricated new event is required. The new packed
facts remain active; this correction retires none of them.

## Implementation and evidence boundary

The prospective implementation owns:

- the one bounded private helper
  `tests/host/evidence_authority_packed.py`, for literal packed-declaration
  schemas and closed source binding;
- the existing inventory's exact family/materialization/discovery wiring;
- focused cases in the existing authority tests and directly dependent
  current-count assertions; and
- directly related documentation.

It does not edit the scheduler source gate's frozen declarations or
assertion algorithms, kernel/service/runtime code, the EV01 transition
state machine, CMake targets/presets or the validation planner.

The first native Red demonstrates that the two real declarations have no
materialized rows despite current inventory acceptance. Green requires all
177 independently expected facts and exact prior-fact conservation.

Required focused negatives cover malformed/nonliteral tables, duplicate
keys/functions, missing/extra records, wrong names/owners, changed full
digests or line counts, changed tagged-token multiplicity, unknown tags,
unknown packed owners/declarations and unsafe current payload bindings.
Add only representative cases that distinguish the introduced behavior.

Preserve original scheduler source/phase tests and prove that COPY01 prior
retirement still works with the enlarged inventory. Then run the unchanged
complete fresh author gate on the exact staged tree. There is no new runtime
transition, target workflow, deadline exception or partial-report substitute.

Independent implementation review remains mandatory before merge. PLAT01's
documentation work may resume only after this corrective implementation is
separately accepted and merged.

## Implementation map

The private `tests/host/evidence_authority_packed.py` helper now owns the
literal schemas, duplicate rejection, source-family and phase-root equality,
and unlinked current-file binding. It reads declaration ASTs without
executing scheduler code. The existing inventory owns family registration,
discovery and the unchanged audit-unit mapping; the transition validator
and scheduler source assertions are unchanged.

Before the extractor edit, a separate fixed-Git derivation recorded every
new key, value and payload together with every prior fact. The new family
fingerprint is
`6949be2673b5e0d6cf694ad2cfa7f7c810c468209f0c6d3c1d4b6de11c0de309`.
The implementation materializes all 177 new facts and conserves all 3,735
previous records and ten fingerprints, for 3,912 facts in eleven families.

`PackedInventoryTests` in the existing authority test module compares
function records and multiset entries with the fixed Git declarations,
rejects malformed or undeclared inputs and unsafe bindings, and checks prior
fingerprints and COPY01's journal-free committed retirement. The original
Red found zero of the required 163 function records and zero of the required
fourteen multiset entries. All new facts remain active; no new runtime
retirement or target execution is introduced.

The existing scheduler/directory source gates and complete fresh author
plan remain required before independent implementation review and merge.

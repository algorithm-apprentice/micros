# Relocated Evidence Payload Bindings

## Status and purpose

This Accepted EV01-C02 contract repairs the fixed-source defect described
in [the gap study](../research/relocated-evidence-payload-gap.md), under
[ADR-0092](../adr/0092-relocated-evidence-payload-bindings.md).
The reference commit is
`de31bce3de93f58c94e053ea55e41040aba79987`.

EV01-C01 #193 and this correction's design #194 and independently reviewed
implementation #195 are merged. PLAT01 resumes from that completed
prerequisite. This adds no runtime-DAG node, duplicate finding owner or
runtime behavior.

## Exact admission

Admit only the seventeen owner/declaration pairs and their 104 direct
logical-file entries enumerated in the fixed-source study. Each has:

- an existing canonical `data-entry` key;
- one direct literal dictionary key naming a fixed logical kernel file;
- an existing full historical hash/property value; and
- one exact entry in the existing `KERNEL_FILE_FACTS` directory map.

The owner's other dictionary entries retain their existing handling.
Current physical paths, host paths and unrelated declarations do not
acquire another mapping rule. This is not arbitrary substring recognition,
a filesystem search, a path-prefix guess or execution of the source gate.

The table need not hash a complete file: the existing file-scope storage,
static-local storage and inline-header entries are source properties owned
by their explicitly named file. Two further qualified keys name a
reconstructed source family rather than the relocated facade alone. Their
complete closure is specified below. Authority classes and values remain
unchanged.

Closed family fingerprint validation still rejects extra, missing,
duplicate, moved or unclassified identities. Recognition of a possible
path does not approve a new authority owner or table.

## Binding algorithm and ownership

For each of the 102 ordinary rows, use the literal logical key to find its
sole current physical path in the fixed directory map. For the two exact
reconstruction keys below, resolve every declared source-family member
and its private header through that same map. Require ordinary unlinked
current files and ordinary unlinked parent directories. Reject a missing
entry or family member, missing source/header, malformed binding, ambiguous
physical owner, hard link, symlink or duplicate old flat owner.

The completed kernel-directory migration remains binding. Do not select
an old flat file, fall back to another directory or invent a new migration
state if the required physical owner is absent.

The ordinary corrected payload is exactly:

```text
{ evidence-owner module, exact current physical source }
```

### Two complete object-reconstruction bindings

These are the only exceptions to the ordinary singleton-source rule:

```text
tests/host/test_scheduler_source_layout.py:data-entry:STABLE_ARTIFACT_HASHES/kernel/kernel_objects.c
tests/host/test_endpoint_source_layout.py:data-entry:STABLE_HASHES/kernel/kernel_objects.c
```

Both actual assertions call `restored_kernel_objects_source()`.
[ADR-0068](../adr/0068-kernel-object-source-decomposition.md) requires that
helper to reconstruct the complete original bytes from the current nine
portable C owners and their private header. The logical label is not a
claim that the facade alone supplies those bytes.

Read the literal `CORE_SOURCES` and first `PRIVATE_HEADERS` member from the
fixed object source gate without importing or executing it. Their admitted
logical members are exactly:

```text
kernel/kernel_objects.c
kernel/kernel_object_state.c
kernel/kernel_object_identity.c
kernel/kernel_object_stack.c
kernel/kernel_object_process.c
kernel/kernel_object_thread.c
kernel/kernel_object_context.c
kernel/kernel_object_hart.c
kernel/kernel_object_validation.c
kernel/kernel_object_private.h
```

Resolve each through `KERNEL_FILE_FACTS`. Each exception's payload is its
own evidence-owner module plus all ten resolved paths, for eleven paths
total. Do not substitute the object gate as the fact owner, omit a helper
source/header, or generalize this exception to another row sharing a
similar label. Missing, extra or duplicate family members fail closed.

### Audit-unit binding

Canonicalize each payload through the existing sorted tuple representation. Use the
unchanged `_fact_primary_unit` and `_consumer_units` rules on that payload.
B14 remains a consumer, along with the source gate's existing subsystem
consumer and any units required by the actual physical path.

Do not erase a unit arbitrarily to avoid a transition conflict. Conversely,
do not preserve a spurious unit merely because it was inferred from an
unrelated fallback file. The independently declared corrected record
includes the complete expected primary and consumer sets.

Read and validate the directory-map data once per inventory operation,
rather than discovering a path by scanning production bodies. The same
bounded literal-source/path helper may serve the already implemented
packed scheduler binding. That refactoring must preserve every packed
record and all of its current rejection behavior.

For source size `s`, map size `m` and selected rows `r`, literal decoding
uses `O(s)` work and host storage. A dictionary lookup is expected
amortized `O(1)`, with `O(m)` worst-case collision work; no claim treats
the fixed map capacity as asymptotic constant time. The materialized
bindings use `O(m + r)` auxiliary storage, plus the parsed source. This
is host-only evidence work and adds nothing to a kernel hot path.

## Preserved and changed fields

For all 3,912 existing facts, retain:

```text
key
family
owner_path
kind
scope
authority_class
evidence_value
```

Only the admitted 104 facts may change:

```text
payload_paths
primary_unit
consumer_units
```

All 3,808 other complete fact records remain identical. Family and owner
membership remain unchanged: 3,912 facts, eleven families and zero new
retirements.

Only the `kernel-structure` and `service-structure` family fingerprints
change. Preserve the other nine exactly, including
`scheduler-packed-transport` and `runtime-memory-baseline`.
The 163 packed function records, fourteen packed minimum-multisets and
COPY01's original memory-file identity do not receive another correction.

This narrowly refines ADR-0091's prior-record conservation requirement for
the independently specified source-binding defect. It does not change
that completed outcome's identities, historical hashes or interpretation
of packed data.

## Independent declaration

Before any extractor edit, derive and retain the complete before/after
records from the fixed Git source, its literal tables, fixed directory map
and unchanged unit-mapping rules. The candidate extractor is not used to
declare the new expectations.

The declaration must demonstrate:

- exactly 104 changed records, with the qualified table counts in the
  source study;
- 102 ordinary source bindings and the two complete nine-source/header
  reconstruction bindings, derived from their actual consumers;
- exactly 55 changes in `kernel-structure` and 49 in `service-structure`;
- no change to identity, class, owner or expected evidence;
- the exact two new family fingerprints;
- identical remaining records and nine remaining fingerprints; and
- exact physical-source inclusion without collateral fallback paths.

The implementation does not add a generated mutable inventory or copy
new hashes from current production code. A logical filename alone is not
sufficient authority to discard an existing reconstruction dependency.

## Historical conservation

Existing earlier-family conservation tests retain their original expected
digests. A bounded test-side projection restores only the three corrected
metadata fields for the exact 104 keys.

Its source authority is the fixed pre-correction Git tree, not the current
worktree's path membership. It derives old declared paths from that tree's
actual file inventory and the original source declarations.

Before applying the inverse, require the complete corrected metadata to
equal its independently specified value. Unknown keys, wrong physical
paths, missing/extra consumers or another primary owner are errors rather
than values to overwrite. All other fields pass through unchanged so an
old identity/value mutation still fails its original fingerprint.

This projection is historical comparison only. Current inventory and
transition validation always use the precise corrected binding; they do
not switch back to conservative old payloads during retirement.

## Current transition behavior

Keep the EV01 types, state machine, event ordering, registry rules and
whole-file immutable/candidate disjointness unchanged.

The repaired bindings remove the demonstrated unrelated-path collision
for PLAT01's source study. A later real consumer must still close its
complete oracle, command, tool, source and per-unit inputs. This design
does not authorize per-transition payload overrides, renamed aliases for
overlapping bytes or mutation of an oracle after Red.

COPY01's exact committed prior claim remains valid without an author
journal. Every other identity remains active; neither this correction nor
its historical projection retires a source assertion.

## Implementation boundary

The separate implementation owns:

- one bounded private literal-source/current-path helper,
  `tests/host/evidence_authority_source.py`;
- precise wiring in the existing inventory;
- reuse of that helper by the packed-data extractor where necessary to
  avoid duplicate directory/path validation;
- focused cases and bounded historical-comparison support in the existing
  `test_evidence_authority.py`;
- directly dependent prior-family comparisons, including the COPY01
  authority test; and
- directly related documentation.

The new helper contains no additional authority pin or independent family.
Existing literal directory data and explicit inventory declarations remain
the authority. Test-only historical comparison can remain in the existing
registered authority-test owner; do not introduce an unregistered
projection owner.

Do not edit production C/assembly, frozen source-gate data/assertions,
transition validation/types, CMake targets/presets or the validation
planner. Unrelated or genuinely unresolved schemas retain their existing
conservative behavior. This is not a general Python interpreter or a broad
re-audit.

## Acceptance

Native Red must show a real current logical-file identity missing its
physical owner and carrying the unrelated fallback. Green proves all 104
independently specified corrected records, not a representative count
alone. It also proves both reconstruction keys retain every non-facade
source and their private header.

Focused negatives cover missing/malformed map entries, unknown or ambiguous
owners, linked source/ancestor paths, a duplicate old flat owner and
tampered corrected metadata that the historical inverse must reject.
Omitting or replacing a reconstructed source/header must fail even when
the relocated facade remains present.
Preserve unrelated fallback behavior and all packed-data schema/path
negatives.

Prove current family closure, exact unchanged identity/value/class fields,
the 3,808 unchanged records, nine unchanged fingerprints, bounded inverse
recovery of every old family fingerprint and no-plan COPY01 retirement.
Keep the original scheduler/directory source gates and whole-module AST
checks active.

Then stage the complete candidate and run the existing complete fresh
native/QEMU/image/stack author plan, documentation and Git checks under
unchanged budgets. Independent implementation review and an identical-tree
squash merge precede resuming PLAT01.

## Phase barrier

This design contains documentation only. No helper, test, model, fixture,
runner, dependency, build scaffold or reusable executable prototype is
authored before separate design acceptance and merge.

## Implementation map

After design #194, `tests/host/evidence_authority_source.py` supplies the
shared literal declaration, ordinary-file and current directory-binding
checks. The packed extractor reuses that helper without changing its
177 facts or schemas. The existing inventory owns the seventeen qualified
table admissions and two exact object-reconstruction exceptions.

The independently recorded pre-extractor declaration from the fixed Git
source supplies these two current family fingerprints:

| Family | Corrected fingerprint |
| --- | --- |
| `kernel-structure` | `667e133360ea23123cabcda1f600e18b7b0ab1419d415f6b25bf885a0d69523d` |
| `service-structure` | `97730d70d876f7504caa34c9ca64e7884b2cc5f4d591790f2480371602e3d99e` |

All 104 corrected complete records match that independent declaration.
The 3,808 other complete records and nine other fingerprints are unchanged.
There are still 3,912 facts and eleven families, with no identity/value/
class change or new retirement.

`RelocatedBindingTests` exercises actual singleton and reconstruction
bindings, complete historical recovery, unsafe paths and rejected inverse
tampering. The test-side `prior_binding_facts` helper reads only fixed-Git
membership and declarations for its historical data; it checks the exact
corrected metadata before restoring those three fields. Earlier EV02,
EV03, packed-family and COPY01 comparisons retain their original expected
digests through this bounded inverse. Current authorization never uses it.

The existing scheduler, endpoint, object and directory source gates remain
unchanged. Complete current fresh author acceptance and independent review
remain mandatory before implementation merge and PLAT01 resumption.

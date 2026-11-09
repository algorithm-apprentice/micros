# Selected Release/Handoff Native Source Authority Gap

## Status and fixed references

This is read-only EV01-C08 research for
[Accepted ADR-0117](../adr/0117-granular-selected-release-handoff-native-source-authority.md).
It authors no executable declaration, extractor, test, model or runtime
correction.

```text
current policy: aba02c1f8a583866a85c3012d20c6007b9445a22
policy tree:    7609a9578e8010c735310923a90953833c93e3d5
MEM05 runtime:  abe05cdd7bced5e77e86c1ec12754d017f920cab
runtime tree:   2c34dac3c259a4975d1c213e3e14c18d8a075658
MINIX:          4db99f4012570a577414fe2a43697b2f239b699e
MINIX tree:     80db89de58d5ecaf31efe48b4d1d98c7e285e81f
```

MEM05 #236 is independently reviewed and merged. Its current attempt passed
real native/target Red and Green, all thirteen affected units and all fifty
fresh author commands. Final coverage was 45 CTests, all 1,635 Python cases,
forty QEMU workflows, four standalone images and complete 44/113/ten-scope
stack evidence. Same-run receipts, exact publication trees and fresh
journal-free committed/main authority passed. Completed refs were removed
without changing the purposeful archives.

OBJ01 is the next preferred runtime correction. It owns B03-F001/F003/F004
and depends on merged MEM04. VM02 is later and remains behind its own
prerequisites. No OBJ01 implementation has begun.

## Concrete required source boundary

`kernel/objects/kernel_object_hart.c:95-125` still resolves a routed hart
context by scanning `MICROS_HART_CAPACITY`. B03-F004 assigns its direct
range/alignment/slot replacement to OBJ01.

The fixed MINIX `cpulocals.h:7-31` accesses CPU-local storage directly.
`proc.c:299-349` reads and updates CPU-local current execution around
selection. Those fixed files were checked against the pinned local
reference. This establishes the later runtime obligation; it does not
authorize copying MINIX or changing the object contract in this task.

That `micros` hart owner is one of MEM05's native inputs, but not one of its
seventeen production subjects.
`tests/host/selected_release_handoff_source.py:1737-1750`,
`validate_retained_source()`, requires its complete current bytes to equal
fixed #232. A read-only in-memory probe accepted the original and rejected
an appended newline with:

```text
MEM05 fixed or noncandidate native source changed: kernel/objects/kernel_object_hart.c
```

The generator independently rejects the same changed bytes first.
`tools/generate_selected_release_handoff_fixture.py:186-205`,
`source_bundle()`, checks the current nonproduction `raw` against fixed
`original`, then calls the source guard. The ordinary five-product checker
uses the same bundle. This is not the different MEM04 stable-read check
that compared two current reads.

## Missing granular identity

The complete current inventory has 5,387 facts in twenty-three families,
seven claims and 185 retired identities. Its complete digest is:

```text
233a0af8ff96425d730b78c7ea18129a202560cec0f86c9e0bc4e4ae8bdb870d
```

MEM05 has eight aggregate records and no individual
`native-source-admission:MEM05/...` records.
For the concrete hart owner, only its recipe `ARTIFACT_CONTRACT` has that
source in its payload. The `source-projection:MEM05` transport does not.
Artifact identity is not source-algorithm replacement authority; the coarse
projection cannot name this missing comparison either.

Existing earlier source families remain real obligations, but their names
cannot silently waive these later MEM05 assertions. OBJ01 must not retire
an entire family, rebaseline an old fingerprint or edit the guard without
an individual independently specified replacement scope.

## Closed current-clause census

The thirteen regular C owners contain 212 original functions. Nine are
removed and 23 helpers are added. Current membership is exactly 226.

| Physical owner | Original | Removed | Raw originals | Mutable originals | New helpers | Current |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `kernel/memory/bootstrap_memory.c` | 36 | 0 | 34 | 2 | 0 | 36 |
| `kernel/memory/frame_ownership.c` | 22 | 0 | 22 | 0 | 11 | 33 |
| `kernel/memory/frame_ownership_bootstrap.c` | 7 | 0 | 1 | 6 | 0 | 7 |
| `kernel/memory/frame_ownership_handoff.c` | 4 | 0 | 2 | 2 | 0 | 4 |
| `kernel/memory/frame_ownership_validation.c` | 16 | 0 | 12 | 4 | 3 | 19 |
| `kernel/memory/frame_ownership_vm_claim.c` | 2 | 0 | 1 | 1 | 0 | 2 |
| `kernel/memory/frame_ownership_vm_freeze.c` | 2 | 0 | 1 | 1 | 0 | 2 |
| `kernel/memory/frame_ownership_vm_release.c` | 4 | 0 | 2 | 2 | 0 | 4 |
| `kernel/memory/user_address_space.c` | 21 | 3 | 16 | 2 | 0 | 18 |
| `kernel/memory/user_address_space_handoff.c` | 13 | 6 | 3 | 4 | 1 | 8 |
| `kernel/memory/user_address_space_mutation.c` | 30 | 0 | 25 | 5 | 0 | 30 |
| `kernel/memory/user_address_space_runtime.c` | 18 | 0 | 15 | 3 | 2 | 20 |
| `kernel/memory/user_address_space_validation.c` | 37 | 0 | 37 | 0 | 6 | 43 |
| Total | 212 | 9 | 171 | 32 | 23 | 226 |

The fixed generator has 58 C inputs and 156 headers. Seventeen are current
production subjects; the other 197 are 45 C files and 152 headers.
Each fixed-input record needs both source and generator assertion owners.

The complete addition is:

```text
226 current functions
 13 definition sets
 13 skeletons
  4 complete current headers
197 fixed native inputs
---
453 existing clauses
```

These counts come from fixed policy/reference membership and were checked
against the actual unchanged #236 definitions. They do not derive from a
future OBJ01 implementation.

## Comparison fidelity

The fixed source guard at lines 1754-1785 preserves distinctions that a
generic function hash would hide:

- all 171 unmodified signatures and bodies compare raw strings, plus exact
  preprocessing ownership;
- the 55 mutable/helper contracts use compact signatures, guards, sorted
  callee sets and ordered primitive shapes;
- all 23 new helpers separately reject body directives; and
- `micros_user_address_space_require_idle` additionally compares its exact
  raw body after one fixed scalar-mode substitution.

A read-only probe reversed only the idle comparison while preserving its
primitive shape. The unchanged source guard rejected it with:

```text
MEM05 idle check changed beyond its retained scalar mode adaptation
```

The new function metadata must therefore retain that additional body clause,
not just the generic signature/callee/primitive tuple. Conversely, adding a
whole-body expectation to the other 54 current contracts would strengthen
the existing policy without authority.

`primitive_shape()` at lines 1654-1679 preserves mode, channel, owner, kind,
normalized primitive, enclosing regions and units. Its 55 current contracts
include forty nonempty and fifteen empty sets.
`_current_observations()` at lines 1788-1811 first calls source admission
and repeats the same current-function primitive equality. Unlike the
earlier MEM03/MEM04 gaps, this is not a distinct missing expectation.
Its function identity can name both uses without duplicate records.

Definitions and compact skeletons remain separate. Four headers compare
complete bytes against independently declared interfaces. Fixed-input
mode and generated-byte checks remain distinct from the fixed-source
comparison. No normalization or consumer may be omitted merely because
another guard often rejects a mutation first.

## Existing historical compatibility

MEM05's `historical_predecessor_records()` already validates the complete
current inventory before positively selecting the fixed #232
twenty-two-family predecessor register and original 5,379-record digest.
No authority-owner change or new projection is needed.

The historical scaffold regression at
`tests/host/test_selected_release_handoff.py:250-270` does read live input
files. `validate_scaffold()` at source-owner lines 2732-2774 expects the
exact old inventory extension and frozen adapter bytes. Adding inventory
registration and authority regressions would make it fail before its
intended negative cases.

The [bounded contract](../architecture/selected-release-handoff-native-source-authority.md#catalog-and-historical-scaffold-compatibility)
provides the complete forty-four-path fixed #236 scaffold-read domain to
that historical test, with an original positive control and all eight
existing mutations. The actual scaffold and frozen declarations stay
unchanged; complete-current catalog tests separately verify the new
registration and reject invalid additions.

## Required adaptation and task boundary

The Accepted addition names existing host source comparisons through one
literal declaration and closed materialization. It preserves all old
records, fingerprints, guards, seven claims and 185 retirements, yielding
5,840 facts in twenty-four families.

MINIX has no runtime analogue for these host evidence identities.
This is a compatible development-evidence extension with no kernel
algorithm, data structure, authority, complexity, storage or stack change.
OBJ01 still requires its own fixed-MINIX trace and separately reviewed
runtime contract after this prerequisite's implementation merges.

No OBJ01 runtime finding, hosted-CI result or later POSIX outcome closes
through this metadata-only design.

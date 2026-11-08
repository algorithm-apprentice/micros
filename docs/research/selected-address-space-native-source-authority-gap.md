# Selected Address-Space Native Source Authority Gap

## Status and fixed references

This is read-only EV01-C06 research for
[Accepted ADR-0112](../adr/0112-granular-selected-address-space-native-source-authority.md).
No executable declaration, test, extractor or runtime correction is authored
in the design phase.

```text
current policy: 8bd33e58a8e661cff78a8e4aad9d6cbc6a406120
policy tree:    fb74bc2458a4a3bf4fce45b4e396fa7fb0cde465
MEM04 runtime:  f28377decc0d20533e05330f2f2211db3f230910
runtime tree:   4b56076479ac53092c4c83d9c9b52829b66139ba
MINIX:          4db99f4012570a577414fe2a43697b2f239b699e
```

MEM04 #228 is independently reviewed and merged. It passed actual
native/target Red and Green, thirteen-unit recertification, all fifty fresh
author commands, complete 44-configuration/113-ELF/ten-scope stack coverage
and current same-run receipts. Its reviewed and published trees are equal.
Fresh committed/squash checkouts and actual main retain journal-free
authority; completed task refs are removed.

The next runtime owner, MEM05, covers B02-F006/B02-F007: selected release/
destruction plans and wired handoff sets rather than full-capacity and
per-process repeated owner scans. Its design is blocked by the source
authority gap below, not by a failed runtime implementation.

## Observed boundary

`tools/generate_selected_address_space_fixture.py:31-69`, `fixed_inputs()`,
selects exactly 56 C inputs and 156 headers from the fixed #221 tree.
`source_bundle()` at lines 72-95 reads each current input and delegates
admission to `source_bytes()`. The latter calls
`validate_retained_source()` at
`tests/host/selected_address_space_source.py:1004-1050`.

The ordinary four-product checker at
`tests/host/selected_address_space_evidence.py:1050-1074` uses that same
bundle and separately validates the uncompiled handoff dependency.
Its remaining generated-byte, mode, symbol, output and closing identity
checks do not become transport identities for the source comparison.

Representative prospective MEM05 subjects already encounter real guards:

| Subject | Current MEM04 comparison |
| --- | --- |
| `kernel/memory/user_address_space.c` | Compiled fixed noncandidate input; complete bytes must equal #221 |
| `kernel/memory/user_address_space_mutation.c` | Current definitions, signatures/guards, calls, exact dependent release bodies, skeleton and live observation shapes |
| `kernel/memory/user_address_space_handoff.c` | Uncompiled explicit dependency; complete bytes must equal the fixed two-assignment extension |

Read-only in-memory probes confirmed that the unchanged guard accepts the
current destruction/handoff sources and rejects an appended newline at the
corresponding complete-byte boundaries. No repository file was edited.

The current inventory has 4,782 facts in twenty families, six claims and
103 retired identities. Its complete fact-set digest is:

```text
d34b82a55bad426aa9e692163d2a577b4fdbcf7e3fd5e70d1c28422b8a361fc6
```

MEM04 has eight aggregate records and **zero**
`native-source-admission:MEM04/...` records. The
`source-projection:MEM04` transport has 45 payload paths, including shared
immutable policy, and does not include the noncandidate destruction source.
Its artifact recipe record is not a replacement source-algorithm oracle.
Neither can be retired to bypass an individual current-source check.

## Closed clause census

The fixed policy's regular production C set excludes the special whole-file
handoff dependency. Current function membership is original minus the three
removed functions plus the 22 explicit additions.

| Physical owner | Native C | Original | Removed | Raw body | Calls only | Dependent body plus calls | Added helpers | Current | Current observation |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `kernel/memory/user_address_space_activation.c` | yes | 7 | 2 | 4 | 1 | 0 | 1 | 6 | 2 |
| `kernel/memory/user_address_space_mapping.c` | yes | 16 | 0 | 12 | 4 | 0 | 5 | 21 | 9 |
| `kernel/memory/user_address_space_mutation.c` | yes | 30 | 1 | 24 | 3 | 2 | 1 | 30 | 6 |
| `kernel/memory/user_address_space_runtime.c` | yes | 17 | 0 | 17 | 0 | 0 | 1 | 18 | 1 |
| `kernel/memory/user_address_space_tty.c` | no | 13 | 0 | 10 | 0 | 3 | 0 | 13 | 0 |
| `kernel/memory/user_address_space_validation.c` | yes | 29 | 0 | 29 | 0 | 0 | 8 | 37 | 8 |
| `kernel/memory/user_address_space_vm_scratch.c` | no | 19 | 0 | 15 | 0 | 4 | 0 | 19 | 0 |
| `kernel/scheduler/scheduler_context.c` | yes | 11 | 0 | 10 | 1 | 0 | 0 | 11 | 1 |
| `kernel/tty/tty_handoff_runtime_validate.c` | yes | 11 | 0 | 11 | 0 | 0 | 2 | 13 | 2 |
| `kernel/vm/vm_handoff_core.c` | yes | 36 | 0 | 36 | 0 | 0 | 3 | 39 | 3 |
| `kernel/vm/vm_handoff_runtime.c` | yes | 10 | 0 | 10 | 0 | 0 | 1 | 11 | 1 |
| Total | 9 compiled | 199 | 3 | 178 | 9 | 9 | 22 | 218 | 33 |

Add eleven definition sets, eleven compact extended skeletons, five
byte-exact headers and the one exact dependent handoff file.
Of 212 native inputs, fourteen are already production subjects: nine C
files and five headers. The remaining 198 are 47 C files and 151 headers.

The complete proposed addition is:

```text
218 functions
 11 definition sets
 11 skeletons
  5 exact headers
  1 exact dependent file
198 fixed native inputs
 33 current observation shapes
---
477 existing clauses
```

These counts were derived from the fixed policy/reference and checked
against its actual current definition membership, not guessed from a
future MEM05 implementation.

## Comparison fidelity and consumers

MEM04 differs from the earlier MEM03 metadata precedent in four important
ways:

- its current headers compare complete bytes, not compact header tokens;
- nine mutable originals constrain both calls and a compact dependent body;
- handoff is one whole-file branch, not a collection of function clauses; and
- all fixed native C/header inputs pass through the same source guard.

The generator's `raw != source_bytes(...)` check proves current-read
stability after source admission. It is not a second independent comparison
against fixed bytes. The generator and ordinary product gate are consumers
of the same source-owner clause. No duplicate generator assertion owner is
needed. Mode, ordinary-file and captured identity checks remain separate.

The 178 retained bodies preserve raw byte-string equality. Fixed
`_dependent_body()` computes the nine compact expected bodies from #221
using only the existing selected-handle argument replacements; their
simultaneous call-set checks must not be discarded. Fixed
`_extended_skeleton()` retains the admitted cursor fields/includes and the
existing compact normalization.

## Distinct current observation guards

`observation_sites()` at
`tests/host/selected_address_space_source.py:1211-1244` performs a further
comparison for selected current added/mutable functions. Its observed
automatic tuples must equal the frozen primitive/compound inventory before
explicit source sites are resolved.

A read-only probe changed only
`decode_pte(table->entries[index], &entry)` to use `(index)`.
The existing function guard accepted the same signature, guards and callees.
The unchanged observation guard rejected it with:

```text
MEM04 current source changed its frozen primitive/compound inventory: micros_user_address_space_select_path
```

This proves that function metadata alone would omit a live source
constraint. The fixed selection yields 33 current observation comparisons:
five nonempty automatic inventories, 28 required empty inventories and
four functions with five explicit sites. All require their own names.
Generic primitive validity, counter ABI, actual capture, passive inversion
and independent work bounds remain unchanged.

## Required adaptation

The [Accepted contract](../architecture/selected-address-space-native-source-authority.md)
adds one literal-only owner and one separate family, preserving every
existing record, comparison, fingerprint and retirement. Each new fact has
only the declaration, exact subject and actual source assertion owner as
payload. The new catalog would contain 5,259 facts in twenty-one families.

There is no MINIX runtime analogue for these host evidence identities.
This is a compatible development-evidence extension with no change to the
fixed MINIX mechanism, runtime data structure, algorithm, work or storage.
MEM05 still requires its own complete MINIX release/handoff study and
separately reviewed runtime design after this prerequisite merges.

## Historical catalog and scaffold compatibility

Independent design review identified a coupled boundary that the initial
three-owner implementation footprint omitted.

`tests/host/selected_address_space_authority.py:204-206` hashes every family
except `selected-address-space-baseline` against the fixed 4,774-record
predecessor value. With the new family that unprojected set would contain
5,251 records and reject committed authority even though no predecessor
changed. `task_definition()` uses that check on ordinary committed paths.
The historical test at
`tests/host/test_selected_address_space.py:336-352` has the same original
record/fingerprint assumption.

The stack-input regression at lines 321-334 also invokes the original
`validate_scaffold()` on live registration files. The source guard at
`tests/host/selected_address_space_source.py:2100-2103` requires those files
to equal the exact MEM04 extension of #227. The new inventory registration
would fail there before the intended stack-input negative.

The [corrected bounded contract](../architecture/selected-address-space-native-source-authority.md#historical-catalog-and-scaffold-boundaries)
first validates the complete current catalog, then positively selects the
nineteen families named by the fixed #227 predecessor register for the
unchanged historical comparison. It does not silently discard unknown
current records or return a historical projection as a current inventory.
Complete current equality with all 4,782 old plus 477 new records remains
an independent acceptance obligation.

The historical scaffold regression receives exact #228 registration-file
bytes, proves the original positive fixture, then reaches each actual
stack-input negative. The production scaffold predicate, runtime source
guards and stack rules remain unchanged. Only the two existing authority/
test owners join the implementation footprint; no new family, fact or
retirement is added by this compatibility correction.

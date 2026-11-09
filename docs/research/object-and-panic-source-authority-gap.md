# Object and Panic Source-Authority Gap

## Fixed scope

This is read-only prerequisite research for OBJ01, not a runtime design or
an implementation certificate. The fixed source is
`89d3b85314754ae225f01fd16f257745bf19ce71`, tree
`ce73f3875dd354ef6c8452808d53ab2b9e14d522`: independently reviewed
EV01-C08 implementation #238.

The current inventory contains 5,840 facts in twenty-four families, with
seven committed claims and 185 retired identities. Its complete digest is:

```text
283ab289da64823a75f6b16a726cb1410870d4a3c8134b4321ed44088cf950c8
```

OBJ01 owns only B03-F001/F003/F004. Its MEM04 runtime prerequisite and
EV01-C08 naming prerequisite are merged. Direct source research has not
authored an OBJ01 runtime, test, model, fixture or executable prototype.
The next prerequisite is the bounded
[EV01-C09 contract](../architecture/object-and-panic-source-authority.md).

## Object identities exist, but bind the wrong subjects

The existing assertion owner is
[`test_kernel_object_source_layout.py`](../../tests/host/test_kernel_object_source_layout.py).
Its portable family has 48 original functions:

| Fixed logical owner | Current physical owner | Original functions |
| --- | --- | --- |
| `kernel/kernel_objects.c` | `kernel/objects/kernel_objects.c` | 2 |
| `kernel/kernel_object_state.c` | `kernel/objects/kernel_object_state.c` | 12 |
| `kernel/kernel_object_identity.c` | `kernel/objects/kernel_object_identity.c` | 12 |
| `kernel/kernel_object_stack.c` | `kernel/objects/kernel_object_stack.c` | 3 |
| `kernel/kernel_object_process.c` | `kernel/objects/kernel_object_process.c` | 6 |
| `kernel/kernel_object_thread.c` | `kernel/objects/kernel_object_thread.c` | 3 |
| `kernel/kernel_object_context.c` | `kernel/objects/kernel_object_context.c` | 4 |
| `kernel/kernel_object_hart.c` | `kernel/objects/kernel_object_hart.c` | 5 |
| `kernel/kernel_object_validation.c` | `kernel/objects/kernel_object_validation.c` | 1 |

These are the fixed `CORE_SOURCES` and `ORIGINAL_FUNCTIONS[0]`
declarations, not names discovered from a future edited kernel.
`BASELINE_BODIES[0]` has the same 48-name set.

`test_complete_original_bodies_and_all_parameter_types_remain_exact`
compares each original body and one aggregate signature digest.
`original_body_digest` and `core_body_text` reconstruct the outlined
attach, hart-stack and base-validator bodies from their actual phases.
Each of these phases remains in the same C owner as its original.

`test_portable_complete_byte_inverse_is_independent_and_rejects_changed_code`
also checks `BASE_CORE_DIGEST`. Its `restored_kernel_objects_source`
consumer reconstructs all nine portable C owners and obtains the original
magic definition from `kernel/objects/kernel_object_private.h`.
The facade alone cannot supply this result.

### Actual metadata defect

All of these fifty records currently have the same 29 fallback payloads,
primary unit `B01` and consumers `B01`, `B03`, `B14`:

```text
tests/host/test_kernel_object_source_layout.py:data-entry:BASELINE_BODIES/0/<48 original names>
tests/host/test_kernel_object_source_layout.py:data-entry:BASELINE_SIGNATURES/0
tests/host/test_kernel_object_source_layout.py:data-entry:BASE_CORE_DIGEST
```

The payloads include `CMakeLists.txt`, `CMakePresets.json` and
`tests/host/c_source_records.py`, but no `kernel/objects/` source.
In particular, this is true for `micros_hart_resolve_context`,
`micros_thread_capture_execution_context` and
`micros_kernel_objects_validate_base`, the exact originals relevant to
OBJ01.

The existing inventory's `_literal_assignments` uses `ast.literal_eval`.
`SOURCE_SETS` contains the `CORE_SOURCES` name, and `ORIGINAL_FUNCTIONS`
contains literal strings followed by `.split()`. Neither whole assignment
is a literal accepted by that collector. `_baseline_body_path` therefore
finds no ownership data. Even a recovered flat logical kernel path must
be resolved through the completed directory map rather than tested as a
current flat file.

The signature aggregate and complete-byte digest have no dedicated
semantic binding and receive the same fallback. This is a metadata
defect, not a missing or incorrect runtime unit test.

EV01-C02 corrected two *other* consumers of the complete object inverse:
the scheduler and endpoint stable-file records. Their eleven-path
bindings already include the complete nine-source/private-header
reconstruction and must remain unchanged.

## Three panic boundaries enforce eleven source policies

[`tools/stack_bound_fatal.py`](../../tools/stack_bound_fatal.py)
has three actual rejecting comparison sites:

1. `trace_preservation` projects each selected resolver/provider unit and
   requires complete equality with `trace_reference_source(unit)`.
2. `trace_fault_artifacts` makes the same projected-file comparison for
   each fault-prefix function's source before accepting its source,
   frame, linked definition and call inventory.
3. `checked_source_record`, in its `TRACE_FIXTURE_GETTERS` branch, checks
   the runtime provider's same projected-file equality with `trace_test`
   enabled. Both getter functions reach it through
   `collect_fatal_artifacts` and `source_calls`.

The reference provider is `panic_trace_source.fixed_source`, pinned by
its unchanged `SOURCE_PROJECTION["source_reference"]` to:

```text
e12b2d6c163ac92282d380eff5be419fad7f7a30
```

`TRACE_SELECTED` has nine functions in five units.
`TRACE_FAULT_FUNCTIONS` has twelve functions in nine units.
Three source policies occur at both first boundaries; the runtime policy
also occurs at the third. The union is eleven, not separate identities
for repeated comparisons or individual functions.

| Subject | `trace_preservation` | `trace_fault_artifacts` | `checked_source_record` getter branch |
| --- | --- | --- | --- |
| `kernel/objects/kernel_object_hart.c` | Yes | Yes | No |
| `kernel/objects/kernel_object_identity.c` | Yes | Yes | No |
| `kernel/objects/kernel_object_process.c` | Yes | No | No |
| `kernel/objects/kernel_object_runtime.c` | Yes | Yes | Yes |
| `kernel/objects/kernel_object_thread.c` | Yes | No | No |
| `kernel/tests/trap/trap_test.c` | No | Yes | No |
| `kernel/trap/trap.c` | No | Yes | No |
| `kernel/trap/trap_context.c` | No | Yes | No |
| `kernel/trap/trap_interrupt.c` | No | Yes | No |
| `kernel/trap/trap_route_core.c` | No | Yes | No |
| `kernel/trap/trap_test_hooks.c` | No | Yes | No |

At #238, every actual projected subject equals its fixed reference.
Only `kernel_object_runtime.c` needs a nonidentity projection: the exact
PLAT03 test-registry definition guard is restored. The other ten units
are already byte-equal to their reference.

Expected SHA-256 values describe the fixed *projected* source, not a
candidate hash:

| Subject suffix | Fixed projected SHA-256 |
| --- | --- |
| `kernel_object_hart.c` | `cdeb06f68956c8013acaf84b519d91fa45cc703fc392faa114807b2646299873` |
| `kernel_object_identity.c` | `6214007c2aba90220a6e4b492469c0f6ba7872357541cb0668de5db6b2ec8a8d` |
| `kernel_object_process.c` | `e5a6062ca66c6d61a1bb31c50d883d372a59c88d95037f3c6fac1309fdf403d8` |
| `kernel_object_runtime.c` | `9a9843919caadf5c7205ed328b3642d59a37eb5ed1c0079ff644eaa5f6c7134a` |
| `kernel_object_thread.c` | `4592489a98067d3b0b1ff8f44fce3875bbd791ea44a19a850408ef42fe4b963f` |
| `trap_test.c` | `ac77ac2fa120fe9da8e3832da6a4f1eb77ab8fbe4d7133153d1a0ad756b6d6d3` |
| `trap.c` | `d7302f80a3f78d20e453ffb30ff3695e7eee735de0e2eca5abb0bb0c8d61fce0` |
| `trap_context.c` | `6fb41bb21a5bf6f552b9b3494be9cfcbfb13401c513002b5403da55e42499731` |
| `trap_interrupt.c` | `ce031c40ca6b9d04014895c49791fcbd85e866d59e718764fd3214087b890309` |
| `trap_route_core.c` | `3d8e4aa9777aad747540c11b5a8268bbd4d2d3b7e84358752136c5ebc49dae93` |
| `trap_test_hooks.c` | `1d77e6e8eb92e1fbfa3decd86164381f319366022e16c061882f989ef795fc2e` |

The coarse PLAT03 source-projection and permanent fatal-effect records
each bind a larger declaration scope. Neither individually names these
eleven whole-file predicates. Their existing values and checks remain
active; the missing names do not authorize retiring the coarse record.

The source equality is also distinct from the actual machine proof.
`trace_fault_prefix_effects` checks every representable hart slot and
retains actual memory effects, complete trap capture, panic continuation
and frame custody. Naming the source clause cannot waive that proof.

## Both committed memory providers pin historical metadata

`selected_address_space_authority.historical_predecessor_records` first
validates the complete current inventory, then requires the original
nineteen family fingerprints and 4,774 predecessor records.
Its `declared_scope` additionally checks the original complete digest.

`selected_release_handoff_authority.historical_predecessor_records`
likewise requires the original twenty-two fingerprints, 5,379 records
and complete digest. MEM05's authority chain also constructs MEM04's
task definition, so adapting MEM05 alone cannot preserve either claim.

Both need the same checked fifty-key inverse for historical comparison,
after complete current validation. Neither may authorize current source
using old metadata. The other five committed providers do not have this
complete predecessor-fingerprint comparison; their claims and all
existing historical test expectations still remain required.

## Consequence for OBJ01

The first defect creates an incorrect candidate closure containing
independent parser/recipe inputs. The second leaves two enforcers of the
hart source without an individually selectable policy. Neither is fixed
by EV01-C08's correctly registered MEM05 native-input identity.

EV01-C09 repairs only these source-authority boundaries. It does not choose
OBJ01's local scheduler preconditions, direct current-hart checks or
stack-disjointness mechanism. It changes no finding owner or runtime
dependency and creates no current source permission.

## MINIX and complexity disposition

The fixed MINIX behavioral/algorithmic reference remains
`4db99f4012570a577414fe2a43697b2f239b699e`, as traced by the
[B03 audit](minix-complete-baseline-b03-objects-scheduler.md).
This host-only correction has no MINIX runtime counterpart. It is the
existing compatible evidence extension under ADR-0082/0092, not another
kernel algorithm or a structural-parity claim.

For declaration/source size `s`, directory size `d` and binding records
`r`, closed decoding and binding use expected/amortized `O(s + d + r)`
work before existing canonical sorts and `O(s + d + r)` storage.
Dictionary insertion/lookup is expected/amortized constant work, with
linear collision worst cases; a conservative binding bound is
`O(s + d*d + r*d + r*r)`. Existing canonical catalog sorting still costs
`O(n log n)` comparisons over its `n` facts and is not removed or called
once per key.

Parse each declaration and directory once per inventory operation; do
not add one Git/authority replay per key. A historical read is bounded
to the fixed metadata source and membership, never a new candidate
execution or persistent unchecked cache.

## References

- [Accepted ADR-0118](../adr/0118-precise-object-and-panic-source-authority.md)
- [Exact EV01-C09 contract](../architecture/object-and-panic-source-authority.md)
- [Relocated evidence binding study](relocated-evidence-payload-gap.md)
- [Granular MEM05 source authority](selected-release-handoff-native-source-authority-gap.md)
- [Evidence transition](../architecture/evidence-authority-transition.md)

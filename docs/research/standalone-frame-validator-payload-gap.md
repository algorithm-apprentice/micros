# Standalone Frame-Validator Payload Gap

## Scope and fixed source

This EV01-C03 study supports
[Accepted ADR-0098](../adr/0098-standalone-frame-validator-evidence-binding.md)
and the [exact binding contract](../architecture/standalone-frame-validator-binding.md).
It is evidence-infrastructure research, not a MINIX runtime correction or a
new frame-validation algorithm.

The fixed source is
`ddca7661d4fa7e13acf0aaa5f0ba55b91f9c9916`, tree
`3bf57ef9d12345f7b72e082a5843c97a2476f23e`, the merged PLAT03 #204 outcome.
There are 3,938 facts and fourteen families at this point.

## Actual assertion and source

`tests/host/kernel_layout_source.py` declares the scalar:

```text
BOUNDED_FRAME_VALIDATION_SHA256 =
448f61074bb3c5d52631660b9537e45a8bb9cca266a645edd503457b6fa03694
```

`restore_frame_validation_hot_path` hashes its input source and admits either
the original source or this exact bounded-validator source before restoring
the older validator. `_restored_kernel_bytes` calls it only for logical
`kernel/frame_ownership_validation.c`.

The current physical owner is unambiguously
`kernel/memory/frame_ownership_validation.c`, through the fixed
`KERNEL_FILE_FACTS` map. The input is actual source text, not a CMake recipe,
planner result or multi-file reconstruction.

This scalar is not one of ADR-0092's direct logical-file dictionary entries.
The existing `_semantic_payload_paths` handles those tables,
`BASELINE_BODIES` and kernel classification facts, but supplies no binding
for this standalone declaration.

## The fallback is overbroad

`_assignment_facts` begins with the evidence owner and literal/semantic
payloads. For this scalar, no C path is discovered. The set contains only
the owner, so the function adds that large module's complete declared-path
fallback.

The resulting current fact has:

| Field | Fixed-base value |
| --- | --- |
| `primary_unit` | `B14` |
| `consumer_units` | B01/B02/B03/B04/B05/B06/B07/B08/B09/B10/B14 |
| Payload cardinality | 797 |
| Actual validator source included | Yes |
| `CMakeLists.txt` included | Yes |
| `tools/validation_plan.py` included | Yes |

The last two paths are named elsewhere in `kernel_layout_source.py` by
unrelated read/transport branches. Their presence does not mean they supply
the bytes hashed by this assertion.

## Concrete blocked transition

The documentation-only MEM01 proposal planned six granular retirements,
including this early input assertion and the later validator whole-file
assertion. It correctly kept independent CMake/planner/oracle inputs in its
immutable envelope.

ADR-0082's `_expected_closures` adds each affected fact's owner and payload
to the candidate closure. `_validate_environment` rejects any intersection
with immutable inputs. The 797-path fallback therefore forces MEM01's
candidate closure to contain those same independent CMake and planner bytes.
The resulting `PROJECTION` rejection is correct.

Independent design review identified this High blocker without running a
suite or editing files. The author verified the actual 797-path fact and
the exact inventory/validator code. MEM01's proposal is preserved as
reference only; no implementation or retirement started.

## Exact repair and independent calculation

The actual assertion requires precisely:

```text
kernel/memory/frame_ownership_validation.c
tests/host/kernel_layout_source.py
```

The unchanged unit rules yield primary B02 and consumers B02/B14. All other
fields, including the historical value, are unchanged.

Before any extractor edit, the author read the unchanged fixed-base
inventory and replaced only those three fields with the independently
specified values. The complete result is:

| Property | Result |
| --- | --- |
| Facts/families | 3,938 / 14, unchanged |
| Authority-core facts | 1,595, unchanged |
| Changed complete records | Exactly one |
| Changed fields | `payload_paths`, `primary_unit`, `consumer_units` |
| Other complete records | 3,937 unchanged |
| Other family fingerprints | Thirteen unchanged |
| Old authority-core fingerprint | `c9f825f5b1488d03ee1c54db9ae3324b312807cd62de6dfa23166331a32e7e5d` |
| Independently specified corrected fingerprint | `152ba765d3a332961b59e88a7725f4a823c335bb62b97ba09c662bb84e1098fd` |

This calculation changes no source assertion or expectation from candidate
code. The separate implementation must first retain its complete independent
declaration, then reproduce the missing precise binding and collision before
changing the extractor.

## Boundary

Do not generalize from this one fact to every standalone hash. In particular,
the separate original validator hash retains its existing handling. Existing
packed tables, 104 relocated bindings and complete object-family exceptions
remain unchanged.

The historical inverse is comparison-only and checks corrected metadata
before restoring old metadata. Current authorization uses the precise
binding and unchanged closure-disjointness rule. No task-specific override,
renamed input alias, retired identity or runtime correction is introduced.

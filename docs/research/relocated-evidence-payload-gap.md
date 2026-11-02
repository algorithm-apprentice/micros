# Relocated Evidence Payload Binding Gap

## Fixed source and scope

This is the read-only study for EV01-C02, a corrective evidence prerequisite
found during PLAT01 design. The fixed source is
`de31bce3de93f58c94e053ea55e41040aba79987`, tree
`294abd7cd7721dd1449c42c47cb55d66c3113630`, after packed-identity
implementation #193.

The inventory has 3,912 individual facts across eleven families. The
163 packed function records and fourteen semantic multisets added by
EV01-C01 have exact physical bindings and are not the defect here.
COPY01's retired memory-file identity also has its correct direct path.

This is project-owned evidence infrastructure, not a MINIX runtime
algorithm. No runtime source, identity retirement, executable prototype,
test or harness is authored in this study.

## Actual extraction boundary

`tests/host/evidence_authority_inventory.py:_path_strings` recognizes
repository paths only when their current spelling is a file.
`_hash_leaves` carries those paths into each direct dictionary hash row.
`_assignment_facts` falls back to the owner's whole declared repository-path
set when the row has no more specific payload.

That works for a current physical path. It does not resolve an old flat
`kernel/...` label through ADR-0073's fixed `KERNEL_FILE_FACTS` map.
The map already names the exact existing physical file, but these direct
file-keyed hash declarations do not consult it.

The resulting fallback is not merely broader than necessary: it can omit
the actual relocated source while including unrelated tests and tools.
The existing source assertions still run, and the scalar inventory
validator accepts the declared fingerprints. Neither observation proves
that these rows bind the correct current source.

## Complete observed surface

All direct logical-file hash entries found by the read-only query lack
their exact mapped source in their payload. There are exactly 104 entries,
in seventeen qualified tables owned by thirteen modules:

| Owner under `tests/host/` | Declaration | Entries |
| --- | --- | ---: |
| `test_endpoint_source_layout.py` | `STABLE_HASHES` | 7 |
| `test_grant_data_plane_source_layout.py` | `STABLE_HASHES` | 14 |
| `test_kernel_bootstrap_source_layout.py` | `STABLE_HASHES` | 8 |
| `test_kernel_ipc_source_layout.py` | `FILE_SCOPE_STORAGE_HASHES` | 5 |
| `test_kernel_ipc_source_layout.py` | `STABLE_HASHES` | 10 |
| `test_kernel_ipc_source_layout.py` | `STATIC_LOCAL_STORAGE_HASHES` | 3 |
| `test_kernel_object_source_layout.py` | `STABLE_HASHES` | 6 |
| `test_kernel_pm_control_source_layout.py` | `STABLE_HEADER_HASHES` | 5 |
| `test_kernel_vm_handoff_source_layout.py` | `STABLE_HEADER_HASHES` | 4 |
| `test_pm_service_source_layout.py` | `STABLE_HEADER_HASHES` | 2 |
| `test_ramfs_source_layout.py` | `STABLE_HASHES` | 2 |
| `test_scheduler_source_layout.py` | `STABLE_ARTIFACT_HASHES` | 5 |
| `test_trap_syscall_source_layout.py` | `EXISTING_INLINE_HEADERS` | 1 |
| `test_trap_syscall_source_layout.py` | `ORIGINAL_SOURCE_HASHES` | 3 |
| `test_trap_syscall_source_layout.py` | `STABLE_HASHES` | 5 |
| `test_tty_source_layout.py` | `STABLE_HASHES` | 22 |
| `test_vm_service_source_layout.py` | `STABLE_HEADER_HASHES` | 2 |

All 104 are `TRANSPORT_IDENTITY`: 55 belong to `kernel-structure` and 49
to `service-structure`. Their keys and expected source/property hashes
already exist. The problem is source and audit-unit binding, not absent
identity materialization or a changed expected runtime result.

### Actual-consumer exception

Two labels cannot be repaired by binding only the relocated facade:

- scheduler `STABLE_ARTIFACT_HASHES/kernel/kernel_objects.c`; and
- endpoint `STABLE_HASHES/kernel/kernel_objects.c`.

The actual assertions in `test_scheduler_source_layout.py` and
`test_endpoint_source_layout.py` call `restored_kernel_objects_source()`,
not an ordinary file read. At the fixed source, that helper in
`test_kernel_object_source_layout.py` reads all nine `CORE_SOURCES` and
`PRIVATE_HEADERS[0]`. Accepted ADR-0068 explicitly requires this complete
actual-source reconstruction while preserving the original hash.

These two facts therefore require their own owner plus the complete mapped
nine-source/private-header closure. The other 102 records use their named
single current source. Source labels alone do not identify reconstruction
boundaries.

For example:

| Existing logical row | Actual current source | Current payload breadth |
| --- | --- | ---: |
| TTY `STABLE_HASHES/kernel/tty_control_core.c` | `kernel/tty/tty_control_core.c` | 46 paths, excluding that source |
| Object `STABLE_HASHES/kernel/boot_component_tests.c` | `kernel/boot/boot_component_tests.c` | 29 paths, excluding that source |
| Grant `STABLE_HASHES/kernel/boot_component_tests.c` | `kernel/boot/boot_component_tests.c` | 27 paths, excluding that source |

## Concrete transition consequence

PLAT01's provisional source study considered twenty existing byte
identities. This is not an Accepted retirement plan. Their current
fact-derived candidate closure contains 102 paths, including:

- `tests/host/c_source_records.py`;
- `tools/validation_context.py`;
- `tools/validation_plan.py`; and
- `tools/run_qemu_smoke.py`.

Those collateral paths conflict with freezing independent inspection and
command inputs. ADR-0082's `_expected_closures` derives candidate paths from
each affected fact's owner and payload; `_validate_environment` rejects any
intersection with immutable oracle/command inputs.

Using the already fixed directory bindings for those same identities
produces eighteen source/owner paths and removes the demonstrated
collateral-tool collision. This read-only calculation establishes why the
repair is a prerequisite. It does not complete PLAT01's future oracle
closure, approve its retirement set or waive any runtime gate.

## Required correction

Resolve the exact logical-source labels above through their actual
consumers and existing current directory owners: 102 single-source
bindings and the two complete object-reconstruction closures. Preserve each
fact's key, family, owner, kind, scope, authority class and historical
evidence value. Change only payload paths and their derived primary/consumer
units.

Independently declare the corrected 104 records from this fixed source
before editing the extractor. Only the two affected family fingerprints
may change; do not regenerate an expectation from candidate output.
The nine other family fingerprints, all other facts, packed identities and
COPY01's prior retirement remain unchanged.

Historical conservation checks need one bounded inverse for these metadata
fields. It must recognize the exact independently specified corrected
record before restoring its fixed pre-correction binding. It must not
erase unknown metadata drift or weaken an old expected digest.

No evidence field is made immutable merely by renaming it to a synthetic
input key. No current source gate is retired, and no new general evaluator
or broad fallback exception is justified by this finding.

## References

- [Evidence authority transition](../architecture/evidence-authority-transition.md)
- [Relocated payload contract](../architecture/relocated-evidence-payloads.md)
- [Kernel directory layout](../architecture/kernel-directory-layout.md)
- [Object reconstruction authority](../adr/0068-kernel-object-source-decomposition.md)
- [Packed identity contract](../architecture/packed-evidence-identities.md)
- [Correction queue](minix-complete-baseline-findings-and-correction-dag.md)

# Current Validation Overhead After OBJ02

## Fixed state and question

OBJ02 is complete and independently reviewed as #246:

```text
commit: f31c3654c5ede2837dad8e89ba90e8c4c0e37e12
tree:   3601709af706a810f835f101acf394abbe9e560d
```

Its actual native/target Red and Green, thirteen-unit recertification, final
author gate, exact-tree publication and journal-free main checks passed.
This study does not reopen its runtime findings or start another runtime
audit.

The user asks to remove historical source-freeze chains, metadata-only
prerequisite PRs and duplicate validation while keeping necessary behavioral,
hardware and cross-service evidence.

The question is not whether to eliminate tests. It is which repeated work
establishes a current contract and which work only reconstructs earlier
development history.

## Actual measured acceptance

OBJ02's final successful producer is
`57d7217259bd4682b15b33deec6c2c7a`, at the fixed tree above.
All fifty required commands ran fresh. Its native results were:

| CTest group | Seconds |
| --- | ---: |
| `selected-ownership` | 232.06 |
| `selected-address-space` | 193.93 |
| `selected-release-handoff` | 205.32 |
| `object-hart-validation` | 160.60 |
| `common-scheduler-return` | 158.81 |
| Those five orchestrators | 950.72 |
| All 47 CTests | 1100.46 |

The five wrappers account for approximately 86.4% of native wall time.
Their cost includes real execution and admission; this table does not prove
that every second is removable.

Full Python discovery ran all 1,686 tests in 944.539 seconds. Reported slow
cases included:

| Case | Seconds |
| --- | ---: |
| First actual-current bootstrap-allocation case, including lazy source-view opening | 144.376 |
| Seven-prior-claims journal-free catalog check | 68.744 |
| Fixed-source selected-ownership slices/control | 57.832 |
| Six-prior-claims journal-free catalog check | 36.421 |
| Fixed-source release/handoff slices/control | 30.561 |
| Runtime-memory raw-journal publication negative control | 25.932 |
| Five-prior-claims journal-free catalog check | 25.883 |

These are observed case durations, not an isolated profile of their internal
functions. Do not add them to overlapping native or whole-suite durations.
The retained log is `obj02-restart-full-producer-v2.log`; #246 records the
exact command, source identity and complete producer/reuse results.

## Cumulative proof at an ordinary source read

At the fixed state:

```text
current native CTest or host source consumer
-> invocation-owned checked_source_view
-> _SourceView._open
-> _verify_authorities
-> nine providers' committed_claim and task_definition paths
-> cumulative predecessor declarations, inventory and transition validation
-> retired-key lookup and captured source bytes
```

The central call path is in
`tests/host/evidence_authority_scope.py:88-108,194-246,263-287`.
The newest provider's `declared_scope` and `task_definition` in
`tests/host/common_scheduler_return_authority.py` include the preceding
OBJ01 definition. Earlier providers similarly retain their predecessor
contracts. Source-view ownership fixed repeated per-lookup reconstruction,
but opening another process still pays the cumulative proof.

The current inventory contains 6,789 facts in twenty-nine families and the
committed registry contains nine claims with 338 retirements. Its
`*_native_source_bindings.py` modules name individual current-source
comparisons. They are not new independent algorithm experiments.

## Historical execution inside current gates

The five current orchestrators are not uniformly current-only:

- `selected_ownership_evidence.check_native_products` executes six current
  products, then calls `measure_native(root, Stage.RED)` for a fixed old
  implementation and compares complete-helper identities;
- `selected_address_space_evidence.check_native_products` executes four
  current channels but still resolves inherited source and audit admission;
- `selected_release_handoff_evidence.check_native_products` executes five
  current channels with the same cumulative source boundary;
- `object_hart_validation_evidence.check_native_products` builds/checks both
  historical and current stages, requires discriminating historical Red and
  compares their outputs on every current CTest; and
- `common_scheduler_return_evidence.check_native_products` repeats the
  historical/current pattern for four products.

The current-case semantics, work bounds, source/product identity checks,
ordinary collector absence and observed composition are useful. Rebuilding
old source and re-proving the historical transition are separate obligations.
They can be removed only while preserving the useful current expectations.

## Source reconstruction has become a compatibility program

`tests/host/kernel_layout_source.py` contains approximately seventeen thousand
lines. Its current readers do not simply read current files:

- `_restored_kernel_bytes` composes terminal, MMU, stack, OBJ02 and inherited
  include projections before comparing historical identities;
- `_restored_include_bytes` requires fixed line positions and restores old
  include spelling;
- `read_bytes_path` dispatches CMake, planner, context, input and execution
  modules through separate historical restorers; and
- CMake restoration repeatedly recognizes later revisions and removes their
  additions until an earlier whole-file identity is recovered.

Other source-layout tests combine real current ownership/reference/ABI
checks with old body, AST, migration and inverse checks. A file-wide deletion
or a runner skip list would lose that distinction.

OBJ02 itself added 5,798 lines of literal native bindings, 1,795 lines of
source/adaptation declarations and 604 lines of authority handling. Those
8,197 lines are about 69.6% of its 11,782 added lines. They include useful
observation contracts as well as historical admission; the count is evidence
of the coupling, not a claim that all those lines are disposable.

## Metadata prerequisites and repeated execution

EV01-C01 through EV01-C09 introduced separate metadata design/implementation
pairs before the runtime correction that exposed each naming/binding gap.
For example, #237/#238 and #239/#240 precede OBJ01; their records describe
metadata-only changes rather than another object/hart behavior.

`tools/check_common_scheduler_return.py:retained_checks/execute` also
re-executes retained memory/object evidence and a 717-case B14 suite, then
B01-B12 workflows. The later final plan executes complete native discovery
and the same applicable QEMU workflows again at the unchanged staged tree.
The extra authority transaction is not an independent hardware scenario.

An additional concrete repetition occurred during OBJ02 publication:

```text
first producer: ef11774eb71e497281d5a23f8ffd3f86
same staged tree: 3601709af706a810f835f101acf394abbe9e560d
native: 47 CTests / 1123.97 seconds
Python: 1686 tests / 941.063 seconds
smoke guest: pass
receipt postflight: execution context/input changed
final_candidate: false
```

The original preflight profile was not retained, so this study does not
claim a fully identified input delta. Subsequent captures proved all three
profiles canonically stable across reconfiguration and no-work builds.
The final producer then ran every command fresh without a source change.
At least 2,065.033 seconds of already successful native work preceded the
first failure; it is not claimed as final acceptance or silently reused.

The existing executor makes the coupling explicit:
`tools/validation_execution.py:_execute_workflow` collects a profile even
when neither reuse nor producer mode was requested, and `_fresh_workflow`
can fail an actual successful command in profile postflight.

## Fixed MINIX baseline and required adaptations

The fixed local reference remains
`4db99f4012570a577414fe2a43697b2f239b699e`.
The existing [driver study](minix-validation-driver-baseline.md) traces
`minix/tests/Makefile` and `minix/tests/run`. It supplies named tests,
explicit dependencies/selections, individual outcomes and aggregate failure,
not a per-private-function historical source registry.

| Difference | ADR-0080 classification | Treatment |
| --- | --- | --- |
| Named tests, explicit selection and per-test failure | Baseline parity | Preserve |
| C17 native models and OpenSBI/RV64 guests | Required adaptation | Preserve the correct observation layer |
| Current input snapshots, artifact closure and explicit receipts | Compatible extension | Retain at the boundary that needs the claim |
| Reconstructing earlier implementation/retirement history to read current source | Divergence requiring correction | Remove the development-tooling cost without weakening current checks |
| Rejecting unsatisfied selected evidence instead of optional reference-driver skipping | Compatible extension | Keep required evidence fail-closed |

For `t` selected programs, driver dispatch remains `O(t)` plus actual test
work. For `n` entries, `s` current source bytes and `q` local reads, sorted
snapshot capture and expected dictionary lookup cost
`O(s + n log n + q)`, with `O(s + n)` host storage. No target stack or
runtime data structure changes are needed.

## Original design conclusion and limit

Keep behavior, ownership, observation, artifact, hardware, integration and
stack evidence. End completed source-transport ceremonies, remove their
cumulative runtime admission role, and keep metadata with the coherent
change it supports. Use one sufficient final plan rather than a second
recertification execution of the same coverage.

The [Accepted contract](../architecture/current-contract-validation.md)
defines that replacement and its loss-prevention tests. This research has
not changed a runner, reduced a test set, executed a replacement prototype
or demonstrated a speedup.

## Implementation measurements

The dependent implementation after design #247 removes the live catalog,
predecessor/author chains, historical native products and broad source
transport. It preserves current native cases/channels, target scenarios and
finite source/machine obligations; the
[implementation map and case disposition](../architecture/current-contract-validation.md#implementation-and-retained-evidence-map)
identify their owners.

One development selection executed all seven affected native CTests:

| CTest group | Fixed #246 seconds | Current development seconds |
| --- | ---: | ---: |
| `selected-ownership` | 232.06 | 29.45 |
| `selected-address-space` | 193.93 | 19.03 |
| `selected-release-handoff` | 205.32 | 36.68 |
| `object-hart-validation` | 160.60 | 5.05 |
| `common-scheduler-return` | 158.81 | 3.35 |
| Those five orchestrators | 950.72 | 93.56 |

That seven-test selection, including bootstrap allocation and kernel mapping,
passed in 94.72 seconds. Its retained log is
`validation-simplification-seven-current-native-v1.log`. This is not a
measurement of all 47 CTests or of build time.

At implementation tree `4bcf3aa8c3f7f658c6221b240e6e3193876071cc`,
`tools/run_host_tests.py --tier full` passed all 1,058 remaining Python cases
in 83.253 seconds, with zero skipped or unexecuted cases. The fixed #246
discovery had 1,686 cases and took 944.539 seconds. The reduced count is
explicitly explained: old transport/author-protocol cases were removed or
consolidated, while current ownership, behavior, artifact and failure controls
remain. The retained current log is
`validation-simplification-current-full-host-green.log`.

These are approximately 90% lower five-orchestrator time and 91% lower
full-host time, not an equivalent percentage reduction in total acceptance.
The actual current `riscv64-debug` ordinary/metadata proof also passed,
including real callback, assembly, fatal, stack and image-passivity checks.
It is a development check, not the complete final stack certificate.

Fast-loop and build costs were measured separately at implementation tree
`d8347467ccda293d1bf331c353008f3fa8a1c6b5`:

| Operation | Measured seconds |
| --- | ---: |
| `cmake --preset host-debug` | 0.37 |
| Native preparation build, 1,314 Ninja actions | 25.28 |
| Subsequent `cmake --build build/host-debug`, explicitly no work | 0.03 |
| Complete `test-unit-fast` workflow | 22.63 |
| Its 37 CTests | 18.08 |
| Its 89 Python cases | 3.999 |

These are not a cold-build benchmark or a six-second fast-loop claim.
There is no matching fixed-#246 fast/build timing in this measurement set,
so no improvement percentage is inferred for those rows. The native
preparation finished before receipt production, keeping generated inputs
stable.

The final implementation PR supplies the exact final producer,
complete native/target/image/stack coverage, explicit-profile reuse and
independent review rather than treating these development measurements as
final acceptance.

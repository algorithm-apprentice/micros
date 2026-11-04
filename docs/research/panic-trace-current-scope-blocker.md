# Panic-Trace Current-Scope Blocker

## Fixed source and observed failure

This study supports
[Accepted ADR-0099](../adr/0099-panic-trace-execution-scopes.md) and its
[execution-scope contract](../architecture/panic-trace-execution-scopes.md).
It changes no MINIX/RV64 trace algorithm.

The fixed source is design #205,
`8d4fd8c0dad354a7840a70d1505fa0cfebd2888f`, tree
`b2a444d27e0e6cc43d5f5d1e6a5926c52b520dc2`.

The following documented full command failed on the uncommitted EV01-C03
candidate:

```bash
/usr/bin/caffeinate -i -s /opt/homebrew/opt/python@3.15/bin/python3.15 -E -S -B tools/validation_plan.py --tier full --execute --producer
```

Its producer was `7f6e377d0ffc47f6901fd83978fc92ae`, staged tree
`7934ddd46103a6a479616e00f3278e7f0a04cf7f`, source SHA256
`2d37ccd659c66589a77f1488772484ff98eddc5db3e3564c4930239b8966bbd8`.
All three reusable profiles were READY. Native execution passed 40 CTests
and 1,406 Python tests. Seven commands passed; the eighth,
`test-qemu-panic-trace`, exited 2 before target execution; 42 commands did
not run. The report correctly has `final_candidate=false` and
`producer.complete=false`.

The error was:

```text
PLAT03 source membership escaped its exact task roles
```

## Actual source path

`tools/check_panic_traces.py:execute` unconditionally calls
`require_source_scope(baseline=baseline)` before capturing the current
manifest and again after execution. For ordinary execution, `baseline`
is false, but the helper still checks:

- the fixed design membership hash;
- membership against the original base plus six candidates/eight evidence
  owners, without any later task additions; and
- every other production file against its original runtime Git blob/mode.

The Boolean changes only whether the new trace production files may be
absent. It does not make the membership or production comparison current.

Read-only inspection found exactly:

| Membership | Count |
| --- | --- |
| Original admitted PLAT03 union | 1,431 |
| Current EV01-C03 index | 1,434 |
| Already committed main #205 | 1,434 |
| Missing originally admitted members | 0 |

The three extra paths are already committed design documents:

```text
docs/adr/0098-standalone-frame-validator-evidence-binding.md
docs/architecture/standalone-frame-validator-binding.md
docs/research/standalone-frame-validator-payload-gap.md
```

Current index membership equals committed HEAD membership. The same
rejection therefore applies to clean main independently of every EV01-C03
implementation byte. Removing only the membership error would still leave
the inappropriate production freeze blocking later runtime corrections.

## Existing current boundary and precedent

`source_manifest` already captures all current tracked paths, requiring a
clean fully staged candidate and no untracked inputs. It preserves required
evidence owners, recipe/preset policy and tool/firmware closure. The controller
compares the complete manifest and actual built artifacts before publishing.
Later additions need not be ignored to admit ordinary current execution.

The existing PLAT02 controller instead invokes its historical scope check
only for baseline execution, while retaining current runtime validation,
the complete current manifest and before/after artifact stability.

The PLAT03 correction must additionally retain complete presence of all
six current trace production members when moving the old historical check
out of the ordinary path. Baseline typed absence is not current validity.

## Disposition

Preserve the EV01-C03 candidate as reference and block its implementation.
Do not add the new documents to old roles, rewrite a frozen digest, ignore
Markdown, disable stability checks or mix this controller-policy change
into ADR-0098.

The separate PLAT03-C02 design specifies exact phase lifetimes, unchanged
historical authority, complete current inputs and renewed actual
baseline/current evidence in an explicitly labeled fixed #204 envelope.
The real current checkout still needs its own complete fresh author gate
and independent review. Historical audit counts and runtime finding
ownership do not change.

# ADR-0111: Fatal TTY Header Extension

- Status: Accepted
- Date: 2026-11-08
- Refines: ADR-0082, ADR-0084, ADR-0086, ADR-0107 and ADR-0110
- Supersedes only: the fatal source-unit guard's rejection of the one
  ordinary TTY binding-inspector declaration below, and ADR-0110's
  two-stack-owner limit for the one additional immutable owner

## Context

Visitor admission design #226 is separately merged. The fresh MEM04
candidate completed actual native/target Red and Green. Its focused
ordinary/metadata `riscv64-debug` pair then passed the repaired actual
callback binding and assembly collection, but stopped in
`require_fatal_source_units()` before complete stack certification.

The [source census](../research/minix-selected-user-address-space.md#fatal-tty-header-certificate-gap)
finds exactly one mismatch in the complete fatal source-unit register:
`kernel/tty/tty_handoff_runtime.h`. Removing only the already required
ordinary binding-inspector prototype recovers the fixed EV03 header
byte-for-byte. No fatal type, layout, CSR, state transition, function body
or budget changed.

The early one-configuration check avoided repeating recertification and
the full producer before finding this next boundary. It did not publish
a complete stack certificate. The candidate is archived and implementation
is blocked; ADR-0110 does not authorize edits to the fatal-source owner.

## Decision

After independent review, Acceptance and separate merge, restart MEM04
with the [exact fatal header admission](../architecture/stack-bound-evidence.md#mem04-fatal-tty-header-extension).

### One exact ordinary declaration

Only for the existing `SOURCE_UNITS` entry
`kernel/tty/tty_handoff_runtime.h`, accept either:

1. its original complete fixed-source token sequence; or
2. that original with exactly this unconditional declaration immediately
   before the final outer include-guard terminator:

```c
enum micros_tty_device_authority_status
micros_tty_handoff_runtime_inspect_device_binding(
    const struct micros_kernel_objects *objects,
    struct micros_tty_device_authority *binding);
```

Construct both expected forms from the unchanged `reference_source()`
authority at `fc98edd26505ac522d3752ad85f09366e8085f32`, never from a
candidate-derived digest or a filtered candidate header. Preserve the
existing tokenization, owned-path/regular-source rules and source
projection order. Original historical/Red input remains valid; MEM04's
independent current-header and compiler checks still require its actual API.

The exact path, return type, name, parameter types/names, qualifiers,
pointer shape and placement are fixed. Wrong, duplicate, relocated,
conditional, definition-form or additional declarations fail. Every
original include, guard, macro, enum, type, field and API remains checked.
The exception does not apply to another path or a matching basename.

Keep all other `SOURCE_UNITS` comparisons, terminal-control declarations,
diagnostic layout/storage checks and conditional observer-source checks
unchanged. Retain every existing fatal function/compound anchor, source/
machine occurrence join, linked-state relation, qualifier, edge disposition,
frame, stack epoch and budget. This is not a new fatal effect or permission
to omit an actual call.

### Exact implementation and authority boundary

Only these existing owners change for this correction:

- `tools/stack_bound_fatal.py`, at the current source-unit header predicate;
- `tests/host/test_stack_bound_evidence.py`, for focused regression cases.

The existing predicate may identify the rejected source path in its failure
diagnostic. No new parser, stack file, workflow, command, flag, source-unit
entry, reference pin, body hash or authority family is introduced.
`tools/stack_bound_bindings.py` retains ADR-0110's already specified behavior;
`reference_source()` and `AUTHORITY_COMMIT` remain unchanged.

The fatal tool joins the two ADR-0110 inputs as the third exact immutable
stack-admission owner before renewed Red. It is not a production candidate.
Keep seventeen production subjects, 29 candidate paths, 33 retirement keys,
eight new evidence owners and one eight-fact family. Preserve all 4,774
predecessor facts, nineteen fingerprints, five claims and seventy prior
retirements. Retire no additional stack identity.

### Work and storage

This is a compatible development-evidence extension. The MINIX selected
walk, controller-binding adaptation and every runtime algorithm are
unchanged. At this one host header boundary, two finite expected token
forms require linear work and storage in header length. No runtime work,
storage, stack capacity or timeout increases.

## Evidence and restart

This design contains documentation only; do not author executable tests,
models, harnesses or prototypes before its separate merge.

Implementation starts with a native regression against the existing public
`require_fatal_source_units()` boundary, proving rejection of the exact
ordinary inspector declaration. Preserve real source comparison rather
than mocking its result. Cover original and exact extended headers,
malformed/duplicate/misplaced/conditional extensions, changed original
types/APIs/includes/guards, unrelated extra declarations, owned-source
failures, and unchanged rejection in other fatal source units.
The terminal/trace observer modes must not bypass their existing checks.

Keep these cases in the existing stack evidence module and B14. Freeze the
new adapter and updated test input before actual MEM04 Red; do not borrow
the archived runtime observations. Renew all four actual native products,
both canonical target captures and all three independent oracles.

After actual Green, run the documented one-configuration ordinary/metadata
development check before expensive recertification/full acceptance. It
must complete actual callback, assembly, fatal-context and stack proof,
not merely pass the header predicate. It remains development-only evidence.

Then recertify B01-B12/B14 and run the complete fifty-command producer with
current same-run receipts. Preserve all forty QEMU workflows, four
standalone-image workflows, 44 static configurations, 113 declared ELFs,
ten observed scopes and explicit blocked B13 admission. Independent
implementation review still precedes the first permanent commit.

## Non-goals

No generic append-only-header policy, declaration filter, type-only
comparison, source-unit exclusion, candidate-derived expected hash,
fatal qualifier or control-flow change, machine-edge waiver, runtime
correction, larger budget, framework, hosted-CI acceptance or later task.

## Design review disposition

Independent read-only review covered the complete ten-document proposal,
the exact fatal-header boundary and the remaining fixed-source census at
staged tree `213741952dd355eaeceb8102905678d8d80b354c`.
The matching author record contains four FRESH/exit0 documentation/diff
checks. The reviewer reported: "No significant issues found in the reviewed
changes."

Acceptance records this design result, not a runtime correction or complete
stack certificate. MEM04 remains blocked until the separate design merge,
then restarts with a fresh base, immutable inputs and actual evidence.

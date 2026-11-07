# ADR-0110: Kernel Visitor Extension Admission

- Status: Accepted
- Date: 2026-11-08
- Refines: ADR-0084, ADR-0082, ADR-0107 and ADR-0109
- Supersedes only: the kernel-visitor whole-header rejection of one ordinary
  declaration, the historical-function lookup's rejection of the five
  callback-free mapping helpers below, and MEM04's stack-tool exclusion
  for these exact admissions

## Context

Corrective handoff design #225 is merged. The restarted MEM04 candidate
completed actual Red, Green and all thirteen affected-unit recertifications.
Its full producer then passed complete native validation, all forty QEMU
workflows and four standalone-image workflows: 45 commands.

The stack workflow stopped with
`kernel-visitor callback header contract changed`.
`require_callback_type_scope()` compares the complete public header token
sequence with the fixed EV03 authority. The only difference is ADR-0107's
ordinary root-preparation prototype. The visitor type, carriers, providers,
constructors and callback-using bodies are unchanged.

The [source comparison](../research/minix-selected-user-address-space.md#kernel-visitor-header-certificate-gap)
establishes this precisely. It is not a stack-budget failure and does not
justify deleting callback closure or moving the expected reference.
The uncommitted candidate is archived; no final acceptance, implementation
review or publication is claimed.

Independent design review then identified a second admission boundary.
After header admission, `bind_current_callbacks()` requires a historical
function record for every linked mapping function, including functions
without callbacks. The five new selected-path helpers are absent from that
fixed source but present in the archived candidate's metadata ELF.
A header-only repair would therefore fail the next predicate. Both parts
of this one visitor-group extension must be specified before restart.

## Decision

After independent review, Acceptance and separate merge, restart MEM04 with
the [bounded extension admission](../architecture/stack-bound-evidence.md#mem04-kernel-visitor-extension-admission)
below.

### One exact ordinary declaration

For only the registered `kernel-visitor` group and
`include/micros/user_address_space.h`, accept either:

1. the original complete header token sequence; or
2. that fixed original header with exactly this declaration immediately
   before its final outer include-guard terminator:

```c
enum micros_user_address_space_error micros_user_address_space_prepare_activation(struct micros_process_handle process, uint64_t *root_physical_address);
```

Construct both expected forms from the unchanged fixed
`AUTHORITY_COMMIT`, not from the candidate header, a candidate-derived digest
or a list of arbitrary added functions. Preserve the existing tokenizer and
its whitespace policy. The complete comparison outside that one insertion
remains mandatory.

The header exception applies only to the exact registered group, including
its unchanged header/type/carrier/member/provider/constructor declaration.
It is not enabled merely by a matching name on a foreign group.
The other seven callback groups retain their original complete header rule.

Wrong, duplicate, relocated or additional declarations; altered visitor
types, existing APIs, includes, macros or guards; and changed callback
carriers/providers/constructors remain failures. Original header input stays
valid for historical/Red evidence; MEM04's separate current-header and build
checks still require its actual new API.

### Five qualified callback-free mapping functions

Only the same exact registered group may admit these five function names
outside the historical function inventory. Both the qualified translation
unit and its actual source origin must equal
`kernel/memory/user_address_space_mapping.c`. No basename, prefix, foreign
group, borrowed origin or whole-translation-unit exemption is sufficient.

The independently specified signatures are:

```c
static bool selected_slot_is_required(
    const struct micros_user_address_space_bindings *bindings,
    struct micros_process_handle process,
    uint64_t root,
    uint64_t virtual_address);
static bool selected_table_is_distinct(
    const struct micros_user_address_space_path *path,
    uint64_t physical_address,
    unsigned int depth);
enum micros_user_address_space_error micros_user_address_space_select_path(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_process_handle process,
    uint64_t virtual_address,
    const struct micros_user_address_space_bindings *bindings,
    struct micros_user_address_space_path *path);
static enum micros_user_address_space_error prepare_selected_path(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_process_handle process,
    uint64_t virtual_address,
    struct micros_user_address_space_path *path);
static enum micros_user_address_space_error read_selected_leaf(
    struct micros_user_address_space_runtime_view *runtime,
    struct micros_process_handle process,
    uint64_t virtual_address,
    struct micros_sv39_decoded_pte *leaf);
```

For each retained helper, require an ordinary non-symlink source file, one
actual function record, this exact signature token sequence and no
preprocessor guard. Keep the existing callback-type scope check over the
complete unit, and reject any visitor-type use or recognized callback
occurrence in a helper. Existing callback-using functions, including their
whole-body anchors, are not exempted.

Then require the exact catalog object and actual function symbol. Reject
any pending indirect call for that qualified function and independently
decode its actual machine instructions to reject every indirect call or
tail transfer under the existing call/switch rules. A missing catalog entry,
object or symbol, or an omitted machine-indirect site, is a failure.
Do not trust an empty supplied pending list or a source-only claim.

The existing graph still owns every direct call, frame, qualified function
and stack epoch; this admission contributes no callback target and removes
no edge. All other unknown mapping functions retain the historical-source
rejection. Other groups and existing diagnostic/VM lifetime exceptions stay
unchanged.

The five declarations are frozen before runtime edits; expected signatures
must not be extracted from the candidate. Their runtime bodies remain
subject to MEM04's behavioral/authority evidence and the complete actual
machine graph, rather than a new candidate-derived body hash.

## Exact owners and authority

Only these existing stack owners receive this immutable evidence extension:

- `tools/stack_bound_bindings.py`: the narrow header predicate and the exact
  five-function callback-free admission in the existing binding boundary;
- `tests/host/test_stack_bound_evidence.py`: focused positive/negative
  header and real linked-catalogue regressions in the existing native suite.

No new stack file, workflow, command, flag, callback group, provider, carrier,
offset, machine-call exception, source parser or receipt rule is added.
`tools/stack_bound_callback_contracts.py`, `AUTHORITY_COMMIT`,
`reference_source()`, existing function anchors, callback occurrence/argument
binding and source-machine inventory checks remain unchanged. The only new
function admission is the qualified callback-free rule above; it grants no
machine-call exception.

The complete inventory has only the existing module boundaries and fixed
authority-commit record for these stack declarations. Its authority pin
still identifies the same original source. Retire no stack identity and
change no existing fact/fingerprint.

The two adapters join MEM04's exact immutable scaffold registration before
fresh Red, not its mutable production domain. Keep seventeen production
subjects, the 29-path candidate domain, 33 retirement keys, eight new evidence
owners and one eight-fact family. Preserve all 4,774 predecessor facts,
nineteen fingerprints, five prior claims and seventy prior retirements.

## Evidence and restart

This design contains documentation only. Do not edit stack code, tests,
models or prototypes before its separate merge.

Implementation starts with a native regression proving that the existing
stack header guard rejects the independently declared new prototype.
Cover unchanged and exactly extended headers, malformed/duplicate/misplaced
extensions, real callback changes, unknown extra APIs, symlink/non-file header
inputs and unchanged rejection for other or foreign groups.
Keep existing provider/constructor, function-anchor and source-machine tests.

Also demonstrate the historical-function rejection through the public
binding entry point using real compiled RV64 objects and a complete linked
catalogue. Cover all five helpers, unknown/same-name foreign functions,
wrong signatures/guards, callback-type/occurrence introduction, borrowed
source origin, missing object/symbol and an actual indirect call concealed
by an empty pending list. Do not mock the machine decoder to claim closure.
B14 must include these header and linked-catalogue cases before the final
stack matrix, using the existing `tests.host.test_stack_bound_evidence`
module rather than a new workflow.

Require actual header and complete callback binding admission for the
current built MEM04 image, then the complete ordinary/metadata/observed
stack gate. Retain all 44 static
configurations, 113 declared ELF outputs, ten observed scopes, every
unchanged budget and explicit blocked B13 admission.
The source-header regression is not itself a stack certificate.

Restart MEM04 from the separately merged design with a fresh independently
frozen base and immutable adapters. Renew actual native/target Red, Green,
B01-B12/B14 recertification and the complete fifty-command producer with its
current same-run receipts. Independent implementation review still precedes
the first permanent commit. No archived result is borrowed.

## Non-goals

No generic declaration filter, arbitrary callback-free function admission,
whole-unit exemption, callback-type-only relaxation, expected-hash rebaseline,
changed source-stage pin, dropped source/machine edge, larger stack or timeout,
runtime change, source-counter ABI change, compatibility framework,
hosted-CI acceptance or later correction task.

## Design review disposition

Independent read-only review found one High issue in the header-only
proposal: the following historical-function lookup would reject the five
new callback-free mapping helpers. The author verified the source and
retained-artifact evidence and added their exact qualified admission,
unchanged callback anchors, actual machine no-indirect checks and real
linked-catalogue regressions before the full matrix.

Complete correction review inspected staged tree
`27683a3771a8a11cf93c125292ffe0be7a3081e2` and its matching author evidence.
All four documentation/diff commands were FRESH/exit0. The reviewer reported:
"No substantive design issue remains. The original High finding is resolved."
No targeted reproduction or author-gate rerun was needed.

Acceptance records this design result, not a runtime correction or stack
certificate. Implementation remains blocked until the separate design merge,
then restarts with a fresh base, immutable scaffold and actual evidence.

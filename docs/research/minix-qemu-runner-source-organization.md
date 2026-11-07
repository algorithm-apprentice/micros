# MINIX QEMU Runner Responsibility Baseline

## Reference and scope

The fixed read-only local reference is
`4db99f4012570a577414fe2a43697b2f239b699e` at
`/Users/alephling/Repo/minix`. Its HEAD and selected
`minix/tests/Makefile`/`minix/tests/run` paths were verified unchanged before
this design trace. No MINIX runner, source fragment, cleanup command or shell
expansion is copied or executed.

The existing [validation-driver study](minix-validation-driver-baseline.md)
is the shared ADR-0025 baseline. This narrower study explains responsibility
boundaries for the Accepted
[QEMU runner contract](../architecture/qemu-runner-source-organization.md),
not another kernel/subsystem design.

## Actual trace and classification

`minix/tests/Makefile:23-27,78-103,134-142` provides explicit named program
and shared-support ownership. `minix/tests/run:49-91` separates option
selection, executable-test counting and TAP plan publication.
`run:111-179` separates execution, per-test result collection and final
success/failure reporting.

| Observed responsibility | ADR-0025 classification | Preserved `micros` boundary |
| --- | --- | --- |
| Named programs/support and one outcome per selected test | Baseline parity | Keep all native/model/QEMU/image identities and one original test discovery. |
| Selection and report publication separate from the program under test | Baseline parity | Keep CLI, command construction, execution and outcome acceptance separate. |
| Hosted executable/privilege runner instead of OpenSBI/RV64 QEMU | Required adaptation | Existing exact `virt` argv, subprocess/serial ownership, guest markers and TAP remain authoritative. |
| Byte-exact ordered guest records, fault PCs and deterministic serial trigger | Compatible extension | Preserve the existing actual parser, symbol, timeout and input semantics, not a new generic protocol. |
| Exact candidate/context receipts and closed tool/default inputs | Compatible extension | ADR-0064/0065 retain authority; this source-only correction transports their reviewed code declaration. |
| Missing/privilege-dependent tests silently skipped | Divergence requiring correction | Never import that hosted-suite behavior; unsatisfied `micros` gates block acceptance. |

MINIX does not define this Python runner's regex vocabulary, process API,
serial event loop, exception boundaries, default-configuration closure or
execution receipts. Those come from Accepted `micros` decisions and the
fixed current implementation. Source organization cannot use MINIX as
authority to simplify them.

## Required adaptations, not optimization

The current `micros` runner already distinguishes classification from
acceptance: a panic marker can classify a run as panic while timeout,
nonzero exit, incomplete records or unexpected context still reject it.
The preserved process/serial loop owns its actual pipes, selector, deadline,
byte buffer, trigger and cleanup as one cohesive mechanism.

The exact 22-element guest command and all help/errors/markers/budgets remain
unchanged. The source split introduces no `-no-user-config`, new serial
retry, broader cleanup, missing-gate fallback or new identity check.
New helper-source/loader admission is a bounded development-code closure
transport, not a MINIX runtime feature.

Independent design review identified a necessary Python-specific closure
adaptation: standard `FileFinder` checks package initializers before sibling
modules and native extensions before source. The active trusted CPython
3.9.6 metadata reports `.cpython-39-darwin.so`, `.abi3.so` and `.so`.
Therefore a matching helper-source hash cannot admit an ignored same-stem
package or native replacement. The corrected bounded declaration rejects
those alternatives and checks the fixed namespace/helper origins using
source-free standard metadata from the admitted workflow interpreter.
No replacement initializer, extension, MINIX runner or new guest behavior
is executed by this design trace.

The baseline is the currently merged TTY outcome
`34f1048449c221b014530484bfec4b3c0ab51142`: 2,087 runner lines/46 functions/
two classes/43 assignments, and 2,492 test lines/six classes/85 methods/
36 independent fixtures. This design records those facts only. Executable
oracles, models, harnesses, prototypes and source transport begin after
independent review, acceptance and separate design merge.

## MEM04 successful-output boundary

At #222 (`91544f9e8ff9354975139c17907e282570adfcf2`), MEM04 requires
independently checked target return values, SATP observations and state
comparisons on both Red and Green. Its uncommitted candidate obtained actual
baseline witnesses. After the current user-address-space workflow passed,
its stdout contained only:

```text
TAP version 13
ok 1 - QEMU smoke test observed expected pass
# outcome: pass
# expected outcome: pass
```

That is not the required semantic evidence. No Green event, retirement,
implementation commit or implementation pull request was published.
The complete 43-file candidate was preserved locally at tree
`6366e078ef25ed0d2756885d0ae6817384dc6869`, and work returned to clean main
before the separate documentation-only prerequisite.

### Actual source and consumers

| Fixed current owner | Observed responsibility or constraint |
| --- | --- |
| `tools/qemu_smoke_execution.py:109-152`, `run_qemu` | One subprocess or existing serial-input loop produces a complete decoded `QemuResult.output`; execution does not retain a separate transcript file. |
| `tools/qemu_smoke_cli.py:121-235`, `main` | Classifies and validates each result, gates serial input, handles aggregate growth and returns original TAP output. |
| `tools/qemu_smoke_outcome.py:437-474`, `print_tap_result` | Accepted success returns before output publication; rejected results print the existing diagnostic transcript. |
| `tools/qemu_smoke_arguments.py`, `parse_arguments` | One parser and ordered validation; no successful-output option at this base. |
| `tests/host/diagnostic_history_source.py:292-359` | Existing runner-source, closed-profile digest and observed-option projection entry points. |
| `tests/host/kernel_layout_source.py:16912-16914` | Historical source reads compose that runner projection before original source-layout comparisons. |
| `tests/host/test_qemu_runner_source_layout.py:1447-1603,1733-1774` | Complete original source/AST, owner/import, closed-family and parser-order authorities remain active. |
| `tools/validation_context.py:59-70,111-132` | Exact ten-member source family is checked through the existing projected digest, before namespace and QEMU metadata inspection. |
| `tests/host/selected_ownership_source.py:1438-1456` | Direct MEM03 recipe admission compares complete restored predecessor bytes; MEM02/MEM01 and the panic/terminal/stack/root chain delegate here. |
| `tests/host/test_selected_ownership.py:393-401` | A separate forward comparison checks raw current CMake against `extend_cmake(original)` before the inverse. It also needs the prerequisite-only projection on its observed side, with original expectations and raw-current inverse/corruption checks retained. |
| `tools/stack_bound_observer.py:246-273` | Recipe must restore its fixed source; generated commands must retain exactly one original runner after the exact interpreter prefix, with no trailing command. |
| `tools/stack_bound_run.py:50-98` | Parses that one actual recipe, retains the result itself and checks complete legacy/observed evidence without using CLI success rendering. |

The specified inverse therefore belongs at the existing source/option and
innermost current recipe entry points, with the one direct forward-comparison
adapter above, not in new wrappers or relaxed aggregate checks.
Existing source-layout expected values, stack extraction,
execution, classification and legacy acceptance need no edits.

### Fixed MINIX mapping and cost

The fixed local MINIX HEAD was reverified as
`4db99f4012570a577414fe2a43697b2f239b699e`; the two selected test-driver
paths remain unchanged. At `minix/tests/run:57-59,78-91,126-167`, TAP mode
selects a diagnostic prefix, executes each chosen test once, records its
exit status, prints its result and emits the captured diagnostics for either
outcome. This trace was read-only; no MINIX shell code or cleanup was run.

| Dimension | ADR-0080 classification | MEM04-C01 mapping |
| --- | --- | --- |
| Execution and result reporting have separate owners | Baseline parity | Keep the existing execution, acceptance and output modules. |
| One execution supplies both outcome and diagnostics | Baseline parity | Print the already owned result; no second QEMU run for a host consumer. |
| Hosted temporary output versus decoded QEMU capture | Required adaptation | Preserve the existing `QemuResult` and Python process boundary; import no MINIX file or shell mechanism. |
| Optional accepted diagnostics with default output preserved | Compatible extension | Add only the requested single-result TAP diagnostic view. |
| Independent semantic witnesses plus historical source/recipe closure | Compatible extension | Keep original expectations; independently prove the additive reporting behavior. |

Reporting consumes `n` decoded characters in linear work and temporary
split-line storage, with constant call depth. The actual result is already
retained by the CLI. There is no new data structure, authority, kernel
algorithm, guest hot-path work or execution cache. A raw-byte claim would be
incorrect because execution has already decoded and may normalize newlines.
The new renderer must nevertheless preserve the line termination it receives,
rather than turning an incomplete observation into a complete one.

The original success summary is insufficient; an unchecked generic panic or
timeout is not substitute evidence. Repeating the workflow would violate
MEM04's within-phase ownership rule and add unnecessary work. The original
runner with one opt-in reporting flag preserves the strict stack command
boundary and is the smallest current-purpose adaptation.

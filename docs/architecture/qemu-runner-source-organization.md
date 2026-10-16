# QEMU Runner Source Organization

## Status, baseline and phase

This is the Accepted exact contract for
[ADR-0072](../adr/0072-qemu-runner-source-decomposition.md).
Independent complete design/correction and acceptance-only reviews are clean.
Design #145 separately merged as
`934faa9fd0e9e406758c0f2e336fb28af57739db`, with its exact reviewed tree.
Only `qemu-tooling-decomposition-implementation` is active, on a fresh branch
from that main. Current progress below distinguishes real owners from
explicit future migration paths. This adds no runtime-DAG node.

```text
reviewed micros base: 34f1048449c221b014530484bfec4b3c0ab51142
fixed MINIX reference: 4db99f4012570a577414fe2a43697b2f239b699e
original runner SHA-256: 9e54f744074d077d05b52880495b73f7c74ae1db0a6ad751f8b7024688bf79e3
design task: qemu-tooling-decomposition-design
implementation task: qemu-tooling-decomposition-implementation
```

TTY design #141, corrective design #143 and implementation #144 are
independently reviewed and merged. The actual #144 squash tree equals its
reviewed tree, main fast-forwarded clean, and obsolete branches are deleted.
Its old-head receipts are historical, not reusable across the squash HEAD.

| Current signal | Lines | Functions/classes | Required disposition |
| --- | ---: | --- | --- |
| `tools/run_qemu_smoke.py` | 2,087 | 46 functions, two classes, 43 module assignments | Move all original definitions/data into responsibility owners; outline three functions. |
| `tests/host/test_run_qemu_smoke.py` | 2,492 | Six classes, 85 methods, 36 fixture assignments | Preserve discovery/IDs while separating independent fixtures and plain case mixins. |

The three long functions are `matches_expected_result` 217,
`parse_arguments` 330 and `main` 187. The 64-method
`ExpectedOutcomeTest` is not one cohesive parser algorithm.

## Preserved behavior

The [fixed responsibility study](../research/minix-qemu-runner-source-organization.md)
does not import a MINIX runner, shell expansion, privilege bypass or skipping
policy. Existing Accepted subsystem reports remain authoritative.

- Preserve all exact marker, regex, help, diagnostic, output and exit-code
  bytes. Do not derive independent test expectations from production tables.
- Preserve plain `splitlines` versus the existing terminated-record helper
  at their actual uses. Do not make every marker require a newline or weaken
  an existing termination requirement.
- Classification checks panic first, explicit failure/`not ok` second,
  timeout third, then clean pass/unexpected exit. Acceptance is separate:
  exact expected outcome, no timeout, exit zero, complete required/ordered
  output, no forbidden output, and original report/fault requirements.
- Preserve every original short-circuit and first-error order, including
  report prerequisite relationships and the fields actually compared.
  Do not add broader VM-identity checks or normalize malformed records.
- Preserve the exact 22-element QEMU argv: single-hart `virt,aia=none`,
  `rv64`, selected memory, no display/monitor/NIC, serial stdio, default BIOS,
  selected kernel and `-no-reboot`. Do not add `-no-user-config`.
- Symbol inspection remains the original `nm -n --defined-only` subprocess,
  exact symbol selection, duplicate/missing handling and stderr behavior.
- Preserve process/pipe/selector ownership and all actual exception sites.
  No new kill, retry, cleanup, catch, fallback, clock or timeout is added.
- CLI parser construction, option order/defaults, help and first error
  remain exact. The same parser mutates the same namespace once.
- Main retains actual symbol resolution/fallback, one independent run per
  requested memory, serial-sent acceptance gating, managed-frame growth,
  single/multiple-memory TAP formatting and all original returns.

## Exact production owners

Exactly ten production paths constitute the source family:

| Owner | Responsibility |
| --- | --- |
| `tools/run_qemu_smoke.py` | Original executable/module facade, exact original bindings via explicit aliases, repository import bootstrap and unchanged `__main__` exit. No new execution policy or wrapper defaults. |
| `tools/qemu_smoke_records.py` | Sole two original classes, 43 original module assignments and complete-line splitting primitive. |
| `tools/qemu_smoke_platform.py` | Original FDT/trap/timer/frame/MMU/ownership readiness/report chain. |
| `tools/qemu_smoke_objects.py` | Original object/user-space/scheduler/endpoint/IPC readiness and acceptance records. |
| `tools/qemu_smoke_faults.py` | Original complete panic, trap-context, VM-self-fault and invalid-scheduler diagnostics. |
| `tools/qemu_smoke_command.py` | Exact QEMU argv and original symbol parsing/inspection boundary. |
| `tools/qemu_smoke_execution.py` | Original output decoding, complete trigger, cohesive serial loop and ordinary subprocess execution. |
| `tools/qemu_smoke_outcome.py` | Original required/ordered checks, classification, ordered acceptance phases and TAP result formatting. |
| `tools/qemu_smoke_arguments.py` | Original parser/options and ordered namespace validation phases. |
| `tools/qemu_smoke_cli.py` | Original main orchestration and one exact acceptance-call forwarding phase. |

The facade retains its original standard-library import bindings and
re-exports every original function, class and module-data name explicitly.
Its sole import bootstrap follows the existing repository script pattern:
insert the resolved repository parent of `__file__` before package imports.
No conditional import fallback, wildcard export, dynamic `globals` update,
new package initializer or serializer compatibility layer is introduced.
Moved functions keep original names/signatures/defaults and reference the
same single enum/result/pattern objects.

### Complete original function partition

Records, one:

```text
_split_output_records
```

Platform, seventeen:

```text
_has_complete_fdt_events _has_complete_trap_ready
_has_complete_trap_test_report _has_complete_timer_test_report
parse_frame_allocator_ready_counts _has_complete_frame_allocator_ready
_frame_allocator_ready_precedes_target_outcome
has_expected_frame_allocator_growth _has_complete_frame_allocator_test_report
parse_mmu_ready _has_complete_mmu_ready _mmu_ready_precedes_target_outcome
_has_complete_mmu_test_report
parse_frame_ownership_ready _has_complete_frame_ownership_ready
_frame_ownership_ready_precedes_target_outcome
_has_complete_frame_ownership_test_report
```

Objects, ten:

```text
parse_objects_ready _has_complete_objects_ready
_has_complete_user_address_space_test_report
_has_complete_user_execution_test_report _has_complete_scheduler_test_report
_has_complete_endpoint_test_report _has_complete_ipc_test_report
_objects_ready_precedes_target_outcome
_has_complete_object_model_test_report _has_complete_nested_trap_test_report
```

Faults, four:

```text
_has_complete_panic_report _has_complete_trap_context
_has_vm_self_fault _has_complete_scheduler_invalid_context
```

Command, three: `parse_nm_symbol_address`, `resolve_symbol_address`,
`build_qemu_command`.

Execution, four: `_decode_output`, `_serial_trigger_observed`,
`_run_qemu_with_serial_input`, `run_qemu`.

Outcome, five: `_has_required_output`, `_has_ordered_patterns`,
`matches_expected_result`, `classify_smoke`, `print_tap_result`.

Arguments and CLI each own their one original: `parse_arguments` and `main`.
All 46 move; none is deleted or replaced by an unchecked body fixture.

### Private dependency direction

```text
records -> standard library only
platform -> records
objects -> records, platform
faults -> records
command -> standard library only
execution -> records
outcome -> records, platform, objects, faults
arguments -> standard library only
CLI -> records, platform, command, execution, outcome, arguments
facade -> explicit source-family bindings only
```

No owner imports the facade. Necessary old cross-owner helpers retain their
complete names/signatures; new ordinary phase helpers remain private to
their owner. The facade aliases rather than recreates enum/result/pattern
objects. File/module provenance changes are source transport, not a promise
of old private pickling/module-qualification compatibility.

## Required ordered outlines

### `matches_expected_result`: 217 lines

Keep its complete original 39 keyword-only parameters/defaults.
`output_lines` initialization and the original common rejection compound
remain in root. Four ordinary boolean phases follow in the original order:

1. `_matches_foundation_reports`: FDT, panic, trap-ready/recovery, timer and allocator checks:
   original body statements 2 through 8.
2. `_matches_memory_reports`: MMU, frame ownership, user-address-space and user-execution checks:
   statements 9 through 14.
3. `_matches_object_reports`: scheduler, endpoint, IPC, invalid context, objects, object-model and
   nested-trap checks: statements 15 through 21.
4. `_matches_fault_reports`: actual `has_trap_context` initialization, required/forbidden trap context
   and VM-self-fault checks: statements 22 through 25.

Statement numbering is zero-based in the immutable original function body.
Each phase receives only its actually used original result/output/flag/
expectation values by explicit same-name binding; it repeats no attribute
read, truth conversion or validation before its original position.
Original guards return false; nonterminal phase success returns true and a
plain caller guard propagates false without adding diagnostics. Root retains
its original final true. No requirements record, reflective dictionary,
dispatch table or duplicate defaults is introduced.

### `parse_arguments`: 330 lines

Keep the original signature, parser construction, timeout option, single
`parse_args` and final namespace return in root. Ordinary void adders receive
the same parser:

- `_add_execution_arguments`: original statements 1 through 13;
- `_add_platform_report_arguments`: 14 through 27;
- `_add_object_report_arguments`: 28 through 34;
- `_add_fault_arguments`: 35 through 45.

Adders run in that order before original timeout statement 46 and parse
statement 47. Two void validators receive that same parser and namespace:
`_validate_execution_arguments` owns launch/default/serial/growth/regex/
kernel checks 48 through 57; `_validate_fault_arguments` owns exact
trap/VM requirements 58 through 61. Preserve `serial_input` initialization
and `vm_fault_arguments` scope/evaluation; `parser.error` remains the
original terminating error, not a status fallback.

### `main`: 187 lines

Keep the complete original signature, local initializers, symbol resolution,
memory loop, exception handlers, serial gate, success accumulation,
managed-growth report and all TAP/return branches in the actual main.
Move only its large `matches_expected_result` call expression into one
private `_matches_observed_run` helper returning that exact call.

Its six inputs are the existing `arguments`, `result`, `outcome`,
`expected_outcome`, `expected_trap_context_sepc` and
`expected_vm_self_fault_sepc`. The caller still assigns the returned value
to the original `accepted` local, then executes the original serial-sent
gate. Every attribute/keyword expression and its evaluation order remains
unchanged. The loop and process/result lifetimes are not extracted.

The result is 57 ordinary production functions plus the two original
classes. Every resulting function must be at most 120 physical lines.
Formatting preserves readable ordinary statements rather than compressing
control flow to satisfy the bound.

## Exact test ownership and discovery

Use the existing ELF-checker plain-case-mixin pattern. The original root
keeps its six aggregate `unittest.TestCase` names and one `unittest.main`
entry. Only that root is a discovery facade; case owners do not subclass
`TestCase` or add another loader/entry. All 85 original method bodies,
signatures, test IDs, counts and normal sorted discovery remain exact.

| Test owner | Exact original content |
| --- | --- |
| `tests/host/test_run_qemu_smoke.py` | Six original aggregate class identities and single discovery/CLI entry. |
| `tests/host/qemu_smoke_test_fixture.py` | All 36 original independent fixture assignments, same order and literal bytes; no production-marker imports. |
| `tests/host/qemu_smoke_classification_cases.py` | All twelve `SmokeClassificationTest` methods. |
| `tests/host/qemu_smoke_panic_cases.py` | `ExpectedOutcomeTest` methods 1-9 and 60-64: fourteen methods. |
| `tests/host/qemu_smoke_platform_cases.py` | Original expected-outcome methods 10-31: twenty-two. |
| `tests/host/qemu_smoke_memory_cases.py` | Original expected-outcome methods 32-46: fifteen. |
| `tests/host/qemu_smoke_object_cases.py` | Original expected-outcome methods 47-59: thirteen. |
| `tests/host/qemu_smoke_execution_cases.py` | All original NM/argv/serial/TAP methods: four plain mixins, nine methods. |

Method numbering is one-based in the immutable original class body.
Expected-outcome aggregate bases follow panic, platform, memory and object
case owners, then `TestCase`; there are no duplicate method names.
Original fixture names remain available through the test facade as explicit
aliases. Private case owners import only their actual facade/fixture and
standard-library dependencies.

Add exactly one structural gate:
`tests/host/test_qemu_runner_source_layout.py`. It is host-unit-only, not a
second acceptance engine or a reason to omit real guests.

## Argument-builder closure transport

Accepted ADR-0065's original complete runner identity remains a frozen
historical oracle. Only after actual source restoration matches its entire
`9e54f744...` bytes and complete module AST may implementation update the
reviewed current source declaration.

For `test-qemu-smoke` only, a bounded
`_validate_qemu_runner_family(root)` replaces the existing single-file
predicate at its same position, before `DarwinTools`/QEMU metadata. It checks
the exact ten production members above as ordinary nonsymlink files with
reviewed content identities. Paths/digests are fixed reviewed metadata,
never caller-selected, inferred from imports or discovered by a glob.

The same bounded declaration rejects every same-stem package path for the
nine helpers, including empty, linked or unreadable directories. A package
initializer precedes a sibling source module; a reviewed `.py` hash alone
does not close that choice. Adjacent legacy `.pyc`/`.pyo`, matching entries
in `tools/__pycache__`, and every supported native-extension alternative
are also ineligible. This includes tagged Darwin `.so`, `.abi3.so` and
plain `.so` forms, not only the collector's current suffix.

The existing namespace must remain the ordinary repository tools directory.
Reject its source/bytecode/native-extension initializers and repository-root
`tools` module alternatives. Do not assume a prepended namespace directory
preempts a concrete package on another admitted Python search root.

After the existing `DarwinTools` workflow-interpreter resolution, but still
before `tools.qemu()` or any QEMU metadata, one bounded
`_validate_qemu_runner_namespace(root, tools)` checks read-only standard
loader metadata from that already-admitted workflow interpreter. It must
observe only the standard supported CPython loader layout, a namespace
whose resolved search locations are solely the repository tools directory,
and exactly the nine expected ordinary source-module origins/loaders.
Duplicate spellings of that same resolved directory are not another input
root. Concrete/merged foreign packages, native or sourceless replacements,
unknown loaders/layouts and unreadable/link/ambiguous metadata are ineligible.

The probe is fixed to `tools` and those nine names and performs only
source-free standard `PathFinder`/`FileFinder` metadata lookups. It never
imports the target package/module, executes an initializer/extension or
follows competing package contents. Native suffixes come from the admitted
workflow interpreter's loader metadata, never environment guesses. No
general module resolver is added. The early file/byte checks retain their
original position; this additional loader check remains before every QEMU
metadata invocation, including `-L help`.

Do not read, execute, delete or repair replacement code/bytecode. Root
facade execution remains direct; helpers cannot import it back.

Existing `repository_python` complete tree identities already bind the
member names/kinds/modes/bytes and loader-input membership. No receipt schema,
new package resolver or persistent digest cache is added. Failure retains
the standard unsupported-context diagnostic and fresh-only behavior, never
an empty successful declaration or automatic environment cleanup.

Every other compiler/configuration/QEMU bundle/default-file/firmware/tool/
environment/source/output rule remains exact. All three supported workflows
must still demonstrate real eligible original-producer reuse after the
source correction; declaring them permanently ineligible is not completion.

## Retained source and selection oracles

Before rewriting, freeze from the reviewed Git predecessor:

- complete original runner bytes/AST, all 46 functions and signatures,
  two class ASTs/decorators, 43 assignments, imports and top-level order;
- three required long-function inventories and exact statement/binding
  dispositions;
- complete test module/85 method ASTs, six aggregate identities and all
  36 independent fixture assignments;
- current QEMU source declaration, full context/planner ASTs, complete
  current CMake recipe, fixed command/memory/help/diagnostic/TAP vectors,
  all consumers and four selection dimensions.

Finite actual raw-source and AST expansion must restore the entire original
runner, not just individual expressions. Original imports/tail come from
the actual facade; original data/classes/functions come from their actual
owners in frozen original order. Plain phase calls expand actual same-bound
statements; boolean success transport removes only its explicitly verified
continuation. No unchecked old body, expected digest substitution or
expectations regenerated from rewritten output is permitted.

Compose this exact source-family inverse before retained TTY/Grant/Object/
Endpoint runner hashes and frame/address-space source inspections. Keep
their old expected values unchanged. A finite context-declaration inverse
removes only the exact new family/loader predicate and metadata, restoring
the old one-file predicate/value before prior TTY/Grant context projections.
No other old context algorithm or expected AST changes.

Add two exact planner alias rows: production family to
`tools/run_qemu_smoke.py`, test family to
`tests/host/test_run_qemu_smoke.py`; add the new host-only structural input.
Restore those exact rows before prior metadata projections and extend their
independent positive/negative fixtures. Preserve full-tier, QEMU, slow-model
and image dimensions independently. CMake, presets, runner argv/budgets and
current recipe digest `730a6dd6...` remain unchanged.

## Acceptance and non-goals

Implementation begins with structural Red and independent raw/AST/parameter/
return/data/import/discovery fixtures before source transport. Preserve all
original callable/type identities through facade aliases and the 85 actual
test IDs exactly once. Add focused deterministic CLI/error/order, symbol,
serial short-write/timeout/cleanup and source-family mutation cases without
sleeps or new guest behavior.

Source mutations, missing/linked family members, initializer/cache presence,
same-stem package directories, native-extension alternatives, foreign
namespace/module origins and wrong bindings must refuse reusable smoke
evidence before QEMU metadata, including `-L help`. Cases include ignored
package `__init__.pyc`, all admitted extension suffixes and linked/unreadable
competitors. Exact good members retain actual eligibility.
Ordinary metadata collection never builds or installs anything.

Both original-move and long-task migrations must finish literal empty,
with all owners real and bounded. Stage the complete current change, prove
tracked/index cleanliness and no untracked task files, then run the complete
current fresh author plan and actual bounded same-original-full-producer
native/smoke/TTY-image reuse with complete-log provenance. Obtain complete
read-only independent implementation review; merge only the reviewed head,
verify identical squash tree/main fast-forward and delete exact branches.

No accepted report, command interface, guest budget, serial/process error or
cleanup behavior changes. No configuration/default flag workaround, new
eligible workflow, generalized parser/import/bytecode/rule engine,
cross-HEAD reuse, kernel layout or production init/shell work is included.

## Current implementation progress

Before any rewrite, immutable Git `934faa9` facts froze the complete runner,
46 functions/two classes/43 assignments and six aggregate classes/85 methods/
36 independent fixtures, plus context/planner/recipe and retained oracles.
The runtime gate contains only immutable hashes/order/whitespace metadata,
not an unchecked legacy body archive.

Structural Red first found the original argument builder still in the
facade. Independent exact actual-node raw/AST/whitespace and facade-binding
fixtures preceded its complete 25-line move into the command owner.
The actual complete original `9e54f744...` file and module AST reconstruct
before either current source digest changes.

All ten production paths are present; eight future modules are explicitly
single-docstring migration owners, not completed responsibilities.
The original other 45 functions, two classes and data still remain in the
facade; all 85 original cases still execute through their unchanged root.
There are 211 original node/method/data moves and all three long-function
dispositions remaining.

The reviewed ten-member code declaration, exact early package/cache/source
checks and fixed source-free admitted-interpreter namespace/helper-origin
checks are wired. Actual installed native/smoke/TTY-image metadata closure
remains eligible; no dirty-iteration workflow receipt or reuse is claimed.
The original full context AST and all prior runner byte/text expectations
restore before retained checks. Current narrow evidence passes 223 primary
checks, four other retained source checks and the real unchanged-budget
smoke guest. Final full author/reuse and independent implementation review
are not claimed complete.

The records slice now moves all 43 original assignments, both complete
class/decorator definitions and the 16-line record splitter into their one
owner. Explicit facade aliases preserve the actual enum/result/pattern
object identities and original dataclass defaults. All original raw bytes/
ASTs restore before updating either current member digest. Current narrow
source/legacy-case evidence passes 187 checks and both real smoke/panic
guests with unchanged budgets. Seven future production owners remain
docstring-only, 44 original functions still reside in the facade, and
165 original node/method/data moves plus three long dispositions remain.

All seventeen platform-report functions now have their one complete owner,
with only actual original records/pattern dependencies. Their full original
bodies/signatures, prerequisite relationships and short-circuits reconstruct
before current source pins change. Current evidence passes 188 focused
checks and actual smoke/timer guests at the original eight-second budgets.
Six future production owners remain docstring-only; 27 original functions
still reside in the facade. There are 148 original node/method/data moves
and all three long-function dispositions remaining.

The ten original object/user-space/scheduler/endpoint/IPC report functions
now occupy their complete owner with exact record/platform dependencies.
All raw bodies, prerequisite/order tests and actual whole-file/context
inverses pass. Current evidence is 189 focused checks and real object-model
and IPC guests at their original budgets. Five production migration
stubs remain; seventeen original functions still reside in the facade,
with 138 original node/method/data moves and three long tasks pending.

# Current-Contract Validation

## Status and scope

This is the Accepted contract for
[ADR-0123](../adr/0123-current-contract-validation.md).
The paired tasks are `validation-simplification-design` and
`validation-simplification-implementation`. Complete independent design review
is clean and design #247 is separately merged as
`7f621f58129b298e983bd5bf41a196087e354e8d`. Implementation
[#248](https://github.com/algorithm-apprentice/micros/pull/248) is merged as
`3f35213e29850e1a3f3f31067b6342af57b7a310`, with reviewed, published and
squash tree `c656e80c9d258a89cceafd19aacb7969fc5dd074`.
That PR owns the complete fifty-command producer, three-profile reuse and
clean independent GPT-6.1 Sol review record. The current owners below are
implemented, not a pending design.

The dependency-ready base is merged OBJ02 #246:

```text
commit: f31c3654c5ede2837dad8e89ba90e8c4c0e37e12
tree:   3601709af706a810f835f101acf394abbe9e560d
```

The outcome replaces historical implementation admission with current
contract validation. It is one tooling task, not another runtime audit.
The kernel, services, user runtime, target assembly and their Accepted
behavior remain unchanged. The runtime correction DAG and its remaining
finding owners do not change.

## What remains authoritative

| Requirement | Current evidence |
| --- | --- |
| Portable behavior, errors, state and rollback | Independent native unit/property/model tests |
| Algorithm family and work/storage bounds | Accepted MINIX/adaptation study, actual primitive observations and independent bounds |
| Current module ownership and dependency direction | Narrow current-source/build checks plus compilation and review |
| Wire/ABI and generated artifacts | Independently specified layouts/bytes and actual artifact checks |
| Observation truth and passivity | Complete current primitive/site coverage, reversible instrumentation, ordinary/observed agreement and linkage checks |
| Privilege, traps, registers, IRQs, MMU and fences | Real QEMU component executions |
| Cross-service authority and resource lifetime | Native protocol models and the ready-service QEMU integrations |
| Stack bounds | Current compiler/machine closure and authenticated target observations |
| Source freshness and test completion | Exact candidate, stable input snapshots, complete outcomes and retained logs |
| Design and correctness approval | Separate read-only review of the complete current change |

Historical source spelling is absent from this table. It cannot certify
correctness, nor may changing it prevent a correction that satisfies these
contracts.

## Historical machinery disposition

The following live mechanisms are removed rather than bypassed:

- the EV01 global identity inventory, family fingerprints, transition/claim
  registry and per-assertion native binding tables;
- cumulative `*_authority.py` predecessor and committed-claim reconstruction
  for COPY01, PLAT01, MEM01-MEM05 and OBJ01/OBJ02;
- author selectors, immutable three-event journals, provisional retirement
  scopes and their generic protocol/model tests;
- historical C/body/AST/token reconstruction, whole-module gate freezing and
  layered CMake/runner/source inverse chains;
- fixed old-source products and tests whose only current purpose is proving
  that a previously completed correction once had a discriminating Red;
- obsolete `--red`, `--green` and `--recertify` author interfaces and the
  requirement to create another such controller for each correction.

Git history and the original PR records retain the old evidence. There is
no migration reader, legacy-success flag, persistent retirement cache or
compatibility wrapper for those local author journals.

This disposition is based on the property being checked, not filename,
digest length or a broad test-class exclusion. Mixed files retain their
current checks. Full host mode still discovers and executes every remaining
`test_*.py` case; it gains no historical-test skip list.

An uncertain classification does not authorize deletion. Resolve the
requirement against its Accepted contract and preserve a current check.
Do not replace a mixed test with a passing stub or reduce the model/target
coverage to make the new suite green.

## Retained source-check boundary

Port useful checks to actual current paths and bytes. Remove implementation
identities and ancestor reconstruction around them.

| Existing family | Retained current responsibility | Historical mechanism removed |
| --- | --- | --- |
| Kernel memory, ownership, VM handoff and PM control | Complete current source sets, private headers, unique state owners, exact ABI and required complete-audit boundaries | Old helper bodies, migration signatures, include line numbers and restored whole headers |
| Object, scheduler, endpoint, IPC and grant evidence | Independent models, unique fixture/context ownership, complete cases, supported public interfaces, source-set/tier ownership and observation integrity | Old private-helper inventories, body/token hashes, ancestor control-flow spelling and projection chains |
| VM, PM, VFS, RAMFS and TTY source/test organization | Current private dependencies, state/ABI ownership, complete build inputs, independent references and replayable model coverage | Original-file reconstruction and historical long-function migration tables |
| ELF checker, QEMU runner and validation tooling | Parsing/CLI behavior, resource cleanup, dependency closure, immutable result types where required and complete discovery | Whole-original-module AST reconstruction and renamed-wrapper transport proofs |
| Fixture generators | Actual payload/ELF/seed/ABI expectations, deterministic output and complete inputs | Generator-body hashes and generated C include-spelling transport |
| Work observations | Actual counted primitives, complete site/function coverage, exact placement, passivity, overflow and composition | Comparison of unrelated source against a frozen complete implementation |
| Stack tooling | Actual source/machine indirect targets, assembly epochs, finite fatal qualifications, metadata passivity, budgets and producer identity | Only unrelated historical transport around those qualifications |

This table does not authorize changing an ABI, observer primitive, finite
indirect-target set, audit boundary or error contract. Such changes remain
with their owning design task.

Current private helper names and harmless source formatting are not public
contracts. A new helper must still be correctly owned and observed when it
performs measured work. An unrelated callback-free declaration must not
require preserving an entire old header; an actual new indirect call must
still fail closed until its target set is proved.

Source-size and function-size limits remain ADR-0051 review signals. They
are not a reason to preserve an old body, manufacture a runtime refactor
inside this task, or invent an exception without review.

## Current native evidence

Retain all 47 CTest identities from the ADR-0123 base and every existing
semantic/work case. IPC01 adds `selected-ipc-validation` to complete native
acceptance; it does not replace an earlier CTest or enter the fast loop.
The slow models keep their existing transition counts and seeds. This task
does not convert a slow model into a smaller test.

The five expensive orchestrators keep their current channels:

| CTest | Current products retained |
| --- | --- |
| `selected-ownership` | Five observation slices and one ordinary control |
| `selected-address-space` | Three observation slices and one ordinary control |
| `selected-release-handoff` | Four observation slices and one ordinary control |
| `object-hart-validation` | Its two current observation channels and current ordinary control |
| `common-scheduler-return` | Current work and current ordinary control |

OBJ01's 171 current cases and OBJ02's 180 current records remain complete.
Other case sets remain those in the fixed base's current fixtures and
Accepted contracts. Existing current-stage spellings such as `green` need
not be renamed merely to remove an authoring state machine.

The ordinary current gate performs, in order:

1. bind the exact current fixture, source, generated inputs, executables and
   required compiler/symbol-tool identities;
2. verify that generated ordinary/observed source corresponds to the actual
   captured current inputs;
3. verify ordinary collector absence and observed collector linkage;
4. execute each required current channel once;
5. validate complete record shape, IDs, capture/owner composition, semantic
   outcomes, work/storage bounds and ordinary/observed agreement;
6. validate the retained current complete-audit and observation boundaries;
7. recheck consumed input/product identities before reporting success.

Do not invoke a historical build or reconstruct a claim registry in any
of these steps. Reuse an already materialized current source bundle within
that invocation instead of regenerating identical expected source for each
local question.

Measurements still describe actual operations and storage. Do not replace
workspace, plan-size, moved-record or copied/cleared-byte observations with
hardcoded success values. If an old whole-body check also supported a real
storage or measurement property, establish its narrow current check before
removing that transport identity.

### Independent expected data

Removing historical execution does not remove its useful independently
established expectations.

Preserve existing explicit bounds, independent reference-model logic and
current diagnostic contracts. Where an oracle presently reads a value from
a fixed-reference product, retain only the necessary reference fields as
fixture data with the fixed commit, case identity and Accepted provenance.
Do not retain a source implementation just to calculate those constants.

For this behavior-preserving tooling conversion, fixed reviewed #246
observations may establish regression data before implementation edits.
Capture them with the existing validated fixtures and record their source
and executable identities. They are not measurements of the edited
candidate and are not a new MINIX certificate for still-open findings.
Do not copy the candidate's output into its expected table.

Expected behavior, work inequalities and storage limits do not become looser.
Keep malformed-record, missing/duplicate-case, invalid capture, overflow,
failure-atomic output and unobserved-state controls. OBJ01's representation
observations must still distinguish rejection byte preservation from
defined-state success effects; removing an old ABI product cannot drop
non-plan outputs or turn a raw observation into a normalized success.

Primitive/site declarations remain independently reviewed descriptions of
what is measured. Instrumentation may inspect current source to locate a
declared primitive, but it may not derive the expected work bound or
semantic result from that source. Unknown or incompletely covered measured
work remains an error.

## Invocation-owned current source snapshots

Keep the useful lifetime boundary from ADR-0105, not its retirement proof.
The host runner and current fixture-generator entry points own one lazy,
process-local snapshot. Library consumers use captured current bytes.

The snapshot binds repository/worktree/PID identity, current source
membership, bytes, modes and admitted links, together with the existing
runtime/environment consistency checks. It does not contain an inventory,
prior claim, selected author journal, historical Git blob or mutable
caller-supplied success flag.

Opening captures and checks the current namespace. Local lookups do not
probe tools, materialize metadata catalogs or replay execution logs.
Before overall success, the owner verifies that the consumed namespace and
identities remain unchanged. Failure or unwinding clears the view; a failed,
closed, nested, foreign-root or inherited cross-process view cannot supply
valid results.

Preserve existing explicit temporary-fixture isolation and final staged-tree
requirements. This task does not introduce cross-process caching, a disk
cache, an environment-selected bypass or a new staging prerequisite for
ordinary dirty-tracked iteration.

For `n` source entries, `s` captured bytes and `q` local queries, sorted
namespace capture costs `O(s + n log n)`, with `O(s + n)` host storage.
Dictionary queries have expected constant lookup cost, giving expected total
`O(s + n log n + q)` plus the actual test work. There is no factor for the
number of earlier runtime corrections. Actual compiler, artifact and optional
receipt boundaries still perform their own required input checks.

## One final author plan

Actual Red and focused Green remain the normal TDD loop. Tests execute the
current worktree before and after the correction; no immutable whole-repository
pre-Red envelope or generic author event chain is required.

An oracle change invalidates the Red claim that depended on it and requires
renewed discriminating evidence. A documentation edit or unrelated test
maintenance does not automatically require abandoning a correct runtime
implementation. A real design defect still triggers ADR-0033's separate
corrective-design barrier.

The final current plan remains:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier pr --base origin/main --execute --fresh
```

Use the full tier when required by the existing planner, milestone or task.
Keep committed/staged/unstaged/untracked path union, unknown/shared-input
escalation, distinct configure variants and all three diff checks.
Do not narrow the plan by supplying explicit paths.

One actual current command result can satisfy both its normal PR requirement
and the affected audit unit it exercises. Do not run B01-B12/B14 again as a
separate retirement transaction. Do not infer that a host case covers a guest
or that one guest configuration covers another.

The PR still records exact HEAD/tree, commands, results, Red, current
coverage and independent review. Failed or not-run required evidence blocks
review. After a fix, carry forward only the explicitly demonstrated
ownership-disjoint evidence allowed by ADR-0034; otherwise rerun it.

## Fresh execution and explicit receipts

The planner remains the coverage owner. Receipt policy cannot select or
remove commands.

| Invocation | Profile collection | New reusable receipt |
| --- | --- | --- |
| Default execution or `--fresh` | No reusable-profile collection | None |
| `--reuse` | Existing exact supported-profile checks | Existing rules for a miss followed by fresh execution |
| `--producer` | Existing readiness, prelaunch and postflight closure | Every selected supported profile, one producer run |
| `--record-workflow` | Existing gate-only profile checks | Existing eligible-profile behavior; never final author evidence |

Fresh execution still uses the existing owned log/attempt lifecycle. Starting
a fresh attempt invalidates an older success before launch. Nonzero exit,
interruption, source drift, failed log finalization or store errors remain
failures. Only successful command completion, stable source and finalized
logs can produce `FRESH` and final-candidate success.

Not creating a reusable receipt is deliberate in fresh mode, not a
success-shaped postflight exception. Explicit receipt modes do not catch
identity disagreement and turn it into success. Keep exact source, tools,
environment, inputs, artifacts, raw logs, lock ownership and current-candidate
checks in those modes.

Replace historical recipe/runner inverse chains with direct current
closed-profile declarations. A recipe/tool identity may select a reviewed
input-closure model; it does not prove behavior or algorithm correctness.
Unchanged runner/tool declarations can derive directly from fixed #246.
Changed supported recipe declarations belong to this reviewed tooling
implementation and its actual closure tests, not another metadata PR.
Do not introduce a general CMake evaluator, weaken unknown-input refusal or
claim support for another platform.

No ordinary runtime task must run a same-producer/reuse exercise. A task
changing receipt behavior must test that behavior; explicit user requests
for producer evidence still apply.

## Metadata, review and correction boundaries

Keep ownership maps, build inputs, fixture schemas and observation sites with
the code or test that owns them. Their review occurs in that outcome.
There is no global digest scanner that turns an ordinary test literal into
a new authority obligation.

A change is mechanical metadata only when it preserves the actual contract:
for example, enrolling a current source dependency, updating a private helper
owner after a reviewed split, or binding an unchanged primitive at its new
location. It cannot change a caller's permission, error precedence, mutable
state, required case, work bound or indirect-target set.

The author must explain any such preserved invariant; the reviewer verifies
it. An actual new decision still receives documentation-first design review.
This is not permission to label a behavioral change "metadata."

## Implementation ownership and sequence

The implementation touches only the evidence/tooling dependency closure:

- current host test/source helpers and their consumers;
- current native fixture generators, evidence parsers/orchestrators and
  test-only fixtures where removing historical products requires it;
- the host runner and invocation-owned snapshot helper;
- validation execution and the directly dependent explicit-profile recipe
  declarations and regressions;
- CMake's affected native products, dependencies and unchanged gate ownership;
- directly related canonical documentation and template/workflow wording.

It does not edit production owners under `arch/`, `kernel/`, `include/micros/`,
`lib/runtime/`, `servers/` or `user/`. Existing target observers and QEMU
scenarios also remain behaviorally unchanged.

After the separate design merge:

1. preserve the fixed-current case/channel and necessary reference-data
   evidence before modifying its consumers; write failing snapshot,
   no-history and fresh-dispatch regressions;
2. port mixed structural checks to current invariants and remove obsolete
   transport, binding, journal and inverse machinery with all dependent
   callers; preserve independent current model/observer/stack checks;
3. make native generation and orchestration current-only with the retained
   channels and exact existing outcomes; preserve source/product binding;
4. separate fresh dispatch from explicit receipt modes and qualify their
   direct current profiles;
5. run focused gates, then one complete current author plan and independent
   implementation review before the first permanent outcome commit.

These are local implementation slices of one outcome, not parallel tasks.
Do not make a permanently broken intermediate commit or merge a temporary
compatibility layer merely to split the deletion.

## Acceptance evidence

Require focused regressions that fail under the old behavior and establish:

- current source access needs no retirement catalog, prior claim, journal
  or ancestor implementation;
- harmless changes outside an actual structural/observation contract are
  not rejected by historical body/header equality;
- current private-boundary, ABI, observation-placement and reference-model
  independence violations still fail;
- source membership/bytes/modes/environment mutation and invalid snapshot
  lifetime prevent overall success;
- all current case IDs, channels, expected failures, work inequalities and
  complete diagnostic boundaries are retained;
- no current generator/CTest imports old implementations or executes the
  removed author protocol;
- ordinary fresh execution does not collect profiles or publish a receipt,
  but still invalidates earlier receipts, retains complete logs and rejects
  failures/source drift;
- explicit producer/reuse/record behavior and its negative controls remain
  fail-closed.

Do not satisfy the no-history criterion by mocking it out in production.
Inspect the real ordinary import/call paths and exercise the actual current
native products. Reference data must identify its fixed source, not a
candidate-generated expected result.

The complete current gate retains 47 CTests, the actual remaining full
Python discovery, forty QEMU workflows, four standalone image workflows,
44 static configurations, 113 ELFs, ten observed scopes and blocked B13.
Removed Python cases must be explained as obsolete transport/author-protocol
checks, not silently reported as passed. Retained behavioral/structural
properties must have a current test owner.

Because this outcome changes receipt dispatch and supported recipe inputs,
verify actual native/smoke/TTY-image producer and reuse behavior. One complete
`--producer` run may supply the final full gate; focused fresh-dispatch
regressions supply the distinct fresh-mode claim. Do not repeat the full
matrix solely to make two aggregate reports.

Record before/after native orchestrator, full host, fast-loop and build costs
separately. Timing is diagnostic, not a flaky correctness threshold. The
deterministic acceptance condition is removal of historical proof/execution
work while retaining the current checks and real coverage above.

## Implementation and retained evidence map

The implementation removes the EV01 inventory, scope, provider, binding and
author-journal modules rather than leaving compatibility readers. No ordinary
native generator or orchestrator selects an ancestor implementation. The
remaining fixed-source reads belong to the finite compiler/observer/stack
qualifications described below, not cumulative runtime-correction admission.

| Responsibility | Current owner |
| --- | --- |
| Invocation-owned current bytes, namespace and closing checks | `tests/host/source_snapshot.py`, owned by `tools/run_host_tests.py` and generator entry points |
| Strict record fields, digests and owned regular source paths | `tests/host/evidence_records.py` |
| Shared current C and bounded CMake inspection | `tests/host/c_source_records.py`, `tests/host/build_source_records.py` |
| Complete native primitive coverage and placement | `tests/host/native_observations.py` and its independently captured JSON declarations |
| Necessary OBJ01 non-plan/effect reference fields | `tests/host/object_hart_reference.json` |
| Current native cases, work bounds and product identities | The memory/object/return/IPC `*_evidence.py` modules and their fixture generators |
| Current source sets, private dependencies, ABI/state/fixture ownership and independent models | The existing `test_*_source_layout.py` families and kernel-directory tests |
| Ordinary fresh versus explicit receipt dispatch | `tools/validation_execution.py`; current profile inputs in `tools/validation_context.py` |
| Complete twelve/eighteen-case platform execution | `tools/check_terminal_failures.py`, `tools/check_panic_traces.py` and their current capture/oracle modules |
| Compiler-derived stack closure and finite qualifiers | Existing `tools/stack_bound_*` owners, including the narrowed callback and fatal source boundaries |

### Independent reference and observation data

Before changing the consumers, the author retained the actual #246 products,
their source/executable identities, and complete current ordinary/observed
outputs. The native observation declaration covers 82 source owners,
774 compiled functions and 2,624 primitive sites. Empty site sets are
intentional coverage declarations, not exemptions for unobserved work.
The existing work bounds remain independent of these source-site records.

OBJ01's retained reference table contains only the necessary fields from its
171 independently captured current cases. It does not store a production
implementation, weaken error/output preservation, or normalize an observed
representation into success.

The nine ordinary fixture generators were also executed in the unchanged
#246 checkout before their old include-spelling wrappers were removed.
All 46 original generator assertions passed; twelve distinct outputs were
captured with source/interpreter identities. Only the six affected output
hashes and two lengths changed, using that independent capture. All fifty
current generator cases retain their payload, ELF/seed and deterministic-byte
checks. Generator implementations were not changed for this transport removal.

### Required target evidence has an ordinary owner

The existing runner accepts nine finite current-evidence scopes:

| Scope | Existing workflow and retained evidence |
| --- | --- |
| `kernel-mapping` | `test-qemu-mmu`: exact mapping records, largest legal leaves and actual ELF/load-probe binding |
| `user-address-space` | `test-qemu-user-address-space`: selected operation, release and rollback witnesses |
| `address-space-handoff` | `test-qemu-address-space-handoff`: complete handoff, wired/index and downstream witnesses |
| `scheduler-return` | `test-qemu-scheduler`: exact selected context counts, including 1/2/1 validation |
| `runtime-memory` | `test-qemu-user-runtime`: actual linked memory object, ELF/map/Ninja recipe and non-stack wide transfers |
| `selected-ipc` | `test-qemu-ipc-syscall`: all six syscall and four completion classes, complete actual work snapshots and zero ordinary complete audits |
| `bootstrap-command` | `test-qemu-bootstrap-launcher`: captured release/readiness/seal, selected context admission and zero ordinary complete audits |
| `tty-bootstrap-command` | `test-qemu-tty`: captured VM/PM/TTY/service roles, console begin, fixed UART points and sealed completion without complete command audits |
| `diagnostic-history` | `test-qemu-panic`: the unique aligned 12,012-byte local BSS history and complete seeded panic witness |

Every scope except diagnostic history requires a pass result. Diagnostic history requires panic,
the complete panic report and the seeded-history expectation. Each scope
uses the same owned `QemuResult` as its workflow, with kernel identity checks
before execution, after execution and after witness validation. Missing
witnesses or artifact drift use the normal failed TAP/raw reporting path.
They never cause a second guest to run.

The bootstrap scopes report only after the original terminal semantic
boundary. Stack-user authentication and reporting precede terminal UART
seizure for the command report, while the original handoff authority is
still intact. Their fixed context/UART invocation counts retain MEM03
scalar/geometry admission separately from complete diagnostics; the
native selected-path evidence still bounds actual PTE/owner work.

Both exact diagnostic command decoders remain exercised by
`diagnostic_control_test.c` in the ordinary `diagnostic-history` CTest.
The separate author-only decoder executable is removed. Real TTY execution
continues to require its diagnostic-forwarding witness.

The terminal and trace controllers retain complete current source/tool/
firmware manifests, artifact revalidation, private debugger authority,
all twelve/eighteen scenarios and atomic report publication. They no longer
have baseline selectors, archive workspaces or accepted historical-absence
modes, including in their internal image/capture/oracle interfaces.
Missing required control or trace state remains a negative case.

### Narrow proof-critical source qualifications

The unchanged fixed stack reference still supplies independent callback,
assembly and fatal-operation expectations. It is not replaced with the
candidate's own bytes. Fixed linked fixtures test those proof engines; they
do not rerun an old operating-system implementation as current acceptance.

Callback headers retain type/layout, preprocessor and callback-bearing API
contracts, but unrelated ordinary prototypes no longer participate in
equality. Qualified callback functions and constructors still bind to their
independent source contracts and complete actual machine occurrences,
carrier offsets and provider sets. A callback-free current helper needs its
actual regular source, exact catalog origin/object/symbol and independently
decoded machine calls, not a historical helper name. Missing or omitted
indirect sites still fail. The VM phase-lifetime qualification remains.

Fatal module checks retain shared declarations, storage, macros and types.
Unrelated function bodies are not whole-module transport obligations;
each used fatal operation is qualified separately against its actual object
and fixed source/effect contract. Hidden shared preprocessor mutation,
changed CSR/state/layout, incomplete calls, unknown effects and altered
qualifiers still fail. The observation overlays, assembly epochs, budgets,
ordinary/metadata byte equality and observed producer ownership remain.

### Python case disposition

The unchanged #246 runner lists 1,686 test identities. Complete current
discovery executes 1,058 with zero skips. There are 891 surviving names,
795 removed names and 167 added names; renames and consolidation contribute
to both latter sets. A surviving name does not imply an unchanged body.
No removed identity is reported as a pass.

The implementation diff and author census enumerate the names. The following
module-level disposition maps their useful properties to current owners;
it is a static review record, not another runtime assertion registry.
Module names below are under `tests/host/`, with the `test_` prefix omitted.

| Modules | Removed mechanism; retained current property |
| --- | --- |
| `evidence_authority`, `evidence_authority_scope` (131 cases removed) | Global catalog, predecessor, journal and provisional-retirement protocols are retired; current bytes/lifetime/failure rules belong to `source_snapshot` and `validation_execution` |
| `runtime_memory`, `bootstrap_allocation`, `kernel_mapping` | Historical compilation, author transactions and old-body identities are removed; actual current work, semantics, malformed records, compiler/access/rollback and target checks remain |
| `selected_ownership`, `selected_address_space`, `selected_release_handoff` | Ancestor products, immutable attempt envelopes and cumulative claims are removed; complete current channels, cases, diagnostic boundaries, failures, work/storage and input/product checks remain |
| `object_hart_validation`, `common_scheduler_return` | Historical plan-ABI executions and author protocols are removed; 171/180 current cases, independent non-plan/effect fields, complete record guards and actual plan/context work remain |
| `diagnostic_history`, `terminal_failure`, `panic_trace` | Author selectors, historical recipe/archive admission and old-runtime expected-failure modes are removed; current state/decoder/capture/model, packet authority, real machine and all target-scenario checks remain |
| `frame_ownership_source_layout`, `kernel_address_space_source_layout`, `kernel_vm_handoff_source_layout`, `kernel_pm_control_source_layout` | Ancestor reconstruction/migration and body/type transport are removed; current complete source sets, private boundaries, state/ABI and full-audit ownership remain |
| `kernel_object_source_layout`, `scheduler_source_layout`, `endpoint_source_layout`, `kernel_ipc_source_layout`, `grant_data_plane_source_layout` | Frozen helper/body inventories and inverse chains are removed; current owner/source sets, independent references, fixture/state custody, cases and tier/consumer bindings remain |
| `vm_service_source_layout`, `pm_service_source_layout`, `ramfs_source_layout`, `tty_source_layout`, `vfs_test_source_layout` | Completed migration tables, original bodies and generated-include transport are removed; current private graphs, storage, independent model directions, seeds/counts and actual consumers remain |
| `kernel_bootstrap_source_layout`, `trap_syscall_source_layout`, `kernel_directory_layout` | Historical path/line/body reconstruction is removed; current module boundaries, live supervisor capture, modes/headers and real native/RV64 diagnostic mapping remain |
| `user_elf_source_layout`, `qemu_runner_source_layout`, `qemu_transcript`, `generate_tty_service_fixture` | Whole-module AST and include/line transport are removed; actual parsing, CLI/result, transcript, generated bytes and current ownership remain |
| `stack_bound_evidence`, `stack_source_projection`, `stack_workflow` | The old kernel-oracle test and unrelated header/helper/recipe freezes are removed; real indirect/fatal/assembly, passivity, complete static/observed scope and new harmless-extension controls remain |
| `host_test_runner`, `validation_context`, `validation_context_layout`, `validation_execution`, `work_count_evidence` | Historical runner/recipe/module transport and automatic fresh receipts are removed; complete discovery, current recipes, strict explicit profiles, honest fresh outcomes and actual work observations remain |
| New `source_snapshot`, `native_observations`, `current_target_evidence` | Current namespace/lifetime/drift, complete independently declared primitive coverage and same-result target/artifact ownership regressions |

The frame/VM work-shape modules retain their existing cases. Whole-function
token hashes no longer mask their actual Clang-AST checks: inactive owner
fields and exact first/last bitmap-word arithmetic now have direct checks,
with the existing line-splice, shifted-start and early-end negatives.

### Measured implementation checkpoint

At staged tree `4bcf3aa8c3f7f658c6221b240e6e3193876071cc`, complete Python
discovery passed all 1,058 cases in 83.253 seconds. The actual
`riscv64-debug` ordinary/metadata pair also completed its callback, assembly,
fatal, stack and byte-passivity proofs. These are development evidence,
not the complete final stack certificate or independent review.

The [before/after study](../research/current-validation-overhead.md) separates
native orchestrators, full host discovery and fast/build measurements.
The final implementation PR supplies the exact current fifty-command
producer, supported-profile reuse and independent-review record.

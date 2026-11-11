# Current-Contract Validation

## Status and scope

This is the Accepted contract for
[ADR-0123](../adr/0123-current-contract-validation.md).
The paired tasks are `validation-simplification-design` and
`validation-simplification-implementation`. Complete independent design review
is clean. Implementation remains pending the separate design merge.

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

Retain all 47 current CTest identities and every current semantic/work case.
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

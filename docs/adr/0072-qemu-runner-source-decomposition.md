# ADR-0072: QEMU Runner Source Decomposition

- Status: Accepted
- Date: 2026-10-16
- Refines: ADR-0025, ADR-0033, ADR-0034, ADR-0051, ADR-0064, and ADR-0065
- Supersedes in part: ADR-0065's single-file QEMU argument-builder source
  declaration, only for the exact reviewed source-family transport below.

## Context

TTY implementation #144 is independently reviewed and merged as
`34f1048449c221b014530484bfec4b3c0ab51142`. Its actual squash tree equals
the reviewed tree. The complete current 47-command fresh author plan,
additional actual three-profile reuse and independent implementation review
passed; obsolete local and remote branches are deleted.

The next dependency-ready maintenance pair owns two signals:
`tools/run_qemu_smoke.py` has 2,087 lines, 46 functions, two classes and
43 module assignments; `tests/host/test_run_qemu_smoke.py` has 2,492 lines,
six test classes, 85 methods and 36 fixture assignments. The three long
functions are `matches_expected_result` at 217 lines, `parse_arguments` at
330 and `main` at 187. `ExpectedOutcomeTest` contains 64 methods.

The runner mixes immutable report vocabulary, dependency-ordered report
checks, classification and acceptance, command/symbol inspection, process
and serial ownership, argument construction and CLI orchestration.
Source organization must expose those boundaries without changing any
existing observation or failure precedence.

The [fixed MINIX responsibility trace](../research/minix-qemu-runner-source-organization.md)
classifies this as development tooling, not a new kernel subsystem.
Accepted marker, default-configuration, coverage and evidence rules remain
authoritative.

## Decision

Only after independent review, acceptance and separate merge, implement the
exact [QEMU runner source contract](../architecture/qemu-runner-source-organization.md).

The outcome has ten production Python owners, eight existing/focused test
owners and one host-only structural gate. All 46 original functions, two
classes, 43 module assignments, 85 test methods and 36 independent fixture
assignments remain accounted for. All three long functions acquire the
specified ordinary phases; no new configuration object, rule engine,
generic dispatcher or test loader is introduced.

The executable path and original callable/type/data bindings remain
available through `tools/run_qemu_smoke.py`. One enum and one frozen result
class have a single records owner and facade aliases, never duplicate type
definitions. Existing six aggregate test-class names, 85 test IDs and
single native discovery remain intact using the existing plain-case-mixin
pattern, not another discovery framework.

### Exact behavior and custody

Preserve actual raw and AST source facts, complete signatures/defaults,
marker/pattern bytes and evaluation order. Classification remains panic,
explicit failure, timeout, then clean pass/unexpected exit. Acceptance
retains its separate clean-exit, forbidden-output, ordered-report and exact
fault requirements; panic classification is not successful acceptance.

The serial loop remains one complete cohesive 77-line body with its actual
process, selector, pipes, byte buffer, deadline, write accounting and
cleanup lifetimes. Preserve the original complete-line trigger, retry
behavior after an incomplete write, timeout kill/drain, decoded output and
result fields. Do not fix or generalize its existing error boundaries.

Argument helpers act on the same parser/namespace, add the same options in
the same order, and preserve help, environment defaults, first errors and
the single parse. Main retains symbol-resolution failure handling, memory
iteration, serial-success gating, aggregate growth checks and TAP/exit
results. Its one large matching-call expression gets an ordinary same-value
forwarding helper, not a new execution/result policy.

### Reviewed argument-builder source family

The original runner's complete byte identity is
`9e54f744074d077d05b52880495b73f7c74ae1db0a6ad751f8b7024688bf79e3`.
Before changing the source declaration, finite actual-source inverses must
restore that entire original file, and its complete original module AST.
Old expected values remain independent and unchanged.

For smoke eligibility only, replace the one-file declaration with the exact
ten-member code-family declaration in the source contract. Check every
fixed member's ordinary-file identity and reviewed bytes before QEMU
metadata inspection. Preserve the existing unsupported diagnostic and
fresh-without-receipt behavior. Existing complete repository-Python
fingerprints continue to bind names, kinds, modes and bytes.

The package remains the existing namespace package: no initializer is added.
Reject source, bytecode and native-extension initializers, same-stem helper
package directories, native module alternatives and source-replacing
bytecode. A matching `.py` digest does not establish Python's chosen loader.
A fixed, source-free standard-loader metadata check after the existing
workflow-interpreter resolution and before QEMU metadata must resolve only
the repository namespace and nine expected source helpers. Reject concrete
or merged foreign namespace/module origins and unknown layouts without
importing or executing them. The facade executes directly and is not
imported back by helpers. No general import, bytecode, package or environment
resolver or replacement-code cleanup is authorized.

This narrow code-family predicate is the only admission refinement.
All compiler/QEMU default-config, firmware, environment, source identity,
locking, publication, coverage, failure and cross-HEAD rules remain exact.
No new flag, absent-config workaround or reusable workflow is added.

## Acceptance and phase barrier

Design authors documentation only. The paired implementation remains
pending until complete independent design review, acceptance and separate
merge. No executable test, model, harness, scaffold or prototype belongs to
this phase.

Implementation freezes the exact reviewed predecessor before rewriting,
starts with structural Red and independent source/AST/binding/scope/data
fixtures, and uses small green responsibility slices. All 46 original moves
and three required dispositions must finish with literal-empty migrations,
all owners real and every resulting function at most 120 physical lines.

Run focused original/discovery/CLI/serial and closure mutations, the complete
current fresh author plan, and additional actual same-original-full-producer
native/smoke/TTY-image reuse. Preserve every complete log identity and all
required guest budgets. Then obtain complete read-only independent
implementation review and merge only the reviewed head/tree, fast-forward
main and delete exact obsolete branches.

## Non-goals

No marker, regex, termination policy, expected report, classification,
acceptance, exit code, argv, subprocess, deadline, serial cleanup/retry,
firmware/configuration, environment or coverage change. No kernel/service,
directory-layout, production init/shell, compatibility serialization,
recovery, generalized rule/import framework or validation-ledger redesign.

## Design review disposition

Initial complete independent read-only review found one substantive loader
closure omission: same-stem packages or native extensions could shadow
matching reviewed source. The proposal now rejects those alternatives and
closes the fixed namespace/source origins before QEMU metadata.
Updated exact current four-command author evidence passed. Complete
independent correction review reported: "No substantive issue remains in
the corrected design." It confirmed the bounded source-free admitted-
interpreter checks close package/native/namespace shadowing before QEMU
metadata without broadening scope or crossing the implementation barrier.
Targeted reproduction: None; no validation suites were rerun.

Acceptance records that clean result. The paired implementation remains
pending until this design separately merges.

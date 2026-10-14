# ADR-0060: Kernel Bootstrap Source Decomposition

- Status: Proposed
- Date: 2026-10-14
- Related:
  [ADR-0025](0025-minix-behavioral-baseline-before-optimization.md),
  [ADR-0032](0032-fail-closed-change-aware-validation.md),
  [ADR-0033](0033-reviewed-design-before-implementation.md),
  [ADR-0034](0034-author-validation-before-review.md),
  [ADR-0043](0043-static-bootstrap-launcher.md),
  [ADR-0044](0044-bootstrap-failure-detail-encoding.md),
  [ADR-0045](0045-static-vm-bootstrap-and-handoff.md),
  [ADR-0047](0047-tty-console-handoff-and-serial-protocol.md),
  [ADR-0050](0050-post-handoff-elf-spawn.md),
  [ADR-0051](0051-responsibility-based-source-modules.md),
  [source-module audit](../architecture/source-module-audit.md),
  [exact bootstrap source contract](../architecture/kernel-bootstrap-source-organization.md),
  [MINIX source baseline](../research/minix-bootstrap-source-organization.md)

## Context

The production PM/VFS/VM coordinator is merged as PR #118. The next selected
dependency-ready correction is bootstrap source organization, a high-risk
boundary spanning immutable manifest validation, prepared service resources,
privileged release/readiness/sealing, fatal diagnostics, boot orchestration,
and their existing evidence.

At reviewed base `49706a8ef8ef2296c42dc1ecadef0df375d2ee2f`, ten audited
sources contain 12,621 physical lines and 27 C/Python functions above the
120-line review signal. More importantly, they mix independently
understandable responsibilities:

- immutable profile and manifest validation with portable runtime transitions;
- singleton state with resident preparation, patch/readback, reverse cleanup,
  target validation, publication and diagnostics;
- common captured-syscall authority with command-specific transactions;
- boot initialization with every conditional component scenario;
- launcher view validation with readiness classification and transport;
- native fixture construction with case domains and reference replay; and
- shared ELF descriptor validation with its configuration/VM-region checks.

The fixed MINIX baseline separates kernel boot identity and privilege
mechanisms from RS boot policy, initialization requests/results and SEF
startup. The accepted `micros` adaptations remain authoritative: static
preparation before handoff, exact generations and immutable profiles,
kernel-controlled publication, serial readiness, one-shot sealing, and fatal
pre-recovery failure. This decision changes their source boundaries only.

## Decision

Apply the exact ownership, dependency, context, migration and acceptance
contract in
[Kernel Bootstrap Source Organization](../architecture/kernel-bootstrap-source-organization.md).
The implementation begins only after this Proposed design is independently
reviewed, marked Accepted and merged.

### One outcome

Expose the current boot algorithm as cohesive source owners and named phases
without changing any supported interface, state transition or execution.

The design and implementation remain separate tasks and pull requests:

```text
kernel-bootstrap-decomposition-implementation
    -> kernel-bootstrap-decomposition-design
```

Only design is active in this phase. No executable tests, models, scaffolding,
prototypes, build targets or implementation changes are committed by this PR.

### Stable interfaces and behavior

Keep the exact current bootstrap/FDT public and existing private headers,
portable control decoder/core, object layouts, profiles, protocols,
operation-11 register shapes, results, diagnostics, markers and timeouts.

In particular:

- preserve complete preflight and success-only plan publication;
- preserve every argument/profile/role/image/topology diagnostic precedence;
- preserve two-pass preparation, complete zero fill and instruction fencing;
- preserve reverse cleanup and generation consumption;
- preserve VM/PM/TTY preparation and retained console deadlines;
- preserve publication, expected-caller reply, captured completion and seal
  commit order;
- preserve source-only launcher identity for already staged acknowledgments;
- preserve fatal post-publication disagreement without recovery or retry; and
- preserve all native, QEMU, generator and budget evidence.

Existing Accepted behavioral ADRs are not superseded by this source-only
decision.

### Source owners and contexts

Separate portable common/profile/manifest validation from the existing
portable runtime lifecycle. Keep the image catalog one cohesive validator
with named phases.

Separate privileged state/common, preparation, resident image/configuration
access, reverse cleanup, read-only validation, diagnostics, and facade
orchestration. One explicit target context consolidates only the ten current
mutable statics. Existing public accessors continue borrowing its one control
state and retain null-before-initialization behavior.

Separate captured-syscall common authority/fatal helpers from console/release,
readiness, completion and failure transactions. Keep plans automatic and
command owners independent from sibling handlers.

Keep one kernel entry with an automatic boot context and named foundation and
conditional test-dispatch owners. Preserve the exact compile-time branch
order rather than adding a runtime dispatch framework.

Keep one launcher entry, its three existing exported image objects, and
automatic order/state storage. Separate independent view validation,
readiness protocol and private control transport without sharing the kernel
manifest algorithm.

Preserve automatic native fixture/reference lifetimes. The three current
static catalog pages gain one explicit static-lifetime fixture context.
Separate fixture, case-domain, independent oracle and replay ownership
without creating another behavioral executable or test.

New private headers are declarations-only. Supported interfaces are not
enlarged merely to expose implementation helpers.

### Cohesive retained neighbor

`kernel/bootstrap_control_core.c` is retained byte-for-byte. Its current
27-function count is a review signal, but it owns one bounded portable
control-state lifecycle and already has named preflight/commit boundaries;
its longest function is 119 lines. Splitting that non-signaled neighbor is not
necessary to correct the audited target/manifest/evidence responsibilities.
The existing bounded inline lookup remains an explicit stable-header
exception, not a pattern for new private headers.

### Structural Red and migration

The implementation starts with
`tests/host/test_kernel_bootstrap_source_layout.py`.
It fails closed over the exact family/source sets/consumers, stable hashes,
declarations and object owners, recursive include dependencies, evidence
identities, validation inheritance, and ordered case/model/dispatch contracts.

Its initial moving-symbol inventory has 60 public/test-case entries:

- six portable runtime/classification functions;
- eight target state/validation/diagnostic entry points; and
- 46 existing manifest/control/FDT native cases.

The initial long-function inventory has exactly 27 entries. Every one receives
the semantic phase outline in the architecture contract. Both active
migrations must be empty before final validation. Moving, renaming or
reformatting the same long function does not satisfy the gate.

### Validation and review

Each extraction uses the narrow current evidence for that slice. Preserve the
manifest model seed `0x42535452504c4159`, 4,096 steps, draw/dispatch schedule,
independent reference and final-16-operation failure trace. Preserve the
existing FDT TAP case registry and native control cases.

All split paths inherit the exact declared current owner classification.
In particular, formerly unknown/full-tier native evidence does not become
host-only. Shared boot-entry/CMake inputs continue selecting the complete
native/QEMU matrix. Identical checked generator inputs produce identical
output bytes, and currently imported generator helpers keep their interfaces.

Before independent implementation review, stage the complete change, prove a
clean tracked worktree and no untracked task files, bind exact commands/results
to HEAD and `git write-tree`, and execute one authoritative PR plan. Selected
workflows are not rerun separately. Reviewers inspect exact author evidence
instead of routinely repeating suites, models, builds, QEMU or docs gates.

## Consequences

Positive consequences:

- boot policy validation and lifecycle mutation become distinct owners;
- privileged preparation and reverse cleanup have visible resource boundaries;
- release/readiness/sealing commits become easier to inspect;
- boot and launcher entry points read as their actual ordered tasks;
- native reference independence and fixture lifetimes are explicit; and
- later bootstrap changes have a fail-closed source/build/evidence contract.

Costs and risks:

- the move spans production, target, native and generation tooling;
- helper extraction can accidentally reorder diagnostics or commit stores;
- context consolidation can obscure storage lifetime if applied mechanically;
- duplicated or omitted CMake consumers can weaken coverage; and
- shared generator imports can drift even when the local fixture still builds.

Exact migration inventories, unchanged interfaces, named task phases, current
evidence and independent review mitigate those risks.

## Non-goals

This ADR adds no boot feature, service, syscall, authority, runtime state,
allocation policy, recovery/reset path, compatibility layer, application
runtime, dynamic spawn behavior, directory layout, workflow, test execution
or validation-deduplication policy.

It does not authorize unrelated endpoint, scheduler, object, grant, trap,
RAMFS, TTY, ELF-checker or QEMU-runner decompositions.

## Alternatives considered

### Leave bootstrap mixed until v0.1

Rejected. It postpones the already audited authority and task-boundary
correction and makes the next boot change harder to review.

### Split every public function into its own file

Rejected. It fragments cohesive resource lifecycles and bounded algorithms.

### Refactor portable control and all neighboring mechanisms as well

Rejected. Those existing interfaces and the cohesive portable control owner
are sufficient for the selected correction.

### Replace conditional test branches with a runtime registry

Rejected. It changes boot structure and adds generality without a requirement.

### Share kernel validation with the launcher or native reference

Rejected. Their independent validation/oracle roles would be weakened.

### Change behavior while moving code

Rejected. Behavioral defects or new authority require their own reviewed
design and regression outcome, not a source-movement shortcut.

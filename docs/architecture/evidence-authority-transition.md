# Evidence Authority Transition

## Purpose

This is the canonical EV01 design for transitioning a reviewed historical
`micros` source identity to an independent MINIX-derived correctness oracle.
It implements ADR-0082's contract; it does not authorize a production
correction.

## Goal

Provide one typed, fail-closed evidence mechanism so every later correction
can:

1. preserve historical decomposition proof until a replacement exists;
2. demonstrate the audited divergence with an independent Red oracle;
3. correct the production mechanism;
4. retire only the invalid historical identity; and
5. preserve unrelated source, recipe, ownership, artifact, and ABI evidence.

## Non-goals

EV01 does not:

- change production C or assembly;
- change a runtime data structure, algorithm, ABI, or behavior;
- add EV02 operation counters or EV03 stack tooling;
- retire any current production-body identity;
- rewrite historical reviewed commits or digests;
- create a general theorem prover or C interpreter;
- infer MINIX authority from matching names or source text; or
- permit broad source-layout gate deletion.

## Existing evidence families

The implementation inventory covers every current Python evidence owner that
freezes historical source, AST, token, data, or recipe identity, including:

- `tests/host/kernel_layout_source.py`;
- `tests/host/ast_evidence.py`;
- every subsystem `test_*_source_layout.py`;
- the frame and VM validation work-shape tests;
- generator/output identities embedded in generator tests; and
- shared CMake/source restoration identities consumed by those tests.

The inventory records individual identity keys, not merely file names. One
file may contain transport identities, permanent invariants, independent
oracles, and artifact contracts simultaneously.

To keep the declaration reviewable, one family row may own a deterministic,
closed key extractor plus explicit per-key exceptions. The validator
materializes the keys and proves:

- declared family membership equals observed family membership;
- every observed key has exactly one class and owner;
- every explicit exception names an observed key; and
- no default classification crosses authority kinds.

The design does not require a hand-maintained duplicate list of every digest.

### Closed top-level family discovery

The initial register has exact owners for these closed families:

| Family ID | Owner set |
| --- | --- |
| `authority-core` | `ast_evidence.py`, `c_source_records.py`, `kernel_layout_source.py` |
| `kernel-structure` | address-space, bootstrap, directory, IPC, object, PM-control, scheduler, trap/syscall, VM-handoff, frame-ownership, and frame/VM hot-path structural tests |
| `service-structure` | PM, VM, RAMFS, TTY, VFS, VFS-test, endpoint, grant, and ELF-spawn-model structural tests |
| `tool-structure` | QEMU-runner, user-ELF, and validation-context structural tests |
| `recipe-impact` | validation-build-impact implementation and tests |
| `fixture-artifacts` | bootstrap, PM, TTY, user-runtime, VM-handoff, and VM-scratch fixture-generator digest tests |

The initial exact owner tuples are:

```text
authority-core:
  tests/host/ast_evidence.py
  tests/host/c_source_records.py
  tests/host/evidence_authority.py
  tests/host/evidence_authority_fixture.py
  tests/host/evidence_authority_inventory.py
  tests/host/evidence_authority_types.py
  tests/host/evidence_authority_validation.py
  tests/host/kernel_layout_source.py
  tests/host/test_evidence_authority.py

kernel-structure:
  tests/host/test_frame_ownership_source_layout.py
  tests/host/test_frame_validation_hot_path.py
  tests/host/test_kernel_address_space_source_layout.py
  tests/host/test_kernel_bootstrap_source_layout.py
  tests/host/test_kernel_directory_layout.py
  tests/host/test_kernel_ipc_source_layout.py
  tests/host/test_kernel_object_source_layout.py
  tests/host/test_kernel_pm_control_source_layout.py
  tests/host/test_kernel_vm_handoff_source_layout.py
  tests/host/test_scheduler_source_layout.py
  tests/host/test_trap_syscall_source_layout.py
  tests/host/test_vm_validation_hot_path.py

service-structure:
  tests/host/test_elf_spawn_model_layout.py
  tests/host/test_endpoint_source_layout.py
  tests/host/test_grant_data_plane_source_layout.py
  tests/host/test_pm_service_source_layout.py
  tests/host/test_ramfs_source_layout.py
  tests/host/test_tty_source_layout.py
  tests/host/test_vfs_source_layout.py
  tests/host/test_vfs_test_source_layout.py
  tests/host/test_vm_service_source_layout.py

tool-structure:
  tools/validation_context.py (QEMU-runner source identity keys)
  tests/host/test_qemu_runner_source_layout.py
  tests/host/test_user_elf_source_layout.py
  tests/host/test_validation_context_layout.py

recipe-impact:
  tools/validation_context.py (recipe identity keys)
  tools/validation_build_impact.py
  tests/host/test_validation_build_impact.py

fixture-artifacts:
  tests/host/test_generate_bootstrap_fixture.py
  tests/host/test_generate_elf_spawn_fixture.py
  tests/host/test_generate_pm_service_fixture.py
  tests/host/test_generate_ramfs_service_fixture.py
  tests/host/test_generate_tty_service_fixture.py
  tests/host/test_generate_user_runtime_fixture.py
  tests/host/test_generate_vfs_service_fixture.py
  tests/host/test_generate_vm_handoff_fixture.py
  tests/host/test_generate_vm_scratch_preemption_fixture.py
```

The initial reviewed non-identity exception set is empty.

A top-level scanner derives its universe from the **current** canonical
W01/W44 Python discovery and import closure. The pinned B14 module set is a
mandatory subset, not the universe ceiling. The scanner reports any
undeclared current module that owns reviewed-base pins, frozen/original hash
tables, restoration projections, body/AST/token digest assertions, or known
identity-helper calls. Declared non-identity exceptions cover ordinary payload
hashes, and observed current modules must equal declared family owners plus
those exceptions. Omitting a new or historical family therefore fails before
per-family key extraction.

One module may appear in more than one family only when the family extractors
produce disjoint canonical fact-key sets. Duplicate ownership of one
materialized fact still fails.

Current extractors materialize individual hash-table entries and function
identities. Common `BASELINE_BODIES` tables resolve through their
`ORIGINAL_FUNCTIONS` and `SOURCE_SETS`; centralized kernel classification
facts resolve through `KERNEL_FILE_FACTS`. A bespoke unresolved fact inherits
the complete closed repository-path set declared by its structural owner, so
its projection is conservative rather than omitting a corrected source.
Historical helper detection uses an explicit qualified allowlist; an ordinary
new function that merely calls a generic `digest()` utility is not a transport
identity.

## Authority model

### Identity key

One reviewed fact has a family-independent canonical key:

```text
<owner-path>:<kind>:<symbol-or-scope>
```

Family ID is separate ownership metadata and never participates in uniqueness.

Examples of `kind` are:

- `function-body`;
- `python-ast`;
- `token-stream`;
- `file-bytes`;
- `data-table`;
- `cmake-projection`;
- `source-membership`;
- `owner-boundary`; and
- `artifact-bytes`.

Raw digest values are evidence payload, not identity keys.

### Authority class

```text
TRANSPORT_IDENTITY
PERMANENT_INVARIANT
BASELINE_ORACLE
ARTIFACT_CONTRACT
```

Every inventory row also names its primary audit unit and the minimum affected
consumer units from the closed B14 ownership/consumer ledger. It has exactly
one family owner and class. Unknown, combined, or duplicate fact ownership
fails.

### Immutable transition plan

Conceptually, one plan contains:

```text
transition_id
owner_task
owner_unit
finding_ids[]
design_commit
design_tree
design_document
affected_identity_keys[]
retained_identity_keys[]
planned_retirement_identity_keys[]
replacement_oracles[]
recertification_units[]
declared_external_inputs[]
red_command[]
green_command[]
recertification_command[]
unit_commands[]
```

`affected = retained union planned_retirement`. These sets and the owner are
immutable after design acceptance.

The validator receives a separate Accepted-design authority registry. For each
correction task it binds the complete DAG-owned finding set, owner unit, exact
merged design commit/tree/document, and canonical commands. The plan must
match that external record exactly; self-declared design or command authority
is insufficient.

The validator also receives the complete transition-ownership registry. Each
claim binds one transition ID, task, planned identity keys, oracle IDs, and
terminal retirements. Transition IDs, identity claims, and oracle IDs are
globally unique. Prior terminal retirements are removed from every later
active set, including validation without a current plan; a later plan cannot
reclaim or retain them. The current claim has
no effective retirement before terminal success and must record exactly the
planned retirement in the same `RETIREMENT_READY` candidate.

The validator derives minimum recertification units as:

```text
owner_unit
union B14
union every B-unit prefix in finding_ids
union every affected fact's primary/consumer audit units
```

The plan may add reviewed downstream units but cannot remove the derived
minimum. Planned retirement requires a nonempty derived set, and
`recertification_units` must equal the derived minimum union explicit
additions. `finding_ids` must equal the complete finding set owned by
`owner_task` in the accepted correction DAG. A task cannot borrow, omit, or
split findings across transition records.

Each replacement oracle records:

```text
oracle_id
test_id
property
replaces_identity_keys[]
authority_kind = MINIX_TRACE | ACCEPTED_ADAPTATION
               | INDEPENDENT_MODEL | EXPLICIT_BOUND
authority_reference
```

The reference names the fixed MINIX commit/path/function, exact Accepted
ADR/section, independent model owner, or explicit design bound.

### Ordered evidence events

The immutable plan has ordered:

```text
RED_OBSERVED
GREEN_OBSERVED
AUTHOR_RECERTIFIED
```

Each event records its kind, canonical command ID/argv, result, expected
failure or success property, typed `oracle_results[]`, canonical plan digest,
prior-event digest, an immutable-oracle projection, and a candidate-source
projection. Each oracle result contains `oracle_id`, `RUN` status, observed
result, and exact expected property/reason. `SKIPPED` and `NOT_RUN` are
invalid. Red, Green, and recertification name the same complete registered
oracle set.

`AUTHOR_RECERTIFIED` additionally contains typed `unit_results[]`. Each result
names one audit unit, canonical command IDs/argv, `RUN` status, pass result,
and the unit-ledger/input projection. Every unit input must have the same
value as its corresponding current-candidate input before that projection
can authorize recertification.

The validator derives two disjoint closures:

- immutable oracle closure: replacement test/model/fixture code and data,
  authority references, canonical command definitions, and their reviewed
  external inputs;
- candidate closure: every affected/retained identity owner and evidence
  payload plus affected-unit production/recipe inputs.

The canonical plan digest covers every immutable plan field. Each event digest
covers the plan digest, all prior event digests, and the event payload.
Only the event currently being formed is excluded, avoiding self-reference
without leaving plan or event semantics unbound. `RED_OBSERVED` pins the
immutable-oracle projection. Green and recertification must match that exact
projection. Red binds candidate closure to the pre-correction commit/tree;
Green and recertification must share one exact current candidate projection.
Author recertification reruns the registered Green oracle set as part of its
atomic transaction.

### State transitions

| Current state | Required input | Next state |
| --- | --- | --- |
| `TRANSPORT_ONLY` | immutable accepted plan with planned retirement and oracle provenance | `REPLACEMENT_REGISTERED` |
| `REPLACEMENT_REGISTERED` | valid `RED_OBSERVED` event | `CORRECTION_ACTIVE` |
| `CORRECTION_ACTIVE` | valid Green event, then successful provisional-retirement author recertification and event | `RETIREMENT_READY` |

State is derived, not supplied. No reverse transition or state reuse is
supported. Independent review consumes the exact `RETIREMENT_READY` tree and
remains the external merge gate.

## Validation order

The validator uses this precedence:

1. schema and enum validity;
2. unique transition, identity, and oracle ownership;
3. known finding IDs and merged design reference;
   the finding set exactly equals the declared owner task's DAG-owned set;
4. complete authority-family inventory;
5. affected/retained/planned-retirement set consistency;
6. replacement-oracle existence and independent authority declaration;
7. oracle-to-retirement coverage and identical Red/Green oracle sets;
   every Red oracle has its expected discriminating failure, and every Green
   and recertification oracle passes;
8. canonical plan/event digest chain, disjoint validator-derived closures,
   immutable-oracle equality, and Green/recertification candidate equality;
9. event order, Red, Green, and author-recertification fields;
10. exact recertification-unit result coverage and passing unit projections;
11. source-layout and recipe consistency; and
12. terminal success.

An earlier failure suppresses later interpretation. Invalid input never
returns an empty or successful transition.

## Set invariants

For one transition:

```text
affected identities != empty
affected = retained union planned_retirement
retained intersection planned_retirement = empty
effective retirement = planned_retirement only in RETIREMENT_READY
every planned-retirement identity is TRANSPORT_IDENTITY
every replacement oracle is BASELINE_ORACLE
union(oracle.replaces_identity_keys) = planned_retirement
every planned-retirement identity has at least one replacement oracle
RED_OBSERVED.oracle_ids =
    GREEN_OBSERVED.oracle_ids =
    AUTHOR_RECERTIFIED.oracle_ids =
    all registered oracle IDs
every RED_OBSERVED oracle result = expected failure
every GREEN_OBSERVED oracle result = pass
every AUTHOR_RECERTIFIED oracle result = pass
for every planned-retirement identity:
    at least one covering oracle has an expected Red failure
AUTHOR_RECERTIFIED.unit_ids = recertification_units
recertification_units includes the nonempty validator-derived minimum
every AUTHOR_RECERTIFIED unit result = RUN and pass
every unit result binds its canonical commands and ledger/input projection
RED_OBSERVED.immutable_oracle_projection =
    GREEN_OBSERVED.immutable_oracle_projection =
    AUTHOR_RECERTIFIED.immutable_oracle_projection
GREEN_OBSERVED.candidate_projection =
    AUTHOR_RECERTIFIED.candidate_projection
```

A permanent invariant or artifact contract cannot be retired through this
mechanism unless a separate Accepted ADR changes its public contract and
defines a replacement of the same authority class.

## Independence rules

A replacement baseline oracle is independent only when:

- expected topology/algorithm/order/bound comes from the fixed MINIX trace
  plus Accepted adaptation;
- the oracle does not call the production helper whose mechanism it proves;
- expected records are not generated from current production output;
- a negative fixture demonstrates that the audited divergent mechanism fails;
- mutation of the required property fails while irrelevant spelling changes
  do not; and
- operation counts or models use test-owned state.

Source inspection is permitted for ownership and topology when it parses the
required invariant independently. Whole-body equality is transport evidence,
not independence.

## Representative positive transition

EV01 implementation includes one synthetic evidence family:

1. two transport identities and one permanent invariant are green;
2. one replacement oracle and planned retirement are registered for one
   transport identity;
3. malformed/missing Red evidence blocks activation;
4. Red and Green cover the exact registered oracle;
5. author recertification provisionally suppresses only the planned identity,
   succeeds, emits its event, and derives `RETIREMENT_READY`;
6. the second transport identity and permanent invariant remain required; and
7. unknown or candidate-derived expectations fail.

The fixture exercises the state machine without retiring a real production
identity.

## Repository ownership

The implementation is expected to own:

- one project-owned public authority facade under `tests/host/`, with private
  type/digest, validation, and synthetic-fixture modules split by
  responsibility;
- one native Python test module for positive and negative transitions;
- one complete inventory declaration under `tests/host/`; and
- narrow updates to existing structural tests only where needed to expose
  stable identity keys.

No production source, build target, CTest identity, QEMU workflow, timeout,
marker, validation tier, or reusable evidence profile changes.

The complete inventory stays directly importable by native tests. It does not
become a generated file, ignored cache, or mutable external database.

## Failure boundaries

- Missing family coverage fails inventory validation.
- A digest discovered only at runtime without a declared identity key fails.
- A transition may not silently absorb a newly discovered identity.
- An old transport assertion remains active during ordinary execution until
  `RETIREMENT_READY`.
- Before terminal state, planned retirement is visible only inside the exact
  author-recertification transaction after Green; failure restores ordinary
  active assertions and emits no recertification event.
- Retirement does not delete historical payload or reviewed-base provenance.
- A replacement test renamed or removed without updating its plan fails.
- An affected correction with no transition blocks implementation review.
- Reviewer approval cannot substitute for Green author evidence, and author
  evidence cannot substitute for independent review.

## Acceptance evidence

### Native Red/Green evidence

The EV01 implementation starts with native Python cases for:

- complete current-family inventory;
- valid class/state declarations;
- duplicate and unknown identity rejection;
- missing finding/design/oracle rejection;
- invalid set partitions;
- candidate-derived expectation rejection;
- premature retirement rejection;
- representative activation and retirement;
- preservation of unrelated identities; and
- missing source-layout family rejection.

### Documentation and structure

The final candidate proves:

- ADR-0082 is Accepted;
- every current authority family is classified exactly once;
- the correction DAG still selects EV02 after EV01;
- no production or executable workflow recipe changed;
- documentation checks pass;
- staged/worktree/branch checks pass on the exact tree; and
- independent implementation review is clean.

## Later correction protocol

Each later correction design names its identity transition. Its implementation
PR records:

```text
transition ID
local Red command/result
Green command/result
retired identity keys
retained identity keys
replacement oracle IDs
affected audit recertification
exact reviewed tree
```

If implementation exposes an unregistered affected identity or an incomplete
oracle, that implementation stops and returns to a corrective design task.

## References

- [ADR-0082](../adr/0082-evidence-authority-transition.md)
- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [B14 evidence audit](../research/minix-complete-baseline-b14-evidence.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)
- [Testing strategy](../testing-strategy.md)
- [AI-native workflow](../development/ai-native-workflow.md)

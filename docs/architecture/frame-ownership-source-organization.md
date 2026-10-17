# Frame-Ownership Source Organization

This document defines the exact behavior-preserving source decomposition for
the bootstrap frame allocator evidence and typed frame-ownership ledger. It
applies ADR-0051 to the third correction identified by the repository
source-module audit.

Implementation must not begin until
[ADR-0054](../adr/0054-frame-ownership-source-decomposition.md) is Accepted
and its design pull request is merged.

## Reviewed base

The reviewed source base is:

```text
fac35da1fe54489b34a13a1c9929029d5660ce72
```

The fixed local MINIX baseline and required `micros` adaptations are recorded
in
[the research note](../research/minix-frame-ownership-source-organization.md).

## Current inventory

The audit signal covers four files:

| File | Physical lines | Functions | Longest function |
| --- | ---: | ---: | --- |
| `kernel/frame_ownership.c` | 1,798 | 43 | `vm_process_set_preflight`, 136 |
| `kernel/frame_ownership_test.c` | 683 | 8 | `micros_frame_ownership_runtime_run_self_test`, 500 |
| `tests/host/frame_ownership_test.c` | 3,619 | 38 | `test_matches_seeded_reference_model`, 493 |
| `tests/host/frame_allocator_test.c` | 639 | 8 | `test_matches_seeded_reference_model`, 156 |
| **Total** | **6,739** | **97** | **15 functions above 120 lines** |

The physical-line and function-size values are review signals. The boundaries
below follow allocator/ledger authority, lifecycle transitions, transaction
phases, model ownership, and evidence scenarios rather than numeric size.

## Behavioral preservation

The decomposition preserves all Accepted behavior in ADR-0017, ADR-0020,
ADR-0045, and ADR-0050.

### Stable public contract

`include/micros/frame_ownership.h` remains byte-for-byte stable. The
implementation does not change:

- owner kinds, handoff targets, phases, errors, constants, or numeric values;
- owner, ledger, snapshot, or VM plan structure layout;
- public operation signatures;
- allocator pointer and immutable geometry binding;
- exact process-slot and generation identity;
- output preservation on failure;
- preflight/commit separation; or
- the external serialization and SIE-clear requirements of target wrappers.

### Initialization and validation

- Initialization still requires zeroed ledger storage and one pristine,
  initialized allocator with nonempty valid geometry.
- The immutable managed-range snapshot remains authoritative for
  physical-address/index translation.
- Invalid allocator counts, active tuples, unused tuples, trailing bitmap
  bits, owner records, handoff targets, phase, or aggregate counts remain
  explicit invariant failures.
- Validation remains read-only and never repairs state.
- Lookup and process counting publish outputs only after complete successful
  resolution.

### Bootstrap mutation

- Allocation remains bootstrap-only, lowest-free-frame deterministic, and
  failure-atomic across the allocator and ledger.
- Exact-owner release remains bootstrap-only and rejects wrong identity,
  unaligned, unmanaged, free, or corrupted state before allocator mutation.
- Selected process-frame release validates the complete bitmap and aggregate
  counts before one bounded non-failing commit.
- No target path may bypass the ledger after initialization.

### One-way handoff

- Per-frame handoff staging keeps exact current-owner validation and allowed
  transition classes.
- Complete wired-set staging remains all-or-nothing across every
  `PROCESS_USER` frame.
- Handoff completion first validates the whole ledger and computes resulting
  owners and counts without mutation.
- The first owner mutation begins a non-failing bounded commit that clears
  every plan and publishes `HANDED_OFF` last.
- Temporary and unclassified process-user frames continue to block the
  transition.

### Post-handoff VM transitions

- VM claim validates one exact managed free frame before a non-failing
  `VM_TRANSFERABLE` commit.
- Transferable rollback release validates the complete distinct set before
  releasing any frame.
- Freeze validates exact transferable data frames, exact free page-table
  frames, disjointness, capacities, and process generation before converting
  the complete set.
- Process release validates exact `VM_WIRED` data and
  `PROCESS_PAGE_TABLE` frames for one generation before releasing any frame.
- The existing prevalidated commit functions remain non-failing and perform
  no callback, allocation, owner resolution, or validation after mutation
  begins.

### Evidence

- Native allocator and ledger tests retain every assertion, seed, operation
  count, random draw, case order, test name, and pass/fail output.
- The target test retains its runtime setup, short-circuit order, owner and
  object checks, exact pass record, timeout, and SBI result.
- Source decomposition creates no new executable, CTest, custom target,
  workflow, marker, seed, or duplicate test run.

## Production source map

| Path | Owned responsibility |
| --- | --- |
| `kernel/frame_ownership.c` | Owner construction, process/owner predicates, pristine allocator binding, immutable geometry and frame-index primitives, initialized-state checks, low-level allocator-bit primitives, and ledger initialization |
| `kernel/frame_ownership_validation.c` | Complete allocator-ledger validation, read-only lookup and process counting, handed-off phase preflight, distinct-frame validation, and exact VM process-set resolution |
| `kernel/frame_ownership_bootstrap.c` | Bootstrap allocation, exact release, and atomic selected process-frame release |
| `kernel/frame_ownership_handoff.c` | Per-frame plan staging, complete wired-process-user staging, resulting-owner calculation, and irreversible handoff preflight/commit |
| `kernel/frame_ownership_vm_claim.c` | Post-handoff free-frame claim preflight and prevalidated commit |
| `kernel/frame_ownership_vm_freeze.c` | Exact data/table capacity checks and prevalidated transferable-to-process freeze commit |
| `kernel/frame_ownership_vm_release.c` | Transferable rollback release and exact process-owned data/table release |
| `kernel/frame_ownership_internal.h` | Sole private production contract shared by the frame-ownership modules |

`frame_ownership.c` is the low-level ledger leaf. It owns canonical encoding,
allocator geometry, address/index translation, and primitive bit changes but
does not own lifecycle orchestration.

`frame_ownership_validation.c` may inspect every allocator and ledger field.
It may fill a caller-owned preflight plan only after complete read-only
validation. It does not commit bootstrap, handoff, claim, freeze, or release
operations.

### Accepted complete-validation execution correction

Accepted
[ADR-0076](../adr/0076-bounded-complete-frame-ownership-validation.md)
preserves this complete read-only authority while correcting its measured
target execution shape.

The active range remains frame-ordered but derives allocation bits from one
loaded bitmap word per 64-frame chunk. The inactive range still checks every
owner and handoff target, while allocator bits beyond the managed count are
checked by a partial-word mask plus one check per later bitmap word.

The inactive owner/handoff loop has no out-of-line calls. The validator does
not cache success, skip inactive storage, lower the fixed capacity, or change
its error result. `frame_ownership.c` retains the low-level allocator-bit and
owner predicates for other owners; `frame_ownership_validation.c` owns the
bounded complete scan.

Implementation evidence must preserve every existing corruption case and add:

- managed-count boundaries at 1, 63, 64, 65, the observed 28,904 frames,
  maximum capacity minus one, and maximum capacity;
- independent active owner/target/bit; inactive generation, slot, kind,
  reserved, and target; and partial-word/final-partial-word/later-word
  corruption;
- a host structural proof that rejected per-frame helper calls or raw
  frame-indexed active/inactive bitmap access cannot return, that one loaded
  word supplies each active chunk, and that the boundary mask plus
  word-indexed tail scan exists;
  and
- one no-retry `test-qemu-vfs` then `test-qemu-elf-spawn` reproduction before
  the complete producer.

The dependent implementation realizes this contract through file-local
`validate_active_frames()` and `inactive_state_is_zero()` owners. No public
symbol, private-header declaration, structure, capacity, or error mapping
changes.

## Stable public-symbol ownership

| Owner | Public symbols |
| --- | --- |
| `frame_ownership.c` | `micros_frame_owner_make_kernel`, `micros_frame_owner_make_process`, `micros_frame_ownership_initialize` |
| `frame_ownership_validation.c` | `micros_frame_ownership_lookup`, `micros_frame_ownership_count_process`, `micros_frame_ownership_validate` |
| `frame_ownership_bootstrap.c` | `micros_frame_ownership_allocate`, `micros_frame_ownership_release`, `micros_frame_ownership_release_process_set` |
| `frame_ownership_handoff.c` | `micros_frame_ownership_prepare_handoff`, `micros_frame_ownership_prepare_wired_process_user_set`, `micros_frame_ownership_complete_handoff` |
| `frame_ownership_vm_claim.c` | `micros_frame_ownership_vm_claim_preflight`, `micros_frame_ownership_vm_claim_commit_prevalidated` |
| `frame_ownership_vm_freeze.c` | `micros_frame_ownership_vm_freeze_preflight`, `micros_frame_ownership_vm_freeze_commit_prevalidated` |
| `frame_ownership_vm_release.c` | `micros_frame_ownership_vm_release_transferable_preflight`, `micros_frame_ownership_vm_release_transferable_commit_prevalidated`, `micros_frame_ownership_vm_release_process_preflight`, `micros_frame_ownership_vm_release_process_commit_prevalidated` |

The QEMU test symbol
`micros_frame_ownership_runtime_run_self_test` remains owned by
`kernel/frame_ownership_test.c`.

## Private contract and state

`kernel/frame_ownership_internal.h` contains declarations and shared private
types only. It has no function bodies, mutable definitions, or target runtime
dependency.

The private contract may expose only the low-level facts required by at least
two accepted owners:

- byte clear/copy and exact owner comparison;
- process-handle, owner, allocatable-owner, and handoff-target predicates;
- initialized-state and geometry checks;
- physical-address/frame-index translation;
- allocator-bit query and prevalidated set/clear primitives;
- handed-off validation and distinct-frame-set resolution; and
- shared local candidate types when a public plan is insufficient.

File-local helpers remain `static`. An operation-specific helper used by only
one transition does not enter the private header.

The split introduces no new mutable production global. The authoritative
allocator pointer, immutable geometry, owner records, handoff targets, counts,
and phase remain inside the caller-provided
`struct micros_frame_ownership`.

## Dependency graph

The permitted internal edges are:

```text
frame_ownership_validation -> frame_ownership

frame_ownership_bootstrap -> frame_ownership
frame_ownership_bootstrap -> frame_ownership_validation

frame_ownership_handoff -> frame_ownership
frame_ownership_handoff -> frame_ownership_validation

frame_ownership_vm_claim -> frame_ownership
frame_ownership_vm_claim -> frame_ownership_validation

frame_ownership_vm_freeze -> frame_ownership
frame_ownership_vm_freeze -> frame_ownership_validation

frame_ownership_vm_release -> frame_ownership
frame_ownership_vm_release -> frame_ownership_validation
```

No mutation owner calls another mutation owner. Validation never calls an
operation handler. Freeze and release share read-only exact-set resolution
through validation rather than depending on one another.

The source graph remains target-independent. Target interrupt serialization,
object-registry resolution, address-space transitions, and VM syscall policy
remain in their existing runtime owners outside this correction.

## Transaction and failure boundaries

Every operation keeps its current ordered shape:

1. validate arguments and phase;
2. validate immutable geometry and whole-state invariants when currently
   required;
3. resolve every frame and exact expected owner into a local candidate;
4. validate distinctness, capacities, and aggregate counts;
5. publish a plan output only after complete successful preflight;
6. perform the bounded prevalidated commit without fallible work; and
7. update aggregate counts and phase at the current publication boundary.

Splitting a function must not:

- change the first returned error;
- write an output earlier;
- mutate before the complete current preflight;
- replace exact owner comparison with owner-class comparison;
- weaken stale-generation or distinct-set checks;
- add validation after a non-failing commit starts;
- turn an invariant failure into a recoverable success; or
- add a success-shaped fallback.

## Native frame-allocator evidence

The `frame_allocator_test` executable retains one execution and uses:

| Path | Owned responsibility |
| --- | --- |
| `tests/host/frame_allocator_test.c` | Exact current case order, TAP-like output, and one explicit test context |
| `tests/host/frame_allocator_test_cases.c` | Range normalization, reservation subtraction/merge, deterministic allocation/reuse, invalid input, and capacity-boundary cases |
| `tests/host/frame_allocator_model_test.c` | Fixed seed, independent managed/allocated reference state, exact operation drivers, coverage, and final result |
| `tests/host/frame_allocator_test_internal.h` | Private context and case-runner declarations |

The model draws random input in the same order and retains 2,000 steps and
seed `0x017a110c`.

## Native frame-ownership evidence

The `frame_ownership_test` executable retains one execution and uses:

| Path | Owned responsibility |
| --- | --- |
| `tests/host/frame_ownership_test.c` | Exact current 14-case order and TAP-like output |
| `tests/host/frame_ownership_test_fixture.c` | Sole mutable test context, owner constructors, allocator/ledger initialization, snapshots, release bitmap, and failure-atomicity helpers |
| `tests/host/frame_ownership_test_bootstrap.c` | Owner construction, initialization, bootstrap allocation/release, rollback, and process-set release cases |
| `tests/host/frame_ownership_test_validation.c` | Geometry corruption, independent ledger corruption, and maximum-capacity cases |
| `tests/host/frame_ownership_test_handoff.c` | Per-frame handoff, complete wired-set, irreversible commit, and post-phase rejection cases |
| `tests/host/frame_ownership_test_model.c` | Fixed reference owner/target/phase state, transition drivers, comparison, and exact 4,096-step trace |
| `tests/host/frame_ownership_test_vm.c` | Post-handoff claim, transferable rollback, freeze, process release, and preflight atomicity cases |
| `tests/host/frame_ownership_test_internal.h` | Private context, case-runner, and fixture declarations |

`frame_ownership_test_fixture.c` defines exactly one static
`frame_ownership_test_context`. Every behavior and model runner receives its
address explicitly. No split source defines another allocator, ledger,
snapshot, bitmap, or random state.

The model retains:

```text
seed = 0x20f0a11c
steps = 4096
handoff step = 3072
full validation interval = 64
```

The trace owner draws each random value exactly once and passes it to the
selected semantic operation driver.

## QEMU frame-ownership evidence

The target test uses:

| Path | Owned responsibility |
| --- | --- |
| `kernel/frame_ownership_test.c` | Public self-test entry, scenario order, final Boolean result, and interrupt save/restore |
| `kernel/frame_ownership_test_fixture.c` | Sole static target-test context, snapshots, owner/frame comparisons, and physical-address/index helpers |
| `kernel/frame_ownership_test_lifecycle.c` | Runtime baseline, generation reuse, substitute registry, owner construction, allocation, stale/foreign rejection, exact lookup, and process-release blocking |
| `kernel/frame_ownership_test_handoff.c` | Forbidden plans, failure-atomic registry mismatch, wired/transferable staging, commit, final ownership/counts, sealed-phase rejection, and final validation |
| `kernel/frame_ownership_test_internal.h` | Private target-test context and scenario declarations |

The entry remains a short-circuit orchestration. It acquires one context from
the fixture owner, invokes lifecycle then handoff scenarios, restores SIE
exactly once, and returns the result. It does not duplicate runtime or object
state.

The exact target report remains:

```text
MICROS_FRAME_OWNERSHIP_TEST_PASS stale=rejected release=blocked handoff=atomic invariants=preserved
```

## Stable evidence identities

The implementation preserves:

- native target `frame_allocator_test`;
- CTest `frame-allocator`;
- native target `frame_ownership_test`;
- CTest `frame-ownership`;
- configure preset `riscv64-frame-ownership-test`;
- workflow and target `test-qemu-frame-ownership`;
- the exact readiness and pass records;
- the current eight-second timeout; and
- inclusion of both native tests in `test-unit-fast`.

No new execution identity is introduced merely because a source is split.
The sole new test source is the structural Python gate below; it runs inside
the existing host-unit discovery and does not add a behavioral execution.

## Build ownership

`CMakeLists.txt` defines exact ordered reusable source sets:

```text
MICROS_FRAME_OWNERSHIP_SOURCES
MICROS_FRAME_ALLOCATOR_HOST_TEST_SOURCES
MICROS_FRAME_OWNERSHIP_HOST_TEST_SOURCES
MICROS_FRAME_OWNERSHIP_QEMU_TEST_SOURCES
```

`MICROS_FRAME_OWNERSHIP_SOURCES` is expanded exactly once by:

```text
frame_ownership_test
vm_bootstrap_test
pm_test
vm_core_test
micros
```

The allocator host set is expanded once by `frame_allocator_test`. The
ownership host set is expanded once by `frame_ownership_test`. The QEMU set is
expanded once under `MICROS_BUILD_FRAME_OWNERSHIP_TEST`.

`kernel/frame_allocator.c`, kernel object dependencies, and target runtime
sources remain separate inputs. No split source is also listed directly
beside its source-set expansion.

## Validation-plan ownership

`tools/validation_plan.py` records path-classification equivalents:

- every new production source and `frame_ownership_internal.h` inherits
  `kernel/frame_ownership.c`;
- every new target-test source and its private header inherits
  `kernel/frame_ownership_test.c`;
- every new native ownership-test source and private header inherits
  `tests/host/frame_ownership_test.c`; and
- every new native allocator-test source and private header inherits
  `tests/host/frame_allocator_test.c`.

Production paths therefore retain the existing fail-closed complete QEMU and
slow-model ownership. Target-test paths retain the isolated
`test-qemu-frame-ownership` ownership. Host-test paths retain their existing
plan behavior.

The structural source-layout test itself is added to
`HOST_UNIT_ONLY_INPUTS`, selecting only the fast native unit gate plus diff
checks.

## Structural Red gate

Implementation begins with:

```text
tests/host/test_frame_ownership_source_layout.py
```

The test initially fails against the accepted layout. Its first Green slice
adds the exact paths, declarations-only private headers, ordered source sets,
and minimal empty translation units while all runtime behavior remains in the
current owner files.

The gate verifies:

- exact production and test paths;
- exact ordered CMake source-set membership and one expansion by each
  accepted consumer;
- no textual C inclusion;
- no production inclusion of test-private headers;
- declarations-only private headers;
- exact public and target-test symbol ownership;
- one mutable context owner for each split test family;
- unchanged executable, CTest, preset, workflow, marker, timeout, seed, step,
  and case-order identities;
- exact validation-plan path-classification inheritance;
- an exact 17-entry public-symbol owner migration map; and
- an exact 15-entry long-function migration allowlist.

Both migration collections may only shrink and must be empty in the final
implementation. A function cannot evade the review signal by being renamed
or moved unchanged.

## Required function disposition

Every current function above 120 physical lines is non-atomic. The
implementation must expose the listed semantic phases. Helper names may be
more concise, but the phases may not be recombined.

| Function and current lines | Required subtask outline |
| --- | --- |
| `micros_frame_ownership_release_process_set` (`kernel/frame_ownership.c:885-1010`, 126) | Argument/phase/process validation; whole-ledger validation; selected-frame owner/count preflight; aggregate bounds; bounded bitmap/owner/target commit; aggregate count commit |
| `vm_process_set_preflight` (`kernel/frame_ownership.c:1455-1590`, 136) | Argument/process/phase validation; expected-owner construction; data-frame resolution, duplicate, and owner checks; table-frame resolution, cross-set distinctness, and owner checks |
| `micros_frame_ownership_runtime_run_self_test` (`kernel/frame_ownership_test.c:184-683`, 500) | Acquire and validate live baseline; create generation-reused and substitute registries; construct owners and allocate frames; reject stale/foreign/release operations; verify exact lookup and alternate-registry atomicity; stage and commit handoff; verify final owners/counts; reject sealed mutations; final validation and interrupt restoration |
| `test_constructs_exact_owner_records` (`tests/host/frame_ownership_test.c:226-394`, 169) | Valid kernel owners; valid process owners and boundary handles; invalid kernel-kind matrix; invalid process-kind matrix; invalid process handles; null output rejection |
| `test_initializes_once_on_pristine_storage` (`tests/host/frame_ownership_test.c:396-524`, 129) | Pristine initialization and snapshot binding; duplicate initialization preservation; nonzero-storage preservation; non-pristine allocator preservation |
| `test_allocates_looks_up_and_releases_exact_owners` (`tests/host/frame_ownership_test.c:598-778`, 181) | Deterministic allocation; exact lookup and process count; wrong-owner preservation; release/reuse; alignment and range rejection; final zero-state validation |
| `test_atomically_releases_process_owned_sets` (`tests/host/frame_ownership_test.c:1100-1387`, 288) | Fixture allocation; empty-set no-op; argument/unmanaged/free/foreign/stale/wrong-class rejection; corrupted aggregate rejection; successful complete-set commit; post-handoff phase rejection |
| `test_stages_and_atomically_commits_handoff` (`tests/host/frame_ownership_test.c:1389-1785`, 397) | Fixture allocation; forbidden plan matrix; incomplete and temporary-state rejection; plan clear/replace; irreversible commit; final owner/count verification; post-phase mutation rejection |
| `test_atomically_stages_complete_wired_set` (`tests/host/frame_ownership_test.c:1787-1987`, 201) | Fixture allocation; null/missing/extra/free/unmanaged/word-count rejection; existing-plan rejection; atomic complete staging; duplicate staging rejection; commit; post-phase rejection |
| `transition_model_to_handed_off` (`tests/host/frame_ownership_test.c:2511-2672`, 162) | Construct a guaranteed not-ready state; prove failed completion is byte-preserving; remove temporary owners; stage every remaining process-user owner; commit; update and compare the reference model |
| `test_matches_seeded_reference_model` (`tests/host/frame_ownership_test.c:2674-3166`, 493) | Initialize production/reference state; draw once per step; dispatch allocation, release, lookup, plan, release-set, count, wired-set, or completion; update reference state; periodic complete comparison and validation |
| `test_commits_post_handoff_vm_ownership_lifecycle` (`tests/host/frame_ownership_test.c:3168-3371`, 204) | Build frame lists and hand off; claim/release rollback frame; claim data set; freeze data/tables; verify owners and counts; release exact process set; verify final zero state |
| `test_post_handoff_vm_ownership_preflight_is_atomic` (`tests/host/frame_ownership_test.c:3373-3542`, 170) | Bootstrap-phase output preservation; seed transferable data; reject duplicate rollback; reject duplicate/cross-set freeze; perform valid freeze; reject wrong-process release with byte-exact preservation |
| `test_rejects_invalid_inputs_without_changing_state` (`tests/host/frame_allocator_test.c:217-347`, 131) | Establish baseline snapshot; null and capacity rejection; overflowing/zero/empty-range rejection; state-preservation comparison; allocate/release null argument rejection |
| `test_matches_seeded_reference_model` (`tests/host/frame_allocator_test.c:439-594`, 156) | Initialize managed/reference state; draw and classify one operation; derive expected allocation or release; execute and update reference state; compare free count; verify operation coverage |

## Implementation sequence

The implementation proceeds as small green commits:

1. Add the structural Red gate, exact source sets, private-header skeletons,
   empty target paths, and complete migration collections.
2. Split the native frame-allocator deterministic and model evidence.
3. Split ledger core, complete validation, bootstrap mutation, and matching
   native fixture/bootstrap/validation evidence.
4. Split handoff planning/commit and matching native handoff evidence.
5. Split VM claim, freeze, and release transitions plus native VM and model
   evidence.
6. Split the QEMU fixture, lifecycle, and handoff scenarios; empty both
   migration collections.
7. Run the authoritative pull-request validation plan once and obtain the
   independent implementation review.

Each permanent commit remains green. During extraction, run only the narrow
existing structural/native gate for the active slice. Run
`test-qemu-frame-ownership` when the target evidence is split. Before review,
run `tools/validation_plan.py --tier pr --base <review-base> --execute` once;
do not separately rerun workflows already selected by that plan.

## Non-goals

This design does not authorize:

- public API, ABI, structure, enum, owner-kind, phase, or error changes;
- new allocator, handoff, claim, freeze, release, VM, or process behavior;
- direct user-space ledger mutation;
- paging, reclaim, overcommit, sharing, copy-on-write, recovery, SMP, or
  migration;
- changes to target runtime serialization or process-resolution authority;
- decomposition of VM handoff, PM control, VM service, address-space, or
  bootstrap sources outside the four audited signal files;
- new behavioral executables, CTests, workflows, seeds, markers, timeouts, or
  duplicate behavioral executions beyond the authorized structural
  source-layout test; or
- mechanical splitting based only on line count.

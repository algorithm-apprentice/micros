# Validation Feedback Cost and Required Test Boundaries

## Status and fixed references

This is diagnostic research for `validation-efficiency-design`, not an
implemented optimization or successful acceptance of the interrupted MEM03
candidate. The user prioritized usable validation feedback before further
runtime development.

The committed reference is:

```text
micros HEAD: 80941ff270b5d75d7edfce65ca96f28e6501f20d
micros tree: 5c43367e822884feb8e47031b4a9772662dd2d19
MINIX HEAD:  4db99f4012570a577414fe2a43697b2f239b699e
```

The measured, uncommitted MEM03 candidate has tree
`56f8545c457b90acab9e1dc58b51ec2361441f3d` and source digest
`c15976295eba2ae2b4d601b7d80e9310dd6b52f54c77ccc26a75099728eea625`.
It is preserved in a reference stash with exactly matching index and
worktree trees, not a permanent implementation commit:
`3ead620cdbdf3ed301178a35eebdf28e35a6e298`.
Its complete patch is 353,522 bytes, SHA-256
`2814c545e64bcd9cce5d3451dd7ff7ec23b2ad7f9651b76349f12d64562274c0`.

The earlier MEM03 candidate at tree
`72822d4e36920b732a2e47198c09d54f4a933a0b` completed all fifty fresh author
commands. Independent review then found a real C launcher omission under
ADR-0075. The corrected candidate added the pre-exec environment rejection,
its failing-then-passing regression and directly related documentation.
Its twenty production paths remained byte-identical.

The corrected actual Red/Green and thirteen-unit recertification passed.
The user-directed stop interrupted its subsequent full producer. Neither
the earlier tree's full result nor the interrupted run is current complete
acceptance. MEM03 remains blocked and unpublished.

## What actually consumed the time

The earlier successful Python discovery ran 1,470 tests in 18,841.945
seconds: approximately five hours fourteen minutes. The corrected suite
contains 1,471 tests. Test discovery was inspected without executing tests.

In the interrupted run:

- all 43 CTests passed in 214.22 seconds;
- the remaining process was `unittest discover -s tests/host -p test_*.py`,
  not a reviewer or a QEMU guest;
- the process was CPU-active on one core;
- its default progress stream contained 389 completed-test dots when the
  expensive source-owner case was identified; and
- the full validation controller wrote subprocess output to its attempt log
  and replayed that log only after workflow completion.

CPU activity alone does not establish useful progress or justify the cost.
The identification above also used the actual append-only progress stream,
the unchanged discovery order and the inspected test body.

The relevant case was
`KernelAddressSpaceSourceLayoutTest.test_scratch_storage_has_one_configured_owner`.
Its intended assertion is small: the existing reachability bitmap, release
bitmap and busy flag each have one configured source owner.

At the fixed source, the method performs three passes over nineteen
production sources. Five of those sources have MEM03-retired file identities:

```text
kernel/user_address_space.c
kernel/user_address_space_validation.c
kernel/user_address_space_handoff.c
kernel/user_address_space_vm_scratch.c
kernel/user_address_space_vm_freeze.c
```

The three passes therefore cause fifteen MEM03 identity queries. These are
not fifteen independent behavioral or hardware experiments.

## Measured query and complete call path

The ordinary read path is:

```text
source-owner assertion
-> kernel_layout_source.read_path / read_bytes_path
-> read_logical_bytes / _restored_kernel_bytes
-> selected_ownership_authority.identity_required
-> retired_identity_keys
-> capture_author_context
-> read_journal, which already invokes validate_journal
-> validate_journal again
-> recheck_context, which captures the context again
```

The archived MEM03 resolver contains the final steps above. The same
uncommitted-author pattern already exists on the committed reference in:

- `tests/host/diagnostic_history_authority.py:identity_required`;
- `tests/host/bootstrap_allocation_authority.py:identity_required`; and
- `tests/host/kernel_mapping_authority.py:identity_required`.

Committed claim lookup is cheaper than an author-journal lookup, but still
reconstructs the current inventory and validates the registry on repeated
affected-key queries. A future implementation must address the actual
ownership boundary rather than optimizing only one benchmark spelling.

One read-only query of the following actual key took 86.935678 seconds:

```text
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/user_address_space.c/2
```

It correctly returned that the historical identity was no longer required.
It did not compile or execute a native fixture, run QEMU, or build an image.
The full suite was running concurrently, so this is not an isolated timing
benchmark or a permanent correctness threshold.

A second, independent-process observation measured the existing function
boundaries without editing their source. It took 85.506 seconds:

| Existing operation | Calls | Inclusive seconds |
| --- | ---: | ---: |
| `capture_author_context` | 2 | 42.245 |
| `validate_journal` | 2 | 43.181 |
| `validate_execution_log`, inside those journal validations | 6 | 43.022 |
| `current_inventory`, inside the context captures | 2 | 6.902 |
| `validate_scaffold`, inside the context captures | 2 | 0.074 |
| `read_journal`, including its own journal validation | 1 | 22.723 |
| `recheck_context`, including its second capture | 1 | 20.675 |

Nested inclusive durations must not be added together. The two context
captures and two journal validations explain essentially the whole query.
This is not a five-hour CPU profile, and it does not assign an exact fraction
of the entire suite to this one path.

The context reconstruction is not merely reading a few C files.
`diagnostic_history_evidence.closed_tool_inputs`, consumed by the memory
authority owners, resolves compiler, linker, native tools, Python, libraries
and QEMU firmware and fingerprints the complete admitted external trees.
Those are appropriate inputs at an execution-proof boundary. Repeating that
closure while a source-owner test asks another local question is the wrong
granularity.

For the fifteen affected reads in the one source-owner case, the inspected
control flow repeats thirty complete journal validations and ninety execution
log validations. Multiplying the observed query scale by fifteen illustrates
roughly twenty-one minutes of work; that arithmetic is not a separately
measured test duration.

The retained diagnostic artifacts are:

```text
mem03-review-c1-python-test-order.json
mem03-python-one-authority-query.json
mem03-python-authority-phase-costs.json
mem03-validation-cost-interrupted-native.log
```

The interrupted native log is 5,556 bytes, SHA-256
`079dadf4a268e78ca1566134801e712709595b8de31f55b84de2241caf6fb915`.
It is partial diagnostic evidence, not a successful command receipt.
An attempted nonblocking live stack dump was denied by macOS process-memory
permissions. No elevated access, alternate memory-access workaround or
successful live sampling is claimed.

## The fast tier does not isolate this work

At the committed reference, both `test-unit-fast` and `test-unit` invoke the
same complete Python discovery command. Only the CTest command in the fast
tier excludes the `slow` label. The distinction therefore does not remove
expensive Python source/authority work from the development loop.

The pending MEM03 contract also puts its six-product orchestration in both
native tiers. Its CTest took 62.53 seconds in the interrupted run. The ordinary
native cases, fixed-source compilation, evidence reconstruction and negative
harness checks need an explicit tier disposition; the word "unit" is not a
reason to run every layer after every edit.

[ADR-0031](../adr/0031-tiered-native-validation.md) already distinguishes
fast iteration from complete native acceptance. Its six-second incremental
budget is a review signal, not permission to skip required behavior or a
credible description of the current aggregate.

An existing, unmodified candidate fast set was measured separately on the
committed reference:

```text
test_check_docs
test_check_elf_sections
test_embed_ramfs_seed
test_python_host_runtime
test_stack_reports
test_stack_observer_state
test_validation_evidence
```

The exact command was:

```bash
"${MICROS_PYTHON}" -E -S -B -m unittest tests.host.test_check_docs tests.host.test_check_elf_sections tests.host.test_embed_ramfs_seed tests.host.test_python_host_runtime tests.host.test_stack_reports tests.host.test_stack_observer_state tests.host.test_validation_evidence
```

It passed 64 tests in 3.188 seconds. This is measured selection evidence,
not an implementation of a fast runner or proof of the complete six-second
native target. No new test, model or executable prototype was authored.

## Fixed MINIX driver baseline

The local reference revision and the inspected
`minix/tests/run`/`minix/tests/Makefile` contents were checked read-only.
No MINIX runner, cleanup operation or shell expansion was executed or copied.

The existing
[validation-driver study](minix-validation-driver-baseline.md) traces the
exact reference files. The relevant mechanisms remain:

- named programs and explicit build dependencies;
- selection of all tests or one explicit named list;
- one execution and individual outcome per selected test; and
- aggregate failure when a required execution fails.

For `t` selected programs, driver dispatch and result accounting are `O(t)`
in addition to the actual test work. The shell driver uses scalar counters
and selected/failed-name lists rather than recursively proving the whole
source tree for every local assertion. This statement concerns the inspected
driver, not every MINIX development tool.

| Difference | ADR-0080 classification | Required treatment |
| --- | --- | --- |
| Named selections and per-test results | Baseline parity | Preserve exact test identities and outcomes. |
| C17 host tests plus OpenSBI/RV64 target execution | Required adaptation | Keep the layer that can observe the real requirement. |
| Source authority and recorded execution provenance | Compatible development extension | Preserve independence and freshness, but account for and bound their added work. |
| Replaying an unchanged complete proof for each local identity query | Divergence requiring correction in development tooling | Remove repeated derivation without accepting a stale or unverified proof. |
| Missing-test or privilege-based skipping in the hosted MINIX driver | Not adopted | Required `micros` evidence still fails closed. |

## What source checks can and cannot prove

The source-decomposition program introduced historical body, AST and recipe
identities to prove behavior-preserving transport. They answer whether a
reviewed implementation was preserved through a source move. They do not
prove that its algorithm, behavior, security boundary or performance is
correct.

Permanent ownership, private-header, dependency-direction, ABI and bounded
storage checks can detect structural mistakes that one behavioral example
does not expose. They should remain narrow checks of an actual invariant,
not become a requirement that every future implementation retain old source
spelling.

[ADR-0082](../adr/0082-evidence-authority-transition.md) already separates
transport from baseline correctness. Independent unit/property/model tests,
actual primitive-work observations and appropriate target evidence justify
a deliberate change; only its affected transport identities retire.
Repeatedly deriving that same authority while checking unrelated local
properties adds no independent experiment.

Removing all source checks would discard useful boundaries. Keeping all
historical equality checks as ordinary correctness authorities would prevent
the intended corrections. Neither is the required optimization.

## Why unit tests alone are insufficient

Portable algorithms and state transitions should receive the smallest
deterministic unit/property/model tests, normally with sanitizers. They are
the main development feedback mechanism.

They cannot replace actual RISC-V privilege, trap entry, interrupt, register
preservation, page-table permission or translation-fence evidence. Those
requirements belong to QEMU component tests, not a host mock.

This repository is also a microkernel operating system with isolated VM, PM,
VFS, filesystem and terminal services. Correct individual handlers do not
prove their composed spawn/rollback/exit transaction, endpoint generations,
grant direction, wakeups or complete resource release. Those requirements
need focused cross-service integration tests. Production shell scenarios
remain the later end-to-end layer, not a pretext to include shell work here.

The optimization must distinguish those layers and their trigger conditions.
It must not remove necessary integration because the system contains a
kernel, nor run every integration scenario as part of each unit-test edit.

## Required correction and design boundary

The measured defect establishes these requirements for the separate design:

1. Derive a complete source/evidence authority result at a deliberate bounded
   verification boundary, not redundantly for each local question.
2. Keep any result bound to the exact source, tool, environment, journal and
   process/root scope that justified it. Timestamps, a mutable success flag
   or an unchecked global cache are insufficient.
3. Preserve strict rejection of source/input mutation, malformed or
   incomplete journals, changed selectors, foreign scopes and premature
   retirement. Actual tests still execute; proof reuse is not fabricated
   runtime evidence.
4. Give expensive Python/compiler/evidence work an explicit tier rather than
   silently including it in the fast loop. Complete acceptance retains the
   required union and reports every selected result.
5. Expose active test/workflow progress and observed durations while
   preserving complete logs, exit status, postflight and receipt semantics.
6. Measure deterministic repeated-work counts and actual before/after costs.
   Faster wall-clock output alone cannot waive a correctness requirement.

This research does not select an unreviewed cache representation, delete an
authority family, change a mandatory gate or claim optimized timings.
Implementation remains pending a separately reviewed and merged design.

## References

- [Tiered native validation](../adr/0031-tiered-native-validation.md)
- [Fail-closed selection](../adr/0032-fail-closed-change-aware-validation.md)
- [Author evidence and carry-forward](../adr/0034-author-validation-before-review.md)
- [Exact command receipt reuse](../adr/0064-fail-closed-validation-evidence-reuse.md)
- [Producer readiness](../adr/0074-fail-fast-validation-producer-readiness.md)
- [Evidence authority transition](../architecture/evidence-authority-transition.md)
- [Validation receipts and owned changes](../architecture/validation-evidence.md)
- [Testing strategy](../testing-strategy.md)

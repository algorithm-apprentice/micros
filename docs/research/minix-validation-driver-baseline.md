# MINIX Validation Driver Baseline

## Reference and scope

The fixed, read-only local reference is MINIX commit
`4db99f4012570a577414fe2a43697b2f239b699e`.
This study concerns development validation, not a kernel or service mechanism.
No MINIX source, cleanup command, shell expansion, or runner is copied or
executed.

`minix/tests/Makefile:23-27,78-103,134-142` defines named test programs,
architecture-specific additions, shared support objects and separate
subdirectories. Test identity and build dependencies are explicit.

`minix/tests/run:49-70,73-91,111-146,148-179` selects all tests or an explicit
test list, counts runnable programs, executes each selected test, records
individual outcomes and returns failure when a test fails. Its privilege
selection and missing-test skipping belong to that hosted suite, not the
`micros` acceptance contract.

## Classification

| Behavior | ADR-0025 classification | `micros` consequence |
| --- | --- | --- |
| Stable named tests with explicit dependencies and per-test outcomes | Baseline parity | Retain every native, model, QEMU and image identity and exact outcome. |
| Native and OpenSBI/RV64 QEMU workflows instead of a hosted POSIX runner | Required adaptation | Keep existing CMake workflows and deterministic machine-readable results. |
| Repository-authoritative Git discovery and unknown-input escalation | Compatible extension | ADR-0032 remains authoritative; a caller cannot hide other changes. |
| Current-candidate command receipts with explicit reused provenance | Compatible extension | Accepted ADR-0064 adds development evidence only, not operating-system behavior. |
| Missing or unsupported gate treated as success or silently skipped | Divergence requiring correction | Such behavior is forbidden; unsatisfied evidence blocks review. |

The MINIX driver is not evidence for a cross-invocation cache. The reuse and
bounded CMake exception therefore need a separately reviewed `micros`
contract rather than an analogy to Make's incremental compilation.

## Current `micros` evidence

At merged node-P commit
`ee6be77c1d786eea0cae96b175a9923eba3e0b55`, the existing planner emits:

- 47 commands for `--tier pr --path CMakeLists.txt`, including all 38 QEMU
  and four image workflows;
- seven commands for `--tier pr --path tests/qemu/elf_spawn_child.c`,
  including complete native, smoke, the owned integration, application
  images and three diff checks.

These were read-only planning probes; the comparison does not justify
narrowing arbitrary CMake changes. The actual node-P author run passed
35 CTests, 566 Python tests, all 38 QEMU workflows and four image workflows.
Its Python phase took 95.643 seconds; the separately observed composed
lifecycle model took 154.23 seconds. Those are observed costs, not new
performance acceptance limits.

`tools/validation_plan.py:1741-1743,1854-1897` already deduplicates identical
tuples within one plan. Its execution loop at `2095-2102` executes every
selected command anew and stores no receipt. Consequently this task concerns
cross-invocation evidence and narrowly provable ownership, not a missing
within-plan duplicate check.

See [the Accepted decision](../adr/0064-fail-closed-validation-evidence-reuse.md)
and [the exact contract](../architecture/validation-evidence.md).

## Default-configuration closure correction

Implementation draft #130 was closed without merge after a read-only
installed-tool trace found the selected LLVM installation's `etc/clang`
alias resolves to the same Homebrew prefix's non-versioned `etc/clang`.
The directory contains Darwin configuration files. The initial admitted
roots do not close that input, so both target profiles correctly remained
ineligible. Native metadata closure is not a native gate execution or proof
that the target profiles work.

The fixed LLVM driver source linked by
[Proposed ADR-0065](../adr/0065-closed-tool-default-configuration-for-validation.md)
separates disabled automatic loading from still-possible explicit loading.
That requires both a declared disable policy and exclusion of explicit,
response and unresolved injected options; hashing a directory alone cannot
prove transitive config inputs are closed.

The installed QEMU reports a prefix-contained default `etc/qemu/qemu.conf`
location; neither it nor the same-prefix global location exists in the
observed reference configuration. Its fixed upstream loader accepts absence.
Both absences must remain checked, not assumed. Existing read-only Ninja
command inventories contain166 smoke and11 TTY-image commands, with no
explicit configuration or response inputs. These are metadata observations,
not executable design prototypes or successful workflow receipts.

These are compatible development-only extensions under ADR-0025. No MINIX
runner mechanism or operating-system behavior is changed.

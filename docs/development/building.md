# Building and Testing

Run commands from the repository root. This guide owns prerequisites,
executable workflows, artifacts and troubleshooting boundaries.
[Testing strategy](../testing-strategy.md) owns evidence policy;
[the roadmap](../roadmap.md#current-status) owns delivery status.
Historical author journals and baseline modes are not current commands.

## Prerequisites

The build requires:

- CMake 3.25 or newer;
- Ninja;
- GIL-enabled CPython 3.15 final, version 3.15.0 or newer within the 3.15
  family;
- Clang with the `riscv64-unknown-elf` target plus `llvm-nm` and
  `llvm-readelf`;
- LLD;
- QEMU 7.0 or newer with `qemu-system-riscv64`, the `virt,aia=none`
  machine option, and default OpenSBI firmware providing SBI TIME and System
  Reset.

On macOS, the supported Homebrew packages are `cmake`, `ninja`, `qemu`, `llvm`,
`lld` and `python@3.15`. Linux installations may provide equivalent packages
through their distribution.

## Tool discovery

The RISC-V toolchain file discovers Clang in this order:

1. `MICROS_CLANG`;
2. `${MICROS_LLVM_ROOT}/bin/clang`;
3. `brew --prefix llvm` on macOS;
4. `clang` on `PATH`.

It discovers LLD in this order:

1. `MICROS_LD_LLD`;
2. `${MICROS_LLD_ROOT}/bin/ld.lld`;
3. `brew --prefix lld` on macOS;
4. `ld.lld` on `PATH`.

CMake selects GIL-enabled CPython 3.15 final in this order:

1. the `MICROS_PYTHON` CMake cache entry;
2. inherited `MICROS_PYTHON`;
3. `${MICROS_PYTHON_ROOT}/bin/python3.15`;
4. `brew --prefix python@3.15` on macOS;
5. `python3.15` on `PATH`.

An invalid explicit selection stops configuration rather than falling through.
Every invocation uses `-E -S -B`. Supported execution also requires
`OPENSSL_CONF=/dev/null`, `SSL_CERT_FILE=/dev/null`, and
`SSL_CERT_DIR=/dev/null`. QEMU remains discoverable on `PATH`. Both tools may
be selected in an explicit configure step:

```bash
cmake --preset riscv64-debug \
  -DMICROS_PYTHON=/absolute/path/to/python3.15 \
  -DMICROS_QEMU=/absolute/path/to/qemu-system-riscv64
```

Missing tools stop configuration with an explicit error. The build never
falls back to a hosted C library or a host linker.

Before running workflows, select the supported tools and disable mutable
OpenSSL/compiler configuration inputs. Use existing absolute executable
paths; the versioned paths avoid silently changing tools during evidence:

```bash
export MICROS_PYTHON=/absolute/path/to/python3.15
export OPENSSL_CONF=/dev/null
export SSL_CERT_FILE=/dev/null
export SSL_CERT_DIR=/dev/null
export CLANG_NO_DEFAULT_CONFIG=1
export MICROS_CLANG=/absolute/path/to/clang
export MICROS_LD_LLD=/absolute/path/to/ld.lld
```

## Build

From a clean checkout, configure and build with one command:

```bash
cmake --workflow --preset build-riscv64-debug
```

The primary artifacts are:

```text
build/riscv64-debug/kernel/micros.elf
build/riscv64-debug/kernel/micros.map
```

The dedicated runtime acceptance workflow additionally produces:

```text
build/riscv64-user-runtime-test/user-runtime/micros-user-runtime-service.elf
build/riscv64-user-runtime-test/user-runtime/micros-user-runtime-service.map
build/riscv64-user-runtime-test/kernel/micros.elf
```

The ELF uses the `rv64imac_zicsr_zifencei` and `lp64` baseline, contains no
host startup objects or libc, and preserves the OpenSBI boot arguments in `a0`
and `a1` until `kernel_main`. Every target link runs
`tools/check_elf_sections.py`; the build fails if an allocatable output section
is unexpected, crosses a permission boundary, lies outside the kernel ranges,
or has write/execute flags inconsistent with its linker-defined range.
It also rejects the superseded standalone `micros_trap_hart_id` and
`timer_state` symbols.

## Native unit tests

The fast target runs nonslow native CTests and the closed, small Python unit
set defined by [bounded validation feedback](../architecture/validation-feedback.md).
The full target retains every CTest/model and complete Python discovery.
Source-heavy checks share one invocation-owned snapshot of actual current
bytes. Opening it does not reconstruct historical authority. Final success
follows complete namespace/input closing checks; this is not unchecked or
cross-invocation caching. See the
[current-contract validation map](../architecture/current-contract-validation.md).

Run the fast native development loop with:

```bash
cmake --workflow --preset test-unit-fast
```

For an affected persistent model, use its command in the
[required model list](../testing-strategy.md#persistent-model-commands).
All models remain included by complete native acceptance.

The complete native milestone gate remains:

```bash
cmake --workflow --preset test-unit
```

The host portion is also directly available from the repository root:

```bash
"${MICROS_PYTHON}" -E -S -B tools/run_host_tests.py --tier fast
"${MICROS_PYTHON}" -E -S -B tools/run_host_tests.py --tier full
"${MICROS_PYTHON}" -E -S -B tools/run_host_tests.py --test test_kernel_address_space_source_layout.KernelAddressSpaceSourceLayoutTest.test_current_scratch_storage_has_one_owner
"${MICROS_PYTHON}" -E -S -B tools/run_host_tests.py --tier full --list
```

Focused selection may repeat `--test`; it does not establish PR coverage.
Listing executes no test or source proof. The runner reports each selected
test's start, outcome and duration, then the slowest ten. Workflow execution
forwards those records live while retaining exact raw logs and source-drift
checks. Only explicit receipt modes collect reusable profiles.

Direct `python -m unittest` remains available for isolated proof-engine tests
and cold-path diagnosis. Use the owned runner for a source-heavy selected
suite when its shared checked view is intended. No test result substitutes
for the affected QEMU component or cross-service integration gate.

The COPY01 native fixture also requires the explicit compiler environment
documented under [runtime memory evidence](#runtime-memory-work-and-authority-evidence).

The EV02 native evidence has two focused targets. Configure the existing host
graph, build both variants, and select their CTests with:

```bash
cmake --preset host-debug
cmake --build build/host-debug --target work_counts_test work_observation_disabled_test
ctest --test-dir build/host-debug --output-on-failure -R '^(work-counts|work-observation-disabled)$'
"${MICROS_PYTHON}" -E -S -B -m unittest tests.host.test_work_count_evidence
```

Both tests remain part of `test-unit-fast` and complete `test-unit`; targeted
iteration is not final author validation.

For selected endpoint/IPC validation, build the ordinary/observed pair and
the existing endpoint/IPC semantic cases:

```bash
cmake --preset host-debug
cmake --build build/host-debug --target endpoint_test selected_ipc_validation_ordinary selected_ipc_validation_test
ctest --test-dir build/host-debug --output-on-failure --no-tests=error -R '^(endpoint-profile|selected-ipc-validation)$'
```

The selected-IPC work pair is included by complete `test-unit`, not the fast
loop. Its current-source, collector, semantic and work checks do not replace
the independent IPC model or actual syscall/completion target evidence.
The same pair includes IPC02 chain/index-lifetime cases. Add `--verbose` to
that focused CTest command to see the validated `MICROS_IPC02_WORK` hop,
bitmap, index and cold-repair summaries; no second producer is required.
`MICROS_IPC03_WORK` in that same output reports direct token-related table
visits and selected indexed reads. It uses the same native pair, not a new
workflow.
`MICROS_IPC04_WORK` reports actual head-only sender append, bounded malformed
walk, selected receive and full-capacity close work, with scheduler request
records shown separately from endpoint/IPC records.

## Validation planning

Generate a changed-path plan with:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier fast
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier pr
```

Add `--execute` to run the commands. Unknown target paths and shared QEMU
infrastructure fail closed to complete `test-unit` and the complete QEMU
matrix. Implementation changes require complete `test-unit` in the PR tier;
a documentation-only plan needs only documentation/diff checks. Fast is
an iteration tier. Explicit paths are unioned with repository changes, and
execution requires every untracked file to be staged first.

Fresh execution is still the default. These explicit forms never reuse prior
command results, collect reusable profiles or publish new receipts:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier pr --execute --fresh
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier full --execute --fresh
```

On macOS, keep the host awake during long target runs. Maintenance sleep
can cross a guest readiness deadline even though the kernel made no
progress during that interval. A command-scoped assertion avoids changing
global power settings or any guest/test deadline:

```bash
caffeinate -i -s "${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier full --execute --fresh
```

The system-sleep assertion requires AC power and lasts only for that
command. Do not treat it as a guest-clock or timeout override. A changed
launcher environment may invalidate an older execution receipt; keep the
exact-context check and run fresh rather than weakening the fingerprint.

Hosted execution status is tracked in the [roadmap](../roadmap.md#current-status);
the [Actions contract](github-actions-ci.md) is not evidence that a workflow
has been accepted or is operational. Local validation does not claim hosted
acceptance.

## Optional exact-context receipts

Use the [tool-discovery environment](#tool-discovery) above for every command
in this section.

When a task additionally requires reusable receipts from the same complete
fresh invocation, use producer mode:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py \
  --tier pr --base origin/main --execute --producer
```

Producer mode computes the unchanged required plan, then checks every reusable
workflow already selected by that plan before opening the evidence store or
running a planned command. A blocked profile leaves every command `NOT_RUN`
and prints either a fixed canonical preparation command for an exact generated
output/dependency condition or an unsupported-profile reason. It never runs
preparation automatically. A ready producer executes the complete plan fresh
and succeeds only when every selected reusable workflow publishes a receipt
under the invocation's one run ID.

Before that explicit producer, build all native products with
`cmake --build build/host-debug` until Ninja reports no work. Generated fixture
headers are validation inputs; a later generation/relink would invalidate
receipt identity. Ordinary runtime work does not require a separate producer/
reuse ceremony unless it changes that feature or explicitly needs receipts.

The opt-in local ledger follows
[ADR-0064](../adr/0064-fail-closed-validation-evidence-reuse.md),
[ADR-0065](../adr/0065-closed-tool-default-configuration-for-validation.md)
and the [closed-profile contract](../architecture/validation-evidence.md).
The initial reusable workflows are complete `test-unit`, `test-qemu-smoke`
and `build-tty-service-image` on the declared Darwin arm64/Ninja profile.
Other workflows still execute fresh; a receipt never removes a required gate.
Explicit full tier rejects `--reuse`. Documentation and all three diff
checks always execute fresh.

`test-unit` includes the kernel-layout native/target preprocessing fixture.
Its reusable identity therefore fingerprints the selected target Clang,
versioned LLVM installation and compiler-configuration alias in addition to
the native compiler. Reusable collection requires explicit `MICROS_CLANG` or
`MICROS_LLVM_ROOT`; automatic discovery is fresh-only. Invalid explicit
compiler overrides fail closed rather than selecting a fallback.

For the supported Homebrew CPython/Xcode configuration, use the same explicit
environment for configuration, producer, and consumer. Python and compiler
spellings must match the configured cache, not merely resolve to the same
executable:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier pr --execute --reuse
```

Missing, stale, unsupported or corrupt evidence prints a `MISS` reason and
runs the command fresh. A valid `REUSED` result names its original producer,
HEAD/tree and complete-log digest; it is not a new execution. No environment
normalization, dependency installation or cross-tree reuse occurs.
The ignored ledger and complete logs live in `build/validation-evidence/v1/`.
`VALIDATION_REPORT` binds the required command sequence and outcomes to the
current candidate. Producer reports additionally contain the ordered readiness
records, barrier result, shared run ID, complete-publication result, and fixed
failure code defined by ADR-0074. Non-producer reports omit that object.

For validation-tooling changes only, after a complete producer succeeds,
this focused exercise verifies its three actual reusable profiles without
executing the other workflows again. It calls the same strict consumption
path as `--reuse`, fails on any miss, and is **not final author validation**:

```bash
"${MICROS_PYTHON}" -E -S -B - <<'PY'
from pathlib import Path
from tools.validation_context import PROFILES, capture_candidate
from tools.validation_evidence import EvidenceStore
from tools.validation_execution import _profile, _reuse

root = Path.cwd()
candidate = capture_candidate(root)
assert candidate.clean
producers = set()
with EvidenceStore(root, PROFILES) as store:
    for name in PROFILES:
        profile, error = _profile(root, name, candidate)
        assert profile is not None, (name, error)
        entry = {}
        reused, reason = _reuse(root, name, candidate, profile, store, entry)
        assert reused and entry["status"] == "REUSED", (name, reason)
        producers.add(entry["producer"]["run_id"])
assert len(producers) == 1
assert capture_candidate(root) == candidate
print("REUSE-ONLY: three profiles; verify this run ID against the complete producer report")
PY
```

Use the same actual launcher as the producer, including `caffeinate -i -s`
when it was used there; even the inherited shell `_` entry is fingerprinted.
The reported producer ID must equal the successful full report's ID.
No current runtime task needs this extra exercise merely to merge.

One exact canonical workflow can also be executed and recorded fresh:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --record-workflow test-unit
```

This is **gate-only, not final author validation**; it cannot select paths,
lower a required plan or authorize review. Reuse and gate-only recording
require the exact clean staged candidate. Ordinary dirty tracked fresh
iteration remains supported, but cannot publish reusable or final evidence.
The final author still supplies the complete plan, clean tracked/index state,
no untracked task files and independent review.
Nonempty sanitizer option/symbolizer overrides are outside the initial reuse
profile; their external option/suppression files are not silently ignored.
They remain unchanged for fresh iteration. Ordinary unstaged tracked
deletions are likewise fresh-only iteration, not reusable or final evidence.

## QEMU smoke test

```bash
cmake --workflow --preset test-qemu-smoke
```

The harness launches one RV64 hart with 128 MiB on `virt,aia=none`, default
OpenSBI, serial standard I/O, and no monitor or network. The eight-second
smoke gate requires exact ordered boot/object/trap/FDT/allocator/MMU/ownership
records, matching nonzero counts, valid firmware reservations and natural
exit zero. Missing/duplicate/malformed records, panic, explicit failure,
unexpected exit and timeout cannot become success.

The host emits TAP and an outcome:

```text
TAP version 13
ok 1 - QEMU smoke test observed expected pass
# outcome: pass
# expected outcome: pass
```

## QEMU workflow catalog

The [complete required matrix](../testing-strategy.md#required-qemu-matrix)
is kept in the planner's existing documentation owner, the testing strategy.
Run the command for the affected mechanism; every workflow configures and
builds its own target. Do not retry until success, raise a deadline, borrow
another image's result or infer hardware evidence from a unit test.
The focused commands below complement rather than replace that matrix.

## Runtime memory work and authority evidence

The current native checks compile the actual runtime implementation and retain
semantic, boundary, canary, typed-object, protected-page, work and compiler/IR
evidence. Historical byte-loop execution and the COPY01 author journal are
retired by [ADR-0123](../adr/0123-current-contract-validation.md).

Use the supported explicit environment and the existing native compiler cache:

```bash
cmake --preset host-debug
"${MICROS_PYTHON}" -E -S -B -m unittest tests.host.test_runtime_memory
cmake --workflow --preset test-qemu-user-runtime
```

The user-runtime workflow also proves the actual linked memory object,
ELF/map/recipe identity and non-stack wide transfers. It uses its existing
guest result, not a second guest. The current evidence ownership and retained
finite source/machine bindings are documented in
[current-contract validation](../architecture/current-contract-validation.md).

## Bootstrap allocation work and authority evidence

MEM01's current native fixture uses actual current source and retains its
semantic, rollback, work-bound and ordinary/observed agreement checks.
The [Accepted allocation contract](../architecture/bootstrap-allocation.md)
still owns lowest-address selection, range safety and cursor coherence.
There is no historical-source build, author selector or retirement journal.

```bash
cmake --preset host-debug
cmake --build build/host-debug --target frame_allocator_test frame_ownership_test bootstrap_allocation_test
ctest --test-dir build/host-debug --output-on-failure --no-tests=error -R '^(frame-allocator|frame-ownership|bootstrap-allocation)$'
"${MICROS_PYTHON}" -E -S -B -m unittest tests.host.test_bootstrap_allocation
cmake --workflow --preset test-qemu-frame-allocator
```

Generated observations remain test-only, primitive-bound and reversible.
Ordinary products contain no collector. Source, generated inputs, executables
and tools are checked before and after the current execution.

## Kernel mapping work and authority evidence

MEM02 retains largest legal leaves, checked table identities, independent
native work/semantic oracles and real 128/256 MiB MMU evidence under its
[Accepted contract](../architecture/kernel-mapping-topology.md).
The ordinary MMU workflow now owns its required mapping records and actual
ELF/load-probe proof, including the original faults and clean shutdown.

```bash
cmake --preset host-debug
cmake --build build/host-debug --target kernel_mapping_test
ctest --test-dir build/host-debug --output-on-failure --no-tests=error -R '^kernel-mapping$'
"${MICROS_PYTHON}" -E -S -B -m unittest tests.host.test_kernel_mapping
cmake --workflow --preset test-qemu-mmu
```

The current native product is not paired with an ancestor implementation.
Required current cases, ordinary collector absence, primitive coverage,
work bounds and input/product drift rejection remain.

## Selected ownership work and authority evidence

The five selected-memory/object/return orchestrators retain their complete
current cases and channels:

| CTest | Current executions |
| --- | --- |
| `selected-ownership` | Five observation slices and one ordinary control |
| `selected-address-space` | Three observation slices and one ordinary control |
| `selected-release-handoff` | Four observation slices and one ordinary control |
| `object-hart-validation` | Two observation channels and one ordinary control; 171 cases |
| `common-scheduler-return` | Work and ordinary controls; 180 records |

Build the native graph before selecting them:

```bash
cmake --preset host-debug
cmake --build build/host-debug
ctest --test-dir build/host-debug --output-on-failure --no-tests=error -R '^(selected-ownership|selected-address-space|selected-release-handoff|object-hart-validation|common-scheduler-return)$'
```

These are full-tier evidence, not additions to the fast loop. They execute
actual current products and preserve semantic/error, output-preservation,
complete-audit, work/storage, collector-linkage and composition checks.
Independent fixed-reference fields retain their provenance without compiling
old implementations or replaying predecessor claims.

The corresponding user-address-space, address-space-handoff and scheduler
workflows validate the required current witnesses from their existing guest
results. The scheduler retains its exact 1/2/1 context-validation counts.
The pre-exec Python-launcher regression remains directly runnable:

```bash
"${MICROS_PYTHON}" -E -S -B tools/run_host_tests.py --test test_selected_ownership.SelectedOwnershipNativeTest.test_launcher_rejects_consumed_environment_before_interpreter_exec
```

Actual Red and focused Green still precede one sufficient final planner run.
Do not invoke the retired `--red`, `--green` or `--recertify` interfaces or run
a second B01-B12/B14 transaction for evidence already supplied by that plan.

## Terminal-failure baseline and current evidence

The [Accepted terminal contract](../architecture/terminal-failure.md) retains
all twelve current QEMU cases and four named oracles. Historical baseline
execution and its paired author report are retired; current evidence uses:

```bash
cmake --preset host-debug
cmake --build build/host-debug --target panic_control_test
ctest --test-dir build/host-debug --output-on-failure --no-tests=error -R '^panic-control$'
"${MICROS_PYTHON}" -E -S -B -m unittest tests.host.test_terminal_failure
cmake --workflow --preset test-qemu-terminal-failure
```

The controller performs a fresh private configure and clean-first kernel
build. Every VM has one absolute eight-second deadline; the private debugger
may write only the fixed four-byte scenario selector. Exact polling bounds,
first-entry/reset ownership, console repair, history preservation, work
snapshots, real reset registers and natural firmware termination remain
mandatory. An overrun, missing control object or host termination is failure.

Complete current source/tool/firmware and artifact identities are checked
through execution and atomic publication. Raw captures, commands, packets
and transcripts remain under `build/terminal-failure/`; only a completed
current run publishes `current.json`. There is no baseline flag, historical
archive, source-role partition or dependency on an earlier author's files.
The complete final plan and independent review remain required.

## Panic-trace baseline and current evidence

The [bounded trace contract](../architecture/panic-stack-trace.md) and
[link-precedence clarification](../adr/0097-panic-trace-link-rejection-precedence.md)
retain all eighteen actual cases and four named current oracles. The old
baseline archive and expected-failure author mode are retired.

```bash
"${MICROS_PYTHON}" -E -S -B -m unittest tests.host.test_panic_trace
cmake --preset host-debug
cmake --build build/host-debug --target panic_trace_test
ctest --test-dir build/host-debug --output-on-failure --no-tests=error -R '^panic-trace$'
cmake --workflow --preset test-qemu-panic-trace
```

Execution uses a fresh private configure and clean-first build. Each VM has
one absolute eight-second deadline, including debugger startup. The controller
accepts no arbitrary image, source, address, case or timeout. Missing trace
helpers or ownership/capture evidence fail; an ordinary reset remains the
explicit no-trace case.

The independent captured-stack walk, exact frame/read/link counts, real
caller return sites, trace/history ordering, recursive-entry captures,
injected fault instruction/cause and stuck-output store rejection remain.
All eighteen cases require natural firmware exit. Complete current
source/tool/firmware inputs and artifacts are checked before success.
Raw evidence remains under `build/panic-trace/`; only a complete successful
run atomically publishes `current.json`. These reports are not reusable
receipts or substitutes for the full stack gate and independent review.

## Standalone images

Run the command in each row. These are distinct from
fixture guests; a test image cannot substitute for its production-image
artifact checks.

| Command | Output and checks |
| --- | --- |
| `cmake --workflow --preset build-tty-service-image` | Real TTY ELF, startup and freestanding service contract |
| `cmake --workflow --preset build-ramfs-service-image` | Canonical seed plus real RAMFS ELF; complete load footprint and external stack within 192 pages |
| `cmake --workflow --preset build-vfs-service-image` | Real VFS ELF and private adapters; complete footprint and external stack within 80 pages |
| `cmake --workflow --preset build-application-images` | Checked runtime probe/minimal application and dynamic test-parent/child images; application and ELF-spawn seeds |

Application outputs are under `build/riscv64-application-images/`, including
`applications/application-seed.bin` and `applications/elf-spawn-seed.bin`.
The separate application entry decodes canonical argv/env/auxv and turns normal
return into PM `EXIT`; it does not change the service entry or publish
production init. Exact image/startup limits are in
[the runtime map](../architecture/application-runtime.md).

## Stack evidence

```bash
cmake --workflow --preset test-stack-evidence
```

The complete gate owns every current ordinary/metadata static configuration,
declared ELF and separately observed scope. Static proof precedes target
execution; actual metadata passivity, complete call/epoch/fatal closure,
producer identity and unchanged budgets precede atomic certificate
publication in `build/stack-evidence/current.json`.
B13-E007 remains explicitly blocked until `STACK13`.
See [the exact contract](../architecture/stack-bound-evidence.md).

For development of the finite source/object/linked proof, the existing
dispatcher can check one fresh ordinary/metadata boot-image pair:

```bash
"${MICROS_PYTHON}" -E -S -B - <<'PY'
from pathlib import Path
from tools.check_stack_evidence import static_configuration
from tools.stack_bound_build import static_profiles
from tools.validation_context import capture_candidate
root = Path.cwd()
candidate = capture_candidate(root)
assert candidate.clean
profile, = (value for value in static_profiles(root) if value.configure == "riscv64-debug")
static_configuration(root, profile, candidate)
print("DEVELOPMENT_ONLY: one static configuration passed; no complete stack certificate")
PY
```

Stage the complete candidate first. This uses the ordinary build seals,
passivity checks and real linked proof; it does not publish the complete
certificate, run the ten observed scopes, or satisfy the full author gate.
Select `riscv64-panic-trace-test` instead of `riscv64-debug` in this same
development command to check the closed trace fixture.

The fixture proof binds the actual outer, nested and word-load fault
instructions, complete trap captures, physical stack pivots and read-only
post-load-fault C route. Hardware transitions retain the interrupted epochs;
explicit panic reentry retains both captures on one stack. The existing
unqualified panic-report certificate supplies a conservative upper bound
for the already-latched destination tail, not permission to enter another
report body. No new panic-edge exclusion or frame-releasing graph edge is
introduced.

## Documentation-only checks

After staging the complete documentation change, use the same authoritative
planner rather than manually substituting a smaller plan:

```bash
git add <intended-files>
git diff --quiet
git ls-files --others --exclude-standard
git write-tree
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier pr --base origin/main --execute --fresh
```

For a pure documentation diff, the plan runs `tools/check_docs.py` and the
staged, worktree and branch-range whitespace checks. Native, QEMU and stack
executions are N/A, not missing author evidence. The documentation gate checks
links, formatting, instruction files, ADR status and the development/runtime
graphs. Review exact changed links and anchors as well as that output.

# B14 Executable-Evidence Baseline Audit

## Status

```text
audit unit:       B14 executable evidence
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; three blocking evidence findings; not certified
implementation:   forbidden during complete-audit phase
```

B14 audits every executable oracle, target scenario, fixture generator,
artifact checker, runner, and recipe selected by the pinned validation graph.
It does not rerun the suites or treat an old passing result as proof of the
newly audited MINIX structural and algorithmic baseline.

## Exact primary and secondary path manifests

The complete materialized path, recipe, conditional, generated-output, CTest,
and workflow ledger is:

- [B14 executable-evidence role manifest](minix-complete-baseline-b14-evidence-manifest.md).

The pinned primary inventory contains **636** B14 paths:

| Primary family | Paths |
| --- | ---: |
| `kernel/tests/**` | 142 |
| `tests/host/**` | 390 |
| `tests/qemu/**` | 71 |
| Assigned checker/generator/runner modules under `tools/**` | 33 |
| **Total** | **636** |

The primary extension inventory is 414 C sources, 111 headers, 12 assembly
sources, and 99 Python modules.

The secondary evidence-role manifest contains **1,063** rows: 1,058 from the
pinned runtime/evidence/recipe closure and five explicit live W01/W44
documentation/process fixtures.

| Primary owner | Selected evidence-role rows |
| --- | ---: |
| B01 | 38 |
| B02 | 37 |
| B03 | 35 |
| B04 | 39 |
| B05 | 52 |
| B06 | 33 |
| B07 | 50 |
| B08 | 59 |
| B09 | 35 |
| B10 | 17 |
| B11 | 15 |
| B14 | 636 |
| Explicit non-MINIX recipe/import/fixture support | 17 |
| **Total** | **1,063** |

The closed recipe inventory has:

- 35 native CTests;
- seven registered native test workflows: one complete, one fast, and five
  slow-model-specific workflows;
- 38 QEMU workflows;
- four standalone-image workflows;
- 49 registered evidence workflows, of which 43 form the authoritative full
  validation tier;
- 169 exact selected compile contexts, each binding one workflow, compiling
  CMake target, and active `MICROS_*` definition set;
- 1,035 tracked paths read or inspected by the identical W01/W44 Python
  discovery, all assigned to both workflows; five required additional
  excluded-support rows, of which `README.md`,
  `docs/architecture/development-dag.md`, and `docs/testing-strategy.md`
  changed after the fixed runtime pin;
- 63 paths whose preprocessor condition expressions select a
  `MICROS_*TEST` macro, of which 49 are B01-B11 production/public-header
  paths and 14 are B14 evidence paths; and
- zero primary paths without a CTest or workflow consumer.

The current audit branch was first verified to have no executable-source,
recipe, production, or public-header difference from the pinned tree. All 43
configure presets were then regenerated. The manifest combines:

1. CMake preset and build-target declarations;
2. Ninja selected-target graphs;
3. active selected-object compiler dependency records and target-scoped
   definitions;
4. CTest JSON inventory;
5. selected Python import closure;
6. every pinned preprocessor conditional expression selecting a
   `MICROS_*TEST` macro; and
7. the four explicit production bridges named by the audit contract; and
8. a targeted 1,033-case Python fixture-access trace for current W01/W44
   file reads and metadata checks, including exact `(path, workflow)` edges.

The three changed documentation/process fixtures are current-tree validation
inputs, not runtime-baseline authority. This remains a source/recipe/evidence-
input closure proof, not a runtime behavior result.

## Closed evidence-operation manifest

| ID | Evidence operation |
| --- | --- |
| B14-O01 | Primary-path and secondary recipe/conditional closure |
| B14-O02 | Deterministic native semantic and malformed-input tests |
| B14-O03 | Stateful model/reference comparison and replayable traces |
| B14-O04 | Source-transport, ownership, and build-recipe structural evidence |
| B14-O05 | Fixture/seed generation and host-side ELF/artifact authentication |
| B14-O06 | Privileged target component and panic/fault scenarios |
| B14-O07 | Multi-service integration, lifecycle, and resource-baseline scenarios |
| B14-O08 | QEMU outcome authentication, timeout handling, and failure visibility |
| B14-O09 | Structural, operation-count, transfer-count, stack, and auxiliary-space proof |
| B14-O10 | Node-Q and later POSIX/conformance executable closure |

## Fixed local MINIX evidence

Only the pinned local checkout was used:

| Source | Evidence role |
| --- | --- |
| `minix/tests/Makefile` | Fixed test inventory, per-test compile variants, and common object inclusion |
| `minix/tests/run` | Complete test enumeration, privilege handling, TAP mode, per-test failure capture, aggregate failure exit |
| `minix/tests/common.h` | Explicit error, formatted failure, cleanup, progress, and resource-query interface |
| `minix/tests/common.c` | Error accumulation, fail-fast threshold, deterministic cleanup, and nonzero final failure |
| `minix/tests/test42.c` | Representative exact process-state assertions and explicit per-case timeout failure |
| `minix/tests/test73.c` | Representative service-started black-box driver with bounded wait and explicit result transport |
| `minix/tests/testvm.c` | Representative isolated VM-service workload, deterministic sizes, and optional larger stress mode |
| `tests/bin/cat/t_cat.sh` | Independent command output/error oracle |
| `tests/bin/ps/t_ps.sh` | Independent formatting/selection oracle derived from expected columns and regular expressions |
| `tests/bin/sh/Makefile` and `tests/bin/sh/**` | Fixed shell behavior cases and checked output fixtures |

MINIX behavior tests generally assert observable results and explicit failure
status. They do not provide a reusable source-identity oracle for the internal
data structures and algorithms audited by ADR-0080. The required `micros`
operation-count, topology, and bounded-stack proof is therefore a compatible
evidence extension, not a test framework copied from MINIX.

## Evidence dimensions

| Dimension | Required meaning |
| --- | --- |
| Semantic | Returned state, ordering, identity, failure code, and rollback match the contract |
| Reference | Expected state is computed independently from the production transition |
| Structural | Required queue/list/tree/hash/bitmap topology and ownership are directly authenticated |
| Complexity | Comparisons, probes, visits, validation calls, copied bytes, and retained ranges establish the required bound |
| Target | Privilege, trap, MMU, interrupt, assembly, linker, and cross-service behavior runs on the RISC-V image |
| Artifact | Generated bytes and ELF shape are authenticated outside the target |
| Failure visibility | Timeout, panic, target failure, malformed report, forbidden output, and nonzero exit cannot become success |
| Stack/storage | Maximum dynamic stack and bounded auxiliary/retained storage remain within the accepted budgets |

One evidence source may cover several dimensions. Passing semantic output
never implies structural, complexity, or stack proof.

## Native evidence ledger

The CTest codes below are defined exactly in the materialized manifest.

| CTests | Primary audited operations | What they prove | What they do not prove |
| --- | --- | --- | --- |
| C07 | B01-O02/O03 | FDT decode, malformed input, and range semantics | Token/byte visits or successful-path rescan count |
| C08, C09, C19, C33 | B02-O01-O06 | Allocator, ownership, Sv39, bootstrap, and selected mapping semantics | Probe counts, complete-validator cadence, release/handoff work, or stack peak |
| C11, C17 | B03-O01-O07/O09 | Object lifecycle, queue behavior, selection, accounting, return, and invalid-state semantics | Complete-validator, queue-shift, return-plan, or nested-stack costs |
| C06, C10 | B04-O01-O10 and B05-O04-O07 | Endpoint/IPC/grant behavior plus deterministic models | MINIX receiver filters, exact work counts, or worst stack depth |
| C20 | B05-O01 | Portable trap-origin routing semantics | Target trap entry/return behavior or nested-stack cost |
| C29 | B05-O08/O10 and B06-O07 | Freestanding service runtime wrappers, portable copy/fill semantics, and console-begin control marshalling | Application startup, target console handoff, transfer width, or runtime copy cost |
| C02, C03 | B06-O01-O09 | Manifest, topology, control, readiness, rollback, and failure semantics | Lookup/comparison counts or complete command validation cadence |
| C33-C35 | B07-O01-O08/O10 | Boot import, VM planning/state, operation-12 handoff, and copy/clear semantics | MINIX allocator/cache structure, per-page validation multiplication, transfer work, or stack peak |
| C12, C13, C18 | B08-O01-O08/O10 | PM/coordinator lifecycle, wait, rollback, request, and stack-layout semantics | Direct-index/free-cursor structure, validation/copy counts, or target stack peak |
| C22 | B01-O05/O08 and B09-O01/O08 | Console ownership, PLIC core, TTY handoff/control, and UART semantics | Target MMIO/CSR ordering, raw-ring topology, or stuck-device bounds |
| C21, C23-C28 | B09-O02-O10 | Control syscall, interrupt, service, UART, fault, and state-model semantics | MINIX FIFO/backpressure behavior, per-byte work, or stack peak |
| C14-C16 | B10-O01/O03-O10 | Seed, namespace, I/O, directory, and model semantics | Hash/zone topology, scan origins, comparison/copy counts, or stack peak |
| C30-C32 | B11-O01-O11 | VFS core/model/service behavior, descriptors, namespace, I/O, console, and spawn state | Direct process indexing, reference-cleanup phase, operation counts, or stack peak |
| C01 | B05-O09, B12-O08, and B13-O04 prerequisites | Application stack/startup/normal-return behavior | Target entry, complete lifecycle cost, or node-Q policy |
| C04, C05 | B12-O01-O09/O11 and B13-O04 prerequisites | Fixture protocol and complete lifecycle model behavior | Composed cross-owner cost, fault exit, node-Q policy, or accepted command algorithms |

The five slow CTests (`ipc-model`, `tty-model`, `ramfs-model`, `vfs-model`,
and `elf-spawn-model`) retain replayable deterministic state-machine evidence.
Their operation-coverage counters prove that generated action categories were
visited; they do not count production comparisons, scans, nested validators,
or copied bytes.

W44-W49 provide the registered fast-native and individual slow-model
subsets. They improve iteration granularity but add no evidence dimension
beyond W01's complete native workflow.

## Target and artifact evidence ledger

The workflow codes below are defined exactly in the materialized manifest.

| Workflows | Primary audited operations | Evidence result |
| --- | --- | --- |
| W02-W10 | B01-O01-O10, B02-O01-O05/O09, B03-O02/O03 | Option-free boot plus clean/failure SBI shutdown, panic, trap, timer, UART, allocator, MMU, object, and nested target behavior |
| W11-W14, W28-W36 | B02-O06-O08, B04-O01-O11, B05-O01-O08/O10 | Endpoint, grant, IPC, service runtime, address-space, handoff, and user-execution target scenarios |
| W15, W26, W27 | B06-O01-O10 | Static launcher success, readiness timeout, and malformed-manifest panic |
| W16, W18, W23-W25 | B07-O01-O10 | VM handoff, scratch/preemption, role gate, copy/clear paths, and two authenticated self-fault outcomes |
| W17 | B05-O09, B08-O01-O08, B12-O01/O03/O05/O06/O08/O09 | Real application startup/normal return plus PM/VM rollback/reprepare, activation, exit, and cleanup |
| W19 | B09-O01-O10 | Real TTY service handoff, serial input, canonical processing, grants, and IRQ-driven output |
| W20 | B10-O01-O10 | Real RAMFS service seed, namespace, I/O, directory, grant, and reference behavior |
| W21 | B11-O01-O11 | Real VFS/RAMFS/TTY routing, regular/directory/console I/O, and balanced grants |
| W22 | B05-O09, B12-O01-O09/O11 | Complete six-service ELF spawn, application startup/normal return, post-freeze rollback, descriptor inheritance, exit/wait, and final resource baseline |
| W37-W39 | B03-O04-O07/O09 and B05-O01-O03 | Scheduler return/preemption plus both invalid-context panic variants |
| W40-W42 | B05-O08 and B09/B10/B11 service images | Checked standalone service ELF and seed artifacts |
| W43 | B05-O09, B07-O04, and B12 fixture inputs | Checked application ELF/seed artifacts only; no runtime execution |

Operations whose mechanism is absent or only partially represented remain
explicitly traced:

| Operations | Executable-evidence status |
| --- | --- |
| B03-O08 | No user-scheduler/load-history evidence; SOM-02 and SOM-21 remain authoritative |
| B04-O02B | No receiver-filter install/match/clear/timed-receive evidence; SOM-24 remains authoritative |
| B04-O11 | W31 covers the retained-buffer panic; the broader deferred IPC mechanisms remain staged |
| B07-O09 | W23-W25 cover current fatal role/self-fault policy, not ordinary page-fault recovery or dynamic VM breadth |
| B08-O09 | No deferred fork/exec/signal/tracing/process-interface evidence |
| B09-O11 | No full termios/PTY/getty/session-accounting evidence |
| B10-O11 | No persistent-storage, complete zone tree/cache, or broader filesystem-interface evidence |
| B11-O12 | No complete POSIX VFS breadth or multi-worker/concurrent suspension evidence |
| B12-O10 | Current target faults terminate through fatal policy; no fault-generated PM/VFS/VM cleanup exists |
| B13-O01-O10 | Only B13-O04 prerequisites have partial evidence; no production init/shell/command workflow exists |
| B13-O11 | Deferred shell/session/POSIX/system-inspection evidence remains owned by the existing SOM rows |

The QEMU runner is an external host oracle for target output. It:

- matches generic markers as exact line content after `splitlines()` and uses
  full-match regular expressions;
- supports ordered and forbidden records;
- rejects timeout, nonzero exit, explicit target failure, and `not ok`;
- authenticates structured FDT, panic, trap, allocator, MMU, ownership,
  object, scheduler, endpoint, IPC, and fault reports; and
- resolves selected fault PCs from the checked ELF with `nm`.

Structured record parsers and serial-input triggers enforce their own exact
termination/order grammar. A generic final marker does not require a trailing
newline.

Its negative host tests cover missing, malformed, duplicate, reordered,
forbidden, timeout, panic, serial-trigger, and symbol-resolution cases.
B14 found no failure-to-success conversion in the pinned runner contract.

The 38 target workflows intentionally compile explicit test variants. The
secondary manifest exposes every selected production conditional. W02 is the
only option-free kernel smoke image; W40-W43 authenticate standalone image
shape. A test-variant pass proves only the shared path and explicit scenario
that the recipe selects. It does not silently certify an option-free
production service/session policy.

## Generator, checker, and source-transport evidence

Fixture and seed generators have deterministic byte tests, boundary/malformed
input cases, and exact generated-input declarations. Host ELF checkers parse
the built artifact independently and enforce segment, symbol, instruction,
resident-page, startup, and application policy. Resident-page checks include
the configured number of stack pages but do not measure runtime stack depth.

The source-layout families correctly prove prior behavior-preserving source
transport:

- exact source-set and consumer reconstruction;
- function/body ownership and private-header boundaries;
- bounded automatic/file-scope state;
- migration lists reduced to empty;
- original body, token, AST, data, and recipe reconstruction; and
- negative mutations that must break those proofs.

They compare the current split tree with earlier `micros` commits and frozen
project-owned identities. That establishes decomposition fidelity only. It
does not establish MINIX structural/algorithmic parity and cannot become the
authority for accepting a correction to known-divergent behavior.

## B01-B13 evidence-gap disposition

All **101** evidence gaps recorded by B01-B13 remain open. B14 found useful
semantic, model, target, artifact, and failure evidence, but no merged oracle
that proves the missing property named by any gap.

| Unit | Open evidence IDs | B14 disposition |
| --- | --- | --- |
| B01 | B01-E001-E006 | Parser/rescan counters and bounded UART/panic correction scenarios remain absent |
| B02 | B02-E001-E008 | Allocator, mapping, validator, release, handoff, and VM-plan work counters remain absent |
| B03 | B03-E001-E007 | Validator/return/completion counts and direct stack proof remain absent; load history remains staged |
| B04 | B04-E001-E008 | IPC validator/deadlock/token/queue counts and stack proof remain absent; filters/asynchronous IPC remain staged |
| B05 | B05-E001-E007 | Grant/copy/runtime work and stack proof remain absent; delegated/vectored grants remain staged |
| B06 | B06-E001-E006 | Lookup/transition/validation/copy counts and stack proof remain absent; recovery/concurrency remain staged |
| B07 | B07-E001-E007 | VM validation/allocation/copy/storage and stack proof remain absent; dynamic VM breadth remains staged |
| B08 | B08-E001-E007 | PM validation/index/order/copy and stack proof remain absent; tracing/rusage/core remain staged |
| B09 | B09-E001-E010 | TTY cadence, raw-ring, ordering, backpressure, boundary, transfer, line-error, and stack proof remain absent |
| B10 | B10-E001-E008 | RAMFS scan/allocation/transfer/order/seed and stack proof remain absent; persistent-zone breadth remains staged |
| B11 | B11-E001-E008 | VFS validation/index/reference/copy/descriptor/chdir and stack proof remain absent; concurrency remains staged |
| B12 | B12-E001-E009 | Composed success/rollback/activation/exit/transfer costs and stack proof remain absent; fault/concurrency breadth remains staged |
| B13 | B13-E001-E010 | Corrected sort/snapshot/command/client/init and QEMU/stack evidence cannot exist before corrected implementation |

Eleven gaps require direct proof against a fixed 4 KiB service/application
stack or 16 KiB supervisor stack:

```text
B03-E006 B04-E006 B05-E006 B06-E005 B07-E005 B08-E006
B09-E009 B10-E008 B11-E006 B12-E007 B13-E007
```

The first ten are executable in the pinned tree. B13-E007 is a future
admission check because the pinned tree has no node-Q production path. The
pinned build has no `-fstack-usage` output, checked static call-chain
composition, or target stack high-water/canary report for the current paths.

## Difference summary

| Evidence area | Classification |
| --- | --- |
| Explicit error accumulation, nonzero failure, and complete test inventory following MINIX test practice | Baseline parity |
| CTest, CMake workflow, and deterministic model organization for the hosted/RISC-V build | Required adaptation |
| Host-side ELF, generated-byte, and QEMU transcript authentication | Compatible extension |
| Generation-safe target reports and forbidden/ordered marker checks | Compatible extension |
| Actual test-build preprocessor conditionals exposed in one closed manifest | Compatible extension |
| Historical source/body/AST reconstruction for behavior-preserving splits | Compatible extension |
| Treating historical `micros` identities as MINIX baseline authority | Divergence requiring correction |
| No operation-count/topology/complexity proof for audited hot paths | Divergence requiring correction |
| No direct dynamic/static proof for accepted stack budgets | Divergence requiring correction |
| No node-Q production or shell workflow while B13 remains blocked | Divergence requiring correction |
| Missing future POSIX mechanisms under their existing SOM owners | Staged substitution |

## Findings register

### B14-F001: Historical source-identity oracles are not MINIX baseline oracles

- Severity: High
- `micros` evidence:
  - `tests/host/kernel_layout_source.py`
  - `tests/host/ast_evidence.py`
  - the subsystem `test_*_source_layout.py` families
  - frozen reviewed commits, complete-body/token/AST digests, and reconstructed
    CMake identities
- MINIX evidence: behavior-focused `minix/tests` and ATF output/error oracles;
  no corresponding authority that declares an earlier `micros` body correct
- Difference: the source-layout gates prove that a decomposition preserves
  an earlier `micros` implementation. B01-B13 now establish that many of
  those preserved algorithms and validation boundaries are not the required
  MINIX baseline.
- Impact: an intended correction either fails the historical identity gate or
  is accepted by rebaselining the gate from the edited source. The first
  blocks correct work; the second is circular self-approval and does not prove
  the replacement mechanism.
- Smallest correction boundary: before each affected production correction,
  add an independent MINIX-derived structural/operation oracle and its
  negative controls. Then narrow or retire only the historical body identity
  invalidated by the reviewed correction while retaining source ownership,
  recipe closure, private-boundary, and unrelated restoration proofs.
- Prerequisites: none.
- Blocks: B14-F002 and every later correction that changes a frozen
  function/body until its independently reviewed evidence transition exists.

### B14-F002: The executable suite does not prove audited operation or complexity bounds

- Severity: High
- `micros` evidence:
  - 35 native CTests and 38 QEMU workflows
  - replayable model operation-coverage counters
  - local one-call fixture counters such as TTY copy validation
  - no complete counter for the work terms defined by B01-B13
- MINIX evidence: fixed data structures and algorithms traced by B01-B13
- Difference: current tests establish semantic outputs, model agreement, and
  target transitions at fixed capacities. They do not count comparisons,
  probes, record visits, queue shifts, complete-validator invocations, copied
  bytes, retained ranges, or composed cross-owner work.
- Impact: all existing suites can pass while the 76 pre-node blocking runtime
  findings
  retain quadratic scans, multiplied whole-state validation, byte-serial
  copies, wrong queue topology, or duplicate snapshots. Fixed capacities and
  wall-clock success cannot prove asymptotic parity.
- Smallest correction boundary: create one reusable, test-only typed
  operation-counter interface whose counters are owned at the primitive work
  sites and are absent from production images. Each correction begins with a
  deterministic native or target Red case over boundary, sorted, reverse,
  equal-key, sparse, and adversarial inputs and checks the exact accepted
  structural/complexity envelope. Cross-owner corrections compose, rather
  than estimate, owner counters.
- Prerequisites: B14-F001.
- Blocks: every B01-B13 evidence gap and correction that requires a counter,
  topology, transfer, or composed-cost proof.

### B14-F003: Resident-page checks do not prove runtime stack bounds

- Severity: High
- `micros` evidence:
  - ELF resident-page limits and declared stack-page counts
  - no compiler stack-usage artifacts
  - no checked call-chain composition
  - no target high-water/canary report
- MINIX evidence: not directly reusable because `micros` has fixed 4 KiB
  service/application stacks and a fixed 16 KiB supervisor stack
- Difference: image policy proves that a stack page is present; behavior tests
  prove only that selected scenarios did not visibly overflow. Neither
  establishes maximum dynamic depth for the audited nested validator,
  scheduler, IPC, grant, spawn, rollback, exit, or command paths.
- Impact: known multi-kilobyte automatic plans can nest on the 16 KiB
  supervisor stack, while PM/VM/VFS/RAMFS/TTY and applications have only
  4 KiB. A green workflow can miss an unselected worst-case path or leave an
  unsafe margin.
- Smallest correction boundary: add compiler-produced per-function stack
  usage with a checked target-call graph for nonrecursive paths, explicit
  bounded handling for indirect/assembly edges, and deterministic target
  high-water evidence for the ten currently executable B01-B12 path
  families. Fail on unknown edges or budget excess; do not infer safety from
  ELF resident pages. Register B13-E007 as a mandatory admission check that
  must run after corrected node-Q production code exists and before final
  recertification.
- Prerequisites: none.
- Blocks: the ten current stack evidence gaps and later correction designs
  that alter call depth or automatic storage; B13-E007 remains blocked until
  node-Q implementation and then blocks final recertification.

## Staged omissions

B14 adds no runtime omission. It confirms that deferred executable evidence
remains owned by each existing SOM row and may be added only after that
mechanism's design is dependency-ready. Missing node-Q evidence is not a new
omission: B13 has no production implementation in the pinned tree.

## Unit conclusion

The B14 primary and secondary manifests are closed: all 636 primary evidence
paths have consumers; all 1,035 traced Python fixture paths name both W01 and
W44; 1,058 secondary rows have pinned authority; five additional rows have
explicit live W01/W44 fixture authority; and all 35 CTests, seven native
workflows, 38 QEMU workflows, four image workflows, generated outputs,
conditionals, and explicit bridges are represented.

The current evidence is credible for semantic behavior, deterministic model
agreement, artifact shape, authenticated target reports, and visible failure.
It is not sufficient to certify the complete MINIX structural/algorithmic
baseline. B14-F001 through B14-F003 remain blocking, and all 101 inherited
evidence gaps remain open.

B14 is **audited but not certified**. The complete audit may proceed to the
consolidated findings register and correction DAG; no production correction
or node-Q implementation may start before that consolidation is reviewed and
accepted.

## References

- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [B14 evidence-role manifest](minix-complete-baseline-b14-evidence-manifest.md)
- [Testing strategy](../testing-strategy.md)
- [Validation evidence](../architecture/validation-evidence.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)

# Evidence Foundations Re-certification and EV04

## Scope and acceptance point

This is the acceptance-only `EV04` record in the
[correction DAG](minix-complete-baseline-findings-and-correction-dag.md).
It evaluates the separately merged EV01, EV02 and EV03 implementations; it
introduces no new design, implementation, production annotation, identity
retirement, runtime correction or workflow.

EV04 opens only when this record is merged after its exact current-candidate
validation and independent review. The pull request records the author HEAD,
staged tree, selected tool context, exact commands/results and review
disposition. Documentation alone does not supply an execution result.

The scope is evidence infrastructure, not complete MINIX parity. Node Q
remains behind `BASE01`, B13 stack admission remains at `STACK13`, and later
ordinary feature work remains behind `RECERT01`.

## Merged prerequisites

| Foundation | Implementation | Outcome commit | Re-certified boundary |
| --- | --- | --- | --- |
| EV01 | #177 | `81abd08b1ac6f24c2df5b2980fbe518a7e3f7c8d` | Exact authority inventory and fail-closed per-correction transition |
| EV02 | #179 | `fc98edd26505ac522d3752ad85f09366e8085f32` | Test-only primitive observations, complete composition and anchored projection |
| EV03 | #184 | `4668f47bafd9fe6f9e4791fe104a511c499547e6` | Complete current compiler/call-graph and target stack evidence |

Each implementation passed its separate independent review. EV03 follows
separately merged designs #180, #181, #182 and #183. Its sole production
exception is ADR-0087's phase-bounded VM temporary lifetime; no runtime
algorithm or complete-validation cadence was corrected by that exception.

The fixed MINIX reference remains
`4db99f4012570a577414fe2a43697b2f239b699e`. This record does not change the
pinned B01-B14 audit commit, historical manifests or original finding counts.

## Authority and operation evidence

The current authority inventory materializes 3,732 facts in nine closed
families: 3,672 transport identities, 51 permanent invariants and nine
artifact contracts. Thirty-six current identity-owning modules have declared
ownership. The original six-family facts remain independently checked; EV02
and EV03 add only their reviewed artifact, binding and lifetime facts.

These counts describe the authority inventory, not all semantic tests or
already completed production corrections. No real production identity is
retired by EV04. The synthetic transition remains a test of
[ADR-0082](../adr/0082-evidence-authority-transition.md), not permission to
skip another correction's independent Red, Green and affected-unit evidence.

EV02's `work-counts` and `work-observation-disabled` CTests belong to both
native tiers. Their hosted controls check complete producer composition,
primitive placement, comparison evaluation/precedence and the freestanding
RV64 boundary. Production annotations and each algorithm's quantitative
oracle still belong to its later owning correction under
[ADR-0083](../adr/0083-test-only-operation-count-evidence.md).

The authoritative mechanisms remain in the
[authority contract](../architecture/evidence-authority-transition.md) and
[operation-count contract](../architecture/operation-count-evidence.md);
this acceptance record does not duplicate their state machines.

## Current stack closure

The merged [stack contract](../architecture/stack-bound-evidence.md) and
`test-stack-evidence` cover all 42 current configurations and 111 declared
ELF outputs, including standalone images and complete-file runtime copies.
The ten observed scopes are:

| Audit row | Existing scenario |
| --- | --- |
| B03-E006 | Scheduler |
| B04-E006 | IPC syscalls |
| B05-E006 | Grant syscalls |
| B06-E005 | Bootstrap launcher |
| B07-E005 | VM scratch preemption |
| B08-E006 | PM service |
| B09-E009 | TTY |
| B10-E008 | RAMFS |
| B11-E006 | VFS |
| B12-E007 | ELF spawn |

All rows require both the actual metadata/observed image proofs and complete
authenticated guest observations. Compiler frames, finite callback/assembly
bindings, saved continuations, trap snapshots and fatal qualifiers remain
mandatory. Original live artifact seals and source/tool identities survive
through atomic complete publication. Metadata passivity does not permit
replacing observed images with ordinary bytes.

The 4 KiB and 16 KiB capacities, application startup occupancy, trap/panic
frames, original markers and deadlines are unchanged. A sampled high-water
value is not an upper-bound oracle. The current VM preparation path's
3,744-byte bound remains below 4,096 bytes without a budget increase.

B13-E007 remains exactly `BLOCKED / production-node-q-absent / STACK13`.
The other inherited work/topology gaps are not closed by the availability
of counters or by these ten stack rows. Later changes must re-certify their
affected graphs and consumers rather than borrow old certificates.

## Current-candidate gate

The acceptance PR executes the existing full fresh author command from the
[build guide](../development/building.md):

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py \
  --tier full --execute --fresh --base origin/main
```

Its 48 canonical entries include complete native discovery and the 37 CTests,
all 38 ordinary QEMU workflows, four standalone-image workflows, the complete
stack gate, documentation and three independent Git diff checks. No
per-foundation command substitutes for that full intent set. The final
report must be authoritative and final-candidate, with every command freshly
successful and the stack bundle bound to the same candidate.

Clean staging, no untracked task files, exact command/results and independent
review follow ADR-0034. Author evidence remains bound to its actual local
source/tool context; it is neither a remote CI result nor an algorithmic
complexity certificate. An unknown edge, excess stack bound, malformed
observation or failed required current-candidate gate keeps EV04 closed.

## Separate hosted CI blocker

The [parallel CI design](../development/parallel-ci-validation.md) adds no
runtime-DAG prerequisite to EV04. Its attempted implementation #188 is
closed without merge and retained at
`2707f078a38876f519648b18042f5159ea873712` on
`archive/github-actions-parallel-hosted-timeout`.
There is no operational Actions workflow on `main`.

Actual hosted run
[41354631730](https://github.com/algorithm-apprentice/micros/actions/runs/41354631730),
attempt 1, tested merge commit `707bf7d9e641b6b3d2825fa4a2bdf74aa1badf4d`
and tree `1c23bcfa4b265f9b9c2ec8766a2600ccae0c3e9f`.
Five producer execution steps overlapped from 21:59:14 to 22:02:14 UTC on
2026-11-01. Native and targets 2, 3 and 5 completed, but observed PM service
timed out at 45 seconds and observed ELF spawn at 180 seconds. Their ordinary
counterparts and metadata proofs had passed. The final collector rejected
the failed matrix and published no aggregate.

All six producers used macOS 26.6.2, LLVM 23.1.4, QEMU 11.1.2 and the exact
Accepted OpenSBI bytes. Local author execution used QEMU 11.1.3. The patch
difference is not evidence of a fix: the inspected
[upstream WFI correction](https://github.com/qemu/qemu/commit/963eb3cb8553ca25e14401b3bc53cdff0c59c526)
addresses `icount` accounting, which the project command does not enable.

A separate, non-acceptance local diagnostic sampled 32 PCs and caller chains
from the observed PM image
`dedfad75697724f69a331bfd53edd10128e506ab12ec54a6b9fe07ce236b8f95`.
It used a temporary QEMU monitor and terminated its owned guest after
sampling; it was not a retry for acceptance. Twenty-eight samples were in
frame-ledger/process-owner validation or its observer hooks during bootstrap
image allocation and verification. The recorded callers included:

```text
micros_frame_ownership_runtime_validate
  <- load_validation_context
  <- micros_user_address_space_validate_locked
  <- micros_user_address_space_allocate_page / micros_user_address_space_lookup
  <- bootstrap_preparation_allocate_image / bootstrap_preparation_verify_image
```

This corroborates the already audited
[B02-F002/B02-F005](minix-complete-baseline-b02-memory.md) valid-path costs.
The remote timeout PC was not captured; these local samples do not claim a
remote instruction trace or retire either finding.

The failed hosted attempt remains failed. It is not carried into EV04 as
passing evidence, and a local Green cannot approve CI. Existing runtime
corrections proceed through their accepted dependencies rather than being
inserted into the CI change. After the relevant corrections, CI must restart
from current `main` and pass the complete same-attempt hosted gate, final
independent review and main-run verification. No timeout, stack budget,
compiler mode, hook coverage or test is relaxed by this record.

## Disposition

EV01-EV03 close the three B14 evidence-infrastructure findings at their
defined boundaries. Merge of this current-candidate acceptance record opens
EV04 and permits only a dependency-ready correction's separate design phase.
The original 76 pre-node runtime findings remain owned by their existing
nodes; `BASE01`, node Q, `STACK13`, `RECERT01` and complete POSIX acceptance
are not satisfied.

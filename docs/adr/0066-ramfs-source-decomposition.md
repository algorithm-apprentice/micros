# ADR-0066: RAMFS Source Decomposition

- Status: Accepted
- Date: 2026-10-15
- Refines: ADR-0025, ADR-0032, ADR-0033, ADR-0034, ADR-0048, ADR-0051,
  ADR-0064, and ADR-0065

## Context

The source-module audit assigns seven RAMFS production, native, target, and
generator sources to one dependency-ready maintenance outcome. At the
reviewed base `4290ca21c06ac88b299d694c236b774896cb6171`, the six C sources
contain 8,454 physical lines, 189 definitions, and sixteen functions above
120 lines. The 293-line fixture generator also hides descriptor validation,
budget calculation, and rendering in one function.

The current implementation already has the behavior required by ADR-0048.
Its whole-state validator, message codec, node/reference ownership, namespace,
regular I/O, directory enumeration, independent reference model, functional
cases, and authenticated target evidence should not share monolithic owners.
The correction is not another filesystem feature or a new runtime-DAG node.

The fixed MINIX source-organization trace is recorded in the
[RAMFS organization study](../research/minix-ramfs-source-organization.md).
The existing VFS/MFS study remains the sole classification ledger for initial
RAMFS behavior. Accepted ADR-0048 remains authoritative.

## Decision

Apply the exact
[RAMFS source contract](../architecture/ramfs-source-organization.md).

The separate implementation exposes eight portable production owners and
keeps the existing seed parser and target service adapter unchanged.
Five evidence families expose native fixtures/cases, an independent model,
the target VFS peer, and the kernel fixture/report. Six ordered CMake sets
contain exactly 32 C sources, with six declarations-only private headers.

The canonical contract assigns every original C definition to one owner:
180 definitions move and nine remain at their existing roots. Sixteen
over-limit C functions and the Python renderer receive named semantic
phases. Shorter operations also expose their existing preflight, staging,
commit, and final-validation boundaries where specified. There is no
line-count exception and no permission to compress statements.

Private cross-module helpers use family prefixes. Ordinary helpers receive
the existing state explicitly; they do not obtain new ambient authority.
The six private headers do not enlarge any public API. Dependency graphs
remain acyclic and independent expected-state code does not use production
private helpers.

Existing mutable test storage gains explicit owners without changing reset
boundaries. In particular, the model's load-once seed survives reset of the
running model; the target peer's initialized data word remains separate from
its BSS buffers and its service-configuration ABI object.

## Preserved contracts

Preserve all public declarations, layouts, wire values, exact VFS authority,
generation checks, root pin, reference/link accounting, fixed capacities,
seed bytes, sparse data behavior, grant direction and revocation,
scratch-clear exits, and protocol versus invariant-failure distinctions.
Preserve validation, copy, mutation, reply, readiness, report, and shutdown
ordering rather than merely final successful results.

Native case order, complete snapshots and comparisons, the model's 8,192
steps, seed, draw order, operation trace, capacity phase, coverage, target
scenario, report assembly, markers, timeouts, and image budgets remain
unchanged. Existing targets, CTests, labels, presets, and workflow identities
are not multiplied by the source split.

Every new path inherits its original owner's full-tier, QEMU, separate-model,
and image classifications independently. The source-layout gate itself is
host-unit-only; recognizing new native paths must not narrow the original
functional tests' full-tier fallback.

The CMake source-set move changes the recipe digest admitted by the initial
ADR-0064 profile. The implementation may update that one literal digest only
after independently reconstructing the complete reviewed-base CMake recipe
by reversing the exact six source-set additions and their consumer operands.
All other recipe bytes and closure rules remain unchanged. Current-candidate
native, smoke, and TTY-image eligibility must remain real, not silently
disabled. No new reusable workflow, platform, tool family, configuration
exception, or receipt-equivalence rule is authorized.

## Non-goals

No unlink, rename, truncate, credential, device-node, mount, persistent
storage, heap, cache, recovery, retry, concurrency, protocol, capacity,
allocation, init, shell, command, or directory-layout behavior is added.
The cohesive seed parser, production service loop, unrelated backend shims,
QEMU runner, and validation execution algorithms are not refactored here.
Existing invariant-only failure branches are not redesigned as transactional
recovery during source movement.

## Evidence and phase barrier

The design contains documentation only. Its paired implementation stays
pending until independent read-only design review is clean, this ADR is
Accepted, and the separate design pull request is merged.

Implementation begins with a failing structural gate. Frozen owner,
definition, storage, semantic-outline, CMake, and validation inventories
come from the immutable reviewed base, not the changing extracted sources.
Literal owner and semantic migration collections must both be empty before
implementation review. Bounded independent positive/negative fixtures prove
that the gate detects authority, predicate, effect, ordering, and
caller-exit drift; no general C interpreter is introduced.

Small green extraction commits retain the narrow existing native/model and
target evidence. One complete current authoritative PR plan precedes
independent implementation review. Shared CMake ownership currently selects
35 native CTests, Python discovery, 38 QEMU workflows, four standalone images,
documentation/structure, and three diff checks. These are workflow identities,
not permanently fixed future suite counts.

Reviewers inspect the author's exact candidate and evidence rather than
routinely rerunning those gates. Acceptance, correction, staged-tree
verification, squash-tree equality, and exact obsolete-branch cleanup follow
the agent contract and ADR-0034.

## Design review disposition

Independent read-only design review identified one model dependency cycle:
the reference record writer and protocol request builders share two existing
LE writers. Assigning those writers to the protocol owner would create a
back-edge. The corrected contract places both in the existing fixture leaf
and retains reply readers in protocol. Original bodies, counts, and evidence
identities are unchanged. Independent bounded correction review found no
substantive issue remaining; no runtime reproduction was needed.

Acceptance records that clean design result, not implementation completion.
The dependent implementation starts only after this separate design merges.

# ADR-0067: Kernel Endpoint Source Decomposition

- Status: Proposed
- Date: 2026-10-15
- Refines: ADR-0025, ADR-0029, ADR-0030, ADR-0032, ADR-0033, ADR-0034,
  ADR-0038, ADR-0043, ADR-0050, ADR-0051, ADR-0064, ADR-0065, and ADR-0066

## Context

The source-module audit assigns four endpoint production and evidence files
to one dependency-ready maintenance outcome. At reviewed base
`7e107f5189cbf6f3aa44dfc05652d041e803c60c`, these files contain 5,457
physical lines, 98 C definitions, and ten functions above 120 lines.

The portable endpoint file combines canonical representation, profile-table
construction, shallow registry validation, complete object/queue/completion
validation, publication, source-only sealing, close, and authorization.
The native and target evidence likewise combines fixture setup, actions,
denial proofs, model transitions, and cleanup.

The fixed
[MINIX endpoint source study](../research/minix-kernel-endpoint-source-organization.md)
traces the corresponding representation, resolution, privilege, inhibition,
and teardown owners. The existing endpoint/IPC study remains the behavioral
classification ledger. Accepted endpoint, IPC, grant, bootstrap, and PM
contracts remain authoritative.

## Decision

Apply the exact
[kernel endpoint source contract](../architecture/kernel-endpoint-source-organization.md)
in a separate implementation after this design is reviewed and merged.

The implementation exposes ten portable owners, seven deterministic-native
owners, five model owners, and three target owners: exactly 25 C sources,
four new private headers, and four ordered CMake source sets.
Ninety original definitions move; eight remain at their existing roots.
All ten original long functions receive the specified semantic disposition.
The shorter registry initializer also exposes its candidate-building,
target-closure, and non-failing publication boundaries.

The existing public endpoint header and six-function inter-subsystem
prevalidated header remain byte-exact. The new production private header
contains only the specified helper declarations, include guard, and existing
registry-magic definition. Only 31 existing file-local helpers require
cross-owner visibility. New phase helpers remain file-local unless the
canonical evidence contract names their cross-file role.

### Acyclic validation

Canonical profile construction belongs to the representation leaf, not to
the checked profile-query owner. Checked profile queries call shallow
registry validation; that validation calls canonical construction.
Combining those responsibilities would introduce a source-level back-edge.

Complete object validation calls registry, completion, queue, and profile
validation. It does not call public active-endpoint resolution, publication,
close, IPC mutation, or grant mutation. Pure identity/ownership queries assume
the caller's existing validation boundary; source movement does not add a
recursive whole-state validation.

### Storage and failure boundaries

Native and model fixtures each consolidate only their five original mutable
objects, with separate fixture identities and exact existing reset sites.
Independent model state, coverage, random state, step values, snapshots,
candidate profiles, queue witnesses, and target work remain automatic.
There is no second target registry or new mutable target singleton.

Target entry retains its single IRQ-save/restore lifetime and common failure
exit. Readonly profiles, scripts, operation names, and the 25-case native
table retain their types, values, static duration, and unique ownership.
Raw source ranges, not lexically masked declarations, prove their literals.

Source-only state retains both accepted uses: strict launcher sealing with
clear held threads, and PM teardown with the exact stopped reply-wait state.
Unchecked prevalidated commits remain non-failing and externally serialized;
they do not become general authority-bearing operations.

### Build and evidence coupling

Every new source/header inherits its original owner's full-tier, QEMU,
separate-model, and image classifications independently. The structural gate
is explicitly host-unit-only. Neither selective ownership nor fixture
movement narrows existing executions.

The source-set move changes the CMake recipe admitted by the initial
ADR-0064/0065 profiles. The implementation may maintain that one literal
digest only after independently reversing exactly four source-set additions
and nine source operands to the complete reviewed-base recipe. The prior
RAMFS and trap/syscall immutable recipe/planner oracles remain intact under
only that exact additional projection. No closure or execution algorithm,
tool admission, reusable workflow, or receipt-equivalence rule changes.

## Preserved contracts

Preserve endpoint packing and special values, process-generation authority,
quarantine, immutable profile bytes, operation-specific masks, argument and
corruption precedence, no-mutation-on-failure, output publication, one consumed
endpoint lifecycle per process generation, and complete cross-object closure.

Preserve sender/receiver witness sharing, bounded queue walks, exact token
uniqueness and staged ownership, notification source rules, source-only
readiness acknowledgments, and the stopped PM reply wait. Closing an endpoint
does not clear the process's consumed-lifecycle flag.

Preserve all 25 deterministic case labels/order, the existing external case
calls, both model link consumers, the four-slot 4,096-step model, seed and draw
schedule, 17 scripted steps, 32-entry failure trace, 19 coverage counters,
cleanup, real runtime initialization probes, grant cancellation/reuse,
pass marker, and eight-second target timeout.

## Non-goals

No endpoint, privilege, process, thread, IPC, grant, bootstrap, PM, scheduler,
or address-space behavior changes. No new profile, public endpoint, syscall,
token, state, capacity, thread policy, concurrency, recovery, caching, heap,
directory layout, init, shell, command, or validation optimization is added.

The kernel-object correction, grant data-plane correction, QEMU runner, and
kernel directory-layout task remain separate. Existing coherent IPC/grant
runtimes, public headers, external native cases, and model drivers are not
refactored here.

## Evidence and phase barrier

This design contains documentation only. Its paired implementation remains
pending until independent read-only design review is clean, this ADR is
Accepted, and the separate design PR merges.

Implementation starts with a failing structural gate. All 98 original bodies,
90 owner moves, ten long functions, readonly objects, fixture schemas,
execution identities, and complete recipes are frozen from the immutable
reviewed base, never from evolving extracted code. Finite typed and
scope-aware projection plus independent positive/negative fixtures verifies
the permitted transformations without introducing a general C interpreter.

Small green slices retain narrow existing native/model and actual target
evidence. Before implementation review, both active migration collections are
literal empty tuples and the complete current staged candidate passes one
authoritative PR plan plus additional real native/smoke/TTY-image reuse
acceptance. Shared CMake ownership currently selects all 47 author commands.
Reviewers inspect the author's exact evidence rather than routinely rerunning
it. Exact reviewed-head squash, identical tree verification, and obsolete
local/remote branch cleanup follow the agent contract.

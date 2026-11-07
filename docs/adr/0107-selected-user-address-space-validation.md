# ADR-0107: Selected User Address-Space Validation

- Status: Accepted
- Date: 2026-11-07
- Refines: ADR-0021, ADR-0022, ADR-0026, ADR-0035, ADR-0039, ADR-0040,
  ADR-0047, ADR-0050, ADR-0053, ADR-0055, ADR-0062, ADR-0070, ADR-0080,
  ADR-0082, ADR-0083, ADR-0084, ADR-0100, ADR-0103 and ADR-0106
- Supersedes only: complete-audit invocation and corrupt-state precedence
  for the selected operations named below; existing state, mutation,
  authorization, sealing and complete-diagnostic invariants remain

## Context

MEM02 and MEM03 are independently reviewed and merged. The separate
EV01-C05 design #220 and implementation #221 provide precise existing
MEM03 native-source identities without changing a guard or runtime
algorithm.

The fixed current source is #221,
`f28377decc0d20533e05330f2f2211db3f230910`, tree
`4b56076479ac53092c4c83d9c9b52829b66139ba`.
The [source study](../research/minix-selected-user-address-space.md) traces
the fixed MINIX walk/bind mechanisms and actual current callers.

An ordinary user-address-space operation currently clears two capacity-sized
scratch prefixes, traverses the complete private tree and scans active frame
owners before using its selected PTE path. Active exception-authority
getters additionally reach complete VM mappings, endpoint registries and
kernel objects. Removing only the visible page-tree call would leave those
hidden costs.

The underlying three-level walker relies on complete prevalidation before
physical dereference. A correct replacement must establish selected table
ownership, generation, PTE shape and exception binding before consuming
those addresses.

## Decision

After separate review, Acceptance and merge, implement MEM04 according to
the [selected user-address-space contract](../architecture/selected-user-address-space.md).
The outcome closes the selected-operation scope of B02-F005/B02-E004.

### Selected operations, not a new global audit

Replace complete-root preflight in:

- page lookup;
- byte translation;
- bootstrap page-allocation preflight;
- process-root activation; and
- scheduler selected-thread root preparation.

Bootstrap allocation is included because its per-page complete audit is not
a one-time root certification. Keep its allocation order, lowest-free-frame
policy, permissions, output publication and bootstrap-only phase. Retain
reverse-order compensation and restore the exact saved allocator cursor
after it, as required by the existing failure-atomic contract.

The complete validator, root construction/certification, release/destruction,
inventory, wired handoff, all-process VM checks and other BOOT/VM/TTY command
preflights retain their explicitly assigned responsibilities. The exact
remaining-call ledger in the contract prevents an implicit waiver or a false
claim that every ordinary complete audit has disappeared.

### One selected path with checked physical authority

Bind the requested process handle explicitly. A reused runtime view is not
a selected-process capability: all-root validation overwrites its process,
handle and root as it enumerates.

Use the existing Sv39 decoder and one fixed three-level path. Check each
consumed private table's managed/allocated state, exact process-table owner
and generation before dereference. Check ancestor identities with fixed
scalar comparisons; no reachability bitmap is needed for this path.

Validate a present leaf's user/PTE shape and its exact phase-specific owner
or exceptional binding. Preserve stored permission reporting and page-local
translation span. Never turn a transferable frame or MMIO address into
ordinary IPC/grant-copy authority.

Retain an operation-local path prefix for page allocation. Do not validate
one path and then independently rediscover a possibly different plan.
Existing prevalidated mutation/compensation helpers remain responsible for
allocation and publication.

The fixed source does not save that higher-level cursor. A valid cursor may
lag allocated words: allocating frame 128 from cursor 64, then compensating
it, rewinds only to 128. This violates ADR-0021's unchanged-allocator
failure rule even though the lower search bound remains valid. The selected
allocation plan therefore saves the existing cursor and restores it after
all unpublished frames are released. No allocator API, primitive search/
release rule or general rollback framework changes.

### Inspect committed exception bindings without authorizing commands

Add narrowly named, read-only binding inspectors in the owning VM and TTY
components. They inspect bounded selected metadata under the caller's
SIE-clear epoch and return the already committed process/root/slot binding,
including an inactive binding's identity.

These are not replacements for the existing complete authorization getters.
They must not inspect every VM mapping, search bootstrap binding tables,
validate endpoint registries, create a mapping, accept a syscall, release
a frame or certify a prepared-address-space seal.

The existing VM/TTY command paths keep their exact authority, phase,
generation, token and non-failing commit rules. Complete diagnostics keep
their original complete getters.

For a selected active exception slot, a missing or substituted leaf remains
an ownership failure. Root preparation probes the selected controller's
known fixed slots even when inactive, so a stale transferable/MMIO leaf
cannot be hidden by clearing the active-binding flag.

### Prepare roots before hardware commit

Add the kernel interface:

```c
enum micros_user_address_space_error
micros_user_address_space_prepare_activation(
    struct micros_process_handle process,
    uint64_t *root_physical_address
);
```

It requires SIE clear on entry, returns one checked root only on success,
and does not touch `satp`, scratch, PTEs or owner state. Its result is valid
only in that same non-mutating protected epoch; it is not a cache, seal,
token or reusable authorization.

Preparation checks the selected root, canonical shared-kernel entries,
the private-root descriptor and the finite selected exception slots.
It does not scan every private leaf or every owner.

Public activation retains its save/clear/restore scope and existing
fence/write/fence/readback sequence. The scheduler uses the new preparation
interface before its existing prevalidated-root commit. Do not add a
fallible check after that commit begins or adopt MINIX's same-root shortcut.

### Keep corruption diagnosis explicit

Successful point lookup certifies the selected mapping, not the whole root.
Unrelated malformed leaves, aliases, orphan owners or unrelated registry
records no longer outrank an absent or valid selected leaf.

This explicitly refines ADR-0040's complete-root-before-`NOT_MAPPED` rule.
It does not legalize those corrupt states: full diagnostics still reject
them, and every legitimate mutation must preserve the complete published
state invariants.

Root preparation still rejects consumed root/owner/descriptor errors,
shared-kernel disagreement and incorrect selected controller slots before
hardware activation. Caller PC/SP and requested-access checks remain in
their existing owners.

Retain the existing argument/runtime/phase/process/range ordering and
scratch `BUSY` boundary with a checked read-only idle guard. Eliminating
scratch use is not permission to change failure publication or interrupt
restoration.

## Work and storage bounds

For height `h = 3`, point selection consumes at most one PTE per level and
the selected table/leaf owners. There is no term in managed-frame count,
mapped-leaf count, process/thread/endpoint capacity or bootstrap-service
count. Retain the explicit inherited `O(g + k)` active-geometry cost from
MEM03.

Root preparation adds one fixed hardware-root comparison and at most two
fixed controller-slot walks. Page allocation adds its existing at-most-three
frame allocations and bounded page initialization; this decision does not
redefine the allocator's search bound.

Use fixed automatic path/binding objects only. No new kernel BSS, capacity
buffer, persistent validation cache, VLA or recursion is permitted. Preserve
all existing stack/image budgets and prove actual compiler/link/target
bounds in the complete acceptance gate.

## Evidence authority and acceptance

The contract identifies 33 prospective existing active identities, with a
28-path direct closure. Retire only the exact invalidated clauses; preserve
all 4,774 complete predecessor facts, nineteen fingerprints, five claims
and seventy prior retirements. One eight-fact family gives a 4,782-fact,
twenty-family catalog. The new family, fixtures, source/recipe
inverses and oracle expectations must be independently frozen before
production edits.

Require actual-source native work/state Red and actual failing QEMU
activation/absent-translation evidence against the fixed runtime. A missing
new API, compiler error, generic panic, timeout or source-gate scaffold
failure is not the required Red.

Green must preserve complete diagnostics, unrelated authority, existing
negative controls, failure publication and all target hardware boundaries.
Recertify B01-B12/B14, then complete the unchanged fifty-command author plan,
current same-run receipts and independent review before the first permanent
implementation commit.

The implementation must establish a native no-work state before its final
producer. Fast checks precede that final producer if they can rebuild
receipt-bound artifacts. Never repair receipt mismatch by relaxing identity.

## Phase boundary and non-goals

This is a documentation-only proposal. The paired implementation remains
pending until complete design evidence, independent review, Acceptance and
separate merge. No executable scaffold or prototype is authorized here.

No process-release index, handoff-set algorithm, all-process VM index,
scheduler queue redesign, grant batching, endpoint authorization rewrite,
new mapping lifecycle, page fault recovery, ASID, SMP, current process,
public syscall, capacity/budget increase, collector ABI change, generic
framework, hosted-CI acceptance or node-Q implementation is included.

## Design review disposition

Independent read-only review inspected the complete ten-document proposal,
relevant Accepted authority, fixed runtime/MINIX evidence, caller and
identity censuses, and current author evidence at staged tree
`a9e866e77770c42d244427688baa9db61b39923e`.
All four documentation-phase commands were FRESH/exit0 and bound to that
exact candidate. Runtime/native/QEMU/integration/stack checks were correctly
N/A for this phase, not waived.

The reviewer reported: "No substantive design issue remains." No gate rerun,
targeted runtime reproduction or additional reviewer was needed.
Acceptance records this design result; it is not runtime execution,
retirement or implementation evidence. Implementation remains pending the
separate design merge.

## Alternatives

- Delete complete validation and keep the old unchecked walker: permits
  physical table consumption without a selected owner proof.
- Keep the old exception getters: retains hidden VM/endpoint/object scans.
- Filter only by physical owner kind: misses the active controller's
  reserved-virtual-slot substitution rule.
- Cache a previous validation result: introduces invalidation and authority
  not required for a bounded walk.
- Treat the last process in a runtime view as the requested process:
  confuses an enumeration scratch view with selected authority.
- Keep complete activation checks only to preserve old test expectations:
  retains the audited divergence instead of specifying its correction.
- Remove complete diagnostics or broaden current VM/TTY authorization:
  unnecessary and outside the selected-operation contract.

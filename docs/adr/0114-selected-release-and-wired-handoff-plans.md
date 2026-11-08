# ADR-0114: Selected Release and Wired-Handoff Plans

- Status: Accepted
- Date: 2026-11-08
- Refines: ADR-0020, ADR-0021, ADR-0040, ADR-0045, ADR-0050, ADR-0053,
  ADR-0054, ADR-0076, ADR-0080, ADR-0082, ADR-0083, ADR-0084,
  ADR-0100, ADR-0103, ADR-0107, ADR-0109, ADR-0112 and ADR-0113
- Supersedes only after separate design merge: capacity-shaped
  selected-release inputs, process-owner discovery by global scans, and
  repeated per-process handoff ownership/thread discovery; their specified
  corruption/error scopes; and four MEM04 residual full-audit call sites

## Context

MEM04 is independently reviewed and merged. The separate EV01-C06 #230 and
EV01-C07 #232 prerequisites now name existing source and observation
comparisons without changing runtime behavior.
The [fixed-source study](../research/minix-selected-release-and-wired-handoff.md)
pins the current runtime at #232 and traces actual MINIX teardown/startup,
including the costs that must not be hidden by simplified summaries.

The remaining release path scans a 262,144-frame selection domain even for
one selected frame. Destruction walks the process root, scans all owners,
copies reachability through another global scan, releases through the
capacity bitmap and scans active frames again to clear them. Handoff repeats
owner and thread searches for each process.

These are B02-F006/B02-F007, not new allocator policy. Replacing the loops
without an exact membership structure would lose orphan, generation,
duplicate, count or failure-atomicity guarantees.

## Decision

After separate design merge, implement MEM05 according to
the [selected release/handoff contract](../architecture/selected-release-and-wired-handoff.md).
This Accepted design remains documentation-only; implementation cannot
begin before its separate merge.

### Index process-bound ownership without conflating generations

Keep the existing eight-byte semantic owner and availability bitmap.
Add a bounded intrusive index over compact frame indices:

- one previous/next pair per managed-frame capacity entry; and
- one head/count pair per process slot.

Links encode `frame_index + 1`; zero is the absent link.
Every process-bound owner occurs exactly once in its slot's list.
Non-process and unused records have zero links.

The portable API continues to permit independently encoded generations in
one slot. Enumeration filters the exact requested generation; it does not
silently assume a slot is a process identity.
Target validation still resolves exact live generations through the
authoritative object registry.

All owner transitions maintain the index in their existing protected commit:
bootstrap allocation/release, selected release, wired/transferable handoff,
VM claim/rollback, freeze and process release.
No recoverable index check is added after the first authoritative store.
Complete diagnostics cross-check the index against the original owner,
bitmap, phase, geometry, target and count authority.

Selected enumeration proves its consumed chain, not an arbitrary owner
omitted from both that chain and the private tree. Only whole-active/
complete diagnosis compares the independent physical owner domain.
The contract retains indexed-orphan and reachable-but-unindexed rejection,
and explicitly tests this scoped-versus-complete boundary.

### Represent a selected set by its nonempty bitmap words

Replace the internal bare-capacity-bitmap inputs for process-set release
and wired-set staging with the finite indexed selection specified by the
contract.
It retains a bitmap, a dense list of published nonempty word indices and
the inverse positions of those words.

Membership, addition and idempotent re-addition are selected operations.
Validation and cleanup visit published words only. The inverse relation
proves that an input word is not listed twice without a quadratic prefix
search or a capacity-sized temporary clear.

Inactive array slots are not members of the selected set. Existing
construction/cleanup maintains the kernel scratch's zero idle state, but an
ordinary selected operation does not turn inactive storage into authority.
No heap, reusable audit cache, epoch token or general container framework
is introduced.

One byte-valued idle/full/selected mode replaces the old scratch Boolean.
The exact owner mode, lease mode and storage pointers must agree.
Legacy full diagnostics retain full-prefix clearing; selected cleanup
visits only published prefixes.

### Separate bootstrap plans from complete diagnosis

Keep public complete root validation and its original exception authority
getters. Add bootstrap-only collection helpers in the existing address-space
validation owner.

They validate checked physical tables before dereference, exact private
root composition, 4 KiB process-user leaves, distinct reachability and the
expected process ownership set. They do not invoke complete VM/TTY command
authorization merely to inspect a bootstrap root.

Page release uses one checked selected path and the existing bounded
table-pruning inspection. It does not certify unrelated leaves or owners.
Destruction still certifies the complete selected process and requires
zero live threads and an inactive root.

Retain the zeroing, cursor, count, phase and output guarantees of the
existing operations. Destruction resolves its mutable process slot before
commit and clears the prevalidated root as its final non-failing store,
rather than discovering a complete object-audit failure after frames have
already been released.

### Compose one wired-handoff plan

Build one protected process summary from one active-owner pass and one
thread-table pass. Preserve the existing process-slot order for context
and root checks.

Keep all process/thread/context checks before the root-walk pass.
Walk each rooted process once, accumulate the complete user-frame selection,
and compare its reachable set with the precomputed exact-generation owner
count. Retain the current context checks and rooted/rootless process rules.

Root failures remain `STATE` at the handoff owner. A valid root with a
non-wired user target remains `OWNERSHIP`; do not turn every handoff error
into the same result. Preserve the lower whole-active classification and
irreversible non-failing ownership commit.

The summary is valid only in the same SIE-clear non-mutating epoch.
Preparation and completion build their own plans; nothing authorizes reuse
across VM startup or another operation.

### Preserve the remaining task boundaries

Do not redesign VM mapping/freeze policy, VM02's all-root preflights or
pairwise distinctness algorithm, PM lifecycle, scheduler queues, endpoint
authorization, direct grants, ASIDs, SMP or recovery.
Index maintenance in an existing VM commit is a required coherence edit,
not acceptance of those later algorithms.

The kernel remains a mechanism and bootstrap owner. VM remains the
ordinary post-handoff memory-policy owner.
No user-visible syscall, wire object or service ABI changes.

## Work and storage requirements

Ordinary selected release and scratch cleanup have no active/inactive
physical-capacity scan. Their work is proportional to selected frames
and nonempty selection words, with the inherited active-geometry cost
reported explicitly.

Selected process enumeration is proportional to that slot's indexed owners;
valid target state has one live generation, while portable mixed-generation
cases retain their exact semantics.
Destruction is proportional to the selected process's table walk and owners,
not unrelated physical memory.

Handoff retains one whole-operation object audit and linear process,
thread, active-frame and reachable-table work. It must not multiply a
global frame/thread scan by process count. The inherited object-audit
cost is reported separately, not called constant.

The index adds exactly 2,097,664 bytes at existing capacities.
The two indexed scratch selections replace the existing bitmaps and add
32,800 bytes of declared storage. No frame-capacity object is automatic.
The handoff process summary is a fixed 1,024-byte automatic object.
All existing stack/image budgets and supported RAM gates remain unchanged.

## Evidence authority and acceptance

The closed source census identifies 82 active transport identities and a
28-path direct closure. One explicitly frozen dependent target fixture makes
the candidate domain twenty-nine paths. Its old identity is already retired
by MEM04 and is not claimed again.
The architecture specifies the 41/21/20 oracle partition, exact existing
adapters, five current native products and 128 common operation rows.

Preserve all 5,379 predecessor records, twenty-two fingerprints, six claims
and 103 retirements. No broad source-projection, work-invariant or artifact
contract is retired to bypass a narrow source change.

Require actual fixed-source work Red for selected sets, process enumeration,
destruction and handoff, plus actual QEMU selected-release Red.
Missing types, compiler failures, source-gate failures, generic panics and
timeouts are not algorithmic Red.

Green must prove all index transitions, selected/complete corruption scopes,
byte-exact failure preservation, current helper/observation closure,
retained legacy native cases/bounds and real target behavior.
Then require B01-B12/B14 recertification, the complete fresh author plan,
current same-run receipts, complete stack coverage and independent review
before the first permanent implementation commit.

Preserve the historical MEM04 declarations and checks in their historical
scope. Its current audit ledger retains sixteen unchanged callers and the
four explicitly replaced plan boundaries. Current target evidence reports
truthful version-two activation rows and isolated selected-release rows;
it never fabricates the obsolete global-release failure.

The three-oracle eight-fact addition gives 5,387 facts in twenty-three
families. Actual terminal acceptance, not this design, permits seven claims
and 185 retired identities. The one full-only CTest gives 45 native CTests;
the fifty-command final plan, existing target/image/stack coverage and
fast-tier membership remain unchanged.

## Phase boundary

This Accepted design does not authorize implementation before its separate
merge. Current documentation evidence and independent review remain required
for publication.
No code, test, model, fixture, generator, build scaffold or reusable
executable prototype is authored during this design.

## Design review disposition

The first independent complete review found one High issue: two retained
MEM03 consumers were missing from the closed immutable adapter register.
The author verified the old-header current-fixture syntax check and the
historical freeze-body Green regression, then specified their exact
current-interface and isolated historical-policy adaptations.
The same inspection preserved MEM03's exact all-free diagnostic counters
by proving index completeness from the original pass's observed total and
disjoint finite chains, without another owner scan or count array.

No runtime subject, retirement key, candidate path, test coverage or budget
was added or relaxed.

Complete correction review inspected staged tree
`b8886fe901500719c2b169f9ed388a59b8556bed` and its matching four
FRESH/exit0 documentation/diff results. The reviewer reported:
"The prior High finding is resolved. No substantive design issue remains."
No gate rerun, runtime reproduction or additional reviewer was needed.

Acceptance records this independently reviewed design, not an implemented
index, runtime correction, identity retirement or native/target result.
The paired implementation remains pending the separate design merge.

# ADR-0100: MINIX Bootstrap Allocation and Range Validation

- Status: Accepted
- Date: 2026-11-04
- Refines: ADR-0017, ADR-0020, ADR-0051, ADR-0073, ADR-0080, ADR-0082,
  ADR-0083, ADR-0098, and ADR-0099
- Supersedes in part: the historical source-byte freezes named by the six
  exact MEM01 retirement keys, only after ADR-0082's successful transition.
  Edited memory-source line positions may then reflect the actual corrected
  code; ADR-0073's logical filename/include mapping and unrelated source
  identities remain required.

## Context

MEM01 is the next serialized runtime correction after PLAT03 #204. Its
actual runtime prerequisite is merged EV04. It owns B01-F006 and B02-F001,
not later ownership-validation cadence, mapping topology or VM selection.

The original design review found a blocking evidence closure: the standalone
bounded-validator hash inherited 797 paths, including immutable command
inputs. Separate design #205 and implementation #208 corrected that one
binding under ADR-0098. Separate #206/#207 also repaired the ordinary
panic-trace workflow's historical-scope restriction under ADR-0099.
Neither correction changed memory runtime code.

This design restarts from #208,
`bcace37522e5beb101ef5278889a2f87742f5721`. The
[source study](../research/minix-bootstrap-allocation.md) reaffirms the fixed
MINIX trace, runtime byte equality with #204 and the now-closed exact
six-key/eight-candidate authority boundary.

The allocator already canonicalizes intervals but restarts its bitmap search
for every allocation: the first `a` allocations require `a(a + 1)/2`
candidate probes. Bootstrap validity separately repeats original-FDT checks
for every frame after proving each enclosing managed interval.

MINIX kernel bootstrap consumes remaining range extents. Its later VM
bitmap/cache and descending policy are a different owner. `micros` must
retain immutable managed geometry, typed ownership and ADR-0017's lowest-
address allocation while preserving bootstrap search progress.

## Decision

After separate review, Acceptance and merge, implement the
[bootstrap allocation contract](../architecture/bootstrap-allocation.md).

### One scalar search coordinate

Insert one `uint64_t allocation_cursor` immediately before the bitmap,
inside existing metadata-prefix snapshots. It is a compact logical bitmap
index, not a physical pointer, owner, free count or second availability set.

For valid state, `0 <= c <= managed_frame_count` and every bit below `c`
is allocated. The cursor may itself name an allocated bit. The bitmap alone
authorizes availability.

Retain ascending managed-range selection and lowest-address results.
Search from the cursor, skip allocated words and mask selected-range
boundaries without shifting by 64. Commit the selected bit, count and
cursor advance only after all fallible work. Failed operations preserve
the complete state and output.

Every release lowers the cursor. Typed defensive rollback restores its exact
saved value as well as bit/count state. Process-set release uses the existing
shared free-bit primitive, which also maintains the privileged mirror on VM
release. Setting bits preserves the lower-bound invariant.

No VM cache, contiguous allocation, mutable geometry/free-extent list or
ordinary full-prefix re-audit is introduced.

### Range-based bootstrap validation

Retain checked geometry, complete original-FDT interval coverage, both
reservation sources, kernel exclusion, count agreement and all-free
initialization.

Remove only duplicate per-frame original-FDT checks. Use word-based bitmap
population/prefix validation, including explicit partial-word high bits and
the complete inactive tail. Validate capacities before indexing; validation
is read-only and never repairs state.

Retained whole-interval work is `O(m(m + g) + gr)`, independent of managed
frame count. Bitmap/count/cursor validity costs `O(P/64)` word work.

The complete ownership validator adds one separate cursor-prefix helper.
Its existing active/inactive algorithms, token identities, coverage and
invocation cadence remain unchanged. MEM03 retains the cadence correction.

### Preserve failure, authority and phase boundaries

Keep the three portable API signatures, original capacities/error
enumeration, valid-input behavior, lowest reuse and explicit exhaustion.
The new field changes kernel/native representation, not an IPC, VM
boot-information or user ABI.

Keep exact typed owners/generations, immutable geometry, SIE-clear target
serialization and irreversible handoff. VM selection remains its existing
separately audited policy.

Mutation entries reject out-of-bound count/search metadata before dependent
indexing. Complete validators diagnose a free bit below the cursor; valid
allocation does not re-audit the complete prefix. Search state never grants
permission to return an occupied frame.

### Independent work evidence and granular retirement

Use the existing test-only EV02 collector at actual range/index/word
primitives. A dedicated MEM01 compile flag excludes these observations from
all other targets; ordinary builds contain no observation state or calls.

Native baseline uses fixed #208 source and type/header layouts. Current
execution uses actual current bodies/layouts. Extract only the exact
selected pure bootstrap helper closure and supply declared native numeric
linker bindings, never a handwritten algorithm or synthetic physical-memory
dereference. Do not invent a cursor field in the baseline.

Three independently declared oracles cover bootstrap range work, monotone
allocation work and typed composition. Include complete semantic, access,
corruption and exact rollback guards. The canonical contract fixes the
input schedules, work envelope and eight new authority facts.

Retire only the five named memory whole-file assertions and the earlier
bounded-validator input assertion. Preserve their original values, all
other current facts/fingerprints, active/inactive token contracts, shared
C inspector, source owners and prior COPY01/PLAT01 claims.

Follow actual Red, Green and provisional affected-unit recertification in
order. A staged completion claim cannot suppress a live assertion. Ordinary
journal-free claims become usable only after exact independent review and
commit.

## Non-goals

No page-table topology/index, ownership-validation cadence, VM cache,
mapping policy, larger capacity, heap/free-extent framework, concurrency,
recovery, new target workflow, deadline/budget increase, node-Q or hosted-CI
acceptance.

## Acceptance

Require actual pre-correction native Red for all three work oracles;
complete current work/semantic/access/rollback Green; coherence through
every actual bitmap mutation; original range/owner/handoff and inactive-tail
safety; ordinary observation absence; the unchanged collector ABI; exact
six-key transition and current-record conservation; actual B01-B12/B14
recertification and the full fresh 50-command author gate with all
44 static configurations/113 outputs/ten observed scopes and blocked B13.

Independent implementation review precedes the first permanent commit.
Verify reviewed/committed/remote/squash tree equality, ordinary committed
journal-free authority and exact task-branch cleanup.

## Phase barrier

This proposal contains documents only. No C/header, executable test/model,
generator, harness, observation, build scaffold, dependency or reusable
prototype is authored here. Implementation remains pending separate design
review, Acceptance and merge.

## Alternatives

- Restart at zero: retains quadratic successful-prefix work.
- Mutate authoritative geometry into free extents: breaks typed/handoff
  indexes and cannot represent arbitrary release fragmentation at the
  accepted descriptor capacity.
- Import VM's cache/descending policy: changes the bootstrap contract and
  pulls in B07-F003.
- Treat the cursor as availability authority: bypasses the real bitmap.
- Audit the whole prefix on every allocation: recreates repeated work.
- Remove all bootstrap validation: loses interval/count/bound/tail safety.
- Recompute old expected hashes: replaces independent authority with the
  candidate; only the declared transition may retire an assertion.

## References

- [Fixed-source study](../research/minix-bootstrap-allocation.md)
- [Exact architecture and evidence contract](../architecture/bootstrap-allocation.md)
- [Standalone binding prerequisite](0098-standalone-frame-validator-evidence-binding.md)
- [Current trace-scope prerequisite](0099-panic-trace-execution-scopes.md)
- [B01 platform audit](../research/minix-complete-baseline-b01-platform.md)
- [B02 memory audit](../research/minix-complete-baseline-b02-memory.md)
- [B07 VM audit](../research/minix-complete-baseline-b07-vm.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)

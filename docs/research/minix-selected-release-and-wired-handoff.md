# MINIX Selected Release and Wired-Handoff Baseline

## Status and source pins

This is MEM05's fixed-source baseline study for B02-F006 and B02-F007 under
[ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md).
The [Accepted contract](../architecture/selected-release-and-wired-handoff.md)
has clean independent design/correction review and separately merged as
#233. The later [count-evidence conflict](#mem05-c01-same-slot-count-evidence-conflict)
stopped implementation preparation before production changes or formal
Red. Its corrective design is Accepted after clean independent review under
[ADR-0115](../adr/0115-selected-count-generation-evidence-scope.md).
This study does not retire an identity or close a runtime finding.

```text
micros commit: abe05cdd7bced5e77e86c1ec12754d017f920cab (#232)
micros tree:   2c34dac3c259a4975d1c213e3e14c18d8a075658
MINIX commit:  4db99f4012570a577414fe2a43697b2f239b699e
```

The current `arch`, `kernel`, `include`, `lib`, `servers` and `user`
directories are byte-identical to independently reviewed MEM04 #228.
Separate EV01-C06 #230 and EV01-C07 #232 added metadata and its tests,
not runtime changes or source-admission relaxations.
The inventory now has 5,379 facts in twenty-two families, six committed
claims and 103 retired identities. All 477 new MEM04 native/source clauses
and 120 new MEM03 observation contracts remain active.

The fixed local MINIX checkout is `/Users/alephling/Repo/minix`.
Every inspected VM source below was compared with the fixed revision;
the worktree bytes match. MINIX is evidence, not source to copy.

## Operation boundary

MEM05 owns the remaining bootstrap selected-page release, process-root
destruction and wired-handoff set construction identified by
[the B02 audit](minix-complete-baseline-b02-memory.md#b02-f006-page-release-and-process-destruction-scan-full-capacity).
It must preserve exact generation-bound ownership, inactive-root mutation,
zero-thread destruction, failure-atomic state/output, SIE-clear epochs and
non-failing commit.

The lower ownership mechanism also serves post-handoff VM claim, freeze and
release. Any new ownership representation must remain coherent through those
existing transitions. That dependency is not permission to redesign VM
policy, remove VM02's all-root preflights, correct VM frame-plan duplicate
searches or claim later lifecycle findings.

The Accepted
[MEM04 contract](../architecture/selected-user-address-space.md) leaves
release, destruction and handoff with MEM05. Its selected PC/SP queries
already avoid whole-root validation; they must not regain it indirectly.
[ADR-0109](../adr/0109-handoff-preflight-error-ownership.md) owns the explicit
handoff `STATE` normalization at complete root-preflight failures.

## Fixed MINIX structures and algorithms

| Fixed source | Structure or operation | Actual mechanism |
| --- | --- | --- |
| `minix/servers/vm/region.h:26-67` | Physical blocks and virtual regions | A process owns an AVL of regions; each region has an offset-indexed physical-reference array; each physical block has a reference count and reference list |
| `minix/servers/vm/phys_region.h:8-24` | Physical reference | The selected relation names its block, region, page offset, memory type and next shared reference |
| `minix/servers/vm/region.c:60-94` | `physblock_get`, `physblock_set` | Direct page-offset indexing; selected publication updates the process's `vm_total` and high-water count |
| `minix/servers/vm/region.c:527-562` | `map_subfree` | Walk the selected virtual-page offsets, skip absent references, unreference and release each present relation |
| `minix/servers/vm/region.c:568-609` | `map_free`, `map_free_proc` | Release one region's references and metadata; process teardown repeatedly removes an AVL root and frees that region |
| `minix/servers/vm/pb.c:96-133` | `pb_unreferenced` | Decrement the selected block, unlink the exact physical reference, invoke its memory-type release at zero references, then clear the region relation |
| `minix/servers/vm/alloc.c:289-300,465-482` | `free_mem`, `free_pages` | Set only the requested physical-page bitmap bits and append to the bounded free-page cache while it has space |
| `minix/servers/vm/pagetable.c:235-248,1427-1436` | `vm_freepages`, `pt_free` | Free selected VM mapping pages; process table teardown scans architecture directory slots and frees present private tables |
| `minix/servers/vm/exit.c:33-92` | `free_proc`, `do_exit` | Validate the exact endpoint and announced exit, free regions then page tables, reset region/resource state, and finally clear the process slot |
| `minix/servers/vm/main.c:262-286,427-522` | `init_proc`, `init_vm` | Import boot information and memory chunks, initialize VM's process/region/allocator state, establish tables, load boot processes, and release their original image blobs |

`alloc.c` still contains old hole-list comments, but its executable
implementation uses `free_pages_bitmap`, not a hole-list allocator.
`PAGE_CACHE_MAX` is 10,000. The fixed `vm.h` disables `SANITYCHECKS` and
`JUNKFREE`; their diagnostic and page-filling paths are not ordinary
release work.

Region removal uses the actual `cavl_impl.h` AVL mechanism, not a generic
recursive-tree assumption. It records branch directions, temporarily
reverses links on the return path, then restores links while rebalancing.
`regionavl_defs.h` fixes `AVL_MAX_DEPTH` at 30, so the branch bitmap fits
one unsigned long on the fixed 32-bit targets. It does not allocate a
per-node pointer stack or recurse through all regions.

### Reference costs and mutation limits

Let `r` be a process's region count, `v` the number of virtual page slots
visited in those regions, `s_j` the physical block's reference-list length
at a selected unlink, `d` the architecture directory-slot count, and `b`
the number of boot processes.

- `free_pages` has `O(q)` work for `q` selected physical pages and constant
  automatic storage. Cache append remains bounded per page.
- `map_subfree` has selected-offset work plus the relevant reference-list
  unlink costs. A non-head unlink is `O(s_j)`; it is not generally constant.
- `map_free_proc` has an `O(r log r + v + sum(s_j))` upper bound, plus
  memory-type and allocator release costs. Repeated AVL root removal must
  not be reported as a simple linear list walk.
- `pt_free` scans `d` slots in that process's table representation, not
  every physical owner in the system. Its called selected unmaps and frees
  have their own costs. The shown function frees present subordinate
  tables, not the retained directory allocation itself.
- `init_vm` does not contain a per-process scan of a global typed-owner
  ledger. It does, however, call `init_proc`, which linearly searches boot
  records. That lookup contributes an `O(b^2)` term across boot processes,
  separately from memory initialization, table construction and loading.

Partial region unmap has additional policy work. Low-end shrinking adjusts
remaining physical-reference offsets and moves the remaining pointer
array; it cannot be described as only `O(selected pages)`.
`map_unmap_region:1065-1146` also unreferences pages before some later
memory-type or page-table failures. `micros` must preserve its stronger
failure-atomic contract rather than copy that mutation/error sequence.

These are subsystem costs, not a claim that every reference memory operation
is constant-time. Heap/slab and memory-type callbacks remain separately
accounted consumers. MINIX's ordinary teardown selects one process's region
and page relations; it does not repeatedly search the complete physical pool
to discover that process's frames.

## Current micros representation

The current mechanisms retain:

- the availability bitmap and MEM01 lower-bound allocation cursor;
- compact managed-frame indices over sparse active physical ranges;
- one eight-byte exact typed owner and one handoff-target byte per frame;
- exact owned/per-kind counts and immutable allocator geometry snapshots;
- separate process, thread, endpoint and hart identities;
- private Sv39 roots, immutable shared kernel entries and 4 KiB user leaves;
- two 32 KiB BSS reachability/release bitmaps with one checked scratch lease;
- exact bootstrap and handed-off ownership phases; and
- one-hart SIE-clear preflight/commit serialization.

`MICROS_PROCESS_CAPACITY` is 64 and `MICROS_THREAD_CAPACITY` is 128.
A process record has its root and live-thread count but no frame-membership
index. The ledger also has no process-specific enumeration structure.
Neither capacity may be treated as the other object's identity.

The portable ledger deliberately validates owner encoding without resolving
process liveness. Existing native models use generations 2 and 3 in the same
slot, and exact-generation counting filters them independently.
An index design must not silently equate a process slot with one generation.
Target runtime validation supplies the stronger live-generation relation.

## Current operation and cost ledger

Use `p` for active managed frames, `P = 262144` for frame capacity,
`g` for active ranges, `k = 8` for owner classes, `w = ceil(p/64)`,
`q` for selected release frames, `f_i` for process `i`'s private table/user
frames, and `e_i` for all PTE entries inspected in its root walk.
`e_i` includes absent slots in visited 512-entry tables, not only live leaves.
`X_i` denotes the existing complete VM/TTY getter work when those controllers
are initialized. `Vobj` is the complete object/scheduler audit, not a
constant-time handle lookup.

| Current owner and operation | Actual work and boundary |
| --- | --- |
| `frame_ownership_bootstrap.c:175-227` selected-release summary | Reads the release bitmap once per index through `P`, then checks selected owners/targets/bits and accumulates counts |
| Same owner, `commit_process_release:258-305` | Scans all `p` active indices again, frees selected records and publishes precomputed aggregate deltas |
| Same owner, `release_process_set:308-354` | Metadata preflight plus the two scans: `O(g + k + P + p + q)`; the local summary is fixed-size, not a selected-frame vector |
| `frame_ownership_validation.c:35-82` process count | Scans all `p` owner records and validates selected matches; exact-generation semantics do not provide a direct process index |
| `user_address_space_runtime.c:197-246` scratch acquisition/release | Clears both active bitmap prefixes on acquisition and again on release; this remains `O(w)` even when only one page is selected |
| `user_address_space_validation.c:461-856` complete root validation | Clears the reachable prefix, calls complete exception getters, walks the private tree with selected frame/geometry checks, then scans all `p` owners for orphan agreement |
| `user_address_space_mutation.c:457-689` page release | Performs that complete root preflight, inspects the selected leaf and at most two table-pruning scans, builds a capacity-shaped release bitmap, then invokes the full-capacity release operation |
| `user_address_space.c:310-462` root destruction | Performs complete root validation, scans `p` to copy reachability, invokes release, then scans `p` again to clear released frames before final detach |
| `user_address_space_handoff.c:74-273` process/context preflight | Scans process slots; rooted processes each search all 128 thread slots, while rootless processes each scan all `p` owners |
| Same owner, prepare/complete at `276-456` | Validates each rooted process again and performs another `p` scan for user-target selection or wired-target agreement |
| `frame_ownership_handoff.c:80-186` wired-set staging | Whole-active ownership preflight, a selection scan through `P`, then an active commit pass |
| Same owner, complete at `189-269` | Whole-active validation, result-count classification and the non-failing active commit; target wrapper additionally checks active process owners |

The root-walk component alone has
`O(e_i + (g + k) * f_i)` work under the current checked physical lookup
helpers. The global owner, scratch and controller terms are additional,
not hidden inside that expression.

Destroy also calls
`kernel_object_runtime.c:171-194` after ownership and frame clearing.
That wrapper performs a complete object audit after successful detach.
The current destruction caller panics on any such post-preflight failure.
This additional `Vobj` cost and irreversible boundary must be addressed
explicitly in the replacement contract, not omitted from the cost account.

MEM04 already made `user_execution_validate_context` resolve exact handles
and check PC/SP through selected page lookups. Handoff still owns one
explicit complete object audit and its separate per-process thread search.
Do not attribute the already removed global PC/SP lookup cost to current
MEM05 Red.

The complete VM scratch getter validates initialized VM handoff state before
returning an inactive result. A bootstrap ownership phase does not make
that getter's work automatically constant. Complete diagnostics retain that
getter; any scoped replacement must state its authority boundary separately.

## Mutation-coherence obligations

Any new process/frame enumeration index must cover every authoritative owner
transition, not only the two bootstrap functions that motivated the task:

1. bootstrap free-to-owned allocation and exact-owner release;
2. selected process-set release;
3. handoff's process-user to wired or transferable transition;
4. VM free-to-transferable claim and transferable rollback;
5. VM transferable-data/free-table freeze into process-bound ownership; and
6. VM process-bound data/table release.

The VM commits already consume prevalidated plans and cannot acquire a new
recoverable failure after their first store. Index preflight and all possible
overflow/foreign/generation/link failures must therefore precede their
existing commit boundaries. Their allocation policy, input plans,
pairwise distinctness algorithm and source of command authority remain
separate later-task responsibilities.

Complete ownership diagnostics must detect a malformed added index,
including inactive state, without replacing their existing owner/bitmap/
target/geometry/count checks. An index is not a reusable successful-audit
flag. Ordinary operations must establish the exact scope they consume.

## Difference classifications

| Difference | Classification | Required treatment |
| --- | --- | --- |
| Privileged dense typed owner mirror with exact generations | Required adaptation | Preserve the kernel mechanism/VM policy boundary and exact frame authority |
| Process-local frame enumeration in that mirror | Required adaptation | MINIX enumerates process-owned region/reference relations; the bootstrap kernel has no VM region-policy tree. A compact intrusive owner list supplies only membership/enumeration, without importing region allocation policy or losing exact generations |
| Indexed selected-word set for a failure-atomic release | Compatible extension | MINIX frees selected references/pages. The stronger micros preflight needs an exact temporary set; published words preserve selected-work complexity without a physical-capacity search |
| Sv39 private roots and shared immutable supervisor mappings | Required adaptation | Keep three-level checked physical consumption, 4 KiB user leaves and target invalidation rules |
| Kernel bootstrap policy followed by the one-way wired handoff | Staged substitution | Retain the Accepted VM replacement boundary rather than invent a direct MINIX handoff equivalent |
| Complete non-mutating preflight and non-failing release commit | Compatible extension | Preserve failure atomicity with an explicitly accounted selected plan |
| Complete corruption diagnostics at explicit diagnostic boundaries | Compatible extension | Retain full active/inactive coverage; do not make a scoped success claim equivalent to complete diagnosis |
| Capacity-shaped scans for a selected release or one process's destruction | Divergence requiring correction | Build and consume an exact selected/owned-frame representation without unrelated physical-capacity work |
| Repeated whole-owner and thread searches for each handoff process | Divergence requiring correction | Compose process/thread/root/frame obligations once per protected operation, without hiding inherited whole-operation costs |
| Failure-atomic micros unmap instead of MINIX's later-error mutation order | Compatible extension | Retain the stronger no-partial-mutation guarantee rather than importing a known weaker failure boundary |

The owner list does not replace MINIX's AVL lookup algorithm with a linear
virtual-address search: it performs no virtual-region lookup at all.
Existing Sv39 walks still select mappings, and later VM owns region policy.
The list replaces only the bootstrap mirror's global search for process
membership. Its no-sharing indexed relations retain process-local teardown
work while adapting the independently accepted dense, typed kernel mirror.

## Evidence and design stop boundary

EV01-C06 and EV01-C07 now identify the relevant existing MEM04 and MEM03
source/observation comparisons. They do not authorize retirement by
themselves. The final design must enumerate exact affected keys, old
unaffected clauses, actual new helper/observation closure and immutable
inputs before implementation.

The existing MEM03 work contract deliberately retains capacity-sized
release-bitmap bounds and whole-active process-count/handoff terms.
Its success therefore does not close B02-F006/F007. MEM05 needs independent
discriminating actual-source bounds while preserving earlier measured
coverage and all unrelated oracles.

The current MEM04 evidence has two additional coupled boundaries:

- `selected_address_space_evidence.py:714-793` accepts only version-one
  target records and requires `release == diagnostic` in every
  `MEM04_ACTIVATION` row. The real target fixture calls release before
  destruction and assumes that release did not mutate the damaged root.
  Scoped release cannot preserve that assumption for the alias/orphan
  cases by relabeling its result.
- Its current native gate also requires the exact twenty-row retained
  root-audit ledger. MEM05 intentionally migrates four of those rows:
  page release, destruction, wired preparation and wired completion.
  The original declaration must remain historical evidence, while the
  new current ledger must check exactly the other sixteen and the new
  plan boundaries. An unconditional omission list would not be authority.

The planned target adaptation must therefore report a truthful new record
version and isolate the changed release tests. It must not synthesize an
old rejection, suppress a success transcript or weaken the complete
diagnostic/destruction checks. The old version-one parser remains useful
for historical MEM04 evidence; it is not the new operation's oracle.

Existing native fixtures have source-stage compile flags for their fixed
references: `MICROS_BOOTSTRAP_ALLOCATION_BASELINE`,
`MICROS_SELECTED_OWNERSHIP_BASELINE` and
`MICROS_SELECTED_ADDRESS_SPACE_BASELINE`. Bounded fixture adaptations can
preserve those original layouts while checking the actual new current
index and selection interfaces. They must be frozen before MEM05 Red,
not inferred from corrected production output.

The final design census has 82 active keys and a 28-path direct closure.
It preserves the already selected release inspector and additionally
names destruction's removed helper, definition-set change and private
object-runtime include. The earlier eighty-key inspection was provisional.
The target validation fixture's old whole-file identity is already
retired by MEM04; it is a separate fixed dependent target input, not a key
to retire again. The contract binds that extra input explicitly.

The original target snapshot holds only four user frames. Each new isolated
release case therefore allocates one page beside the original three.
The alias is an extra originally absent PTE, not another allocated page;
cleanup removes only that injected alias before normal destruction.
Page release preserves user data and clears only pruned tables; destruction
clears all its released frames. Allocation's complete zeroing remains
independently checked by dirty reuse.

The user-address-space and handoff mutation snapshots use the complete
`sizeof` ownership object and therefore include the added index.
Some unrelated grant-syscall/resource comparisons deliberately compare
active owner/target prefixes instead; they are not new index evidence.
MEM05's independent native transition model and full failure snapshots
own that additional proof rather than claiming that an old prefix comparator
already covers it.

The Accepted contract specifies index mutation/scope, lease modes,
handoff's original two-pass ordering, exact storage, source roles, finite
native bounds and actual Red/Green/recertification commands.
These are design requirements, not execution evidence.
No runtime/header, test, model, fixture, generator, build scaffold or reusable
executable prototype is authored in this phase.

## Independent design review closure

The first complete review of staged tree
`3414dad311bab35e2998ac8a83526e3e24d2fb99` found one High compatibility gap.
Author inspection confirmed both retained consumers:

- `selected_ownership_evidence.py:422-453` builds Red headers, appends only
  MEM03 declarations, and compiles the current fixture without its baseline
  flag. Those declarations cannot express the new index and replaced
  selected-set signatures.
- `test_selected_ownership.py:318-322` observes the historical freeze body
  as current Green. It needs a fixed historical policy/authority context,
  not the future current observer or a skipped check.

The correction adds exactly those two existing owners to the immutable
pre-Red adaptation register, specifies independently declared complete
current interfaces for syntax-only checking, and retains the historical
positive/negative observer evidence in its true scope. Runtime subjects,
82 selected keys, 29 candidate paths and eight new owners do not expand.

The same retained-consumer inspection checked
`selected_ownership_evidence.py:387-410`: its all-free diagnostic cases
require exact original owner/target/geometry/bitmap counts.
The complete index proof therefore reuses the owner pass's independently
observed process-bound total in the same call, validates finite disjoint
slot chains and their cardinality, and separately checks link storage.
It does not add another all-free owner scan or weaken those exact counters.
This also removes the unnecessary proposed 256-byte count array.
Added index/link work remains fully measured.

These are static design corrections. No gate was rerun by the reviewer
and no runtime reproduction was needed. Complete correction review of
`b8886fe901500719c2b169f9ed388a59b8556bed` concluded that the High finding
is resolved and no substantive design issue remains.
Acceptance is not implementation or runtime evidence.

## MEM05-C01 same-slot count evidence conflict

The separately merged design is commit
`d256a5bb1ed8bf67e435a36107c9cec931a49d46`, tree
`2b134a5cf27d467ae072801dca79b110b5a74dd8`.
During its implementation's immutable fixture preparation, inspection found
one retained MEM03 result incompatible with the Accepted count scope.
Production remained at the fixed #232 runtime throughout.

The fixed sources establish both sides:

| Fixed source | Evidence |
| --- | --- |
| `kernel/memory/frame_ownership_validation.c:35-82` at #232 | `count_process` checks owner encoding while scanning physical records, but calls `selected_record` only after slot and generation match |
| `tests/host/selected_ownership_test.c:1216-1235` at #232 | `selected_count_guard` writes `HANDOFF_TARGET_COUNT` to frame 0, requires exact-generation `INVARIANT`, then increments only the query generation and requires `OK`/zero |
| `docs/architecture/selected-release-and-wired-handoff.md:145-156` at #233 | The new count validates every reached record's encoding/bit/target relation in the complete selected slot chain |
| Same #233 contract, `Legacy native fixtures` and immutable fixture table | All MEM03 results are preserved; the current fixture's named adaptations cover selected-set arguments and expected index transitions, not this changed result/output |

The new index deliberately permits several encoded generations in one
portable slot. It therefore consumes the malformed record even when the
requested generation differs. Checking it only after generation filtering
would contradict the Accepted whole-slot relation proof.
Moving the second test query to another slot would instead hide the input
that exposes the conflict.

A minimal native probe compiled the actual fixed #232
`frame_allocator.c`, `frame_ownership.c`, `frame_ownership_validation.c`
and `frame_ownership_bootstrap.c` with their fixed headers, strict C17
warnings and ASan/UBSan. It initialized 65 frames, allocated one
`PROCESS_USER` frame to slot 0/generation 1, injected target value 3, and
called the real count function for generations 1 and 2 in slot 0.
Its exact output was:

```text
14 18446744073709551615 0 0
```

The columns are exact-query result/output followed by other-generation
result/output. The process exited zero without stderr.
The focused command below passed one test in 2.416 seconds in the abandoned
implementation scaffold, not in this documentation-only tree:

```bash
"${MICROS_PYTHON}" -E -S -B tools/run_host_tests.py --test test_selected_release_handoff.SelectedReleaseHandoffNativeTest.test_fixed_foreign_generation_count_keeps_its_historical_target_scope
```

The preserved scaffold tree is
`3cefa0b69d9f72f4a72d38d8cd4ae6037027a4fb`; its local stash is
`3156e3722a299e0051f0a50bc69d203f0ae9703f`.
`archive/mem05-count-scope-gap` retains its base. The complete patch digest is
`29c1d809f3487d8517fc7a6332a26c2ab11875d563bb373d1a022eb7edab64a7`.
The scaffold is incomplete and unaccepted. No production edit, formal
Red/Green/recertification event, retirement, implementation commit or PR
exists for it. Earlier five-product and target probes are likewise not
formal author evidence.

Accepted [ADR-0115](../adr/0115-selected-count-generation-evidence-scope.md)
retains the runtime algorithm and specifies only the missing current
expectation: `INVARIANT` with unchanged output for that same-slot query.
Separate current controls retain valid mixed-generation counting, reject a
reached invalid target and distinguish another slot's corruption from
complete diagnosis. All existing source roles, old facts, work bounds,
target protocols and final gates remain unchanged.

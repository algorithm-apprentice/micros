# MINIX Grant Allocation and Endpoint-Lifetime Study

## Fixed inputs

```text
micros source: b33ed83d5a49f9aecc7a42808c758027daef326c
micros tree:   5a1bee6f1cdf75f4493d21782a45dce8e6036d83
MINIX source:  4db99f4012570a577414fe2a43697b2f239b699e
```

GRANT01 #259 is merged. Both repositories were checked clean at these
revisions. Reference research uses the existing local
`/Users/alephling/Repo/minix` checkout read-only, not a web copy.
MINIX remains evidence, not a source donor.

GRANT02 owns B05-F003/F004 and requires merged GRANT01 and IPC03 #255.
The [canonical contract](../architecture/grant-free-lists-and-targeted-cancellation.md)
owns the Accepted replacement; this study is not implementation evidence.

## Fixed MINIX trace

| Source | Behavior, structure and algorithm |
| --- | --- |
| `minix/include/minix/safecopies.h:9-38` | One grant entry has flags, sequence and a union whose free role contains a next-slot index. Active direct authority and free-list navigation have separate roles. |
| `minix/lib/libsys/safecopies.c:41-115` | A process owns its table and one free head. Initial/static and new growth slots form an ascending chain; new capacity precedes the previous free head. Growth copies old records, initializes new records and publishes the new table only after registration succeeds. |
| `minix/lib/libsys/safecopies.c:119-171` | Allocation pops one free head, growing only if empty. Direct creation fills endpoint/range/fault fields before publishing used/valid flags. |
| `minix/lib/libsys/safecopies.c:229-272` | Revoke selects one index/sequence, invalidates the entry, advances sequence and pushes it onto the free head. Reuse is LIFO. |
| `minix/include/minix/safecopies.h:103-107`, `minix/kernel/system/do_setgrant.c:15-29` | Registration binds table address, count and the current grantor endpoint in its privilege object. |
| `minix/kernel/priv.h:61-63`, `minix/kernel/system/do_privctl.c:153-162` | Grant-table authority belongs to one process privilege record; a newly initialized privilege record has no table and is bound to the current endpoint. |
| `minix/kernel/system/do_safecopy.c:42-141` | Verify exact grantor endpoint, table-owner/extent, index, copied record flags and sequence. Its separate temporary-table/live-update branch is not ordinary `micros` authority. |
| `minix/kernel/system/do_clear.c:31-70` | Process teardown removes endpoint communication authority and releases the process/privilege slot. It does not enumerate a global collection of grant entries. This does not claim that all IPC/IRQ teardown work is constant. |

For table extent `g`, allocation is constant when a slot is available.
Growth is `O(g)` worst-case and amortized over newly available slots;
revoke is constant. Persistent table storage is `O(g)`. Selected
allocation/revoke use constant auxiliary/stack storage.

The table's process ownership and endpoint-generation checks invalidate
grant use without searching unrelated grant tables. This is not a MINIX
two-sided reclamation list: surviving grantors still own their table
entries. `micros` must explicitly reclaim kernel-owned slots when either
participant closes, and must retain the existing failure-atomic lifecycle.

## Current `micros` trace

GRANT01 already changed ordinary validation to local admission. Its current
complete diagnostic is `O(G + Vep)`, not the original audit's historical
`O(G * Vep)`.

| Current owner | Remaining work |
| --- | --- |
| `kernel/grant/grant.c:560-691`, create prepare/commit | Admit selected participants, then validate every storage row in the lowest-free prefix. Publish one unpublished record only after target device exclusion. |
| `kernel/grant/grant.c:522-554`, authority clear/retirement | Clear one record, advance its generation or retain terminal quarantine. There is no free-list publication. |
| `kernel/grant/grant.c:858-928`, cancellation prepare | Complete grant/endpoint/object validation, exact ACTIVE/SOURCE_ONLY target resolution, full candidate-plan clearing, one capacity scan and whole-plan copying. |
| `kernel/grant/grant.c:930-1027`, cancellation commit | Complete storage scan, another capacity scan proving exact selected membership/generation and zero unselected witnesses, then a capacity-indexed retirement pass. |
| `include/micros/grant.h` | 64 records, scalar active count, 32-bit slot/generation token; a 288-byte cancellation plan includes a mask and 64 expected generations. |
| `kernel/pm/pm_control_syscall_exit.c:65-122` | The sole production caller prepares cancellation, closes IPC, commits grants and only then finishes detach. Its plan is automatic and not capacity-cleared before preparation. |
| `kernel/tests/runtime/user_runtime_test_cleanup.c:62-119`, grant/endpoint/syscall fixtures | Preserve the same close ordering and distinguish grant preparation, IPC close and grant commit failures. |
| `tests/host/grant_model_operations.c:175-295` | The independent model exercises both standalone cancellation and close/endpoint-generation reuse. Neither may be dropped. |
| `tests/host/grant_lifecycle_test.c:407-635`, `grant_validation_test.c` | Poisoned output, selected-generation corruption, foreign registry, consumed plan, SOURCE_ONLY and failed-IPC-close evidence already exist. |
| `tests/host/grant_model_reference.c:159-202` | The old reference additionally requires every unselected generation entry to be zero. That expectation must be explicitly replaced, not retained through an unobserved full-array operation. |

Let `G` be grant capacity, `P` process/endpoint capacity, `a` the allocation
prefix and `k` the incident grant count:

```text
current create:          O(a), worst O(G), after selected participants
current indexed revoke: O(1), after selected authority
current cancel prepare: O(G + Vep), including capacity-sized plan movement
current cancel commit:  O(G), including unrelated records
```

The existing GRANT01 evidence records one/64 allocation-prefix visits for
empty/last-slot creation. It deliberately keeps dense cancellation in the
complete-audit scope. These are current baseline facts, not a GRANT02 Red
run or an implemented speedup.

## Mapping and required adaptations

| Mechanism | Classification | Proposed mapping |
| --- | --- | --- |
| Embedded free next and one head | Baseline parity | Initially ascending; constant head pop; revoke-time LIFO push. |
| Fixed capacity instead of growth | Required adaptation | Keep the existing 64-slot/no-heap bound; empty free head yields capacity after existing participant admission. No amortized-growth claim. |
| Kernel-owned immutable active authority | Required adaptation | Existing exact participants, permissions and scalar syscall publication remain. |
| Full token rather than bare slot in links | Compatible extension | Check both slot and generation before following kernel-owned navigation. |
| Terminal quarantine instead of sequence wrap | Compatible extension | Preserve existing nonzero generation and reserved slot-63 token rules; never link quarantined records. |
| Two membership nodes and one endpoint root | Required adaptation | Explicit reclamation on either endpoint needs an incident index because records are not reclaimed with a user's table. |
| Prepare before create publication/IPC close | Compatible extension | Defer free-head/index mutation until every fallible check is complete; preserve all-or-error state. |
| Exact generation plan with sparse meaningful entries | Compatible extension | Retain the existing bounded allocation; touch only header/selected entries and reject exact selected-set/generation changes. |
| Complete index/record diagnostics | Compatible extension | Full reachability/count/uniqueness remains explicit, not repeated on ordinary operations. |

The proposed combined participant list is chosen over separate grantor and
grantee roots because the endpoint owns the union of incident grants.
Each active record still contains two role-specific link pairs. Distinct
endpoint roles may use the same neighboring record, so aliasing of records
is valid when the affected link fields are different.

One exact endpoint tag and one head per process slot prevent stale-generation
root reuse. No redundant free/list counts or epoch are needed. Full
diagnostics derive membership/counts from records and lists.

## Failure and storage implications

Allocation must not pop a free head during prepare: the existing TTY
exclusion can still deny publication. Revoke and cancellation must preflight
all unlink neighbors before clearing a record's authority or links.

After IPC close, the endpoint/process primary relation has intentionally
changed. Grant commit therefore validates only the retained exact set and
grant-owned topology, not a live endpoint or the complete grant relation.
Within the serialized epoch, only IPC close may intervene; it does not
modify the grant registry.

Preserve increasing-slot cancellation retirement. A mask-driven selected
iteration can provide that order without enumerating all 64 entries.
The commit must consume links updated by earlier non-failing splices;
captured original-neighbor plans cannot be blindly replayed after removing
adjacent selected records.

The current plan reserves `O(G)` storage. Retaining those 288 bytes avoids
new allocation/state while preserving exact generations, but does not make
the storage proportional. Removing its whole-array clear/copy and ignoring
unselected witnesses is necessary for `O(k+1)` work, including empty cases.
There is no value, initialization or authority promise for unselected
entries after success; failures preserve every caller byte.

The proposed LP64 representation has a 72-byte record and a registry bounded
by 5,144 bytes, including endpoint roots. Two existing runtime registry
objects reserve at most 10,288 bytes. This is an explicit persistent
index cost, not a budget increase or third registry. The fixed token, user
ABI, one-thread/hart policy and copy plans remain unchanged.

The sole production cancellation caller already uses an uninitialized local
plan; it does not impose a hidden whole-plan zero. Fixture/model
initialization remains test setup, never measured production work. Existing
output/model consumers must stop reading unselected generation entries.
Full plan copies/initializers in any production cancellation path would
invalidate the claimed bound even if incident-list visits were correct.

## Required acceptance

The architecture contract defines the exact new footprint, node admission,
publication, failure and evidence obligations. In particular:

- old allocation-prefix cases must distinguish records newly consumed as
  insertion heads from genuinely unvisited records;
- terminal-generation fixtures must update dependent token links coherently
  unless stale links are the deliberate invalid input;
- the independent model must retain its seed/draw/operation trace while
  deriving the free stack and ordered incident lists independently;
- every normalized comparison includes the new heads/links, not just old
  authority fields; and
- actual target lifetime/copy, current primitive qualification and full
  stack evidence remain mandatory.

No implementation, executable prototype, new model, harness or measurement
is authored during this design phase.

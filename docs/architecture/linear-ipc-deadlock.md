# Linear IPC Deadlock Projection

## Status and scope

This is the independently reviewed IPC02 contract Accepted by
[ADR-0125](../adr/0125-linear-ipc-deadlock.md).
Design #252 and its independently reviewed implementation #253 are separately
merged. The [correction register](../research/minix-complete-baseline-findings-and-correction-dag.md#current-correction-progress)
records B04-F003 closure; exact execution/review/publication records are in #253.

The prerequisite is merged IPC01 #251. The
[fixed study](../research/minix-linear-ipc-deadlock.md) traces its exact current
source and the local MINIX reference. IPC02 owns only B04-F003.

The outcome replaces the per-hop thread-table scan and visited-prefix
comparison with a directly maintained sole-thread identity and constant-time
visited membership. Keep the existing private deadlock interface and its
three operation callers, matching-before-blocking order, permissions, reply
authority, queue choices, late scheduler boundary and syscall results.

Non-goals are general thread-owner collections, user threading/wait-set
exploration, token indexes, sender-tail redesign, filters, new syscalls,
endpoint/process/thread equivalence, SMP, memory/scheduler policy, PM
transaction redesign, larger stacks/deadlines and unrelated validation work.

## Canonical sole-thread index

Append `struct micros_thread_handle sole_thread` to `struct micros_process`.
It has exactly these meanings:

| Process state | `live_thread_count` | Required index |
| --- | --- | --- |
| Free or quarantined | zero | Exact null `{slot=0, generation=0}` |
| Live, no thread | zero | Exact null |
| Live, sole thread | one | Implemented slot, nonzero full generation, exact live thread owned by this process generation |
| Live, multiple threads under a larger checked policy | greater than one | Exact null; no thread is selected as a representative |

Handle padding is not semantic state; both defined fields must satisfy the
null or exact-live rule. Check width/range before indexing. A slot-only match,
generation-only null, stale generation, free/quarantined thread or foreign
owner cannot satisfy the sole index.

The field is maintained by lifetime publication, not reconstructed by
queries. A count-one missing/bad index fails; it never triggers a fallback
table scan, repair, alternative thread choice or success-shaped error.
The field does not confer runnable, current, endpoint, reply or memory
authority. Every consumer still checks its required current metadata.

Portable callers retain their existing external serialization. Target
publication and lookup retain the existing SIE-clear lifetime. Neither the
index nor a local resolved pointer is an SMP protocol or permission to retain
a proof across mutation, blocking, generation reuse or return.

## Lifetime transitions

### Portable process and thread owners

Initialization, process creation and process release establish zero index
when no thread exists. Preserve lowest-slot allocation, generation advance,
quarantine, resource admission and output preservation.
An already initialized selected free process slot must have a zero index
before portable or PM creation can publish it. Empty-process release likewise
requires zero before clearing; neither path repairs a corrupt index by
overwriting it.

Thread creation preflights the selected owner's count/index shape after its
existing argument, owner and policy admission. A sole existing index must
resolve its exact live owner relation. Counts must fit the checked policy
and registry scalar bounds before arithmetic.

After selecting the existing lowest free thread slot and its next generation,
the same non-failing publication performs:

| Old owner count | New owner count | Index publication |
| --- | --- | --- |
| zero | one | Newly published exact thread handle |
| one | two | Exact null |
| greater than one | one more | Exact null |

Do not invoke complete object/endpoint validation to maintain this field.
Thread creation retains its existing allocation search; this outcome adds
constant index work, not an allocation redesign.

Thread release retains its existing state/context/IPC/current-hart/owner
checks and generation/quarantine choice. Complete all new index checks and
any survivor selection before the first state or count mutation:

| Old owner count | Preflight | New index |
| --- | --- | --- |
| one | Index equals the exact thread being released | Exact null |
| two | Index is exact null; one complete bounded scan finds exactly one other live thread with the exact owner generation | That survivor's exact handle |
| greater than two | Index is exact null | Exact null |

The two-thread scan visits at most `T` slots once. It excludes the admitted
release slot, checks the matching survivor's nonzero generation and exact
owner, rejects zero or multiple survivors, and retains the handle locally.
It does not require the survivor to be inactive or inspect its scheduler,
IPC, token or mapping state. Unrelated owners supply no authority.
No survivor lookup, retry or recoverable failure is allowed during commit.

All rejection paths preserve the complete registry and promised outputs,
including the old index. The index-specific corruption result in portable
object operations is `MICROS_KERNEL_OBJECT_ERROR_INVARIANT`; existing
valid-state argument, stale, policy, capacity and state ordering remains.

### Explicit cold-path tradeoff

The `2 -> 1` release path adds `O(T)` preflight to an otherwise
`O(H)` current release. This is deliberately not called constant-time or
no-worse for larger native policies. It is the narrowly accepted bookkeeping
substitution for SOM-08: production's checked limit one cannot enter it.
The actual native bound must be measured and retained as acceptance evidence.

The alternative is a new per-thread ownership collection solely to support
constant removal for a user-thread feature that is not yet exposed.
This contract chooses one process handle and one cold bounded pass instead.
Before SOM-08 exposes general thread lifecycle, its owner must reconsider
this cost alongside wait-set projection. It is not a permanent POSIX-thread
algorithm or permission to add a repair scan to an ordinary IPC hop.

### PM's direct prevalidated publications

PM does not always call the portable lifetime functions. All of these
existing owners participate in the same index invariant:

| Owner | Required refinement |
| --- | --- |
| Reserve and abort reserved process in `pm_control_reservation.c` | Publish/retain zero index with zero threads. |
| Prepare child in `pm_control_execution.c` | The existing empty-process preflight requires zero index; commit publishes `plan->thread` with the first live thread. |
| Abort prepared child in that owner | Preflight requires count one and exact prepared-thread index; commit clears it with thread removal. |
| Exit detach in `pm_control_exit.c` | Preflight requires the exact stopped exit thread as sole index; commit clears it with detach. |
| Final exited-process release in that owner | Require and preserve zero index. |

Empty/reserved/prepared/stopped process shape predicates in the directly
dependent PM owners must include the corresponding index shape, including
paths that do not call a full validator. Preserve the existing request/
phase/token ordering; a malformed consumed index maps to
`MICROS_PM_CONTROL_ERROR_INVARIANT` before publication.

These are additional field checks/stores in existing transactions, not new
authority, plans, resource acquisition, fences, validation cadence or fallible
commit operations. Target stack attach/detach observations remain at their
original lifetime boundaries.

Endpoint close or source-only sealing alone does not remove a thread.
Scheduler flags, context attachment and address-space mutation do not change
thread ownership. None of those operations clears or replaces `sole_thread`.

## Complete diagnosis versus selected projection

The complete base validator retains its independent thread-to-process count
pass and comparison with every process's stored count. Add the canonical
index shape and, for count one, exact indexed liveness/generation/owner
checks. Since the independent actual count is one, that matching indexed
thread is the unique owner.

No nested per-process thread scan, new persistent validation cache, additional
capacity-sized witness or scheduler/IPC dependency belongs in base validation.
Its object/owner-count component remains `O(P + T)`; other existing stack/hart
diagnostic costs are unchanged.

A selected projection checks the current process count and exact indexed
thread, not every unindexed table row. Corruption of consumed index/count/
generation/owner metadata remains an invariant error. A hidden extra thread
outside that selected proof need not take precedence over a valid selected
operation, but the explicit complete validator must still reject the count/
ownership disagreement. This is an explicit refinement of the old scan's
whole-owner proof, not a claim that the index makes corruption impossible.

For a candidate or cleared-dependency thread whose live owner count is one,
its owner's index must name that exact handle. An unindexed duplicate cannot
present itself as that owner's sole candidate. With a larger actual count,
the canonical index is null and existing native caller behavior remains;
the walker still rejects any attempt to project that multi-thread process.
Resolve that thread's complete owner handle with the existing checked process
resolver before reading owner count/index. Zero live count or counts beyond
the selected policy/registry bounds are invariant errors, not an absent edge.
Preserve the private entry's existing initial `ANY`/`NONE` short circuit.

## Selected dependency walk

Keep the current order:

1. Initial `ANY` succeeds without walking; initial `NONE` is invariant.
2. Admit the candidate and optional exact cleared handle under the existing
   private entry contract and the consumed index consistency rule above.
3. For each of at most `T` hops, admit the named endpoint and exact owner,
   retaining active/source-only visibility.
4. Require the selected process count to be exactly one and admit its indexed
   thread before any use. Retain IPC01's selected scheduler/thread and
   consumed dependency-role checks.
5. Compare the full thread handle with the candidate first: equality is
   `DEADLOCK`.
6. Test and set its visited bit. A repeated intermediate slot is `INVARIANT`.
7. If it equals the exact counterfactual cleared handle, terminate with no
   dependency. Otherwise choose `SEND -> REPLY -> specific RECEIVE`, or
   terminate for no dependency or receive `ANY`.
8. Reject an invalid stored next endpoint or exhausted hop cap without
   mutation.

The exact generation and owner checks precede bitmap use. During one
serialized read-only walk, a slot cannot change generation, so slot membership
does not collapse two valid live identities. A stale stored index fails before
it can become a visited or candidate result.

Use two scalar `uint64_t` visited words, initially zero, for the current
128-slot domain. Select one word by the admitted slot and one bit within it.
Require a compile-time bound that this representation covers the complete
thread table. Do not retain the old handle vector, scan a visited prefix,
clear a variable-length bitmap, add persistent stamps or recurse.

Two explicit word initializations and one membership probe/update per
non-candidate hop give `O(d)` time and 16 bytes of visited storage in this
admitted domain. This is not an arbitrary-capacity claim; growing the table
beyond that representation requires its own explicit design/cost admission.

Do not add a reply-token uniqueness scan or full completion validation at
each node. An unblocked thread may have a staged delivery and still have no
blocking dependency. IPC01's exact selected role/corruption boundary remains,
apart from the explicitly changed ownership projection.

The three existing call sites continue to run after matching failed and
before the final scheduler mutation. `reply_receive` retains one reply
preflight and one receive selection; its cleared handle changes only the
read-only graph, not the live token or index. Candidate-before-visited-before-
cleared precedence is unchanged.

## Work and storage obligations

Let `d` be consumed dependency hops, `P/T/H` the existing capacities, and
`n` the selected process's live-thread count.

| Operation | Required cost |
| --- | --- |
| Dependency endpoint/sole-thread resolution | Constant indexed admission per hop; no thread/process/hart/profile table scan |
| Valid chain, candidate cycle, intermediate cycle | `O(d)` within the existing hop cap; constant visited membership |
| Index publication for creation and `1 -> 0` release | Constant extra work; existing allocation and current-hart checks stay separately accounted |
| Larger-policy `2 -> 1` release | At most one `T`-slot repair preflight, constant scratch; zero calls from the dependency walker |
| Other larger-policy count transitions | Constant index work |
| Complete ownership/index diagnostic | Existing count passes plus constant work per sole process, `O(P + T)` for this component |

Add only one existing eight-byte handle per process. Bound actual registry
growth by `8*P` (512 bytes currently); do not add fields to threads or harts,
new runtime globals or owner-list tables. The dependency walk removes its
1,024-byte visited vector and adds only the two words and fixed local views.
Keep all actual simultaneous compiler-derived stack bounds within existing
budgets; an observed sample does not supply an upper bound.

Record hop, selected-index, visited-word and cold-repair visits at the
executing primitives. The original implementation must fail the per-hop
work bound because it visits `T` slots and compares the visited prefix.
Green must not merely rename those loops or charge their work to setup.
Unchanged scheduler, token, queue and buffer work remains separately owned.

## Current layout and evidence qualification

This is a kernel-internal process layout extension, not a user-visible
message, syscall, handle, trap or context ABI change. Keep all existing fields
and their meanings; append the sole handle rather than repacking earlier
fields. Registry array offsets/extent change and must be measured, not guessed.

The implementation must maintain:

- current object/IPC/native fixture construction and defined-state
  serializers, including the new index; failed operations still preserve
  all actual bytes and outputs;
- the original independent object and IPC models, not a reference that reads
  the new production index to decide its own expected owner;
- complete observation/function/site and build/recipe ownership;
- actual compiler-produced size/offset data and selected panic-owner pointer/
  generation effects for the new registry geometry.

`trace_object_layout` currently guards the predecessor object header before
compiling its `sizeof`/`offsetof` object. Admit only this exact typed process
field extension at that proof-critical boundary and keep the remaining
layout/type/constant/source obligations. Do not replace expected complete
headers with candidate hashes, accept arbitrary members, or infer offsets
from old artifacts. No panic resolver, memory-read authority, assembly epoch,
observer effect, fatal qualification or exclusion is broadened.

Any necessary current schema or closed-profile update belongs in this same
implementation under ADR-0123. It does not create another metadata-only
prerequisite. Unknown actual indirect or machine effects still fail closed.

## Test-first and acceptance

Begin implementation only after this design separately merges.
Extend existing current native evidence where possible; no new author
journal, historical implementation execution or duplicate target workflow is
required.

Native evidence must retain all original cases and add focused proof of:

- chains of every supported process depth, sparse/high thread slots including
  both bitmap words, no-dependency/`ANY` termination and exact generation reuse;
- candidate and intermediate cycles, all dependency precedence and post-reply
  cases, source-only traversal and exact unchanged failure state/output;
- zero, one and multiple-thread projection; count-one missing, out-of-range,
  stale, foreign or mismatched index; noncanonical zero at zero/multiple count;
- portable creation/release/quarantine and repeated `0 -> 1 -> 2 -> 1 -> 0`
  transitions, releasing either of two threads and a survivor at a high slot;
- cold-repair missing/duplicate survivors and late rejection without partial
  count/index mutation; no scan on the production one-thread path;
- PM reservation, prepared publication/abort, exit detach/final release and
  their generation-reuse/rollback boundaries with the same index invariant;
- complete diagnostic rejection of unindexed ownership corruption, separately
  from the explicitly scoped selected result;
- actual linear hop/index/membership work, cold `T`-visit limit, new persistent
  field size, reduced local storage and ordinary/observed agreement.

Keep the 4,096-step object model, 8,192-step IPC model and all original seeds.
Models derive ownership independently from their logical state, not from
`sole_thread`. A new malformed index must not be normalized away in comparison.

This portable algorithm starts with native work/semantic Red. No new trap,
CSR, assembly, mapping or hardware mechanism is introduced. Existing real
object, IPC/ecall/syscall, scheduler, bootstrap, PM and spawn/rollback/exit
workflows still prove target composition and actual layout use. Keep the
IPC01 zero-audit/retained-buffer and OBJ02 context-work witnesses.

Before implementation review, stage the complete candidate and execute the
authoritative planner without path narrowing:

```bash
"${MICROS_PYTHON}" -E -S -B tools/validation_plan.py --tier pr --base origin/main --execute --fresh
```

Shared object/header changes select the complete required native, QEMU,
integration, standalone-image, documentation and stack scope. Preserve all
44 static configurations, 113 ELFs, ten observed scopes, current budgets and
blocked B13 admission. One actual final plan also supplies affected-unit
evidence; do not duplicate a B-unit recertification ceremony. If supported
receipt/runner closure changes, verify the affected profiles under existing
rules, with one full producer sufficient for that acceptance.

Independent GPT-6.1 Sol implementation review precedes the first permanent
outcome commit. Exact reviewed/published/merge-tree equality and scoped branch
cleanup follow the existing workflow. Only accepted implementation and merge
close B04-F003.

## Current implementation and evidence owners

The object identity leaf checks the selected index. Portable process/thread
owners and the enumerated PM commits maintain it with their existing count
updates; complete diagnosis retains the independent count pass. The dependency
owner resolves one indexed thread per hop and uses two local visited words.
The original independent object, endpoint and IPC models derive the expected
sole handle from their own thread state, not the production index.

The existing `selected-ipc-validation` pair keeps all sixty IPC01 cases and
adds fifteen dependency and eight index-lifetime cases. They cover long,
cross-word, high-slot and permuted chains, candidate/intermediate cycles,
`ANY`, the cleared edge and its corrupt index, and the explicit cold repair.
Actual summaries distinguish dependency hops/index probes/bitmap loads from
the retained queue work and distinguish eight current-hart visits from a
two-thread release's additional 128-slot repair. Word initialization is
measured separately; these are not elapsed-time estimates.

The fixed OBJ01 reference data remains unchanged. Its native producer now
reports a separate reference-state view retaining the original common bytes
and original 40-byte process representation, with only the old four-byte tail
padding represented as zero. Full current raw before/after hashes and
byte-preservation checks remain separate and unchanged in meaning. New
sole-index input/output digests are checked against the independently specified
single-process fixture cases; non-plan outputs and apply effects retain every
old comparison. No current state, handle or result is normalized, and no
historical implementation is compiled or executed.

Observed stack producers keep their independent scenario-specific slot and
generation expectations, actual linked intervals and compiler-derived bounds.
The existing attachment/detachment observers still authenticate current
generation, close old records before reuse and reject foreign, duplicate or
missing reports. Those unchanged checks, not whole-file equality of earlier
lifetime implementations, qualify the current producer inventory.

Panic ownership qualification admits the exact appended typed member while
retaining all other types, macros, storage and protected APIs. Its compiler
layout cache includes current header identities, so the two admitted fixture/
current geometries cannot borrow each other's offsets. Selected-provider and
load-fault-prefix functions retain exact source/signature/guard and linked effect checks;
unrelated lifetime/helper bodies do not freeze the whole provider. Shared
preprocessor/state mutations, unknown members and altered resolvers still
fail. No fatal exclusion, assembly epoch or stack budget changes.

Exact author commands, current complete stack/target acceptance, independent
review and publication identities belong to the implementation PR. The
correction register records closure only after accepted implementation merge.

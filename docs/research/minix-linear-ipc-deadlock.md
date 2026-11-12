# MINIX Linear IPC Deadlock Study

## Fixed inputs and question

```text
micros source: 5b24b333f20906a1ee4408e3e7215e0133d921f7
micros tree:   389c5ba6c5f81f9dd863431b9893e9ef735cf7dd
MINIX source:  4db99f4012570a577414fe2a43697b2f239b699e
```

The local MINIX checkout was checked at that revision with a clean worktree.
IPC01 is independently reviewed and merged as #251. This study owns the
remaining B04-F003 question: how to follow the current single-thread
dependency chain without scanning the thread table and visited prefix at
every hop. It is design research, not executable Red/Green evidence.

The [fixed B04 audit](minix-complete-baseline-b04-endpoint-ipc.md#b04-f003-deadlock-projection-is-quadratic-in-thread-capacity)
already identifies direct sole-thread resolution and bitmap membership as
the smallest correction. IPC02 changes neither reply-token lookup nor
sender queue organization.

## Fixed MINIX mechanism

Paths in this table are relative to the reference checkout.

| Source | Established behavior and structure |
| --- | --- |
| `minix/kernel/proc.h:23-90` | One process record carries its endpoint, RTS flags, send target and receive source. There is no separate kernel-thread projection on this path. |
| `minix/kernel/proc.h:180-200` | `P_BLOCKEDON` selects sending before receiving; the receive field need not be meaningful while a combined operation is still sending. No blocked reason yields `NONE`. |
| `minix/kernel/proc.h:264-274` | `proc_addr` maps the decoded process number directly to one process-table entry. |
| `minix/kernel/proc.c:701-767` | `deadlock` follows those endpoint dependencies, stops at no dependency or `ANY`, and detects a return to the caller. A complementary two-party send/receive is exempted by the existing function/flag rule. |
| `minix/kernel/proc.c:868-960` | `mini_send` tries matching delivery before checking deadlock and blocking. The sender-queue append walk is a separate cost. |
| `minix/kernel/proc.c:965-1117` | Receive considers pending work and matching senders before the specific-source blocking path. |
| `minix/kernel/proc.c:1828-1860` | Endpoint admission checks the implemented slot, nonempty process and complete endpoint equality. |

For `d` traversed dependency nodes, the non-debug reference path is `O(d)`
time and constant auxiliary storage. Average and amortized bounds add no
table scan. Debug warning configuration has a separate process-vector
diagnostic; it is not the ordinary algorithm's workspace.

MINIX's normal published graph does not supply `micros`' separate result for
a pre-existing intermediate cycle. `micros` retains that bounded corruption
check rather than relying on an acyclic incoming graph.

The established adaptations remain:

- process, thread, endpoint and hart identities stay distinct;
- each projected process must have exactly one live thread, even when a
  native registry admits a larger thread policy;
- reply wait contributes its exact callee after send and before receive;
- `reply_receive` examines the counterfactual graph with one exact caller's
  dependency cleared, before consuming the reply token;
- matching first handles the complementary pair, rather than copying the
  MINIX function-bit exception;
- stored links carry generations, not raw cross-process pointer authority;
- source-only nodes retain their existing traversal rule.

These are already fixed by ADR-0011, ADR-0030 and ADR-0124. Full multithread
wait-set exploration remains SOM-08.

## Current `micros` mechanism

`kernel/ipc/ipc_deadlock.c` has three cohesive stages:

1. `resolve_dependency_endpoint` uses IPC01 selected endpoint admission,
   accepting the existing active/source-only dependency states.
2. `resolve_sole_live_thread` checks the selected process count, scans all
   128 thread slots, rejects duplicate owners, then admits the selected
   thread's metadata and consumed IPC role.
3. `ipc_preflight_deadlock` compares the new thread with the caller and every
   prior visited handle, then selects the next edge.

For thread capacity `T` and chain length `d`, current work is
`O(d*T + d^2)`, with `O(T)` automatic visited handles. The current RV64
128-entry handle vector alone occupies 1,024 bytes.
IPC01 removed ordinary full audits but intentionally retained this algorithm.

The only operation call sites are the blocked send/call continuation in
`ipc.c`, the blocked receive continuation there, and the post-reply
continuation in `ipc_reply.c`. Matching, permissions, token availability and
the final scheduler transaction remain with those owners.

## Ownership lifetime is part of the correction

The process currently stores `live_thread_count`, not a directly resolvable
sole-thread identity. Adding an index only in `micros_thread_create` would
miss the production PM spawn path.

| Current owner | Required index lifetime |
| --- | --- |
| `kernel_objects.c:micros_kernel_objects_initialize` | Initial zero storage has no indexed thread. |
| `kernel_object_process.c:micros_process_create_at`, `micros_process_release` | Create/reuse/free/quarantine retain an empty index at zero threads. |
| `kernel_object_thread.c:micros_thread_create`, `micros_thread_release` | Maintain zero/one/many transitions together with both live counts. |
| `pm_control_reservation.c:micros_pm_control_reserve_commit_prevalidated`, `micros_pm_control_abort_commit_prevalidated` | PM's direct empty-process construction/destruction also initializes or clears the index. |
| `pm_control_execution.c:micros_pm_control_prepare_commit_prevalidated` | PM directly creates the prepared thread and increments counts; publish its exact index in that same non-failing commit. |
| `pm_control_execution.c:micros_pm_control_abort_prepared_commit_prevalidated` | Clear the index when the prepared thread is removed. |
| `pm_control_exit.c:micros_pm_control_exit_detach_commit_prevalidated` | Clear the index during exact exit detach, not later VM resource release. |
| `pm_control_exit.c:micros_pm_control_release_exited_commit_prevalidated` | Final process release retains zero index. |

Empty/prepared/stopped process predicates in the adjacent PM owners must
consume the corresponding new shape. Endpoint close, scheduler transitions,
context changes and VM mapping changes do not change thread ownership and
must not independently clear or rewrite the index.

The complete object validator already counts every live thread per owner,
then compares those counts with process records. At actual count one, an
exact live indexed thread with the same owner is necessarily that unique
thread. This supplies complete index validation without a second per-process
thread scan or another capacity-sized witness.

## Smallest current representation

Append one existing typed thread handle, `sole_thread`, to each process.
It is exact zero at counts zero or greater than one, and the exact sole
live handle at count one. It is lifecycle-maintained ownership metadata,
not a validated-state bit or an optional cache with a lookup fallback.

The production checked policy permits one live thread per process. Its
create/release/PM transitions maintain this field with constant work.
Existing larger native policies must still work: creating a second thread
clears the sole index, and removing one of exactly two threads preflights
one bounded table pass to identify the survivor.

That `2 -> 1` release pass is an explicit cold-path tradeoff, not a hidden
constant-time claim. It costs `O(T)` time and constant scratch, executes
zero times under the production one-thread policy, and is never part of a
deadlock lookup. A future SOM-08 design must reconsider this maintenance
cost before exposing general user-thread lifecycle. No intrusive ownership
collection or opaque XOR membership aggregate is needed for this MVP.

The Accepted decision accepts that additional bounded native-policy cost
explicitly. Implementation acceptance must measure the exact scan, failure
preservation and repeated zero/one/many transitions; it cannot justify a
broader production regression by calling every capacity constant.

Visited membership uses two scalar RV64 words for the existing 128-slot
domain. Initialization is exactly two scalar stores; each admitted thread
selects one word and bit. This is not a variable-sized bitmap clear.
A larger thread-capacity design must account for its own initialization and
storage rather than inheriting an unqualified `O(d)` claim.

## Classification and cost ledger

Each row has one classification.

| Mechanism | Classification | Cost and reason |
| --- | --- | --- |
| Direct blocked-endpoint chain and send-before-receive precedence | Baseline parity | `O(d)` reference mechanism; existing reply stage remains between send and receive. |
| Process-owned exact sole-thread handle | Required adaptation | Distinct objects require one explicit indexed identity; constant selected work, `O(P)` persistent storage. |
| Intermediate-cycle bitmap and checked hop cap | Compatible extension | One word probe/update per admitted hop, 16 bytes in the admitted domain; no prefix comparison. |
| Full generations, exact owner and selected IPC role | Required adaptation | Constant checks before consuming one node; no global or token-uniqueness scan per hop. |
| Larger-policy `2 -> 1` index repair | Staged substitution | `O(T)` release preflight outside the production one-thread policy; explicit SOM-08 replacement review, not a general threading completion. |
| Complete index/ownership diagnosis | Compatible extension | Reuse the existing `O(P + T)` count passes; at most one direct indexed proof for a sole owner. |
| Reply token allocation/lookup and sender-tail organization | Divergence requiring correction | Remain IPC03 and IPC04; their cost is not charged to the new dependency walk or claimed fixed. |

The new persistent raw field cost is one eight-byte handle per process,
512 bytes at current capacity. Actual process/registry size and table offsets
must be compiler-measured in implementation; no thread, hart, message,
trap-frame or user-context field is added. The visited vector is replaced,
not retained beside the bitmap.

## Current evidence and layout consumers

`ipc_deadlock_test.c` already covers every endpoint chain length, independent
bounded reference behavior, send/receive/call cycles, dependency precedence,
`ANY`, source-only traversal, post-reply graphs and atomic corruption errors.
The independent mixed IPC model retains all 8,192 transitions and its seed.
New work evidence must use actual current primitives, not model action counts.

Process-layout changes also affect current native state serializers, fixtures
and the real panic-owner layout qualification. In particular,
`tools/stack_bound_fatal_effects.py:trace_object_layout` checks a fixed header
boundary before compiling actual `sizeof`/`offsetof` constants.
The owning implementation must admit this exact typed field extension there
and requalify the actual current object/linked effects, without freezing a
new whole ancestor, deleting the guard or changing a fatal exclusion.

The [canonical contract](../architecture/linear-ipc-deadlock.md) defines the
complete transition, failure, work and acceptance requirements.

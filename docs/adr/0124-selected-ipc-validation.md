# ADR-0124: Selected IPC Validation

- Status: Accepted
- Date: 2026-11-12
- Refines: ADR-0029, ADR-0030, ADR-0035, ADR-0036, ADR-0037, ADR-0059,
  ADR-0062, ADR-0067, ADR-0080, ADR-0083, ADR-0084, ADR-0119, ADR-0121
  and ADR-0123
- Supersedes after Acceptance and separate design merge: complete
  endpoint/profile/object validation as an ordinary endpoint, IPC and
  selected-completion precondition, only at the nineteen boundaries in the
  canonical contract

## Context

The correction DAG selects IPC01 after merged MEM04, OBJ01 and OBJ02.
Those outcomes make translation, scheduler checks and common-return work
selected, but ordinary endpoint/IPC operations still reconstruct complete
endpoint/profile/object relations repeatedly. One pending completion pays
the same audit before delivering one message or result.

The [fixed local study](../research/minix-selected-ipc-validation.md) traces
MINIX's direct endpoint/privilege checks and affected queue/delivery state.
The current `micros` source contains 31 direct production complete-audit
call sites; nineteen belong to this correction and twelve remain explicit
initialization/diagnostic or other-subsystem boundaries.

The existing global audit also supplies real safety preconditions. Several
queue and completion helpers index owner/link slots without local admission.
Simply removing the calls would permit unsafe reads, malformed no-match
fallback or mutation before a late failure.

## Decision

After review, Acceptance and separate merge, implement the
[selected IPC validation contract](../architecture/selected-ipc-validation.md).
It owns only B03-F006, B04-F001 and B04-F002.

### Selected authority, not cached success

Ordinary queries validate current scalar metadata, exact selected generations,
endpoint/process/profile relations and the fields their operation consumes.
Installed profile queries use direct indexed rows and bit tests, not repeated
profile-name/table closure scans.

One operation owns its selected caller, peer, profile, queue and completion
records through its existing serialized lifetime. Pass that concrete view to
internal authorization/rendezvous work rather than reentering checked public
APIs and reconstructing the same proof.

The view is private automatic state, not persistent authority, a global
validated bit, a mutable cache, an epoch shared across operations or a
caller-supplied permission to skip checks. It is not retained across an
interrupt-enable boundary, return, blocking interval, context switch,
endpoint reuse or unrelated mutation.

### Check what will be consumed before it can be indexed or published

Selected owner, profile, queue and completion corruption remains an invariant
failure before mutation. Slot/generation admission precedes indexing and
profile narrowing. Matching returns distinct error/no-match/match outcomes;
corruption cannot become an enqueue fallback.

Queue selection preserves first-match/FIFO order. Retain the checked prefix,
predecessor, matched node, successor and affected endpoints/tail needed for
one splice. An append after no-match consumes that same result rather than
searching again. No private helper may assume a full registry audit unless
called only by the explicit complete validator.

Existing kernel-only low-level rendezvous entry points retain their structural
contract; they do not acquire a new user permission model. The six public
operation paths still enforce their exact operation and destination authority.

### Preserve reply and completion authority

Reply remains one-shot and bound to the exact caller thread and callee
endpoint generation. A staged but unreturned request token is not presentable
by another callee thread. Keep token exhaustion, duplicate-binding rejection,
request-return ordering and complete failure preservation.

IPC03 still owns token indexing. This outcome keeps the existing bounded
token scans where the operation actually allocates, resolves or presents
that token. A token-bearing completion therefore need not be constant-time;
it must not invoke a complete endpoint/object audit.

Selected completion validates the exact receiver, source/envelope and optional
caller/token relation, then retains the existing ABI result, message and
at-most-two-page write plan. No-message success/error and malformed residual
non-pending state remain distinct. Preserve source-`NONE` kernel notification
and the exact source-only bootstrap acknowledgment exception.

Commit uses the retained physical chunks and selected context without another
lookup, patches only the existing result register, and clears completion once.
Invalid accepted bootstrap buffers still panic with
`invalid-bootstrap-ipc-buffer`. Do not change MEM04 translations, OBJ02
context reuse, root preparation, fences, timer preparation or stack epochs.

### Lifecycle and complete diagnostics

Endpoint reservation/publication/sealing/close use the concrete selected
process/endpoint/profile and the owned-thread or reference scans that their
transition requires. IPC close still preflights all references and queue
rewrites it will consume; it does not become an unchecked clear or silently
drop an unmatched stale same-slot reference.

Keep complete registry, profile, object, queue, pending-source, matchability
and token diagnostics, their original corruption cases and explicit callers.
The twelve residual production audit boundaries are not removed through this
task. Unrelated grants, bootstrap and PM validation cadence remains with its
own correction owner.

A selected success certifies its consumed scope, not an unrelated slot or a
whole-state invariant. Corruption wholly outside that scope may no longer
take precedence over an otherwise valid selected operation; the explicit
complete validator must still reject it. Stable argument/authority/state
failures remain unchanged, except for the coupled full-capacity close defect:
a valid queue ending after exactly `T` members must not be rejected as
nonterminating.

### Current evidence and bounded work

Use the existing test-only primitive collector and current-source generation.
Actual native Red demonstrates complete audits and repeated/global work at
the declared boundaries. Current Green must establish zero complete audits
there and the independent selected-operation work bounds, while retaining
complete semantic/error/output, independent-model and ordinary/observed
agreement evidence.

Real QEMU evidence covers the captured syscall and selected-completion
boundaries. It must retain the existing guest semantics and publish complete
actual work witnesses, not timing estimates, summary success bits or a second
guest. Unknown, incomplete, duplicate, overflowing or unowned observations
fail. No timing-sensitive UART output or overlapping collector scope is added.

Existing native model seeds/transitions, all original QEMU scenarios,
standalone images, complete stack coverage, budgets and blocked B13 remain.
One sufficient final planner run supplies current acceptance and affected-unit
evidence under ADR-0123; no historical catalog, journal or metadata naming
prerequisite is revived.

## Supersession boundary

The old complete-audit precondition and its unconsumed-corruption precedence
change. This includes ADR-0037's former whole-registry validation cost on
every kernel notification and ADR-0036's dependence on a prior whole-object
audit: their actual selected authority is checked explicitly. The checked
queue replacement also corrects the fixed close walk's off-by-one rejection
of a valid full-capacity queue, with a reproducing regression.

All endpoint lifecycle states, generation/profile ownership, operation
permissions, token/payload ABI, notification order/coalescing, reply exclusion,
failure atomicity and source-only restrictions remain. The full diagnostic
API remains available and complete. A private selected view cannot authorize
a new destination, stale generation or broader memory access.

## Phase and non-goals

This Accepted design contains documentation only. Independent design review
is clean; `ipc01-implementation` remains pending the separate design merge.
No executable prototype, test, model, observation harness or build scaffold
belongs to this phase.

Non-goals are IPC02 deadlock projection, IPC03 token indexes, IPC04 sender
queue redesign, IPC05 filters, asynchronous IPC, VM-assisted faults, signals,
SMP, new public syscalls or persistent state, changed service policy, byte-copy
optimization, larger budgets/timeouts, generic verifier/cache frameworks,
hosted-CI acceptance or closure of another runtime finding.

Implementation proceeds in small Green slices: selected admission and query
paths; retained queue/operation/lifecycle preflight; selected target completion;
current work/target evidence and directly dependent documentation/metadata.
Independent implementation review precedes the first permanent outcome
commit, followed by exact-tree publication and scoped branch cleanup.

## Design review disposition

Independent read-only GPT-6.1 Sol review inspected the complete six-document
proposal, the relevant current and fixed MINIX source, Accepted contracts and
exact author evidence. It reported no substantive design issue remaining.
All four documentation-phase checks passed; no targeted reproduction or
reviewer gate rerun was necessary. Acceptance records the design result, not
an implemented correction or a closed runtime finding.

## Alternatives

- Remove audit calls without replacing their implicit preconditions: unsafe
  owner/link indexing and incomplete failure preflight.
- Cache a complete-valid bit across operations: stale authority and another
  invalidation protocol.
- Keep global audits around every selected primitive: retains the three
  audited divergences.
- Fix tokens, deadlock, sender queues and service validators together:
  combines separately owned correction outcomes.
- Drop malformed-state or target evidence: conceals the authority and
  hardware boundaries rather than correcting their cost.

## References

- [Selected IPC contract](../architecture/selected-ipc-validation.md)
- [Fixed MINIX/current study](../research/minix-selected-ipc-validation.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)
- [Current validation](0123-current-contract-validation.md)

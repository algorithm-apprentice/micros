# ADR-0126: Direct Reply-Token Binding

- Status: Accepted
- Date: 2026-11-12
- Refines: ADR-0004, ADR-0011, ADR-0019, ADR-0030, ADR-0035, ADR-0036,
  ADR-0059, ADR-0067, ADR-0080, ADR-0083, ADR-0084, ADR-0096, ADR-0123
  and ADR-0124
- Supersedes after Acceptance and separate design merge: consecutive opaque
  token-number issuance and its terminal-tail assumption; capacity-wide
  allocation/waiter/staged-request searches and their unindexed-corruption
  precedence; token-specific pairwise complete diagnosis

## Context

IPC01 #251 and IPC02 #253 are independently reviewed and merged.
IPC03 is the next sequential preferred correction and owns only B04-F004.
Current allocation, reply and selected completion still scan every thread
for one token or request.

The [fixed local study](../research/minix-direct-reply-token-binding.md)
traces MINIX's direct endpoint destination and selected reply-wait state.
MINIX does not have the one-shot token extension. `micros` must retain exact
caller-thread/callee-generation authority and ADR-0036's request-return
boundary while removing those repeated searches.

## Decision

After independent review, Acceptance and separate design merge, implement the
[direct binding contract](../architecture/direct-reply-token-binding.md).
This proposal authorizes no implementation artifacts before that barrier.

### Direct opaque caller routing

Use the low seven token bits for the admitted caller thread slot in the
existing 128-slot domain. Keep the existing global 64-bit high-water value;
plan the least representable greater token with that slot tag. Publish the
high-water value only on successful call commit.

This retains token width, opacity, strict monotonicity and nonreuse, but
deliberately changes the old consecutive values. Exhaustion means no greater
value for the selected caller tag; it may precede the final value available
to another slot. The exact arithmetic, examples and failure boundary are
canonical, not left for implementation to invent.

Clients still echo opaque values unchanged. No token migration, dual format,
new syscall or reset exists. A slot only routes selection: full active token,
live caller/owner, exact callee and presentability checks still confer
authority. Multiple native threads are not collapsed into a process index.

### Explicit current request lifecycle

Append a fixed-width current request phase and one typed receiver handle
to each thread. Phases are `NONE`, `QUEUED`, `STAGED` and `RETURNED`.
`STAGED` requires an exact live reciprocal recipient; `RETURNED` requires
exact null. A missing staged index cannot mean "already returned".

The phase is current call protocol state, not cached whole-state validation
or consumed-token history. Existing call publication, queued/immediate
delivery, reply/receive, selected return and cancellation maintain it under
the existing serialization.

Only the selected request completion's successful commit publishes
`RETURNED`, after the existing physical copy. Preflight retains the admitted
caller pointer, so commit adds stores but no lookup or new failure.
An authorized callee sibling may reply afterward; the receiver index is not
permanent thread-specific reply permission.

### Selected checks and complete diagnosis

Decode one caller and, when staged, one exact receiver. Validate consumed
phase, tag, generation, endpoint and reciprocal message relations before
mutation. Keep the current replier/argument/operation order, expected-caller
restriction, one-shot consumption and atomic combined operation.

Complete diagnosis retains every token/lifetime invariant. Per-slot tags
exclude duplicate canonical active tokens, and reciprocal links exclude
duplicate canonical staged requests, without token-specific pairwise scans.
Unindexed malformed duplicates/orphans remain diagnostic errors but no
longer dominate an otherwise unrelated selected lookup. A wrong-slot copy
cannot route a second reply.

Close retains its original lifecycle transaction and drain barrier. Its
existing thread pass consumes new pending relations for reference selection;
malformed staged/link state cannot be ignored through unrelated legacy
endpoint fields. No additional capacity pass or complete hot-path audit is
introduced.

### Bound implementation and evidence

Keep existing ownership direction: endpoint-owned codec/relation leaves,
IPC token/orchestration owners, object clear-state leaves and the real
scheduler completion boundary. Include PM's stopped EXIT-call predicate
and direct teardown, plus all existing fixture drain simulations.

Token-specific ordinary work is worst-case constant with constant scratch.
Close retains its lifecycle pass; the token component of complete diagnosis
is linear. Queue, deadlock, scheduler and translation work remain separately
accounted. Add no global map, hash/tree, heap or per-operation witness.

Append only the phase and typed receiver handle to threads; bound actual
registry growth by `16*T` and keep the selected-completion plan within its
existing 256-byte limit. Maintain actual compiler layout and source/machine
qualification for those exact additions, current serializers and independent
new-field observations. Keep existing fixed reference data, budgets, fatal
exclusions and B13 block.

## Phase and acceptance

This phase is documentation only. `ipc03-implementation` remains pending.
No code, test, model, observation harness, build scaffold or reusable executable
prototype belongs to this design.

Implementation starts with actual current scan-work Red whose semantic
inputs remain comparable, followed by independent tests of the new explicitly
specified opaque-value boundaries and current request transitions.
Preserve all original IPC/model scenarios, transition counts and seeds,
callee-sibling authority, failure bytes, close, selected-return and real
target composition. No candidate-derived oracle is permitted.

One sufficient current author plan supplies required acceptance and
affected-unit evidence under ADR-0123. Independent design review precedes the
first design commit; fresh independent implementation review precedes the
first permanent implementation commit. Neither this proposal nor a partial
Green closes B04-F004.

## Non-goals

No sender-tail redesign, filters, asynchronous IPC, user-thread API, SMP,
general token dictionary, new public completion operation, different PM/VM/
scheduler policy, token secrecy claim, backward token format, history or
recovery ledger, larger deadline/stack, hosted-CI acceptance or node-Q/POSIX
completion.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
eight-document proposal, fixed local MINIX/current source, relevant Accepted
contracts and exact four-command author evidence. It reported no substantive
design issue remaining, with High confidence. No targeted reproduction,
validation rerun or delegated review was needed.

Acceptance records that design result, not a runtime correction or finding
closure. The separate design merge still precedes implementation.

## Alternatives

- Keep consecutive numbers with a direct modulo ring: an old active call can
  collide with a new one despite free thread capacity.
- Use a hash/tree or rebuild a map: unnecessary collision/storage policy or
  linear work instead of direct identity.
- Equate process and thread slots: violates separate objects and native
  multi-thread call/reply authority.
- Treat a null pending link as returned: turns missing metadata into early
  reply authority.
- Keep only a returned flag: loses direct reciprocal staged-owner admission.
- Delete global scans without complete diagnosis: silently drops existing
  corruption and lifetime obligations.
- Preserve old numeric test values with compatibility machinery: clients have
  an opaque per-registry-lifetime token, not a persisted cross-version format.

## References

- [Canonical direct binding](../architecture/direct-reply-token-binding.md)
- [Fixed local research](../research/minix-direct-reply-token-binding.md)
- [Current selected IPC contract](../architecture/selected-ipc-validation.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)

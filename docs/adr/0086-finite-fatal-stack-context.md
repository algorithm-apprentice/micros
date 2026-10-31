# ADR-0086: Finite Fatal-Stack Context

- Status: Accepted
- Date: 2026-11-01
- Refines: ADR-0012, ADR-0014, ADR-0033, ADR-0034, ADR-0080, ADR-0082,
  ADR-0084, and ADR-0085
- Supersedes in part: ADR-0084's context-free call-graph composition, solely
  for the closed UART panic-state proof below

## Context

EV03 design #180 and its artifact correction #181 are merged. The restarted
uncommitted implementation preserved native evidence, passive application
maps and complete-ELF seed bytes, and closed the current C callbacks.
It then verified this cycle against actual object and linked instructions:

```text
micros_panic_report
-> uart_write
-> micros_panic_entry
-> shared micros_trap_panic_entry body
-> micros_panic_report
```

The existing context-free graph correctly rejects it. However,
`micros_panic_report` first calls `micros_panic_seize`. Normal return from
UART seizure establishes a valid `PANIC` console, with supervisor interrupts
disabled. The existing `uart_write` ownership guard cannot fail while that
specific fact remains true. Neither a name-based edge deletion nor an
assumption that every panic is safe supplies that proof.

The implementation is blocked and its branch abandoned. The actual evidence,
fixed MINIX trace and remaining runtime findings are in
[the fatal-context study](../research/fatal-stack-context-evidence.md).

## Decision

Add only the finite state-qualified analysis defined in
[the stack contract](../architecture/stack-bound-evidence.md#finite-fatal-context).
Runtime code and authority remain unchanged.

### Actual functions, separate analysis contexts

Keep the actual `(variant, image, translation-unit, machine-symbol)` function
identity and its compiler or assembly frame. An analysis node additionally
has one of exactly two qualifiers:

- `UNQUALIFIED`: no special UART-state fact;
- `UART_PANIC_READY`: the exact kernel console has valid initialization
  magic, owner `PANIC`, and `quiesced=true`, with SIE and SEIE clear.

The qualifier is derived evidence, not a caller-supplied flag, object identity,
stack epoch, runtime mode or privilege. It does not release a caller frame.
Two live invocations of the same function retain two actual frames even when
their qualifiers differ.

Only the closed transition, preservation and guard sites in the canonical
contract may derive or consume the qualifier. Every other edge remains
unqualified or is rejected if it attempts to borrow the fact.

### Normal-return and preservation proof

Qualifying a call requires all of:

1. an independently declared source-stage, function, compound and occurrence
   anchor for the exact transition and subsequent call;
2. actual compiler/object/linked control-flow evidence that the checked
   transition's normal-return continuation dominates that call;
3. the exact console-object and state-field binding;
4. the closed source/machine proof of normal-return effects and preservation;
5. no intervening unbound call, state mutation, alias escape, interrupt enable
   or control-flow bypass.

Nonreturning failure paths confer no return effect and no termination success.
`_Noreturn`, source ordering alone, an observed UART marker and a passing
scenario are not qualification authority.

The reporter and seizure wrapper may qualify only their declared post-seizure
formatting calls. The formatting chain may preserve the fact only through its
declared readers, MMIO-only writer/flush and exact returning System Reset
boundary. The latter requires the independently pinned OpenSBI artifact and
the closed normal-return firmware/CSR contract in the stack specification;
the generic SBI GPR calling convention alone is insufficient.
The observed mode's two compiler hooks must separately prove their existing
zero-stack ABI and observer-only writes before preserving the fact.

Keep the required SBI-return fallback, its final flush and fatal wait loop.
Success is nonreturning, but a returning call is not discarded as unreachable
or used as a success result. All returning register/error values remain on
the existing failure path. Missing or changed firmware, a different EID/FID,
event injection, unbound memory/CSR effects or a redirected return invalidate
qualification instead of inventing preservation authority.

### One infeasible edge, no recursion waiver

Only `uart_write`'s exact `console-owner-violation` call to
`micros_panic_entry` may be marked infeasible in `UART_PANIC_READY`, after the
complete source/machine predicate proof succeeds.

The same call remains present in `UNQUALIFIED`. Every other panic call,
unknown edge, actual recursion, stack write and budget rule remains required.
A context label alone cannot exclude an edge.

Compose `F + max(child bounds)` over the resulting finite analysis graph.
Reject any remaining same-epoch cycle, unknown frame, incomplete binding,
overflow or excess. At most two analysis nodes exist per actual function,
so work and storage remain `O(V + E)` after artifact decoding.

The certificate retains actual functions and frames, adds the qualifier and
its source/machine proof references, and explicitly records the single
infeasible edge. It cannot silently publish a smaller unexplained graph.

### Preserved obligations

Keep every ADR-0084/0085 stack/image budget, current variant, dynamic scenario,
observer/capture requirement, future B13 admission and complete author gate.
No compiler optimization, deadline, polling policy, source identity,
ordinary recipe or production behavior changes.

This does not implement MINIX's recursive-panic guard, bounded polling,
corrupt-console fallback or stack trace. `PLAT02`/`PLAT03` and their B01
findings remain open. A stack certificate is not fatal-path liveness or
runtime baseline certification.

## Acceptance

The restarted implementation begins with the actual context-free cycle Red,
then proves:

- complete source/assembly/linked bindings for every qualification site;
- normal-return state establishment and conditional failure nonqualification;
- no borrowed, hoisted, skipped or nondominating state proof;
- complete preservation and exact infeasible-guard justification;
- flush and normal-return System Reset preservation under the pinned
  firmware/CSR contract, including the complete failure diagnostic and
  rejection of unsupported or mutated firmware/effects;
- the ordinary ownership-failure edge and both live UART frames remain;
- all other cycles, unknown edges and budget excess still fail;
- certificate context/proof inventory equality and fail-atomic publication;
- both metadata and observed graphs, all ten current target rows and B13
  blocked admission under the unchanged gates;
- retained ordinary bytes, native/model/recipe identities and independent
  implementation review.

No implementation or successful certificate is claimed by this design.

## Phase barrier

This is the sole active corrective-design prerequisite for EV03. It contains
documentation only: no code, test, model, harness, build scaffold or reusable
prototype is authored.

Only after independent review, Acceptance and separate design merge may EV03
restart from current `main`, referencing #180, #181 and this correction.
The preserved implementation patch is reference only; valid behavior returns
through the normal test-first loop, not a wholesale cherry-pick.

## Design review disposition

Independent GPT-6.1 Sol review found one substantive issue: the closed
preservation register omitted `uart_flush` and the returning System Reset
boundary before the required fallback output.

The corrected design binds both normal-return effects, the independently
pinned upstream firmware/source and exact CSR/console preservation scope.
Unknown firmware, changed effects and injected returns remain failures;
the fallback and all live frames remain required.

The reviewer established the omission statically and needed no reproduction.
Corrected independent re-review reported no substantive design issue.
Acceptance records that design result, not an implementation or certificate.

## Alternatives considered

- Delete every panic edge: conceals ordinary ownership failure and real
  recursion.
- Trust the successful panic scenario: samples one execution, not a bound.
- Increase budgets or optimize away the guard: changes another Accepted
  contract instead of proving the existing code.
- Implement `PLAT02` inside EV03: expands the evidence foundation into a
  separately owned runtime correction.
- Add a general whole-program predicate verifier: unnecessary for this one
  closed state fact and finite source/machine contract.

## References

- [Stack-bound contract](../architecture/stack-bound-evidence.md)
- [Fatal-context evidence](../research/fatal-stack-context-evidence.md)
- [B01 audit](../research/minix-complete-baseline-b01-platform.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)

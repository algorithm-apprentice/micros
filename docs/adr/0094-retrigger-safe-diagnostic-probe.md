# ADR-0094: Retrigger-Safe Diagnostic Probe

- Status: Accepted
- Date: 2026-11-02
- Refines: ADR-0012, ADR-0033, ADR-0034, ADR-0037, ADR-0047, ADR-0082,
  and ADR-0093
- Supersedes in part: ADR-0093's initial TTY probe ordering and native
  fixture operands, only through the test-only admission contract below

## Context

PLAT01 design #196 and collector-source correction #197 are merged.
The uncommitted implementation recorded four real expected Red failures,
then passed the native history/work/reader/consumer cases, the original
operation-14 cases and the seeded panic workflow.

The real TTY workflow failed before its large transfer. At the first
diagnostic-read hook, an exact debugger observation found:

- requested and registered cursors both 830;
- fixture phase one and console `TTY_OWNED`;
- route `IN_SERVICE`, source 10;
- two claims, one completion and one accepted readiness; and
- UART `IER=0x05`, with transmit interrupts already disabled.

The [source and artifact record](../research/minix-diagnostic-history.md#observed-startup-irq-retrigger)
distinguishes this observation from acceptance. No Green event, terminal
retirement, complete author gate or implementation review is claimed.
The implementation branch is abandoned and its patch is reference only.

ADR-0047 already permits a later claim after completion. Completing the
initial coalesced interrupt does not make `IDLE` persist across a later
user-mode canary setup and read syscall. The old probe correctly rejects
large work with a retained claim, but its startup sequence does not
establish that precondition.

## Decision

After separate review, Acceptance and merge, implement the bounded
[retrigger-safe probe contract](../architecture/diagnostic-history.md#retrigger-safe-target-probe-correction).
Keep the production reader, notification, IRQ and TTY-consumer contracts
unchanged.

### Test-only deferred admission

Only the existing `MICROS_BUILD_TTY_SERVICE_TEST` image gains the following
fixture behavior. The ordinary production binary has neither this branch
nor a new result contract.

After the actual new READ has passed its complete production authority,
lifetime, cursor and page-plan preflight, the first-probe hook checks the
exact fixture cursor and accepted readiness. If the route is `IDLE`, it
appends the fixed 8,193-byte pattern and admits the ordinary copy.

If the route instead retains source 10 in `IN_SERVICE`, the hook may defer
only after checking the exact TTY destination has its corresponding
queued kernel IRQ notification. This check is read-only. The adapter returns
the existing `MICROS_SYSCALL_ABI_STATE` result before any history insertion,
copy, header write, cursor publication, pending-bit consumption or
fixture-phase advance.

No other negative result, route, source, caller, cursor, phase or missing
queued event is a deferral. It remains an explicit failure.

This is not a production nonblocking read, poll operation, IRQ query,
retry policy or new syscall command. A malformed or unauthorized READ
never reaches the fixture hook.

### Consume the real pending interrupt

The immutable user-side fixture distinguishes `COMPLETE`, `DEFERRED` and
`FAILED`. On `STATE`, it requires the complete snapshot canary and all
consumer scalars to remain unchanged, then reports only `DEFERRED`.

The test-only startup facade responds to `DEFERRED` by receiving exactly
one canonical IRQ-only kernel notification and running the unchanged
UART drain/completion path. Only that observed, successfully completed
interrupt permits another attempt with the same cursor and buffer.
It does not wait or retry merely because a negative result occurred.

Each deferred iteration must correspond to a real retained claim and
queued event. There is no sleep, timed poll, guessed interrupt count,
workflow rerun or new deadline. The existing 90-second host timeout
remains the failure bound if progress does not complete.

No 8,193-byte producer injection or snapshot copy occurs while a claim is
retained or the readiness deadline is armed. Local canary initialization
and verification are fixture preparation, not kernel transfer admission.

### Preserve the original outcome

Keep the exact initial `TTY_IRQ | DIAGNOSTIC` envelope, readiness and IRQ
assertions. A deferred attempt does not advance the producer fixture from
registered to injected. One successful admitted transfer still checks every
pattern/header byte and inactive suffix, advances the real private cursor
and discards only the identified fixture spool.

The subsequent short diagnostic is still forwarded through the ordinary
TTY consumer once. Keep the existing marker, early-output suppression,
kernel-THR observations, VFS grant/input/completion/busy-retry scenario,
physical drain and clean shutdown. Repeated later ordinary diagnostic reads
do not enter the first-probe deferral protocol.

### Independent evidence and authority

Link the already declared immutable
`tests/qemu/diagnostic_history_tty.c` into the same future
`diagnostic-history` native CTest, immediately after its three host case
operands. This adds one source operand, not another file, target or test
execution.

Native cases exercise the actual fixture helper and fixed raw-syscall
double: a deferred read preserves every snapshot/consumer byte, a
non-`STATE` error fails, output mutation on `STATE` fails, and a later
successful attempt proves the complete three-page transfer. Ordinary
consumer cases still reject every negative read result.

The immutable source/constructor inverses must preserve all original cases
and callback providers while admitting only this exact startup progress
path. Target evidence must check deferred and admitted phases rather than
turning a failed fixture or absent notification into successful forwarding.

Keep the same twenty retirement keys, twenty-nine candidate paths,
fourteen immutable additions, four actual oracles, thirteen affected units,
42/111/10 stack scope and blocked B13 admission. After this design merges,
independently derive the new eight-fact PLAT01 declaration from the corrected
contract before changing extraction or owner files. Preserve all 3,912
already committed facts and eleven fingerprints, including COPY01's prior
claim. The abandoned unmerged declaration and journal are not prior
authority or current execution evidence.

## Phase barrier and acceptance

This corrective design contains documentation only. PLAT01 implementation
remains blocked until this design is independently reviewed, Accepted and
separately merged. It then restarts on a fresh branch from current `main`,
reintroducing valid reference behavior through the normal test-first loop.

Acceptance requires:

- the actual retained-IRQ failure and its independent source explanation;
- explicit fixture-only admission, state and no-mutation boundaries;
- actual native deferral/failure cases and real TTY integration after restart;
- unchanged production negative-result handling, IRQ authority and budgets;
- fresh actual Red, Green and complete unit recertification for the
  corrected immutable inputs;
- complete post-terminal author validation and clean independent
  implementation review.

Design Acceptance alone does not close B01-F002 or authorize a completed
claim.

## Design review disposition

Independent read-only review inspected the complete seven-file design and
its exact current author evidence. It reported no significant issue and
no substantive design issue remaining. No targeted reproduction was
needed, and the reviewer reran no validation gate.

Acceptance records that design result, not a working probe, retired
identity or completed PLAT01 outcome. Implementation remains blocked until
the separate corrective-design merge.

## Alternatives

- Remove the `IDLE` guard: rejected; that would admit the large probe while
  a real claim remains in service.
- Delay after completion: rejected; elapsed time does not establish
  interrupt ownership.
- Drain an assumed fixed number of notifications: rejected; neither the
  hardware contract nor host timing promises that count.
- Complete the claim in the kernel read path: rejected; TTY still owns
  device draining and explicit IRQ completion.
- Change production READ to return transient `STATE`: rejected; the
  production consumer has no such retry contract.
- Increase a timeout or add a new command: rejected; neither is needed for
  the existing event-driven fixture.

## References

- [Diagnostic histories](0093-minix-diagnostic-histories.md)
- [Console and IRQ protocol](0047-tty-console-handoff-and-serial-protocol.md)
- [Fixed baseline and observed blocker](../research/minix-diagnostic-history.md)
- [Correction contract](../architecture/diagnostic-history.md#retrigger-safe-target-probe-correction)

# ADR-0095: Bounded UART and Terminal Panic

- Status: Accepted
- Date: 2026-11-03
- Refines: ADR-0012, ADR-0014, ADR-0080, ADR-0082, ADR-0083, ADR-0084,
  ADR-0086, and ADR-0093
- Supersedes in part:
  - unbounded early/panic UART waits and the corrupt-magic seizure spin;
  - unguarded entry to the complete panic report;
  - the two-qualifier fatal analysis, only through the closed two-fact
    qualification specified below.

## Context

PLAT01 implementation #199 is independently reviewed and merged as
`8375ca2f5285bc42063af8ed92ba6f3811d517e8`, with tree
`cb472a9262b9d6f22c42c4c9e1182bf1da069d78`. Its actual transition,
complete fresh author gate, current stack proof and journal-free committed
authority checks passed. It closes B01-F002, not the remaining platform
findings.

PLAT02 now owns B01-F001, B01-F003 and B01-F005. The
[fixed-reference study](../research/minix-terminal-failure-baseline.md)
establishes a 100,000-attempt serial polling baseline and a separate
one-way panic sentinel. It also distinguishes reference best-effort writes
after exhaustion from successful physical drain.

Current `micros` waits indefinitely for THRE/TEMT. Its panic reporter has no
entry guard, and corrupt console initialization magic can spin before raw
UART setup or reset. Console owner `PANIC` cannot substitute for an entry
guard: legitimate callers already seize the device before their first
panic report.

## Decision

After separate review, Acceptance and merge, implement the complete
[terminal-failure contract](../architecture/terminal-failure.md).
Keep the PLAT01 histories, reader, notification, ordinary TTY policy,
captured panic ABI and healthy report unchanged.

### Bound the actual waits

Each THRE or TEMT wait performs at most 100,000 actual status reads. A
ready result on the last permitted read succeeds. Exhaustion performs no
additional read, no unready character store and no successful handoff
publication.

Retain the reference's local counter and early-exit algorithm, rather than
adding a clock, calibration, sleep, interrupt dependency or configurable
timeout. A logical LF still has two separately bounded physical writes.
Drain still requires TEMT, not ARM's weaker any-bit predicate.

The explicit exhaustion policy is a compatible failure-safety extension:
stop normal execution through the no-console failure path. Do not copy
MINIX's unconditional best-effort store into an interface whose caller
would otherwise assume successful handoff.

### Separate terminal ownership

One kernel-owned four-byte control object has only `IDLE`, `REPORTING` and
`RESET_REQUESTED` phases. It is distinct from console ownership and
diagnostic routing. BSS provides the initial state; production has no reset
or reinitialization API.

The reporter claims first entry before seizure, formatting or dereferencing
report arguments. A repeated entry does none of those operations. It goes
directly to no-console failure termination.

Reset ownership is published at the shared `sbi_system_reset` boundary,
before the actual `ecall` for shutdown/system-failure. At most one such
request occurs in the admitted terminal execution. This includes the
existing FDT-error caller, which does not enter the panic reporter.
A normal-return failure keeps the original failure line and final bounded
drain when UART is still usable. Another I/O failure or reentry after that
request goes directly to WFI, without another reset or recursive report.

The guard does not remove live frames. Both actual assembly capture frames
and reporter invocations remain in recursive-entry stack evidence.

### Recover only terminal console metadata

Panic seizure retains its fixed UART address and hardware initialization.
If the existing console transition rejects its initialization magic, the
target adapter overwrites only the known console object's magic, owner and
quiesced fields with the canonical panic values before raw UART setup.
It does not initialize arbitrary storage or resume normal operation.

`uart_panic_seize` returns a Boolean indicating that this repair occurred;
normal return always retains the established panic-console guarantee.
The kernel seizure owner emits the exact separately named repair notice
before the unchanged healthy report when output is possible. If the
device is also stuck, the bounded no-console path takes precedence.

The portable console validator and its ordinary failure behavior remain
unchanged. This is an irreversible panic-only fallback, not recovery,
restart or an ordinary state-repair interface.

### Keep the failure path independent of UART

The no-console terminator disables supervisor/external interrupt delivery
and uses the existing SRST wrapper. Only that wrapper calls the reset
claimer for the exact failure-shutdown tuple; callers do not preclaim it.
If reset returns or was already requested, it enters WFI without any
further UART, history, IPC, allocator, scheduler or metadata-repair work.

The normal first report still contains the original five contiguous core
records, optional trap record and PLAT01 encoded history. Preserve the
fixed SRST arguments, pinned firmware contract, failure-return diagnostic
and all existing healthy target requirements.

### Prove entry state and UART state separately

Finite stack analysis gains exactly two independently proved facts:
panic ownership is latched, and the UART panic state is ready. Their four
fixed combinations replace the previous two qualifiers.

Only a successful first-entry claim establishes the latch fact. Console
seizure establishes only UART readiness. Each fact needs its own closed
source/object/linked preservation proof; unknown effects still fail.

Keep the three PLAT01 UART exclusions under UART readiness. Add only the
first-report-body call exclusion under the latched-entry fact. That
exclusion is proved through the actual claim result and control flow, not
through a function name, `_Noreturn` or an observed marker.

A qualifier change releases no frame. Composition remains linear after
decoding, with at most four analysis nodes per actual function. All other
cycles, unbound calls, live frames and budget excess remain failures.

### Add only the required evidence

One native `panic-control` CTest covers the actual portable ownership
state machine. One new kernel-only configuration and workflow exercise
twelve fixed target cases using actual MMIO instructions, test-only fault
masks, EV02 snapshots and bounded debugger observations.

The original UART and panic workflows remain unchanged and required.
Static coverage extends from 42 configurations/111 ELF outputs to
43/112. The ten current EV03 observed scopes remain required, and B13
remains blocked. The new terminal observations do not replace them.

Actual baseline Red uses the pinned pre-correction runtime with only the
independently reversible fixture/observation overlay. Logical overrun and
the exact old corrupt-magic spin are observed directly; a host timeout,
broken harness or absent process is never an expected Red result.

No new transport identity is declared retired by this outcome. The
affected UART/panic whole-source keys already belong to PLAT01's committed
claim. Preserve all 3,920 old facts, twelve fingerprints and both prior
retirements, while independently registering the new baseline/effect
evidence. If implementation discovers another active identity that must
change, stop and correct this design rather than inventing a retirement.

## Non-goals

No panic trace, SMP coordination, general recovery, dynamic log subscriber,
new user ABI, normal TTY algorithm change, guest-clock substitution, timeout
increase, arbitrary debugger interface or general predicate solver.
Hosted CI and the inherited validation-cost findings remain separate.

## Acceptance and phase barrier

This Accepted design contains documentation only. No code, executable test,
model, harness, build scaffold or reusable prototype is authored in this
phase. Implementation remains pending until independent design review,
Acceptance and separate merge.

Implementation must demonstrate the exact native transitions, last-read
boundaries, stuck transmit/drain behavior, real second panic entry,
corrupt-metadata rescue and combined failure precedence. It must conserve
the original report, handoff and history assertions, validate the expanded
finite source/machine proof, and complete current affected-unit and full
fresh author evidence before independent implementation review.

## Alternatives

- Bound a wall-clock delay: rejected; early/fatal operation must not depend
  on clock setup, interrupts or scheduling.
- Write anyway after exhaustion: rejected for `micros`; it cannot establish
  successful output or the required ownership drain.
- Use console owner `PANIC` as the entry latch: rejected; it suppresses
  legitimate first reports after prior seizure.
- Print another panic on device timeout: rejected; it repeats the failed
  dependency and adds avoidable recursive work.
- Reinitialize the whole system after corruption: rejected; only the fixed
  terminal console fields are restored, with no return to normal execution.
- Remove panic edges or increase budgets: rejected; actual latch and UART
  predicates must justify the exact exclusions.
- Introduce another authority journal without a new retirement: rejected;
  existing committed claims and ordinary current-candidate evidence suffice.

  ## Design review correction

  Initial independent design review found one substantive omission: the
  unchanged FDT-error path requests failure shutdown directly, and a later
  UART failure after a returned request could otherwise issue another
  request while the proposed control object remained `IDLE`.

  The corrected design puts admission at the one shared SRST instruction
  boundary for the exact failure-shutdown tuple, without changing that
  protected boot source or weakening the guarantee. Reporter and
  no-console callers no longer preclaim. A new fixed target case executes
  the real FDT-error caller and observes ownership before its actual
  `ecall`; native and source/machine evidence cover rejection of a repeated
  request after return. Other reset tuples and TIME retain their original
  behavior.

  Independent read-only GPT-6.1 Sol re-review found the reset-ownership issue
  closed and reported no significant issue or substantive design issue
  remaining. The reviewer established the finding statically and reran no
  validation gate. Acceptance records that design result, not an implemented
  poll bound, terminal handler or target controller. Implementation remains
  pending until the separate design merge.

  ## References

- [Terminal-failure contract](../architecture/terminal-failure.md)
- [Fixed MINIX baseline](../research/minix-terminal-failure-baseline.md)
- [B01 findings](../research/minix-complete-baseline-b01-platform.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)

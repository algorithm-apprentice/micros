# Bounded UART and Terminal Panic

## Status and scope

This is the Accepted PLAT02 contract under
[ADR-0095](../adr/0095-bounded-uart-and-terminal-panic.md). Its only finding
owners are B01-F001, B01-F003 and B01-F005, with the corresponding
B01-E002/E003/E005 evidence gaps. Implementation remains pending until
independent design review, Acceptance and separate merge.
Complete independent design review and correction re-review are clean.
The remaining implementation barrier is this design's separate merge.

The fixed source base is PLAT01 implementation #199:

```text
micros commit: 8375ca2f5285bc42063af8ed92ba6f3811d517e8
micros tree:   cb472a9262b9d6f22c42c4c9e1182bf1da069d78
MINIX commit:  4db99f4012570a577414fe2a43697b2f239b699e
```

The [reference study](../research/minix-terminal-failure-baseline.md)
records the actual source and complexity differences. PLAT01 history
algorithms, reader/notification authority and TTY arbitration remain
unchanged. PLAT03 owns tracing. This outcome does not repair other runtime
validators, change normal TTY service or activate hosted CI.

All interfaces, files and commands described as additions below are
prospective implementation obligations, not executable design artifacts.

## Bounded physical output

Use the fixed reference limit of **100,000** LSR observations for each
physical-character readiness wait and each explicit drain.

| Operation | Success predicate | Success effect | Exhaustion |
| --- | --- | --- | --- |
| Physical character | `LSR.THRE` is set | One THR character store | No THR store; terminate without console |
| Drain | `LSR.TEMT` is set | Return to the existing caller | No successful drain/handoff return; terminate without console |

Retain one local unsigned counter and the existing status-read/early-exit
loop family. The counter counts failed readiness observations. A ready
first observation succeeds; a ready 100,000th observation also succeeds.
The 100,000th unsuccessful observation transfers to terminal failure before
another read. No counter wraps, resets inside one wait or survives into a
later wait.

Each observation performs exactly one volatile LSR load. Do not add a
pre-loop read, a final extra read or a predicate that accepts THRE instead
of TEMT for drain. No timer, retry sleep, calibrated delay, alternate UART,
caller-selected budget or return-success fallback is introduced.

A logical LF retains the two separately bounded physical writes for CR and
LF. The extra CR still does not enter diagnostic history. Every logical
byte is retained only after its successful early physical write, as in
PLAT01. Failure before that write does not append the failed byte or
complete its logical record.

Buffered/quiesced/TTY-owned diagnostic production still performs no UART
MMIO. Physical output failures never invoke a second ordinary diagnostic
producer. The panic direct path still bypasses history insertion.

### Handoff boundary

`uart_console_begin_quiesce_prevalidated` retains its existing order:
physical drain, UART interrupt disable/fence, console quiescence and
history handoff-cursor capture. A stuck drain reaches the no-console
terminator before those publication effects. No normal caller resumes,
and no failed drain is represented as completed ownership transfer.

### Work account

For `p` physical characters and `d` drains, the readiness work is at most
`(p + d) * 100000` LSR observations. A successful wait stops earlier.
Failed execution stops at its first exhausted wait. Counter updates,
comparisons and terminal-state selection add constant work per
observation/transition; there is no claimed amortization.

The actual two wait predicates use the existing EV02 comparison
annotation, owner `KERNEL_DIAGNOSTIC`, kind `COMPARISON`. Its disabled mode
preserves the original single volatile read. The new target fixture alone
enables the collector. These measurements count readiness predicates, not
all comparisons in panic formatting or history validation.
The exact site IDs are `terminal-thre-poll` and `terminal-temt-poll`.
They wrap the actual wait comparison, including the single observed LSR
load; they are not loop-exit or caller-side estimates.

## Terminal ownership

The new portable `micros_panic_control` object contains one `uint32_t`
phase. The exact values are:

| Phase | Value | Meaning |
| --- | --- | --- |
| `IDLE` | 0 | No report or terminal reset has been claimed |
| `REPORTING` | 1 | The first reporter owns the fatal path |
| `RESET_REQUESTED` | 2 | The one terminal reset request has been claimed |

The production runtime owns exactly one four-byte BSS instance,
`panic_control`. It is not the UART console state, a diagnostic-history
field, a process/thread object or a per-message token. There is no
production initializer, clear, reuse or restart operation.

The existing one-hart policy and SIE-clear entry protect these transitions.
No lock, atomic owner election or future SMP interface is added.

### Portable transitions

`micros_panic_control_enter` returns `FIRST`, `REENTERED` or `INVARIANT`:

- `IDLE -> REPORTING` returns `FIRST`;
- `REPORTING` and `RESET_REQUESTED` return `REENTERED`, unchanged;
- a null object or other phase returns `INVARIANT`, unchanged.

`micros_panic_control_request_reset` returns `ISSUE`, `ALREADY_ISSUED` or
`INVARIANT`:

- `IDLE|REPORTING -> RESET_REQUESTED` returns `ISSUE`;
- `RESET_REQUESTED` returns `ALREADY_ISSUED`, unchanged;
- a null object or other phase returns `INVARIANT`, unchanged.

Each operation's three result values are respectively zero, one and two.
The two result types remain distinct; neither is a public syscall result.

Invalid control state never becomes another first report. The runtime
halts without further I/O or reset when this private ownership state
cannot establish whether a request was already made. This is not a claim
to recover arbitrary memory corruption.

### Runtime operations

The private runtime facade exposes:

- `micros_panic_claim_first`: disable SIE/SEIE, perform the entry transition,
  return true only for `FIRST`, false for `REENTERED`, halt for invalid
  state;
- `micros_panic_claim_reset`: disable SIE/SEIE, perform the reset transition,
  return true only for `ISSUE`, false for `ALREADY_ISSUED`, halt for invalid
  state; the shared SRST boundary is its sole production caller;
- `micros_panic_terminate_no_console`: disable SIE/SEIE, request failure
  shutdown through the shared SRST boundary, then halt if reset returns;
  that boundary itself halts before issuing an already-claimed request; and
- `micros_panic_halt`: a nonreturning WFI loop with no further effect.

The no-console path never reads or repairs UART ownership metadata, polls
or writes UART, inserts history, publishes IPC, invokes a validator over
unrelated state or calls the reporter. It retains only scalar state and
the existing SRST wrapper.

All its issued resets use EID `0x53525354`, FID zero, shutdown type zero and
system-failure reason one. A returning call is not a successful reset.
No second attempt, new reset backend or firmware argument is introduced.

### One shared failure-reset admission

`sbi_system_reset` in `arch/riscv64/sbi.c` owns the single admission site
for the exact tuple `(SHUTDOWN, SYSTEM_FAILURE)`, numerically `(0, 1)`.
Before the original register binding and `ecall`, it calls
`micros_panic_claim_reset`; a false result halts instead of reaching that
instruction. The claim publishes `RESET_REQUESTED` before firmware can
return. The wrapper delegates state to the diagnostic owner rather than
creating another latch.

Neither the reporter nor the no-console terminator preclaims reset.
Otherwise the wrapper would reject the first legitimate request.
All other SRST argument tuples, including ordinary shutdown/no-reason,
retain their original mechanism behavior. `sbi_set_timer` is unchanged.

This boundary covers the existing direct FDT-error reset at
`kernel/boot/boot_foundation.c:103`. If it returns, that caller keeps its
original failure line, drain and WFI fallback. Any exhausted UART wait
there enters the no-console path, whose repeated `(0, 1)` request is
rejected before another `ecall`.
The protected boot source and its active transport assertions remain
unchanged; there is no per-caller exemption to the one-failure-request
guarantee.

## Reporter and capture

Keep the two assembly entry points, their exact 48-byte capture and the
current six-argument reporter ABI. The public reporter claims first entry
before console seizure, formatting or dereferencing reason/file/context
arguments. Reentry immediately selects the no-console path.

The existing report body moves into one private nonreturning
`panic_report_claimed` helper with the same argument values. This
separates the entry decision from formatting; it does not duplicate or
generalize the reporter.

Healthy first entry preserves:

1. console/PLIC/TTY seizure;
2. the exact five contiguous core records;
3. the optional separately captured trap record;
4. PLAT01's bounded encoded history;
5. the pre-reset drain;
6. one claimed SRST failure request;
7. the exact `MICROS_TEST_FAILURE sbi-system-reset-returned` line if that
   request returns;
8. a final bounded drain; and
9. terminal WFI.

The existing SRST call goes through the shared admission; do not insert a
second reset claim in the reporter. If the final diagnostic or drain
itself exhausts, the no-console path reaches the already-claimed shared
boundary and halts rather than issuing another request.

`panic_seizure_failure_emitted` retains only its old duplicate-warning
role. Console owner `PANIC` never substitutes for the entry phase.
A first report after `micros_tty_owner_fault_record` or an independent
seizure remains a first report.

## Corrupt-console fallback

`uart_panic_seize` changes from void to Boolean return. True means its
ordinary console transition rejected initialization magic and the target
adapter repaired the fixed terminal object; false means no such repair
was necessary. Normal return always establishes the complete current UART
panic guarantee.

Keep the portable `micros_uart_console_panic` operation unchanged. On its
magic failure, and only inside the irreversible target seizure with
SIE/SEIE clear, assign the existing console object's three semantic fields
to the existing initialization magic, owner `PANIC` and `quiesced=true`.
No other object or padding is cleared, no pointer is selected from corrupt
metadata, and no normal state is restored.
The pinned console object is sixteen bytes: 64-bit magic
`0x554152544f574e52` at offset zero, 32-bit owner three at offset eight,
and Boolean quiescence one at offset twelve. Its remaining padding is not
repair authority.

The fixed UART setup then executes on both paths with the existing
address, 8-N-1/divisor, interrupt-disable and fence sequence. A valid-magic
object with an unusual old owner retains the existing portable panic
transition; it is not misclassified as a magic repair.

The kernel seizure owner consumes the Boolean and, after raw setup but
before its old optional seizure warning, emits exactly:

```text
MICROS_CONSOLE_METADATA_REPAIRED
```

The LF is part of the record. This prefix does not impersonate a core
`MICROS_PANIC_*` record. The original PLIC/TTY seizure warning, fields and
one-time suppression remain unchanged.

If physical output is also unavailable, the bounded UART failure path
takes precedence. The repair notice and complete report are best-effort
diagnostics in that failure case, not preconditions for reset/halt.

## Failure precedence

| Situation | Required action |
| --- | --- |
| Invalid private panic-control phase | Halt; do not invent first-entry/reset ownership |
| Reentry while reporting | No formatting/seizure; claim one no-console failure reset |
| Reentry after reset was claimed | No I/O and no second reset; halt |
| Early or handoff wait exhausted | No failed write/publication; no-console failure reset |
| Panic output/drain exhausted before reset | Stop report; one no-console failure reset |
| Console magic corrupt with usable UART | Terminal field repair, repair notice, normal first report |
| Console magic corrupt and UART stuck | Repair does not restore normal execution; bounded no-console termination |
| Normal SRST request returns | Original failure line and final bounded drain, then halt |
| Output fails after that return | No second reset or recursive report; halt |
| Direct FDT-error SRST returns, then its output fails | Shared admission already owns the failure request; halt before another `ecall` |

## Source and build ownership

Only four new production files are required:

| Path | Responsibility |
| --- | --- |
| `kernel/diagnostic/panic_control_core.h` | Exact phase/result types and portable transitions |
| `kernel/diagnostic/panic_control_core.c` | First-entry and one-reset state machine |
| `kernel/diagnostic/panic_control.h` | Private target entry/reset/no-console facade |
| `kernel/diagnostic/panic_control.c` | Sole runtime object, interrupt boundary, SRST and WFI |

The existing UART owner retains both actual waits, their local counters,
logical/direct selection and emergency metadata repair. The existing
panic owner retains seizure diagnostics, the reporter ABI and formatting.
The existing SBI owner adds only the shared failure-shutdown admission
before its unchanged register/firmware boundary. The FDT boot caller is
not a production candidate and keeps its complete original body.
No generic polling abstraction or callback-driven hardware interface is
needed for two fixed MMIO predicates.

The prospective immutable test/tool additions are:

```text
tests/host/panic_control_test.c
tests/host/terminal_failure_source.py
tests/host/terminal_failure_evidence.py
tests/host/test_terminal_failure.py
tests/qemu/terminal_failure_kernel.c
tests/qemu/terminal_failure_kernel.h
tools/check_terminal_failures.py
```

The native `panic-control` executable links the actual portable core and
its one case owner, follows the existing C17/warning/sanitizer policy and
joins both native tiers. There is one new CTest, not a second model runner.

The one new `riscv64-terminal-failure-test` configuration inherits the
existing debug kernel recipe and enables only
`MICROS_BUILD_TERMINAL_FAILURE_TEST` plus the test-only work collector.
It adds the immutable kernel fixture and existing
`tests/work/work_counts.c`. Its `test-qemu-terminal-failure` workflow runs
the closed controller after building the actual kernel ELF.

Append its dispatcher call after the existing boot-component cases.
Preserve all original cases and their source locations, including line
107; no `#line` or changed expected location substitutes for preserving
the unchanged sites. Headers keep the existing private include bindings.

The full plan gains only this one workflow: 49 required commands. Static
stack scope gains one kernel-only configuration/output: 43 configurations
and 112 ELFs. The ten current observed scopes and blocked B13 admission
remain unchanged. Do not represent the new fault observations as a
substitute EV03 high-water certificate.

## Independent target fixture

The fixture exists only in the new kernel-only test configuration. It has
one fixed scenario selector, a small fixed observation object and one
static EV02 snapshot. The existing collector remains a separate static
object. Neither page-sized work object is automatic.

The selector is the test-owned volatile 32-bit object
`micros_terminal_test_scenario`; zero is invalid and the twelve IDs below
are its only admitted values. The controller writes it only while stopped
at `kernel_main`, after BSS clear and before fixture execution.

The eight-byte-aligned observation object
`micros_terminal_test_observation` is exactly 104 bytes. The fixed
little-endian layout is:

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 4 | Version, exactly one |
| 4 | 4 | Scenario ID |
| 8 | 4 | Observation phase |
| 12 | 4 | Reserved, zero |
| 16 | 8 | THRE observations |
| 24 | 8 | TEMT observations |
| 32 | 8 | Physical THR stores |
| 40 | 8 | UART seizure entries |
| 48 | 8 | First-report-body entries |
| 56 | 8 | SRST wrapper entries |
| 64 | 8 | First entry's captured pre-entry SP |
| 72 | 8 | Observed `sstatus` |
| 80 | 8 | Observed `sie` |
| 88 | 4 | EV02 finish result, initially `UINT32_MAX` |
| 92 | 4 | Requested reset type |
| 96 | 4 | Requested reset reason |
| 100 | 4 | Reserved, zero |

Observation phases are `SETUP=0`, `ARMED=1`, `RESET_REACHED=2`,
`POLL_OVERRUN=3`, `UNEXPECTED_RETURN=4` and `FIXTURE_ERROR=5`.
Version/reserved fields, case, phase and each field's availability must
match the actual stop. Overflow or an invalid capture never becomes a
usable saturated/wrapped observation.

The static `micros_terminal_test_work_snapshot` uses the existing complete
EV02 version-one layout. Producer label is
`0x504c415430320000 | scenario_id`, capture label is one and the owner
mask is exactly two (`KERNEL_DIAGNOSTIC`). Each fresh VM has one capture;
there is no overlapping or reset-and-reused measurement.

Before arming a case, finish ordinary boot preparation and drain the real
UART while fault masking/capture are inactive. Start one exact capture for
owner `KERNEL_DIAGNOSTIC`, then stop at the named armed checkpoint. The
controller snapshots the actual history and console objects at that
checkpoint; it does not infer them from a final pass marker.

The two wait comparisons retain one actual volatile LSR load. A test-only
status observer sees that loaded value, counts the indicated wait purpose
and may clear only its selected readiness bit. It never sets a readiness
bit that the real device did not supply. A last-ready control clears the
bit for its first 99,999 observations and leaves the 100,000th real value
unchanged.

Record a physical-character observation only after the actual THR store.
Do not count DLL programming during seizure as a character write.
Keep PLAT01's separate TTY-test THR observer intact.

The fixture counts seizure entries and first-report-body entries.
One hook records the first body before seizure and observes the real
captured `context->sp`; a separate post-seizure hook injects a second real
`MICROS_PANIC` call for the recursion case.
Only the first body writes the saved pre-entry SP; a later baseline body
increments its count without replacing that first witness.
Only that fixture-owned first injection flag prevents the fixture itself
from creating an unbounded sequence. It does not set the production
entry state or substitute a fake second-entry result.

After the shared SRST admission succeeds, or after an ordinary tuple
bypasses it, a test-only observation hook records the requested scalars
and closes the EV02 capture before the original register binding and
`ecall`. A rejected repeated request never reaches that hook.
It does not change arguments, replace firmware, fabricate a return or
issue reset itself. The baseline wrapper has no admission but receives the
same observation hook at its corresponding pre-register-binding boundary.

The observation code/layout and independent decoder are frozen before
baseline Red. Control counts are observation/input data, not another
summable work producer. The EV02 snapshot is the sole typed work producer.
Its comparison count must equal the sum of the two observed wait-purpose
counts.
Only readiness-predicate attempts belong to that assertion; counter-limit
checks and unrelated scalar validation are not represented as LSR reads.
Positive single-byte transmit controls preserve the independently expected
PLAT01 history movement. The FDT-error case appends its exact original
failure record. Other cases do not mutate that history.
Use only interfaces present in the pinned baseline from the immutable
guest fixture. It must not depend on the new production control header or
manufacture a substitute control implementation for the old image.

The observation hooks update the raw `sstatus`/`sie` fields at the armed,
overrun and reset checkpoints. Current reset observations require SIE bit
one and SEIE bit nine to be clear. A complete capture requires EV02 status
zero, exact version/labels/mask, zero reserved fields and all declared
dimensions; a missing or invalid snapshot is not replaced by zero counts.

### Fixed case table

Each case starts a fresh VM and capture. The actual firmware tuple is
shutdown/no-reason for returning positive controls and
shutdown/system-failure for fatal cases.

| ID | Case | Required current observation | Fixed baseline observation |
| --- | --- | --- | --- |
| 1 | Transmit ready first | One read/store; one retained logical byte | Same positive control |
| 2 | Transmit ready last | Exactly 100,000 reads, one store, successful return | Same positive control |
| 3 | Handoff drain ready last | Exactly 100,000 TEMT reads, then unchanged quiesce/cursor publication | Same positive control |
| 4 | First report after prior seizure | One full first report despite console already `PANIC` | Same positive control |
| 5 | Early THRE stuck | 100,000 reads, no character/history write, one failure reset | Actual 100,001st read |
| 6 | Handoff TEMT stuck | 100,000 reads; no quiesce/cursor/IER publication; one failure reset | Actual 100,001st read |
| 7 | Panic THRE stuck | One first body/seizure, 100,000 reads, no failed character store, one reset | Actual 100,001st read |
| 8 | Panic TEMT stuck | Retained report/history, 100,000 drain reads, one reset | Actual 100,001st read |
| 9 | Recursive panic | Two actual entries, only one first body/seizure, immediate no-console reset | Second full body/seizure |
| 10 | Corrupt console magic | Exact repaired fields, one repair notice and complete first report/reset | Exact pre-UART corruption spin |
| 11 | Corrupt magic and THRE stuck | Repair cannot evade 100,000-read bound; no report retry; one reset | Exact pre-UART corruption spin |
| 12 | Direct FDT-error reset | Original FDT failure record, no panic report/seizure, ownership published before the actual failure `ecall` | Actual failure `ecall` without the control ownership object |

The corrupt fixture changes only the fixed console's magic through a
test-only same-owner hook: flip bit zero of the valid 64-bit magic. It does
not offer a generic kernel-memory write API. It arms THRE failure before
the repair notice for the combined case.
The standalone panic THRE/TEMT cases arm their mask after seizure.
The single-byte transmit control uses the fixed byte `x`, not LF.
Case four records two seizures but only one first report body; it must not
be confused with case nine's forbidden second report. Cases one through
three use reset reason zero; cases four through twelve use reason one.

Case twelve uses one static test-owned `boot_context` and an immutable
forty-byte zero header. After arming, it calls the actual unchanged
`boot_discover_memory` with that invalid header. The required record is
exactly `MICROS_TEST_FAILURE fdt-magic` followed by LF.
It observes `RESET_REQUESTED` at the actual SRST instruction in the
current image, and the control object's typed absence in the baseline.
No firmware return is injected. The native reset-state model and actual
shared-boundary control-flow proof separately require a repeated request
to halt before `ecall`, including after this caller's returning fallback.
Cases three and six set the real UART IER to `0x03` before arming, with
SIE/SEIE clear and DLAB clear. At reset, case three requires IER zero and
case six requires it still be `0x03`. Thus a failed handoff cannot hide an
early IER write behind an already-zero value.

On a 100,001st observation, the immutable observer closes the capture,
records overrun and reaches a fixed debugger stop. That gives the old
unbounded algorithm a deterministic Red witness without accepting a host
timeout. A fault operation that unexpectedly returns reaches a different
stop and is rejected, not relabeled as the expected old divergence.
This threshold belongs only to the selected forced wait, not to the total
healthy THRE observations across a complete panic report. Cumulative
purpose counts remain complete observations; healthy character waits must
not falsely trip a per-wait overrun detector.

For the old corrupt-magic case, the baseline controller binds the unique
self-loop inside the pinned `uart_panic_seize` error branch. Reaching that
actual instruction proves the audited spin. It is not successful
termination or a completed work capture.
Only those two baseline cases admit an explicitly absent completed capture:
phase remains `ARMED` and finish status remains `UINT32_MAX`. They do not
contribute a fabricated work snapshot.

The returning armed checkpoint is named `micros_terminal_test_armed`.
Overrun, unexpected return and fixture error have distinct named
nonreturning WFI stops. The controller places breakpoints at those stops;
the fixture does not inject a hardware exception or change production
control flow merely to signal a failed observation.

### Checks independent of a serial marker

The target controller uses a private Unix-domain GDB-remote connection to
its own QEMU process. It admits only the fixed image, one hart, actual
ELF symbols/instructions and the finite operations required below:

1. stop at `kernel_main` after BSS clear and write only the test-owned
   scenario selector;
2. stop at the armed checkpoint and read the declared observation,
   console/history and capture inputs;
3. stop at the actual SRST `ecall`, the fixed overrun/unexpected-return
   point, or the pinned baseline corruption loop;
4. read the exact integer registers and declared bounded data objects;
5. remove the reset breakpoint and let the actual firmware terminate
   successful current cases.

At reset, require `a0=0`, the case's exact reason in `a1`, `a6=0` and
`a7=0x53525354`. An entry-function breakpoint is not a substitute for
the actual instruction/register check. Validate the real SP against its
owned stack; recursive evidence retains the first invocation and the
second 48-byte capture.
In the current image, also bind the real four-byte `panic_control` object.
It is `IDLE` at the armed checkpoint, remains `IDLE` for cases one through
three, and is `RESET_REQUESTED` at the fatal reset boundary. Its verified
absence in the fixed baseline is a typed absence, not a synthetic zero
state or a missing current-image allowance.
For the handoff cases, bind the existing eight-byte
`diagnostic_routing` object: cursor at offset zero, handed-off byte at four,
pending byte at five. Case three publishes the armed history's next cursor
and handed-off one; case six preserves the unhanded-off state and cursor.
Neither case may invent a registered reader or a pending notification.

Validate all fixed object names, sizes, ordinary writable load ownership,
addresses and source/image identities before accessing them. The program
accepts no arbitrary debug command, address, scenario list, expected
result, source root or external transcript. Packet checksum/length/type
errors, unexpected stops, early exit, lost connection and unsupported
objects fail closed.
The only debugger MMIO read is the fixed one-byte IER address
`0x10000001` for cases three and six at their declared checkpoints. No
debugger LSR read is allowed to bypass or disturb the measured wait
observations.

Each VM has one unchanged eight-second controller deadline. It includes
startup and protocol work and is not reset at checkpoints. No correctness
sleep, guessed notification count or workflow retry is used. Baseline
overrun/spin processes are terminated only after their exact witnesses are
validated; current successful cases must actually exit after SRST.
Every failure/exit cleans up only the controller's owned process/socket.
Read memory in bounded chunks of at most 4,096 bytes. The only writable
debugger operation is the four-byte test selector; no register, instruction,
production-state or firmware modification is admitted.

The fixture executes on the linker-owned
`[__boot_stack_bottom, __boot_stack_top)` stack. Each stopped SP must be
aligned and within that 16 KiB extent. In the recursive case, the reset
SP must leave room below the observed first pre-entry SP for both
48-byte captures. This is a bounded target witness, not a replacement
for the complete compiler/assembly stack proof or an assertion that the
stopped SP is the unique high-water mark.

The regular workflow requires all twelve cases to pass. Baseline mode
requires the four positive controls and eight exact expected divergences.
Timeout, compiler/link failure or incomplete observation is an execution
error, never a qualifying Red.

## Finite source and machine proof

Retain the actual qualified function key and compiler/assembly frame.
The four fixed analysis qualifiers encode these two separate facts:

| Qualifier | UART ready | Panic ownership latched |
| --- | --- | --- |
| `UNQUALIFIED` | Unknown | Unknown |
| `UART_PANIC_READY` | Proven | Unknown |
| `PANIC_LATCHED` | Unknown | Proven |
| `PANIC_LATCHED_UART_READY` | Proven | Proven |

UART readiness retains its existing metadata/SIE/SEIE meaning. Latched
ownership means the exact bound four-byte `panic_control` object is in
`REPORTING` or `RESET_REQUESTED`. It does not mean that formatting
completed or reset succeeded.

Successful first-entry claim establishes only the latch fact at the exact
call to `panic_report_claimed`. Successful seizure establishes only UART
readiness at its declared continuation. Retain a caller's already proved
other fact only through a separately complete preservation proof.

The source/effect register must bind:

- the control object's actual symbol, size, phase offset and disjointness
  from console, history, stacks and test objects;
- the exact native transition bodies and actual load/compare/store/return
  effects, including rejection of unknown phases and no phase clearing;
- the actual claim-result branch dominating the first-body call;
- the shared assembly capture's stack-only writes and interrupt effects;
- both normal and corrupt-magic paths through raw UART setup and the
  repaired-status return;
- every pre-seizure PLIC/TTY helper that preserves the latch without
  establishing UART readiness;
- the complete formatting/history, local poll-counter, terminal,
  reset-claim and returning-SRST chains;
- the exact shared `(0, 1)` tuple guard, sole reset-claim invocation,
  successful-claim dominance over the actual SRST instruction, and
  rejection of caller-side preclaims or an unguarded failure request;
- every test observation and EV02 collector/snapshot effect in the new
  fixture, including actual bounded atomic writes to disjoint test-owned
  objects; and
- the existing compiler observer effects in observed configurations.

Unknown effects do not lose a fact silently; they reject qualification.
All actual calls remain in the graph even when their failure continuation
does not return. A source inverse may preserve old report code or a
normal-return effect anchor, but cannot remove the new terminal edge or
serve as the independent poll-bound oracle.

There are exactly four guarded call sites:

| Caller/site | Target excluded only when the required fact is proved |
| --- | --- |
| Existing `uart_write_direct` ownership failure | `micros_panic_entry`, under UART readiness |
| Existing `uart_write` nonpanic branch | `uart_write_record`, under UART readiness |
| Existing `uart_write_hex64` nonpanic branch | `uart_write_hex64_record`, under UART readiness |
| New reporter first-body call | `panic_report_claimed`, under latched ownership |

The last exclusion requires actual source, object and linked proof that a
latched claim cannot return `FIRST` or reach the first-body call. It does
not exclude the reporter entry, its frame, the claim operation or
no-console termination.

The certificate advances its fatal-proof version to three and records all
four sites with required facts. Each real invocation retains its frame.
A fact transition is never a stack reset. All remaining cycles, unknown
frames/calls, bypassed dominance, alias escape, phase clearing and stack
excess still fail.

Preserve the exact ordinary SRST EID/FID/type/reason and independently
pinned default firmware contract. No injected return, different reset
argument, SSE event or new firmware is admitted by this design.

## Source authority and original evidence

The active inventory has 3,920 facts in twelve families. Preserve every
complete old record and fingerprint, both committed COPY01/PLAT01 claims,
the twenty retired PLAT01 keys and all still-active assertions.

No new retirement claim or author authority journal is introduced.
The old UART/panic whole-source checks already belong to PLAT01's prior
claim; the current inventory has no transport key for `platform.h`.
This does not waive its other declarations or the finite effect proof.
The header change is only the named seizure return type.
The active inventory also has no transport key for `arch/riscv64/sbi.c`;
its existing register-binding and firmware-effect invariants remain
required. The complete protected `boot_foundation.c` source stays intact.

All other source maintenance uses exact independently declared extensions:
four new kernel members, one native target, one kernel configuration,
one workflow, fixture hooks, annotation sites, reporter extraction,
four-context/four-site proof and directly dependent structural assertions.
Keep old values and recover complete original bytes/ASTs where those
transport contracts still apply.

Do not change the callback source anchor or provider tables. Do not
regenerate the old recipe, runner or case expectations. The new recipe
inverse runs before existing PLAT01/EV03/EV02 inverses, and the old
timeout-classifier fixtures retain their independently pinned original
recipe. Newly extended recipes continue to select full coverage.

The two new source/evidence owners register one closed
`terminal-failure-baseline` family with nine facts:

| Owner | Fact | Class |
| --- | --- | --- |
| Evidence owner | Merged-design/fixed-source anchor | Transport identity |
| Evidence owner | Transmit bound oracle | Baseline oracle |
| Evidence owner | Drain/handoff bound oracle | Baseline oracle |
| Evidence owner | First entry and failure-reset ownership oracle | Baseline oracle |
| Evidence owner | Corrupt-console fallback oracle | Baseline oracle |
| Source owner | Exact production/fixture membership | Permanent invariant |
| Source owner | Native/target recipe and observation layout | Artifact contract |
| Source owner | Two facts, four qualifiers and exact effect/exclusion register | Permanent invariant |
| Source owner | Fixed-reference source/annotation/recipe extensions | Transport identity |

Independently declare these records from the merged design before changing
owners or extraction. The resulting 3,929 facts have thirteen families.
They add authority for current properties, not another completed-retirement
claim.

If an implementation change cannot preserve an additional active identity,
stop and correct the design before implementation proceeds. A broad
exception or regenerated hash is not an allowed repair.

## Baseline execution and validation

Before production changes, finalize the immutable native/model/source,
target fixture, controller and recipe/effect inputs. The eight prospective
production candidates are the four new production paths plus
`arch/riscv64/uart.c`, `arch/riscv64/platform.h`,
`arch/riscv64/sbi.c`, and `kernel/diagnostic/panic.c`.
Other implementation changes are fixed test or transport inputs for the
comparison.

The prospective author baseline intent is:

```text
<MICROS_PYTHON> -E -S -B tools/check_terminal_failures.py --baseline
```

It builds the fixed source archive, not an arbitrary caller checkout.
Apply only the independently declared test integration, observation,
comparison-annotation and build overlay; inverse every altered original
file back to its exact pinned Git blob/mode before accepting the build.
Use the same frozen fixture/controller in the baseline and current image.
The new production control files are absent from the baseline, not
substituted by empty or fake implementations.

Record the actual compiler/link/ELF identities, immutable input manifest,
per-case register/object/work observations, raw transcript, commands and
results. Baseline exit one means all eight expected divergences and four
positive controls were actually observed; execution/closure errors use a
distinct failure and are not Red. Changing a frozen test/overlay/oracle
requires a new baseline observation.

Current Green uses the native `panic-control` CTest, focused source/machine
and protocol regressions, and the real
`cmake --workflow --preset test-qemu-terminal-failure` workflow.
Its immutable-input manifest must match the qualifying baseline for
first-commit author evidence. Ordinary future workflow execution does not
depend on an old local baseline log.
Manifest identities include every frozen fixture/model/controller and
source/recipe/effect adapter plus the compiler, linker, QEMU/default
firmware and Python inputs actually consumed. Canonical baseline mode
records them before and after execution; ordinary target mode emits its
current manifest for the author comparison. This comparison does not
publish a retirement, suppress an assertion or create an execution-reuse
receipt.

The final fresh plan provides affected-unit recertification without a
duplicate suppression transaction:

| Unit | Required current commands in the full plan |
| --- | --- |
| B01 | New terminal workflow and retained smoke/panic/trap-panic/UART-console workflows |
| B02 | Address-space handoff and retained memory components |
| B03 | Scheduler and both invalid-context workflows |
| B04 | IPC and retained ecall/syscall components |
| B05 | User runtime and retained grant/execution components |
| B06 | Bootstrap launcher and both existing failure workflows |
| B07 | VM handoff and retained VM fault/preemption workflows |
| B08 | PM service |
| B09 | TTY integration |
| B10 | RAMFS integration |
| B11 | VFS integration |
| B12 | ELF-spawn integration |
| B14 | Complete native discovery, extended stack workflow, documentation and all diff checks |

Finally run the prospective 49-command full fresh author plan, bind its
exact clean staged tree and every result, then obtain independent
implementation review before the first permanent commit. Confirm ordinary
committed authority still preserves the two existing retirements without
an author journal. Merge only the reviewed tree and clean exact obsolete
local/remote task branches.

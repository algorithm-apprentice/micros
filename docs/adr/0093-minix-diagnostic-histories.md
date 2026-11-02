# ADR-0093: MINIX Diagnostic Histories

- Status: Accepted
- Date: 2026-11-02
- Refines: ADR-0003, ADR-0012, ADR-0014, ADR-0037, ADR-0047, ADR-0051,
  ADR-0062, ADR-0070, ADR-0073, ADR-0080, ADR-0082, ADR-0083, ADR-0084,
  ADR-0086, ADR-0091, and ADR-0092
- Supersedes in part:
  - ADR-0047's omission of nonfatal kernel-log forwarding and its treatment
    of every post-begin logical diagnostic write as a physical ownership
    violation;
  - ADR-0086's single infeasible call-site register, only for the three
    explicitly proved UART sites in this contract.

## Context

EV04 #189, COPY01 #191, the packed-identity prerequisite #192/#193 and
relocated-payload prerequisite #194/#195 are merged. PLAT01 is the next
preferred runtime correction and owns only B01-F002.

The [fixed MINIX study](../research/minix-diagnostic-history.md) establishes
two distinct histories, record-end notification, a sampled modulo cursor,
TTY consumption and panic replay. The current kernel retains neither
history and has no ordinary diagnostic path after console handoff.

A pair of unused arrays would not close the finding. Conversely, importing
MINIX's mapped kernel pointer, general diagnostic-producer syscall,
privilege-table subscription, automatic 10,000-byte TTY buffer or
interactive panic reset would violate current isolation and lifecycle
contracts.

## Decision

After separate review, Acceptance and merge, implement the complete bounded
[diagnostic-history contract](../architecture/diagnostic-history.md).

### Preserve both reference mechanisms

Keep a 10,000-byte circular history with next index and retained size.
Separately keep a 2,000-byte rolling tail. Its maximum payload is 1,998
bytes; saturation writes the new byte at position 1,998 and then shifts
exactly 1,999 bytes forward, including the terminating zero. Do not replace
that algorithm with another ring or an optimized tail representation.

Retain every nonzero byte, including control and high-bit bytes. Physical
CR insertion is not diagnostic data. Record completion is a separate
operation and inserts no byte.

### Separate logical production from physical UART

The existing kernel text and hexadecimal entry points become logical
diagnostic producers without changing their signatures. Before console
quiescence, they retain early direct output, with physical output preceding
history insertion for each byte. After quiescence, they retain bytes and
request notification without touching UART registers.

Panic still seizes UART and uses direct output. Once the console is
`PANIC`, report output bypasses history insertion and record-end signaling.
The retained pre-seizure tail is therefore stable while the existing core
and trap records are emitted. This is a declared diagnostic-format and
terminal-path adaptation, not a recursive-panic guard.

### One exact TTY reader

Add only one-shot reader registration and bounded readback to operation 14.
Keep its original commit/completion commands, version and error behavior.
No other process gains a control bit or diagnostic-producer operation.

Registration binds one page-aligned, 10,016-byte static TTY snapshot to the
exact existing process, thread, endpoint, profile and private-root tuple.
It validates and retains three managed writable page bindings after UART
commit. Those static wired mappings cannot be replaced during the admitted
lifetime. There is no general buffer registration, remapping or recovery
API.

Readback samples the ring index once, reconstructs only the requested
modulo span and copies a sixteen-byte header plus those bytes. It does not
copy or clear the full 10,000-byte capacity for a short request. Retained
physical bindings avoid a new complete mapping walk on each read.

Preserve the reference's complete-lap ambiguity explicitly. Initial TTY
cursor placement skips the bytes already physically drained at handoff.
There is no invented lossless sequence counter.

### Deferred notification and ordinary TTY output

Record end sets a coalesced pending flag. Actual kernel notification occurs
only at the common user-return or idle-selection boundary, after outgoing
custody is safe and before selection plans exist. It never mutates IPC
queues inside an arbitrary diagnostic caller.

TTY accepts only the union of its existing IRQ bit and the new diagnostic
bit in a canonical kernel-origin envelope. IRQ work retains its exact
claim/completion behavior and precedes diagnostic copying. Diagnostic-only
delivery does not complete an IRQ.

A static TTY snapshot and bounded spool feed the existing resident output
lane. They do not consume VFS grants or create, modify or collect a VFS
completion. An already resident write drains first; an uncollected VFS
completion alone does not prevent diagnostics from using a subsequently
empty output lane. Existing busy/writable retry behavior remains valid.

### Panic consumption and finite proof

Append an exact length record and bounded hexadecimal data records after
the unchanged five core records and optional trap record. Never replay raw
marker text, write history while replaying it, allocate, wait for TTY, or
wait for interactive input. Preserve SBI failure shutdown and its complete
returning-call fallback.

Retain exactly two analysis qualifiers. The ready qualifier proves the
direct-writer ownership failure infeasible and proves each logical text/hex
writer's nonpanic recording branch unreachable. All three sites need complete
source, object and linked control-flow evidence. Every other edge, real
frame, unknown effect, remaining cycle and original budget stays required.

## Evidence and scope

The implementation must supply independent native history/work and
authority/consumer evidence, real retained target scenarios, a granular
EV01 transition and complete affected-unit recertification before the
unchanged full author gate. The canonical contract fixes twenty existing
transport keys, four actual replacement oracles, eight new authority facts,
whole-file input roles and the exact three-intent/unit command tables.
Historical literals and all unrelated authority remain active.

Existing mapping, object, IPC and TTY validation costs remain visible with
their original correction owners. This task neither silently optimizes
them nor declares their audited findings closed.

PLAT02 retains bounded polling, recursive panic and corrupt-console
fallback. PLAT03 retains stack traces. No new syscall namespace, service,
driver, grant forwarding, dynamic subscriber list, unregister/restart path,
general interpreter, optional optimization or hosted-CI waiver is added.

## Phase barrier

This Accepted design contains documentation only. No C, assembly, test,
model, harness, fixture, build scaffold, dependency or reusable executable
prototype is authored in this phase. The paired implementation remains
pending until this independently reviewed design is separately merged.

## Design review disposition

Independent read-only review inspected the complete fourteen-file design
and its exact current author evidence. The staged tree remained unchanged
throughout review. The reviewer reported no significant issues, required
no targeted reproduction and reran no documentation, native, build, model
or QEMU gate.

Acceptance records that design result, not an implemented history, retired
identity, stack certificate or closed B01-F002 finding. The paired
implementation begins only after the separate design merge.

## Alternatives

- Add only the arrays: rejected because notification, consumption and
  terminal replay are part of B01-F002.
- Use one circular structure for both roles: rejected as a pre-baseline
  algorithm change.
- Map kernel memory into TTY: rejected because the existing isolation
  contract provides no such pointer authority.
- Copy all capacity on every read: rejected because it changes the
  reference's sampled-span work from `O(n)` to `O(capacity)`.
- Advance the cursor only after physical drain: rejected because it
  conflates snapshot ownership with terminal progress.
- Route a pending log from inside every producer: rejected because a
  producer may run inside another IPC transaction.
- Let a VFS completion block all diagnostics: rejected because completion
  metadata is independent of an already emptied resident output buffer.
- Remove panic edges or increase stack/time budgets: rejected; the changed
  program needs an actual finite proof under the existing limits.

## References

- [Diagnostic-history contract](../architecture/diagnostic-history.md)
- [Fixed MINIX baseline](../research/minix-diagnostic-history.md)
- [B01 audit](../research/minix-complete-baseline-b01-platform.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)

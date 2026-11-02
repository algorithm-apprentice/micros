# Kernel Diagnostic Histories

## Status, readiness and boundary

This is the Accepted PLAT01 contract under
[ADR-0093](../adr/0093-minix-diagnostic-histories.md). Its original
implementation barrier was the separate design merge; the current
corrective-design barrier is recorded below.
The baseline is `32f19782075ab34ff2bd6992f80540b6bfd47a4f`, following
EV04, COPY01 and both evidence-identity prerequisites. The
[fixed study](../research/minix-diagnostic-history.md) distinguishes the
earlier runtime research pin from this evidence-complete authoring base.

PLAT01 owns B01-F002: both retained history roles, record completion,
notification, consumer transfer, panic consumption and early/panic direct
fallback. PLAT02's polling, recursive-entry and corrupt-console findings
and PLAT03's stack trace remain separate. Existing IPC, VM, object and TTY
work-cost findings retain their correction owners.

No executable artifact is authored in this design phase. Independent
review reported no significant issue in the original complete design. Implementation
and its runtime, source-authority, artifact and acceptance evidence remain
a separate outcome.

Design #196 and collector-source correction #197 are separately merged.
The subsequent uncommitted implementation is blocked and abandoned after
the [observed startup IRQ retrigger](../research/minix-diagnostic-history.md#observed-startup-irq-retrigger).
The [Accepted correction](../adr/0094-retrigger-safe-diagnostic-probe.md)
below is its sole active prerequisite. Existing runtime obligations remain
unchanged; no PLAT01 implementation or retirement is accepted.

## Fixed roles and storage

The kernel owns one zero-initialized history object:

| Member | Capacity or invariant |
| --- | --- |
| Circular bytes | Exactly 10,000 bytes |
| Next index | Unsigned 32-bit value in `[0, 10000)` |
| Retained size | Unsigned 32-bit value in `[0, 10000]` |
| Rolling tail bytes | Exactly 2,000 bytes, separate from the ring |
| Tail position | Unsigned 32-bit value in `[0, 1998]` |

BSS initialization supplies the initial zero state. There is no production
reset or reinitialization operation. Before circular saturation,
`next == size`; the tail position is `min(size, 1998)`. The current tail
terminator and its final array byte remain zero. Scalar validation checks
bounds before indexed access and does not scan the whole history on every
append.

The producer retains no caller pointer. A separate scalar record-end flag
means notification is pending, not that any byte was consumed. One captured
handoff cursor identifies the end of already-drained early output.
Those routing scalars remain outside the 12,012-byte history object in one
small producer-routing record; they do not become another byte buffer.

The kernel reader binding is a separate fixed object. It retains the exact
process handle, thread handle, endpoint, profile, private root, registered
virtual address and three physical page addresses. It is not another
history or a generalized grant table.

TTY owns one page-aligned static snapshot and scalar consumer state:
previous cursor, valid spool length, consumed offset and pending-read flag.
The 10,000-byte snapshot payload is never automatic on the 4 KiB user
stack. No heap, per-message allocation, second snapshot or extra output
ring is introduced.
The real service facade owns one aligned consumer state object beside its
existing service/UART owners. The diagnostic module operates on that
explicit state; native cases use independent test-owned instances, not a
production reset or hidden singleton replacement.
The snapshot is its first field; the scalar state follows the 10,016-byte
snapshot. Page alignment rounds the complete small state object to three
pages, rather than inserting another page before the snapshot.

## Byte and record algorithms

For each nonzero logical byte:

1. If direct early output is allowed, emit the physical byte first,
   inserting a physical CR before LF exactly as today.
2. Store the logical byte in `ring[next]`.
3. Store the same byte in `tail[tail_position]`.
4. Increase retained size only below 10,000 and advance next modulo 10,000.
5. If the old tail position was 1,998, move exactly 1,999 bytes from
   `tail[1..1999]` to `tail[0..1998]` in forward order. Otherwise increase
   the position.

The overlapping shift uses the reference's differing-alignment byte
fallback, not the nonoverlapping runtime copy API. It does not gain a
general `memmove` implementation. Both trailing zero roles remain intact.
Nonzero control and high-bit bytes are retained without filtering.

Each completed `uart_write` call is one record end, including an empty
string. `uart_write_hex64` produces its complete `0x` plus sixteen
lowercase digits and one record end. This maps the current nonvariadic
writer boundary to MINIX's completed diagnostic emission; it does not
interpret LF as the only possible end.

Record end inserts no byte and changes no history cursor. Before quiescence
it requests no TTY notification because output is already direct. After
quiescence it sets the pending flag. The flag is not an event counter.

## Logical output and console ownership

Keep the signatures of `uart_write` and `uart_write_hex64`. The public
kernel text writer selects one of two concrete responsibilities:

- In `PANIC`, use the ownership-checked direct writer and return without
  entering the ordinary recording path.
- Otherwise save and clear SIE, validate the ordinary console/history state,
  retain each logical byte and emit it physically only while early direct
  output is allowed, finish the record, and restore the exact saved SIE
  state.

Private direct text and hexadecimal writers retain the original bodies,
with only their approved private names/visibility changed. Their existing
MMIO character primitive and flush polling are unchanged. Each public
writer selects its direct counterpart in `PANIC`; otherwise one recording
scope emits the complete string or hexadecimal prefix/digits and one
record end. The ordinary text/hex paths share the same logical-byte
primitive, so hexadecimal digits cannot bypass either history.

Ordinary invalid console or history metadata remains an explicit fatal
invariant, not a dropped diagnostic. Valid quiesced-early and TTY ownership
are buffered modes, not physical ownership violations. No buffered-mode
call reads or writes UART MMIO, waits for TTY or mutates IPC queues.

Successful console quiescence captures the current next index after the
old transmitter is drained and ordinary physical output is disabled. This
one-shot handoff cursor cannot be moved by a later producer or registration.
TTY starts consuming from that cursor, so already printed early text is
not printed a second time.

Once seizure establishes `PANIC`, all report output bypasses both histories
and record-end signaling. The existing pre-seizure tail remains stable.
This intentionally differs from appending formatted panic fields to MINIX's
general log: those fields already have their own mandatory machine records,
and replay must not overwrite its source or synthesize duplicate markers.
This rule is not a recursive-entry sentinel or a corrupt-console recovery
path.

## Operation-14 reader ABI

The existing control version remains one. Commands one and two keep their
exact register layouts, validation order, results and state effects.
Add only:

```text
MICROS_TTY_CONTROL_DIAGNOSTIC_REGISTER = 3
MICROS_TTY_CONTROL_DIAGNOSTIC_READ     = 4
MICROS_KERNEL_EVENT_DIAGNOSTIC        = 0x10
```

The new event does not overlap existing bits `1`, `2`, `4` or `8`.
It belongs to the new diagnostic ABI rather than widening another service's
accepted mask. No privilege profile gains a bit or target.

### Snapshot

The snapshot is exactly 10,016 bytes, with a sixteen-byte header:

| Offset | Field | Encoding |
| ---: | --- | --- |
| 0 | Version | Little-endian unsigned 32-bit, one |
| 4 | Sampled next index | Little-endian unsigned 32-bit, below 10,000 |
| 8 | Valid payload length | Little-endian unsigned 32-bit, below 10,000 |
| 12 | Reserved | Unsigned 32-bit zero |
| 16 | Linear diagnostic bytes | Capacity 10,000; only valid length is transferred |

The buffer is page-aligned in the TTY's existing wired managed memory.
Its exact range occupies three pages. Inactive payload bytes are owned by
TTY and remain untouched; neither their value nor their zeroing is part of
a successful short read. They carry no authority and are never consumed.

### Registration

Registers:

| Register | Value |
| --- | --- |
| `a0` | Command 3 |
| `a1` | Control version one |
| `a2` | TTY service ID four |
| `a3` | Exact self endpoint |
| `a4` | Page-aligned snapshot virtual address |
| `a5` | Exact size 10,016 |
| `a6` | Zero |
| `a7` | Existing TTY-control syscall number fourteen |

Registration requires completed UART ownership commit, an existing handoff
cursor and no prior reader registration. The caller must pass the existing
exact TTY process/thread/endpoint/profile/root and manifest-binding checks.
Process, thread and endpoint generations remain distinct.

Validate canonical widths and scalar shape before caller resolution.
After authority and applicable state checks, reject a wrong tuple, version,
size, alignment, overflow or user-range crossing before publication.
The complete range must end at or below `MICROS_TTY_UART_VIRTUAL_BASE`;
neither the adjacent device page nor the one-page user stack is an admitted
registration region.
Preflight all three pages through existing managed-memory translation and
require exact writable TTY-owned wired memory, never UART MMIO. Retain the
complete physical plan before the first registration mutation. Mapping or
permission denial changes no binding, notification or user byte.

The commit publishes one complete reader binding. It returns the handoff
cursor as a nonnegative `a0` value below 10,000; failures remain existing
negative syscall results. It writes no snapshot byte. If the history is
nonempty it requests an initial notification, even if the resulting unread
span is empty because early output was already drained.

Registration is one-shot. There is no replacement, unregister, generalized
buffer lifetime, endpoint reuse or recovery path. Static TTY owner loss is
already fatal; a future RS/mapping design must explicitly replace this
retained-page lifetime before allowing those changes.

The new-command error order is scalar decode, exact caller authority,
applicable invariant validation, phase/registration state, tuple/range
validation, then page preflight. Unknown commands or noncanonical widths
are `ARGUMENT`; a foreign caller is `UNAUTHORIZED`; duplicate registration
or a read before registration is `STATE`; a bad version, tuple, size,
alignment, range or cursor is `ARGUMENT`. An absent or nonwritable candidate
page is `MEMORY_FAULT`. An inconsistent bound phase, stale root, invalid ownership or
structural errors after exact binding checks are fatal invariants, not
reclassified ordinary denials. Commands one and two retain their original
separate failure order unchanged.

### Readback

Registers:

| Register | Value |
| --- | --- |
| `a0` | Command 4 |
| `a1` | Control version one |
| `a2` | Previous consumer cursor, below 10,000 |
| `a3` through `a6` | Zero |
| `a7` | Fourteen |

Readback resolves exact live authority again and compares every retained
binding identity with the existing TTY owner and caller. It requires the
registered, wired, `TTY_OWNED` lifetime. It performs no new complete
bootstrap, address-space, frame or TTY-table validation on this hot path.
Existing generic syscall-entry costs remain separately accounted.

With SIE clear, sample next once and compute:

```text
n = (sampled_next + 10000 - previous_cursor) % 10000
```

Reject a cursor whose span exceeds the currently retained size. For an
admitted span, read the physical ring suffix followed by its wrapped prefix,
using at most two logical source spans. Copy only those `n` bytes through
the retained three-page destination plan, followed by the exact header.
All possible returning failures precede the first output write; the copy
commit has no fallible mapping operation. Return zero on success.

`n == 0` writes only the header. Complete laps remain indistinguishable from
no new bytes, exactly as in the reference cursor algorithm. There is no
sequence-number extension or claim of lossless delivery.

The page plan is valid because the exact static TTY mappings are immutable
after the already completed UART exception. A changed identity, root,
binding or impossible lifecycle is not repaired or rebound. Registration
cost and the inherited syscall-entry checks are not misreported as free.

## Notification boundary

Record end only marks work pending. Registration and ordinary producer calls
never perform a scheduler/IPC transaction inline.

At common ordinary/captured user return, deliver pending diagnostic work
after outgoing custody capture and before `micros_hart_plan_user_return`.
At idle selection, do so after the existing idle preflight and before that
same planning call. Keep SIE clear. These are the only production pump
sites; no callback, periodic timer or new scheduling policy is added.

When no reader is registered or no record is pending, return without IPC
work. Otherwise validate the exact retained destination and use the existing
kernel-notification prepare/commit mechanism with only bit `0x10`. Clear the
pending producer flag only after successful commit. An impossible
authority, state or injection failure is fatal, never a silent drop.

Destination validation is distinct from syscall-caller authorization.
The pump may run while another process is current or while current is
null in idle. It checks the stored TTY process/thread/endpoint/profile/root
against the authoritative static binding and live destination objects; it
does not require the interrupted or selected caller to be TTY.

The normal source-`NONE` envelope, token zero, nonzero little-endian event
mask and zero payload tail remain unchanged. Pending IPC bits may coalesce
with UART bit `0x8`. Reply wait remains excluded, and the common return
path still owns completion copying and selected-context publication.

Supervisor-origin interrupt return preserves its interrupted continuation;
it does not gain an arbitrary user scheduling point. A pending record waits
for one of the two existing safe selection boundaries.

## TTY consumer and arbitration

After console commit and local ownership commit, register the static
snapshot and store the returned initial cursor before ordinary readiness.
An initial pending notification survives the readiness call's reply wait.

The service accepts a kernel notification only when its source, type,
token and zero tail are canonical and its mask is a nonempty subset of
`TTY_IRQ | DIAGNOSTIC`. Unknown bits, zero masks and foreign envelopes are
fatal. For a combined mask, drain and complete the real IRQ first, then
mark diagnostic work pending. Diagnostic-only delivery never invokes IRQ
completion or changes PLIC ownership.

After each completed IRQ or VFS-call dispatch, perform one bounded
diagnostic progress step:

1. If a read is pending and no unconsumed snapshot bytes remain, invoke
   readback. Validate version, zero reserved field, bounds and the exact
   cursor/length relation before using bytes.
2. Accept that snapshot into the private spool and advance previous cursor
   immediately to sampled next. Do not wait for physical drain.
3. If bytes remain and the existing terminal write buffer is not resident,
   accept at most 4,096 bytes through `micros_tty_write_accept`, apply its
   ordinary UART effects and advance the spool offset.

A step performs at most one read and one output admission. It never
overwrites unread spool data or an existing resident write. A subsequent
notification while the spool is occupied sets the pending-read flag and
does not replace that snapshot.

An uncollected VFS completion does not own an already emptied terminal
buffer. The existing service validator deliberately permits resident output
independently of completion metadata. Diagnostic admission therefore does
not wait for `COLLECT`, change completion IDs/results, advance the last
accepted VFS request ID, borrow a VFS grant or issue a completion event.

A VFS write encountering diagnostic resident output follows the unchanged
busy/writable retry protocol. Its unaccepted ID is not consumed. Echo,
LF-to-CRLF conversion and transmit interrupt policy remain in the existing
TTY core/UART owners. The diagnostic adapter neither copies those state
machines nor changes their algorithms.

Already accepted ordinary output is never displaced. Continuous producer
traffic may overrun the kernel ring or delay another output source; no new
fairness, lossless delivery or producer-backpressure guarantee is invented.

## Panic tail records

Preserve the five ordered, contiguous core records and the optional
separately captured trap record. Then emit exactly one history header:

```text
MICROS_DIAGNOSTIC_HISTORY state=retained bytes=0x<16 lowercase hex digits>
```

The declared byte count is in `[0, 1998]`. Emit successive data records in
chunks of at most thirty-two bytes:

```text
MICROS_DIAGNOSTIC_HISTORY_DATA offset=0x<16 lowercase hex digits> data=<lowercase hex>
```

Offsets start at zero and are contiguous; each payload has exactly twice
the remaining chunk length in hexadecimal characters. An empty tail has no
data record. Do not include either terminating zero in the reported payload.
Encode each byte through one three-byte local pair plus terminator, ending
the data record after at most thirty-two bytes. Do not copy the whole tail
or introduce a variable-index writable array in the qualified formatter.

If scalar history metadata cannot safely describe a tail, emit instead:

```text
MICROS_DIAGNOSTIC_HISTORY state=unavailable bytes=0x0000000000000000
```

Do not access an invalid index or recursively panic while constructing that
diagnostic. This explicit handling concerns the new history object, not
PLAT02's pre-existing corrupt-console-seizure defect.

All history records use the direct panic path. Raw historical marker text
can never become a second live boot/panic record. The report still flushes,
requests SBI system-failure shutdown and retains the exact reset-returned
failure record, second flush and terminal wait.

The history prefix deliberately does not start with `MICROS_PANIC`: the
existing core/trap validators count that entire prefix and still require
exactly five contiguous core records. Keep those validators unchanged.
One explicit diagnostic-history requirement checks the additional header,
placement, unique/gap-free chunk sequence and exact byte count; it does not
reinterpret an old panic fixture or relax a core-record check.

## Resource and complexity account

Let `c` be produced bytes, `k = 2000`, `n` the sampled unread span, and let
`Vtranslate`, `Vcontrol`, `Ventry`, `Vnotify` and `Vtty` denote the actual
inherited validation/adapter costs, not constants inferred from capacities.

| Path | New work | Retained cost or storage |
| --- | --- | --- |
| Ordinary history append | Two byte stores; 1,999 additional byte moves after saturation | `O(c)` before saturation, `O(c*k)` afterward; 12,000 history bytes plus scalars |
| Record end | `O(1)` flag mutation | No queue operation or event-count allocation |
| Registration | Exactly three page-plan admissions plus scalar checks | Existing translation/setup validation costs are explicit |
| Readback | `O(n + 3)` transfer/plan work, sixteen header bytes | No capacity-wide copy/clear; generic `Ventry` remains |
| Pending notification | One existing notification preparation/commit | `Vnotify` remains owned by the IPC correction queue |
| TTY consumption | At most one read and one bounded output admission per step | One static 10,016-byte snapshot, scalar spool state, existing `Vtty` |
| Panic replay | `O(k)` encoding and output | One three-byte encoding pair plus scalar cursors and ordinary frames |

For `c` bytes starting from empty histories, direct history-store/move work
is exactly `2*c + 1999*max(0, c-1998)` byte units. Record end contributes no
history byte work. Use the existing KERNEL_DIAGNOSTIC owner and primitive
copy-unit/copied-byte dimensions; production-disabled annotations remain
passive. Transfers through the existing runtime memory routines retain
their own RUNTIME observations rather than fabricated aggregate hits.

No physical-poll, guest deadline, host timeout, compiler optimization,
image quota or stack capacity is increased. Inherited global validators
are neither hidden in an `O(1)` label nor corrected opportunistically in
this task.

The bounded new local algorithms do not certify the complete inherited
call path. Setup still pays the existing translation/control cost, notification
pays the existing IPC cost, and TTY output pays its existing validation
cost. Those remain the already owned B02/B04/B09 divergences, not new
claims of baseline parity or reasons to weaken their later corrections.
Three setup translations and no new per-read complete translation are
explicit limits of this adaptation. Target acceptance retains the real
unextended readiness/host budgets.

The generated manifest must still describe the actual new image page
count. That derived count is not a fixed quota to keep artificially stale:
the existing aggregate mapping/service limits and every fixed checker
budget remain unchanged. The fixed baseline TTY ELF has eight load pages
and its separate stack; new BSS alignment and code/data growth are measured
by the existing image and stack gates rather than estimated as free.

### Atomic baseline classifications

| Difference from the fixed reference | Classification and boundary |
| --- | --- |
| Two histories, exact capacities and circular/rolling update family | Baseline parity |
| Nonvariadic text/hex calls define explicit record completion | Required adaptation of the existing writer interface |
| Singleton exact-generation TTY reader instead of privilege-table subscribers | Required adaptation to the accepted fixed service/authority model |
| Checked copyout instead of a mapped kernel pointer | Required isolation adaptation; local copy remains `O(n + 3)` |
| Static snapshot instead of a 10,000-byte automatic TTY buffer | Required adaptation to the unchanged 4 KiB stack |
| Reuse of ordinary resident output without overwriting VFS completion state | Required protocol/ownership adaptation |
| Skip physically drained early bytes at handoff | Required adaptation to the existing exclusive early-console phase |
| Complete-lap modulo cursor ambiguity | Baseline parity, explicitly not a lossless extension |
| Frozen pre-seizure tail, hexadecimal records and SBI failure termination | Required diagnostic-format/platform adaptation |
| Current translation, IPC and TTY global-validation work | Existing divergence requiring its separately assigned correction; not waived by PLAT01 |

## Source ownership and build admission

The implementation adds these responsibility owners:

| Path | Sole responsibility |
| --- | --- |
| `include/micros/diagnostic.h` | Pointer-free diagnostic snapshot/version/capacity/event contract |
| `kernel/diagnostic/diagnostic_history_core.h` and `.c` | Portable ring/tail state, scalar validation, append and sampled-span algorithms |
| `kernel/diagnostic/diagnostic_history.h` | Narrow kernel-only producer, reader-dispatch, notification and panic-report facade |
| `kernel/diagnostic/diagnostic_history.c` | The single history object, handoff cursor and pending producer flag |
| `kernel/diagnostic/diagnostic_control_core.h` and `.c` | Portable exact reader tuple, three-page plan and copy-commit checks |
| `kernel/diagnostic/diagnostic_control.c` | One reader binding, existing translation/TTY authority adaptation, copyout and safe-boundary notification |
| `kernel/diagnostic/panic_history.c` | Read-only bounded terminal history formatting |
| `servers/tty/tty_diagnostic.h` and `.c` | Explicit consumer-state representation, cursor/spool transitions and ordinary terminal admission |

The eight new kernel files join the existing diagnostic directory through
one explicit membership extension. They are new implementation owners, not
fictitious members with regenerated historical hashes. Keep all 373 old
directory entries, old path/mode/include identities, directories and
diagnostic prefix maps. Check the new members for exact names, unique
ordinary-file ownership and private include direction too.

The architecture UART owner supplies the direct/recording selection and
shared logical-byte primitive. Existing syscall decode/dispatch adds only
commands three and four; command-one/two request fields remain exact. The
private decoded request may reuse `virtual_base` and `mapped_length` for
the registered buffer, and adds one explicitly named diagnostic cursor.
All irrelevant fields remain zero in every command.

The two scheduler files gain only their specified notification call before
planning. Their queue, completion, timer, accounting, root and context
algorithms do not change. The TTY service facade registers the consumer,
decodes the two-bit kernel envelope and invokes bounded progress after
ordinary dispatch. Keep its portable terminal, UART, service-core,
completion and VFS-grant implementations unchanged.

The existing TTY-control wrapper owner gains the two exact raw calls.
The diagnostic consumer has no UART-bus or grant-copy callback parameter:
it returns ordinary `micros_tty_effects` to the facade, which applies them
through its already bound UART bus. This preserves the existing callback
providers and does not create another callback family.
Its read/registration operations call those fixed wrappers directly.
The real facade supplies its sole state; native raw-syscall doubles observe
the same register ABI and registered destination, as in existing runtime
tests. Successful initialization publishes the returned cursor once;
failure does not invent an initialized consumer or reset live state.

Native evidence uses one `diagnostic-history` CTest in both native tiers,
with separately owned history, reader and consumer cases under
`tests/host/diagnostic_history_test.c`,
`tests/host/diagnostic_control_test.c` and
`tests/host/tty_diagnostic_test.c`. The first file owns the runner; shared
test declarations stay in one private
`tests/host/diagnostic_history_test.h`. These are one executable and one
correction, not three independently developed tasks.

The native target's closed source operands are those three case files,
the two diagnostic portable-core C files, `kernel/tty/tty_control_core.c`,
`servers/tty/tty_diagnostic.c`, `servers/tty/tty_control.c`,
`servers/tty/tty_core.c`, the unchanged `MICROS_TTY_SERVICE_CORE_SOURCES`,
`lib/runtime/memory.c` and `tests/work/work_counts.c`.
Use the existing host C17/warning/sanitizer policy and enable the existing
work-observation definition only for this native evidence target.
Its syscall double is test-owned, not a new production callback interface.

Design #196 at `60ca0ebad264714b079e0630a5c7bb47726e1250` incorrectly
spelled the existing collector source as `tests/work/work_observation.c`;
that file does not exist in the merged tree. The corrected operand above
names the already Accepted EV02 collector, as specified by the
[operation-count contract](operation-count-evidence.md) and
[existing native recipe](../../tests/work/WorkCounts.cmake).
This is a source-name correction, not a new collector, alias, source member,
measurement rule or ADR decision. Implementation restarts only after this
documentation correction is separately reviewed and merged, using that
merged design as its authoring base. The fixed runtime/MINIX pins, closed
candidate/immutable membership and all other contracts remain unchanged.

No new target image, workflow, service or preset is added. Every kernel
variant links the same production history/control owners, and every
existing TTY image links the same consumer. The current forty-two static
configurations, 111 ELF outputs and ten observed scopes stay mandatory;
they now include the changed code. B13 remains explicitly blocked.

New sources retain full validation selection. The CMake extension must
reconstruct the complete pre-PLAT01 recipe through a closed, independently
declared inverse before any current recipe admission changes. This permits
only the enumerated source operands, native target and additional existing
panic/TTY requirements. It does not change planner algorithms, previous
workflow membership, host/guest budgets or historical recipe expectations.

## Finite fatal source and machine proof

This subsection is the exact Accepted refinement of ADR-0086. Keep its two
qualifiers and the complete original meaning of `UART_PANIC_READY`.
No new history-validity qualifier, frame reset or recursion exception is
introduced.

The raw text body moves from `uart_write` to private `uart_write_direct`;
the raw hexadecimal body moves to `uart_write_hex64_direct`. Except for
their names and private visibility, their fixed baseline signatures/bodies
are the independent source authority. The ordinary recording functions
remain unqualified and retain their real failure paths and frames.

A single pure selector, `uart_panic_mode`, reads the actual console owner's
field and compares it with `PANIC`. It performs no MMIO, state write or
interrupt operation. Both public wrappers call it and select exactly one
direct or recording callee. The direct branch returns before the recording
branch. Neither wrapper changes the selector's object or result.

Only these three call sites may be excluded in ready context:

| Caller | Infeasible edge | Required proof |
| --- | --- | --- |
| `arch/riscv64/uart.c:uart_write_direct` | Its exact `console-owner-violation` call to `micros_panic_entry` | Original allowed-output predicate over the same valid panic console |
| `arch/riscv64/uart.c:uart_write` | Call to `uart_write_record` | Exact owner selector is true and the recording arm is reachable only from its false continuation |
| `arch/riscv64/uart.c:uart_write_hex64` | Call to `uart_write_hex64_record` | The same complete selector/branch proof for this distinct call site |

In unqualified context, all three edges remain. Source order or a function
name is not proof: bind actual object/linked calls, pointer/field origins,
normal-return continuation and complete branch reachability, including
bypass rejection. The selector must read the one actual linked console
object, not a same-shaped foreign state. No arbitrary list of supposedly
infeasible calls is admitted.

Preservers are the two wrappers, selector, direct formatters, unchanged
character writer/flush, existing console predicates, exact SRST boundary,
bounded history readers/formatter, and separately proved observed-mode
compiler hooks. The history readers make no writes; the formatter writes
only scalar/local three-byte pair storage and calls this closed chain.
It never calls the producer, notification, reader-registration or copyout
paths. Source, object and linked evidence must reject an unbound write,
alias escape, CSR enable, callback or additional callee.

The read-only panic boundary returns a signed size (`-1` for invalid
metadata, otherwise `0..1998`) without an output pointer. Only after a
valid size may the formatter read bytes through a prevalidated indexed
reader. Its loop index is below that size and the history cannot change
after seizure. Both readers bind the same single linked history object.
No helper writes through an unknown output pointer or recursively reports
an error. A negative size selects the explicit unavailable record before
any data header or indexed read.

The TTY test's raw-character observation is the one additional
test-configuration-only write: a fixed private unsigned-64 counter
saturating at its maximum after each actual kernel THR store. Its exact
source/machine update and distinct linked object must be proved; reaching
the maximum invalidates the target evidence. It cannot alias console
metadata, affect control flow or appear in an ordinary production image.
It adds no panic call and is not a runtime polling or recovery policy.
The counter and its saturating increment function live in the immutable
kernel test fixture; the candidate character writer has only the exact
test-only call immediately after the actual THR store. Historical source
reconstruction removes only that admitted observation after its complete
site/provider binding is checked. Actual ordinary/observed TTY-test
artifacts retain its callee and frame; non-test images contain neither.

Keep exact fixed-source checks for unchanged seizure, console, firmware,
interrupt and fallback functions. The two renamed direct formatters use
the explicit rename/visibility transport. New wrappers/readers/formatter
use their independently specified operation/effect contracts, not a new
hash learned from edited C. Actual source definition positions may change
after an approved insertion: compiler origin lines bind the actual checked
definition, while its semantics still bind the fixed source or Accepted
operation contract.

The fatal proof records all three exclusions with their roles and
source/object/linked references, using a version-two fatal-proof record.
The enclosing current-candidate bundle retains its existing scope and
artifact sealing. An old single-site or unversioned fatal proof cannot
satisfy the new proof. At most two analysis nodes still exist per real
function; all live frames remain and graph work/storage stay `O(V + E)`.

The existing TTY callback providers, signatures, field offsets and
constructor initializers remain exact. Only `micros_service_main` receives
the bounded diagnostic initialization/progress/test insertion. Before
using an older callback anchor, its exact insertion inverse must recover
the fixed original constructor. New diagnostic sources receive explicit
no-callback-type admissions, checked against actual source and complete
machine calls; an absent old Git file is neither an implicit exception nor
a reason to invent a new provider.

Native negative controls cover a false or foreign selector, bypassed
branch, extra exclusion, changed console or counter object, counter
overflow, hidden history mutation, stack escape, interrupt enable and
unbound call. Retain all original firmware, SRST return/fallback, observer,
recursion, frame and budget negatives.

## Native and target acceptance

### Portable histories and consumer

Native tests start before the production edits. A small independent
sequence model checks complete ring/tail state after each input, including
zero/end separation, byte values, capacities, wrap, overwrite, tail
positions 1,997/1,998/1,999, repeated saturation and exact original byte
movement. Deterministic work observations distinguish the selected rolling
shift from another ring, a whole-buffer clear or an unobserved shortcut.

Reader tests cover exact distinct process/thread/endpoint generations,
profile/root binding, one-shot registration, three-page range geometry,
read-only/absent pages, preflight failure preservation, unchanged inactive
output bytes, both source-wrap spans, each destination boundary, empty
reads and complete-lap ambiguity. The native proof distinguishes three
setup translations from zero new readback translations; it does not
mislabel inherited callee work as zero.

Consumer tests use the real TTY core/service state. They cover occupied
resident output, an uncollected completion with an empty output lane,
busy/writable retry without ID consumption, multiple diagnostic chunks,
coalesced pending reads, echo/CRLF behavior, no VFS grant/completion
mutation, and IRQ-only/diagnostic-only/combined canonical envelopes.
Malformed snapshot headers and unexpected negative control results fail
explicitly.

### Existing TTY target

Extend `test-qemu-tty`, not a new target or a substitute mock service:

1. The real TTY registers before readiness. Its initial diagnostic event
   coalesces with the existing deliberately retained pre-ready IRQ.
2. After readiness acknowledgment, the test-only startup consumes that
   exact combined envelope, runs the unchanged IRQ drain/completion and
   proves route `IDLE`. No large probe runs under the readiness deadline or
   while a PLIC claim is retained.
3. Before the first admitted diagnostic read, a test-only kernel hook
   appends exactly 8,193 bytes, with byte `i` equal to `1 + i % 251`,
   through the real buffered producer. The actual read copies a header
   and the three-page span. The hook is admitted only after the exact
   fixture caller/cursor and successful request preflight, not on a
   malformed or unauthorized call.
4. The test TTY verifies the entire fixed pattern, header relation and
   untouched suffix canaries, then advances the real private cursor and
   discards only this explicitly identified fixture spool. It does not
   flood UART with the large transfer or treat that discard as production
   consumer behavior.
5. After that snapshot commit, the kernel hook appends one short fixed
   diagnostic marker. The ordinary service loop must obtain and forward
   it through the real consumer and UART output lane.
6. Preserve every original VFS peer, grant, input, completion, retained
   claim, readiness, drain, pass-marker and clean-shutdown assertion.
   The peer honors an initial `BUSY` through its existing writable-event
   protocol rather than assuming no diagnostic output can be resident.

The hooks exist only under the existing TTY-service-test definition and
only at the new reader's explicit before-copy/after-copy test boundary.
They introduce no syscall command, trap authority, new assembly or
production callback. The first large transfer and short forwarded record
are different measured outcomes.

New expected patterns, kernel observations and user-side snapshot checks
live in `tests/qemu/diagnostic_history_kernel.c`/`.h` and
`tests/qemu/diagnostic_history_tty.c`/`.h`, frozen before the real Red.
Existing kernel/VFS test components receive only the specifically admitted
integration or busy-retry edits. An immutable source check must recover
their complete original cases, assertions, profile/manifest data, markers
and failure paths after removing exactly those additions. They cannot
replace a new oracle or manufacture its observations.

The short record is exactly:

```text
MICROS_DIAGNOSTIC_TEST_PASS transfer=three-pages envelope=irq-diagnostic output=tty early=drained
```

Its LF is part of the logical record. The large fixture's nonzero control
and high-bit bytes test preservation without leaking them into the host
terminal transcript.

The existing TTY workflow additionally selects
`--require-diagnostic-forwarding`. This closed check requires the unique,
terminated short record before the existing input-trigger/pass sequence,
in addition to every original acceptance condition. Native controls reject
duplicates, truncation, missing forwarding and a reordered marker. A
generic required-line match alone is insufficient.

Compare the test-only kernel THR counter at registration, around readback
and before final stack-report/shutdown output. It must not advance while
ordinary ownership is with TTY. Require the exact forwarded diagnostic
marker once, in addition to the complete old target acceptance. An old
legacy-only pass, an early direct marker, a timeout or partial report is
not PLAT01 success.

### Existing panic target and report validator

Under the existing intentional-panic image definition, append a fixed
2,000-byte seed immediately before the existing panic call: 1,999 bytes
`'a' + index % 23`, followed by LF. The independent expected retained tail
is seed bytes `[2, 2000)`. This exercises actual saturation and leaves the
physical panic classifier at a line boundary. It changes no production
boot path or panic reason.

Add one closed runner option:

```text
--require-diagnostic-history {retained,seeded}
```

Its default absence preserves the old runner interface and fixtures.
`retained` requires the complete old core/trap report plus one retained
history header and exactly the contiguous, terminated chunks described
above. `seeded` additionally requires the exact independent 1,998-byte
intentional-panic tail. An unavailable history is an explicit diagnostic
but is not a passing retained/seeded acceptance result.

The intentional-panic workflow uses `seeded`; other existing workflows
requiring the panic report use `retained`. Keep every previous outcome,
marker, FDT/register check, timeout and exit requirement. The exact runner
extension must project back to the complete old source/fixture identities;
do not regenerate the original eighty-five case/fixture expectations.

The new checks live in the existing runner owners: argument declarations,
CLI forwarding, outcome composition and fault/report validation. They do
not add another runner source family. Exact per-module extension inverses
recover all old source digests and complete flat-runner reconstruction
before old eligibility checks. Actual new bytes remain in the current
execution fingerprint. Old expected digests are not replaced with hashes
of the changed runner, and optional absence retains the old API behavior.

Native malformed-report cases reject duplicate headers, missing or extra
chunks, noncontiguous offsets, wrong lengths, uppercase/nonhex data,
unterminated lines, misplaced history, raw injected markers and unavailable
history. The complete stack workflow executes the changed ordinary and
observed images, with their original budgets and current/future scope.

## Exact source-authority transition

Use transition `PLAT01-diagnostic-history`, task `PLAT01`, primary unit
`B01`, and finding set exactly `{B01-F002}`. The affected and planned-
retirement sets are the following twenty existing transport identities.
The plan's retained subset is empty; every other identity remains active
except COPY01's already committed prior retirement.

```text
tests/host/kernel_layout_source.py:data-entry:EXTERNAL_SOURCE_FACTS/arch/riscv64/uart.c/1
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/boot_component_tests.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/panic.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_idle.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/scheduler_return.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/tty_control_core.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/tty_control_core.h/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/tty_control_syscall.c/2
tests/host/kernel_layout_source.py:data-entry:KERNEL_FILE_FACTS/kernel/tty_service_test.c/2
tests/host/test_grant_data_plane_source_layout.py:data-entry:STABLE_HASHES/kernel/boot_component_tests.c
tests/host/test_kernel_object_source_layout.py:data-entry:STABLE_HASHES/kernel/boot_component_tests.c
tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/include/micros/tty.h
tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/kernel/tty_control_core.c
tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/kernel/tty_control_core.h
tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/kernel/tty_control_syscall.c
tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/kernel/tty_service_test.c
tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/servers/tty/tty_control.c
tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/servers/tty/tty_control.h
tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/servers/tty/tty_service.c
tests/host/test_tty_source_layout.py:data-entry:STABLE_HASHES/tests/qemu/tty_service_vfs.c
```

Keep all twenty literal values and source pins. Retirement changes their
current equality assertions, not their existence or historical meaning.
Do not retire directory membership, include transport, diagnostics,
classification dimensions, private directions, phase/guard/signature
obligations, whole source-gate ASTs or old recipe/runner contracts.

In particular, none of EV01-C01's 177 scheduler entries retires. The
selected return/idle functions retain their original semantic minimum
multisets and phase obligations while gaining one independently checked
notification call. A changed physical file does not automatically
invalidate every historical fact that mentions it.

After EV01-C02, these twenty facts derive exactly eighteen current payload/
owner paths. Independent command and inspection tools no longer appear as
collateral candidate inputs. This design still closes all remaining input
roles explicitly; it does not rely on that example as complete provenance.

### Replacement oracles

The four fixed oracle/test IDs are:

| Oracle ID | Test ID under `tests.host.diagnostic_history_evidence.DiagnosticHistoryOracles` | Independent property |
| --- | --- | --- |
| `plat01-history-storage` | `test_storage` | One actual kernel history object with the specified two arrays and three scalar fields |
| `plat01-reader-commands` | `test_reader_commands` | The actual decoder admits the two exact version-one command shapes |
| `plat01-forwarded-diagnostics` | `test_forwarding` | Complete legacy TTY acceptance plus the unique real forwarded diagnostic witness |
| `plat01-panic-history` | `test_panic_tail` | Complete legacy panic acceptance plus the exact retained seeded tail |

These are real author-selected tests in the evidence helper, not extra
QEMU invocations hidden in ordinary native discovery. Normal native
discovery covers the portable semantics/work, source bindings, codecs,
failure controls and retirement policy. The existing target workflows
carry the integrated target requirements in an ordinary fresh checkout.

The storage oracle inspects the actual compiled kernel image and the
independently specified current native layout. The single static history
object is `diagnostic_history`, with 12,000 array bytes and three
unsigned-32-bit scalar fields, for 12,012 bytes. Its absence from a valid
baseline image is the expected Red, not a compiler or missing-image failure.
Green storage alone is not algorithm certification: the complete native
sequence/work matrix is also mandatory before recording Green.

The reader oracle compiles the actual fixed old decoder, then the actual
current decoder, against the same immutable admission probe
`tests/host/diagnostic_control_fixture.c`. Inputs use the literal Accepted
command/version/size values rather than learning them from candidate
constants. The old decoder returns `ARGUMENT` for both shapes; Green
requires success for both. Detailed field, precedence, authorization and
copyout tests remain additional required native/target evidence.

The admission probe's register tuples are fixed: registration uses command
3, version 1, service 4, endpoint `0x1003` (generation one, slot three
under the existing twelve-bit slot encoding), address `0x40010000`,
size 10,016, zero `a6` and syscall 14. Read uses command 4, version 1,
previous cursor 17, zero `a3..a6` and syscall 14. The probe checks the
existing decoder result/common fields without requiring a new private
member to compile against the old header. Green's separate complete native
tests verify the new cursor field and all command-specific zero fields.

The two target Reds execute the real pinned old images and require their
complete original acceptance first. Only the exact absence of the new
diagnostic witness/tail is the expected failure. A legacy failure, timeout,
crash, malformed new record or skipped execution is not qualifying Red.
Green requires the complete old contract and the complete new contract,
including the independent panic seed.

Let `R1..R20` denote the exact keys above in their displayed order; these
labels are documentation shorthand, not accepted caller masks. The
declaration expands these exact coverage sets to full identity keys:

| Oracle | Covered keys |
| --- | --- |
| Storage | `R1` |
| Reader commands | `R6`, `R7`, `R8`, `R12`, `R13`, `R14`, `R15`, `R17`, `R18` |
| Forwarding | `R1`, `R4` through `R9`, `R12` through `R20` |
| Panic history | `R1`, `R2`, `R3`, `R10`, `R11` |

Every planned key has at least one actual expected Red failure, and no
unmentioned identity can be suppressed.

### Source roles and allowed changes

The only mutable candidate paths during a Red-to-Green attempt are:

```text
arch/riscv64/uart.c
include/micros/tty.h
kernel/boot/boot_component_tests.c
kernel/diagnostic/panic.c
kernel/scheduler/scheduler_idle.c
kernel/scheduler/scheduler_return.c
kernel/tests/tty/tty_service_test.c
kernel/tty/tty_control_core.c
kernel/tty/tty_control_core.h
kernel/tty/tty_control_syscall.c
servers/tty/tty_control.c
servers/tty/tty_control.h
servers/tty/tty_service.c
tests/qemu/tty_service_vfs.c
tests/host/kernel_layout_source.py
tests/host/test_grant_data_plane_source_layout.py
tests/host/test_kernel_object_source_layout.py
tests/host/test_tty_source_layout.py
include/micros/diagnostic.h
kernel/diagnostic/diagnostic_history_core.h
kernel/diagnostic/diagnostic_history_core.c
kernel/diagnostic/diagnostic_history.h
kernel/diagnostic/diagnostic_history.c
kernel/diagnostic/diagnostic_control_core.h
kernel/diagnostic/diagnostic_control_core.c
kernel/diagnostic/diagnostic_control.c
kernel/diagnostic/panic_history.c
servers/tty/tty_diagnostic.h
servers/tty/tty_diagnostic.c
```

The four Python candidates own the narrow existing equality adapters and
retained assertions. An immutable independent projection must recover
their complete original ASTs after removing exactly the admitted adapters;
the candidate module cannot certify its own preservation.

The admitted source maintenance is finite:

- kernel-directory source reads retain actual current bytes and exact old
  include operands, skipping only a retired whole-file checksum;
- the directory test and the TTY/object/grant stable-artifact loops retain
  every unplanned equality and apply the same exact-key decision;
- the old kernel-directory inventory remains the original 373 entries,
  with a separately checked exact eight-member diagnostic extension;
- the bootstrap branch check retains all thirty-one original branches,
  calls, markers and fallback order while independently requiring the
  one intentional-panic seed call; and
- source-gate, root-recipe, runner and callback-constructor inverses admit
  only their named diagnostic additions, recovering the full original
  bytes/ASTs rather than regenerating their expected values.

New ordinary includes follow the old private include operands without
silently changing their bindings or line-transport contract. Any genuinely
required operand movement needs an exact independently checked transport,
not a blanket removal of include validation.

Existing kernel/VFS test components are observed integration subjects:
their complete old assertions and scenario are independently conserved,
and their new expectations/observations live in the separately frozen
fixtures. Public/private production headers consumed by a probe are
subject inputs, not a source of expected command numbers, capacities or
results. The probe's own code, reference values and source bindings are
immutable.

The eleven new production paths may be absent from the pinned Red tree.
Record exact verified Git absence as a typed absence identity, distinct
from an empty file. Green requires every member as an ordinary unlinked
file. No other missing or extra candidate input is admitted.

The prospective non-candidate implementation additions are exactly:

```text
tests/host/diagnostic_history_test.c
tests/host/diagnostic_control_test.c
tests/host/tty_diagnostic_test.c
tests/host/diagnostic_history_test.h
tests/host/diagnostic_control_fixture.c
tests/qemu/diagnostic_history_kernel.c
tests/qemu/diagnostic_history_kernel.h
tests/qemu/diagnostic_history_tty.c
tests/qemu/diagnostic_history_tty.h
tests/host/diagnostic_history_authority.py
tests/host/diagnostic_history_source.py
tests/host/diagnostic_history_evidence.py
tests/host/test_diagnostic_history.py
tools/check_diagnostic_history.py
```

The source-projection helper is a dependency leaf over the existing
inspection primitives and standard library. It does not import the task
driver, runtime oracle executor or validation-context facade, avoiding a
recipe/authority import cycle. It owns its fixed source reference and
closed projection declarations. The authority owner may use that helper;
actual-execution validation calls back into the evidence owner only at
explicit runtime boundaries.

Build the immutable input set from the merged design tree's complete
tracked membership plus these fourteen paths and the eleven new candidate
paths, then exclude the twenty-nine candidate paths. It is a closed set,
not a glob. Root recipes, runner/stack evidence changes, source-projection
rules, ownership registration, test fixtures and implementation
documentation are therefore finalized before the real Red. Changing one
later requires a new explicit attempt.

The accepted design commit/tree/document and fixed baseline archive are
separate immutable authorities. The driver's base HEAD/tree are the
separately merged design's actual Git objects; its runtime source must
match the fixed pre-correction baseline above. Subject headers and sources
are selected from that pinned tree for Red and the current staged tree for
Green. No current-source hash or output becomes the expected baseline.

Close the selected native compiler/cache, target compiler/linker/tools,
QEMU/default firmware and Python runtime as named immutable external
inputs. Generated objects/images, actual logs and journal records are
outputs under the private attempt namespace, never expected-value inputs.
No overlapping bytes are made independent by changing their input labels.

### New authority declaration

Add one closed `diagnostic-history-baseline` family owned only by
`tests/host/diagnostic_history_authority.py` and
`tests/host/diagnostic_history_source.py`. Its eight facts are:

| Owner | Kind/scope | Class |
| --- | --- | --- |
| Authority module | `design-anchor:PLAT01` | Transport identity |
| Authority module | `source-membership:PLAT01` | Permanent invariant |
| Authority module | `artifact-contract:PLAT01-recipes` | Artifact contract |
| Authority module | One `baseline-oracle` key for each of the four IDs above | Baseline oracle |
| Source module | `source-projection:PLAT01` | Transport identity |

The design anchor binds the merged design and fixed source/MINIX pins.
Membership binds the closed source roles. Recipe authority binds the exact
independently specified build/validation additions. The source projection
binds its fixed reference and exact inverse admissions. Each oracle binds
its test ID, property and structured MINIX/Accepted-adaptation provenance.

Independently declare those key/value/payload records from the merged
contract before changing inventory extraction. Preserve all 3,912 current
individual records and eleven family fingerprints, including the corrected
EV01-C02 bindings. The resulting 3,920 facts have twelve families; the old
twenty transport rows remain present even when their assertions retire.
Execution-log digests are observations, not extra authority facts.

## Real author transaction

The only author recording intents are:

```text
<MICROS_PYTHON> -E -S -B tools/check_diagnostic_history.py --red
<MICROS_PYTHON> -E -S -B tools/check_diagnostic_history.py --green
<MICROS_PYTHON> -E -S -B tools/check_diagnostic_history.py --recertify
```

The selected interpreter and its admitted Homebrew launcher/runtime-image
handoff follow the existing closed runtime contract. No caller-supplied
command, source root, finding, retirement mask, expected result or success
file is accepted.

Use fixed storage below `build/evidence-transitions/PLAT01/`, one OS-backed
writer lock, immutable attempt records and an atomically published selector.
Every event is produced from actual native/compiler/QEMU/unit execution;
synthetic EV01 fixtures never certify a real transition.

Red builds the pinned baseline through its existing `riscv64-panic-test`
and `riscv64-tty-service-test` configure presets and the `micros` build
target. Green uses the same configurations from the current staged source.
The existing target executable/firmware, machine, RAM, input trigger/bytes,
markers, failure checks and timeouts are fixed by the reviewed workflow
descriptors. The immutable harness captures actual serial output through
the existing process owner, performs complete legacy acceptance, then runs
the separate named new-property assertions. It executes each required
scenario, not an imported transcript.

Native decoder admission uses the same immutable probe against the actual
selected-stage C/header sources. Green additionally builds and runs the
new `diagnostic-history` CTest with the existing native sanitizer policy.
Record exact commands, compiler/input/executable/image identities, actual
outputs and per-oracle outcomes. An aggregate success or a self-reported
pass marker without the closed fixture/source/artifact checks is not enough.

Recheck source/tool/input identities before and after execution and
publication. Preserve raw record bytes and fresh predecessor hashes through
the entire chain, including the selector digest. Semantic JSON equality
cannot hide a byte-chain rewrite. A new Red attempt invalidates prior
current success before running; an oracle change or post-Green source
change starts another attempt rather than relabeling old events.

After actual Green, recertification reruns all four oracles and enters one
process-local, exact-root/plan/PID suppression scope for only the twenty
planned keys. Clear it on every exit. Keep COPY01's previously retired
memory key as separate prior registry authority, never a new PLAT01
suppression or event.

Every affected unit records its actual successful command and the same
current candidate/input projection:

| Unit | Canonical recertification command |
| --- | --- |
| B01 | `cmake --workflow --preset test-qemu-panic` |
| B02 | `cmake --workflow --preset test-qemu-address-space-handoff` |
| B03 | `cmake --workflow --preset test-qemu-scheduler` |
| B04 | `cmake --workflow --preset test-qemu-ipc` |
| B05 | `cmake --workflow --preset test-qemu-user-runtime` |
| B06 | `cmake --workflow --preset test-qemu-bootstrap-launcher` |
| B07 | `cmake --workflow --preset test-qemu-vm-handoff` |
| B08 | `cmake --workflow --preset test-qemu-pm-service` |
| B09 | `cmake --workflow --preset test-qemu-tty` |
| B10 | `cmake --workflow --preset test-qemu-ramfs` |
| B11 | `cmake --workflow --preset test-qemu-vfs` |
| B12 | `cmake --workflow --preset test-qemu-elf-spawn` |
| B14 | The actual `--recertify` driver invocation above |

The inventory-derived minimum is B01/B03/B05/B06/B09/B14. The other seven
current runtime units are explicit downstream recertifications for the
shared kernel, ABI and image changes. No B13 result is invented.
B14 owns the in-process retained source/authority, recipe/runner/stack-
binding checks and oracle replay; it does not claim separate Python
subprocess argv. Its candidate projection includes all twenty-nine paths,
as do the other units; every remaining execution dependency is immutable.

Only complete success publishes `AUTHOR_RECERTIFIED` and derives
`RETIREMENT_READY`. Failure or interruption leaves ordinary assertions
active and publishes no usable terminal retirement.

## Ordinary assertions and final acceptance

The adapter skips only a named planned equality after valid authorization.
It still reads the actual current source and preserves mode/path/include
transport; it never substitutes an archived C body. All nonplanned
equalities and structural assertions remain active.

A newly staged completed-claim declaration is not prior retirement.
Before the first reviewed commit, ordinary assertions require a valid
terminal journal for the exact current candidate, or keep the original
equality. Only the validated author transaction may provisionally suppress
the keys after Green. No environment flag or caller mask can borrow it.

After actual transition, post-terminal full acceptance and clean independent
review, the first permanent green commit may contain the completed claim.
Its exact unchanged HEAD Git declaration supplies later no-plan retirement
without requiring old journals. Preserve and validate COPY01's prior claim
alongside it. No old event becomes a current execution receipt.

Required negatives include absent/staged-only authority, wrong design or
finding, omitted/extra keys, false or nondiscriminating Red, missing native
or target execution, changed immutable inputs, changed Green source,
partial unit coverage, altered log/record bytes, failed publication
rechecks, competing writers, leaked/nested suppression and attempted
retirement of a packed/permanent/unrelated identity.

Finally run the existing complete fresh author plan, including full native
discovery, all ordinary QEMU/image workflows and all current stack evidence,
on the exact staged tree. Retain every original budget and future B13
admission. A fresh ordinary committed checkout must preserve both completed
retirements without manufacturing a journal.

Independent implementation review verifies the actual records, authority
partition, baseline mechanism, new native/target evidence and complete
current author report before publication. PLAT01 closes only B01-F002;
all other platform, memory, object, IPC, TTY, node-Q and hosted-CI outcomes
remain with their documented owners.

## Retrigger-safe target probe correction

This subsection is Accepted under
[ADR-0094](../adr/0094-retrigger-safe-diagnostic-probe.md) after clean
independent design review. Implementation remains blocked until its
separate design merge. It replaces only the first TTY probe's admission and
startup progress rules above, plus the single native source operand below.
All production behavior and other PLAT01 scope remain unchanged.

### Precise failure boundary

The initial combined notification is still consumed after `READY_ACK`,
and its real IRQ is drained and completed first. A later source-10 claim
may nevertheless precede the next READ. Completion of one claim proves
that transition, not persistent absence of a subsequent claim.

The existing first-probe hook acquires a Boolean admission result. It
runs only after successful production READ preflight and checks the same
exact caller, registered cursor, completed readiness, disabled SIE and
unchanged test kernel-THR counter before either result.

| First-probe condition | Required action |
| --- | --- |
| `TTY_OWNED`, readiness deadline cleared, route `IDLE` | Append the exact 8,193-byte pattern once, move fixture phase from registered to injected, return admitted |
| Same owner/readiness/cursor, route `IN_SERVICE`, claimed source 10, exact TTY endpoint has queued kernel IRQ bit `0x8` | Return deferred without mutation |
| Unknown route, wrong source/cursor/phase, missing queued IRQ, premature readiness or another failed invariant | Fail explicitly; never defer |

Queued-event inspection uses the authoritative destination registry. It
does not inject, consume, acknowledge, synthesize or clear an event. The
queued mask must be exactly the fixture's expected IRQ-only mask; an
unexpected diagnostic or other bit is not borrowed as progress authority.

The target READ adapter returns `MICROS_SYSCALL_ABI_STATE` (`-4`) for that
test-only deferred result before invoking copy or after-copy hooks.
Within the READ/fixture transition, only `a0` and the already advanced
syscall `sepc` may differ; ordinary return/accounting keeps its existing
behavior. The reader
binding, history arrays/cursors, producer pending flag, destination bytes,
notification state and fixture phase remain unchanged.

An admitted first probe still samples its transfer span after the fixture
append and commits the normal header/payload copy. Its after-copy hook
then moves injected to complete and appends the single short forwarding
record. A later read in the complete phase follows the original ordinary
test observation; it never reenters first-probe deferral. An impossible
reentry in the injected-but-not-completed phase fails.

None of these deferral branches or result semantics appears without
`MICROS_BUILD_TTY_SERVICE_TEST`. No additional production state, control
command, event bit, callback provider or mapping lifetime is introduced.

### User fixture and startup progress

The private target-fixture helper returns one of exactly `COMPLETE`,
`DEFERRED` or `FAILED`, rather than treating every nonzero scalar as
success. It prepares the existing snapshot canaries, invokes the unchanged
private READ wrapper and handles the actual result:

1. `STATE` is `DEFERRED` only if all 10,016 snapshot bytes remain canaries
   and every consumer scalar is unchanged.
2. Any other negative or positive nonzero result is `FAILED`.
3. Zero requires the complete original version/length/cursor/pattern and
   inactive-suffix checks. Only then may it publish the new cursor and
   mark exactly the fixture spool consumed.

On `DEFERRED`, the test-only startup facade receives one canonical
source-`NONE`, token-zero, zero-tail notification with exactly bit `0x8`.
It performs the unchanged `handle_irq` drain/completion, then retries the
same probe with its unchanged cursor and destination. Missing, malformed,
foreign, combined or unknown notifications fail instead of authorizing
another read. Every successful iteration consumes a real queued event and
completes its corresponding retained claim.

The loop is event-driven fixture progress, not production retry or an
acceptance-workflow retry. It has no sleeps, polling, guessed interrupt
count or timeout extension. The existing 90-second host bound remains
mandatory. Local canary preparation/checking is allowed while an event is
retained; the large kernel append and snapshot-copy probe are not.

The original initial combined-event check remains exact. A bounded private
startup helper may keep the facade's constructor within its review signal,
but it receives only the already bound bus/I/O values and introduces no
new callback initializer or provider. The independently frozen constructor
inverse must restore the complete original constructor and preserve every
existing callback binding. The ordinary diagnostic consumer still rejects
every negative READ and never implements this fixture loop.

### Native evidence and restart

Append the already declared
`tests/qemu/diagnostic_history_tty.c` operand immediately after
`tests/host/tty_diagnostic_test.c` in the same native target's closed list.
All other operands and both native tiers remain exact. The existing
`tests/host/tty_diagnostic_test.c` owns focused raw-double cases for:

- a valid deferral without cursor, spool, notification or snapshot mutation;
- rejection of a changed snapshot or scalar on `STATE`;
- explicit failure for every non-`STATE` error and unexpected positive result;
- a subsequent exact 8,193-byte, three-page transfer with the same cursor;
- no repeated injection or fixture-spool publication after completion; and
- unchanged ordinary-consumer failure on a negative READ.

The new source is already one of the fourteen immutable additions; no path,
CTest, preset, workflow, target image or authority family is added. Source
checks bind the fixture-only return before copy/after-copy, required queued
event, exact startup consumption and unchanged legacy assertions.

After separate design merge, derive the eight new authority facts from the
corrected merged contract before their owner/extractor edits. Bind that new
design commit/tree while retaining the original MINIX/runtime source pins,
all twenty keys and all 3,912 committed predecessor facts. The previous
unmerged eight-fact declaration, raw Red journal, source tree and narrow
passes remain historical reference only.

Restart with a new actual four-oracle Red, then current native/target Green,
the unchanged B01-B12/B14 command table, raw-byte terminal chain and full
post-terminal gate. The target must still prove the large copy only in
`IDLE`, real diagnostic forwarding, zero kernel-THR delta, all original
grant/input/claim/completion assertions and clean shutdown.

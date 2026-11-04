# Bounded Panic Stack Trace

## Status and outcome

This is the PLAT03 contract defined by Accepted
[ADR-0096](../adr/0096-bounded-kernel-panic-stack-trace.md).
The dependency-ready predecessor is merged PLAT02 implementation #201,
commit `e12b2d6c163ac92282d380eff5be419fad7f7a30`, tree
`53d900f539d0e766b2998ff57aeea990ac25e30a`.

The outcome closes only B01-F004/B01-E004: reproduce the reference's
current-kernel frame-chain walk, with bounded traversal and authenticated
stack/entry-epoch reads. The
[fixed-source study](../research/minix-panic-stack-trace.md) defines the
MINIX behavior, RV64 representation and complexity baseline.

The design phase added no executable code, test, model, harness, build
scaffold or reusable prototype. Independent design review found no
substantive issue and the design separately merged as #202.

The first uncommitted implementation later found that case 8's shorthand
could be misread as the preceding case's stop code. Its initial
declaration did so. That attempt stopped before production edits and
was abandoned. [Accepted ADR-0097](../adr/0097-panic-trace-link-rejection-precedence.md)
clarifies the target expectation without changing the cursor algorithm.
That correction separately merged as #203 before the restarted implementation.

The private production owners below implement the bounded owner/cursor,
authenticated target adapter and two zero-frame assembly helpers. The
native fixture, eighteen-case controller and version-four source/object/
linked proof cover the declared behavior and terminal boundaries. The
implementation acceptance barrier remains the complete fresh author plan,
matching immutable baseline/current inputs and independent review before
the first implementation commit. No unrelated runtime finding or hosted-CI
acceptance is included.

Implementation #204 is independently reviewed and merged as
`ddca7661d4fa7e13acf0aaa5f0ba55b91f9c9916`. Its reviewed, committed,
remote and squash trees are identical:
`3bf57ef9d12345f7b72e082a5843c97a2476f23e`.
All 50 fresh author commands passed, including 40 native CTests, 1,399
Python tests, matching eighteen-case baseline/current evidence,
44 static configurations/113 ELF outputs and ten observed scopes.
Independent review found no substantive issue before the first
implementation commit. A fresh committed checkout passed 101
authority/source checks without author journals. Exact task branches
were cleaned while purposeful archives and reference stashes were retained.
This closes only B01-F004/B01-E004.

## Preserved authority and non-goals

Keep the original direct/trap panic entry signatures, 48-byte machine
capture, six-argument reporter, 288-byte trap frame and all field offsets.
Keep the original five contiguous core records, optional trap record,
PLAT01 history, repair/seizure notices, final drain, exact shared SRST
admission and returning-firmware fallback.

PLAT02's first claim still precedes every argument dereference and all
report work. The trace is called only from the already claimed report
body. A second entry, invalid panic-control phase, already-issued reset
or exhausted UART wait cannot start another trace.

No user-stack, saved-process-stack, cross-trap-epoch, DWARF, symbol-name,
module, SMP or arbitrary-address unwind interface is added. There is no
new registry, dynamic allocation, stack scan for plausible PCs, general
safe-copy service, recovery operation, configurable depth, runtime tracing
enable flag or extra ordinary-path bookkeeping.

Kernel code is independently written in C17 and narrowly scoped RV64
assembly. No MINIX/NetBSD source is copied.

## Source owners and interfaces

The new private production owners are:

| Path | Sole responsibility |
| --- | --- |
| `kernel/diagnostic/panic_trace_core.h` | Portable fixed layouts, results and cursor contract |
| `kernel/diagnostic/panic_trace_core.c` | Owner admission and bounded frame-step decisions, without memory dereferences through supplied addresses |
| `kernel/diagnostic/panic_trace.h` | Private target entry and assembly declarations |
| `kernel/diagnostic/panic_trace.c` | Resolve actual fixed owners, snapshot current entry state, perform admitted reads and stream records |
| `arch/riscv64/panic_trace.S` | Obtain the current frame register and load one admitted eight-byte word |

The only existing runtime body changed is
`kernel/diagnostic/panic.c:panic_report_claimed`: add one direct call
immediately after the optional trap record and before
`micros_diagnostic_panic_report`. The new private include consumes one
existing blank include-line slot where needed to preserve unrelated
source-location contracts. No old panic call site or assembly capture is
rewritten.

The target entry is `void micros_panic_trace_report(void)`. It accepts no
caller-supplied stack, frame, hart, thread, process or trap pointer.
Its only valid production caller is the first claimed report body.

The assembly interfaces are:

- `uintptr_t micros_panic_trace_frame_pointer(void)`: return `s0` in `a0`
  and preserve every other register; no stack or memory access;
- `uint64_t micros_panic_trace_load_word(uintptr_t address)`: perform
  exactly one aligned `ld` from `0(a0)` into `a0`, then return; no other
  load, store, stack allocation, call or CSR instruction.

Only the target trace owner may call the load helper, after a successful
cursor admission. The helper is private, not a public kernel read service.
Object and linked instructions, caller operands and lack of an indirect
call must be checked independently.

The portable API consists of one owner-selection operation and three
cursor operations: open, plan the next pair of word addresses, and consume
the two already read values. It takes fixed-value views, never callbacks.
Output parameters are unchanged on API misuse. A malformed traced frame
is a terminal cursor result, not an API failure or a reason to panic.

The cursor stores only the immutable admitted bounds, current/last frame,
record count and stop state. No production object or local array scales
with depth. Native assertions limit its complete size to 64 bytes.
An owner view and its two selection outputs use at most 256 bytes.
Implementation must not evade these limits by nesting another depth-sized
object in a wrapper.

## Kernel compiler and frame convention

Every C translation unit linked into the kernel `micros` target explicitly
uses:

```text
-fno-omit-frame-pointer
-mno-omit-leaf-frame-pointer
-fno-optimize-sibling-calls
```

These are kernel C options only. Do not alter native optimization,
service/application options, ISA, ABI, linker relaxation, existing
sanitizer flags or output budgets.

The required RV64 LP64 record is two eight-byte words immediately below
the frame pointer: saved PC at `fp - 8`, previous frame pointer at
`fp - 16`. `s0` denotes the procedure's entry SP after its prologue.
The caller/callee graph remains the actual compiled graph; this does not
reconstruct inlined logical calls or invent a frame for an assembly
function that never establishes `s0`.

Actual linked metadata and observed images must establish the two stores,
CFA assignment and caller return-address relationship for the C chain
used by the target acceptance cases. Missing/overridden options,
contradictory actual prologues or a tail call that erases a required
witness fail acceptance. Source flags alone are not a frame proof.

The existing boot, trap, idle and emergency assembly boundaries remain
unchanged. Their real allocated frames still count in static stack
composition even when they are not psABI-linked records.

## Authentication of one active stack epoch

### Trusted inputs

The target adapter obtains:

- its current SP and `sscratch` using fixed register/CSR reads;
- the current frame pointer through the no-frame assembly helper;
- the linked boot, primary, emergency, thread-bank and executable-text
  symbol bounds;
- the existing production registry pointer and registered boot-hart
  handle through their fixed runtime owners.

No diagnostic argument, supplied trap-frame pointer, arbitrary `tp`
value, saved user register or payload pointer becomes lookup authority.
The runtime registry pointer is the sole existing fixed object, not an
address supplied by a caller.

The adapter admits the exact boot-hart handle with the existing selected
slot/generation resolver. It may read only that hart, its selected current
thread and that thread's owning process. It never calls the complete
registry validator, walks other slots, changes object state or invents a
zero generation as a successful owner.

For non-boot ownership require an initialized one-hart production registry,
an exact live registered hart, and installed traps. Its idle-primary
bounds must equal the linked primary allocation. Its emergency bounds
must equal the linked production emergency allocation. The retained
`MICROS_BUILD_NESTED_TRAP_TEST` image alone admits its exact separately
linked existing emergency-test allocation; no caller or mutable field
selects a third allocation.

### Physical interval and root CFA

Use the adapter's actual SP, not the panic capture's earlier SP, to
classify the active physical allocation. Require it to be 16-byte aligned
and inside that allocation, with room for at least one two-word record.
The actual compiler-generated trace frame must remain in the same
allocation.

| Owner | Admission | Root CFA, the last frame permitted |
| --- | --- | --- |
| `boot` | SP in linked boot interval; no runtime-registry success is required before initialization | `__boot_stack_top` |
| `primary` | SP in linked primary interval and exact authenticated hart's immutable idle-primary bounds | Primary top minus 288 when `sscratch == 0`; otherwise primary top only when `sscratch` equals that hart's anchor |
| `thread` | SP in one checked thread-bank slot; exact authenticated hart selects that slot's live current thread and matching attached context | Thread top minus 288, with `sscratch == 0` |
| `emergency` | SP in the exact installed emergency allocation and exact authenticated hart, with `sscratch == 0` | Emergency top |
| `unavailable` | Any failed or ambiguous admission | No readable interval and no frame read |

For a thread, derive the candidate slot directly from the actual SP and
the linked 16 KiB slot geometry before consulting its metadata. Require:

- the slot is within the 128-slot linked bank;
- the hart's current-thread slot is that same slot;
- the complete nonzero thread generation resolves a live record;
- its context is attached and its bottom/top equal that derived slot;
- its owner resolves an exact live process generation;
- the hart's selected primary bounds equal that same slot.

Do not require `runtime_flags == 0` or scheduler assignment: a legitimate
fatal report can occur during a held/selection transition. Conversely,
bank membership alone, matching a process slot, a stale generation or
matching another thread's bounds is never sufficient.

For a primary epoch, a selected thread's mutable primary bounds never
authorize the separate primary allocation. Use only its immutable
linked bounds and the authenticated hart's equal idle-primary bounds.
Selecting a thread just before return does not transfer ownership of
the still-live hart-primary continuation. Thread-bank admission remains
stricter: if the selected generation no longer matches that live stack,
emit `unavailable` rather than guessing an old owner or scanning slots.

The admitted interval is immutable for the trace. SIE/SEIE remain clear,
one hart is active, and no function in the trace can schedule, mutate
ownership, allocate, transfer a frame or enable an interrupt.

### The interrupted-frame barrier

The root CFA is not just another upper address bound. After reading and
emitting that exact root record, terminate with `boundary` without
following its saved predecessor, regardless of its numeric value.

For trap dispatch, `root = selected_top - 288`. This excludes every byte
of the saved trap frame from possible frame-record loads. In particular,
a user-controlled `s0` that points into that trap capture cannot make the
walk interpret saved user registers as supervisor call-chain records.

The same terminal boundary prevents a boot firmware register, a previous
idle epoch or an emergency pivot's inherited `s0` from being followed.
No preserved or restored GPR is cleared or rewritten merely to terminate
the trace.

## Cursor, reads and failure precedence

The maximum is exactly 32 emitted frame records. It is not a cache size,
user input, command-line argument, build option or a substitute for
analyzing linear traversal.

Opening a cursor first requires a valid admitted owner, non-wrapping
interval and root CFA, and the actual SP/frame relationship.
The first frame must satisfy:

```text
SP <= fp - 16
allocation_bottom <= fp - 16
fp <= root_CFA <= allocation_top
fp % 16 == 0
```

Check ordering and subtraction safety before forming either word
address. Every later planned record has the same bounds and alignment
checks; every accepted nonzero successor is strictly greater than the
current frame. Thus frame-record addresses never move into the tracer's
newer local allocations or out of the selected epoch.

For each admitted record:

1. load the saved PC at `fp - 8`;
2. load the predecessor at `fp - 16`;
3. validate the saved PC: nonzero, two-byte aligned and strictly inside
   `[__kernel_text_start, __kernel_text_end)`;
4. if the PC is invalid, emit no row for it and stop with `invalid-pc`;
5. otherwise emit that PC and the current frame pointer, then increment
   the count;
6. if the current frame is the authenticated root CFA, stop `boundary`;
7. otherwise if the predecessor is zero, stop `end`;
8. otherwise if it is non-increasing, stop `nonmonotonic`;
9. otherwise validate its alignment and complete two-word range;
10. if invalid, stop `invalid-frame`; if count is 32 and another valid
    record would remain, stop `limit` without loading it;
11. otherwise advance to that predecessor.

This preserves MINIX's output-before-link-error order. A valid 32nd
record whose predecessor is null ends `end`, not `limit`; a root frame
ends `boundary`. A malformed successor is reported as malformed without
reading it, even at the depth boundary.

No word is loaded for initial owner/frame failure. A bad saved PC incurs
the two already admitted record reads but no published frame row.
There is never a 33rd record read.

Null initial frame yields `end` with zero rows only after owner admission;
owner failure yields `owner`, not an invented successful empty trace.
Other initial frame failures yield `invalid-frame`.

The private helpers initialize and publish fixed scalar fields, not
whole-depth aggregates. Their qualified call graph admits no explicit
or compiler-introduced copy/fill call. This keeps trace-local setup out
of the existing RUNTIME work dimensions and must be checked in actual
artifacts rather than assumed from C spelling.

### Meaning and fault containment

In-range PCs are diagnostic data, not proof that corrupted kernel memory
contains a genuine historical call. The design guarantees bounded,
owner-confined reads and deterministic rejection, not authenticity
against arbitrary corruption of both stack data and kernel metadata.
Normal target cases independently prove the actual caller chain.

The retained kernel invariant supplies wired, supervisor-readable stack
storage and executable text in every active root. The trace does not
call a page-table walker or add a recoverable-load protocol. A synchronous
fault after admitted access enters the existing trap machinery and hits
the already-latched PLAT02 entry boundary; it issues at most one failure
reset and performs no recursive trace.

The same documented non-faulting anchor/stack requirements as ADR-0019
apply. Corruption that prevents even trap entry or access to the wired
emergency stack is not made recoverable by a stack trace.

## Output grammar and placement

After the optional `MICROS_TRAP_CONTEXT` and before the PLAT01 history,
the first report streams:

```text
MICROS_TRACE_BEGIN version=1 owner=<boot|primary|thread|emergency|unavailable> limit=0x0000000000000020
MICROS_TRACE_FRAME index=0x<16 lower hex> pc=0x<16 lower hex> fp=0x<16 lower hex>
MICROS_TRACE_END count=0x<16 lower hex> stop=<end|boundary|limit|owner|invalid-frame|invalid-pc|nonmonotonic>
```

There are zero through 32 frame rows, consecutive zero-based indices,
exactly one header and one end record for a completed trace. Every
record ends in one logical LF; the retained UART performs its ordinary
CR/LF physical conversion. No symbol spelling, file lookup, variable
precision, variadic formatting or buffered aggregate is used.
The new names deliberately do not start with `MICROS_PANIC`: the
retained original classifier counts exactly five lines with that
prefix. Do not weaken that classifier or strip trace bytes from its
transcript to accommodate new output.

An unavailable owner produces only its header and `count=0, stop=owner`.
A frame/PC error records its exact stop reason and continues to the
unchanged history/flush/reset tail. It does not repair memory, clear
metadata, restart tracing or return to ordinary execution.

UART exhaustion or recursive panic may truncate output at any point.
They retain PLAT02's no-console termination precedence; absence of a
trace end in such a fault scenario is not a successful complete trace.
A normal trace-specific acceptance case requires the end record and a
natural firmware exit, not a timeout or host termination.

## Work and storage evidence

Production traversal has `O(min(s,32))` time and `O(1)` auxiliary storage.
Selected owner admission is constant work in the logical number of
processes, threads and harts. An array scan or complete-state validator
in that admission is a design violation, even with fixed capacities.

The portable/native trace test enables the existing EV02 collector.
The new target trace configuration separately enables
`MICROS_TEST_WORK_COUNTS=1`. For the trace-specific instrumentation only:

- `KERNEL_DIAGNOSTIC/RECORD_VISIT` records each completed frame-word load:
  the target annotation is after each actual assembly load returns;
- `KERNEL_DIAGNOSTIC/LINK_TRAVERSE` records each accepted nonterminal
  cursor advance, immediately after the advance;
- all other trace-specific dimensions remain zero.

The trace's instrumentation is enabled only in its native/target
fixtures. PLAT02's separate configuration must not acquire those new
hits. Its existing UART/history counters remain untouched.

For `r` actually read frame pairs and `a` accepted advances, independent
expectations require `RECORD_VISIT=2*r` and `LINK_TRAVERSE=a`.
An invalid initial pointer produces zero/zero. An invalid saved PC
adds its two reads but no row or advance. A refused successor and the
depth cap add no read. Native cases also place guard pages and canaries around the host fixture
consumed through actual portable address plans. That host reader is not
claimed to execute the RV64 assembly helper. Actual caller/operand
binding and the real target cases separately prove target memory access;
counters alone do not prove absence of an out-of-range load.

Target snapshots use existing version 1 and owner mask 2,
producer `0x504c415430330000 | case`, capture 1, all reserved fields
zero and the unchanged complete 27-owner/14-kind matrix. UART
comparison and retained history movement remain separately accounted
exactly; no counter is reset halfway through a case to discard work.

The fixture may retain bounded expected records in test-owned storage.
The production algorithm may not do so. The target's actual linked
private objects, caller stack usage and every reachable continuation
must prove the fixed-space assertion and all original 4 KiB/16 KiB
budgets.

## Implementation and evidence membership

The prospective mutable production set is the five new owners above
plus `kernel/diagnostic/panic.c`. Existing boot, trap, scheduler, object,
address-space, panic-capture and service sources are not algorithm
candidates. Their content may not be changed to make authentication or
the trace algorithm work. The exact test-only overlays below are the
sole exception and must inverse to the complete original bytes.

The design admits these new evidence owners:

| Path | Role |
| --- | --- |
| `tests/host/panic_trace_test.c` | Independent portable owner/cursor boundary and operation-count cases |
| `tests/host/panic_trace_source.py` | Fixed membership, source/recipe/annotation and machine-effect declarations |
| `tests/host/panic_trace_evidence.py` | Independent fixed-reference expectations and actual-run record decoder |
| `tests/host/test_panic_trace.py` | Malformed input, source transport, machine binding and harness regressions |
| `tests/qemu/panic_trace_kernel.c` | Fixed actual target scenarios and observation producer |
| `tests/qemu/panic_trace_kernel.h` | Private fixture declarations and observation layout |
| `tests/qemu/panic_trace_kernel.S` | Exact frame-chain and malformed-boundary target inputs |
| `tools/check_panic_traces.py` | Closed fixed-baseline/current build and execution owner |

One `panic-trace` native CTest enters both native tiers. One kernel-only
`riscv64-panic-trace-test` configuration and `test-qemu-panic-trace`
workflow exercise the fixed matrix. It adds one static kernel ELF:
44 configurations/113 outputs, with all ten current observed scopes
unchanged and B13-E007 still blocked.

The test boot hook is appended after the existing PLAT02 boot hook and
must preserve all original source-location checks. Only the exact
test-flagged fixture hook and independently reversible test annotations
may appear in existing runtime subjects. No existing workflow loses a
case, marker, counter, timeout, exact-source check or target assertion.

## Exact test-only overlays

The new target flag is `MICROS_BUILD_PANIC_TRACE_TEST`. It is never
combined with PLAT02's terminal flag or EV03 compiler-observer mode.
Only these existing production files admit new test integration:

| Existing file | Exact permitted overlay |
| --- | --- |
| `kernel/boot/boot_component_tests.c` | Include the private fixture header without moving original panic locations; append the one trace-scenario invocation after the current terminal hook |
| `include/micros/kernel_object_runtime.h` | Add the new test flag to the existing test-registry declaration guard only |
| `kernel/objects/kernel_object_runtime.c` | Add the same flag to that existing accessor's definition guard; retain its body |
| `arch/riscv64/trap.S` | Under the new flag, inject one fixed illegal instruction for the emergency scenario after the complete first trap capture and before the dispatcher call; no change to ordinary prologue/epilogue |
| `kernel/diagnostic/panic.c` | Private first-body observation after the existing entry claim, plus a no-output fixture point after core/trap records for declared corruption cases |
| `arch/riscv64/uart.c` | Test-only THRE result filter after its one actual LSR read and before the existing comparison; count actual seizure/physical-output events without changing ordinary behavior |
| `arch/riscv64/sbi.c` | Test-only snapshot/reset observation after the shared admission and before its actual `ecall`; preserve exact arguments |

New trace owners also expose exact test-only entry, completed-word,
accepted-link and completed-trace observation points. Their normal
preprocessed bodies contain no observation state/call. A test failure
stops at the fixed fixture-error symbol and is never an expected Red.

The source inverse removes only these declared, occurrence-bound
overlays. It cannot remove a changed ordinary statement, whole function,
conditional body, arbitrary include, call or compiler option.

Thread scenarios use the existing test-registry accessor and production
portable process/thread/context/scheduler operations. Allocate two exact
process/thread pairs, keep slot zero held, attach the proper static
kernel-stack slots, initialize the existing hart accounting state, and
admit/select slot one through
`micros_thread_scheduler_admit`, `micros_hart_plan_user_return` and
`micros_hart_commit_user_return`. Keep the large existing selection plan
in fixture-owned static storage. No test writes a fabricated
successful current-thread relation.

While still in S-mode, the fixture triggers a real trap through the
unchanged assembly entry; that entry selects the authenticated thread
stack. It can supply hostile saved GPR values without adding a user
image, user syscall, new scheduling rule or a fake trap-frame pointer.
These cases are explicitly S-origin trap tests, not claims that a new
U-mode integration was implemented. The common saved-register/entry
boundary applies equally to U origin; retained real U-origin fatal
workflows and the complete source/linked trap proof remain required.

Declared corruption cases change only their exact fixture-owned input
or selected metadata field at the no-output fixture point. They never
patch code, mappings, a different object or the panic/reset latch.
Normal owner cases must use unmodified production-created identities.

## Fixed target matrix

Each case uses a fresh one-hart VM, current fixed default firmware and
one absolute eight-second deadline including debugger startup. The
trace test does not lengthen any existing workflow or guest deadline.

| ID | Input/scenario | Required current outcome |
| --- | --- | --- |
| 1 | Boot stack, three distinct non-inlined real C caller frames | Exact corresponding return PCs in order, `owner=boot`, root `boundary` |
| 2 | Same boot chain with registry initialization magic corrupted at the no-output point after core records | Boot ownership still succeeds without borrowing registry validity; original seizure/records remain valid |
| 3 | Real primary S-mode trap with forged saved `s0` inside its 288-byte trap capture | `owner=primary`; stop at root `boundary`, never read the forged record |
| 4 | Real selected slot-one thread S-mode trap with the same forged-register shape | `owner=thread`, exact live generation and slot; same boundary exclusion |
| 5 | A real second trap after the first complete capture, before first dispatch | Original nested-trap panic on the installed emergency stack; `owner=emergency`, root `boundary` |
| 6 | Boot fixture supplies a null outer predecessor | The real report frames remain; stop `end`, with no invented extra frame |
| 7 | First synthetic outer successor is greater than the current frame but misaligned | Emit the current valid record, stop `invalid-frame`, no successor read |
| 8 | Nonzero successor names the user window below the admitted kernel stack | Emit current PC, stop `nonmonotonic` before range validation, no user-memory read |
| 9 | Successor equals its current frame | Emit current PC, stop `nonmonotonic`, no loop |
| 10 | Synthetic record has zero saved PC | Stop `invalid-pc` without emitting that record |
| 11 | Synthetic record has a nonzero PC in the user window | Same PC rejection without treating executable user memory as kernel text |
| 12 | More than 32 valid ascending records remain | Exactly 32 rows, `limit`, 64 completed word reads, no 33rd pair |
| 13 | Slot-one trap; current-thread generation becomes stale at the fixture point | `owner=unavailable`, `stop=owner`, zero trace word reads |
| 14 | Slot-one trap; attached/selected bounds are changed to the other slot at the fixture point | Same rejection; another live slot cannot authorize this stack |
| 15 | One actual load fault inside the first trace, before its first completed word | One trace entry, no end/second trace, no completed trace-word hit, one admitted failure reset |
| 16 | Explicit second `MICROS_PANIC` after the trace header and before its first load | Same first-claim cutoff; retain both actual panic capture frames |
| 17 | THRE forced stuck only after the trace header | Exactly 100,000 reads for that failed physical write; no unready store, completed trace or further reset attempt |
| 18 | Ordinary shutdown/no-reason without a panic | No report, trace or failure-reset claim; unchanged successful reset tuple |

Synthetic records are test-owned data constructed on the real boot
stack by one fixed-frame assembly fixture. They are malformed/boundary
inputs, not evidence that an invented chain is a normal C backtrace.
Case 1 separately requires actual linked C calls and return addresses.
The synthetic constructor's fixed reservation and every live production
frame remain included in stack bounds.

For successor-error cases, the current synthetic record has a valid PC
and lies strictly below the authenticated root, so a preceding PC or
root-boundary result cannot hide the intended successor check.
Case 8's user-window successor is necessarily lower than that current
kernel frame; `nonmonotonic` is the first applicable failure under the
unchanged cursor order. This explicit expectation is the clarification
defined by ADR-0097, not a reordered algorithm or a new scenario.

For cases 3/4, set the saved predecessor to a record-shaped address
inside the actual trap capture and arrange the relevant saved GPRs as
a plausible kernel PC/link pair. The host checks that neither word of
that pair is consumed. Merely choosing an obviously unmapped or
unaligned pointer is not the entry-epoch regression.

Case 5's illegal instruction is only in the new test configuration;
the existing nested-trap workflow and its first-store test retain their
original injection. The trace fixture uses the ordinary installed
emergency allocation, not the existing nested-test replacement stack.

Case 15 injects an actual bad load in the private load helper under the
test flag after successful production address admission. The test
declares that deliberate exception to the usual admitted address; it
does not redefine an invalid value as a valid production read.
The real trap/latched-panic path, not a fabricated callback result,
must end the VM. Cases 16/17 arm their fault only after the actual trace
header so they exercise the trace continuation rather than an earlier
report failure.

Every current case must reach the actual SRST instruction and exit
naturally through firmware with status zero. Case 18 uses `(0,0)`;
all other cases use `(0,1)`. A timeout, host termination, incomplete
case, malformed record, observer error or compiler failure is an
execution error, not success or a qualifying baseline divergence.

## Guest observation ABI

One eight-byte-aligned, 192-byte test object is named
`micros_panic_trace_test_observation`. All multi-byte fields are
little-endian; reserved fields are always zero.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | version, exactly 1 |
| 4 | 4 | scenario, exactly 1 through 18 |
| 8 | 4 | phase |
| 12 | 4 | work status |
| 16 | 8 | first-report body entries |
| 24 | 8 | actual seizure entries |
| 32 | 8 | trace entries |
| 40 | 8 | completed traces |
| 48 | 8 | completed trace-word loads |
| 56 | 8 | accepted cursor advances |
| 64 | 8 | reset-boundary observations |
| 72 | 8 | first captured pre-panic SP |
| 80 | 8 | first trace's actual SP |
| 88 | 8 | first trace's frame pointer |
| 96 | 8 | admitted allocation bottom, or zero |
| 104 | 8 | admitted root CFA, or zero |
| 112 | 4 | selected owner |
| 116 | 4 | terminal stop code |
| 120 | 4 | emitted frame count |
| 124 | 4 | reserved |
| 128 | 8 | trace-entry `sstatus` |
| 136 | 8 | trace-entry `sie` |
| 144 | 8 | trace-entry `sscratch` |
| 152 | 8 | reset-boundary `sstatus` |
| 160 | 8 | reset-boundary `sie` |
| 168 | 8 | reset-boundary `sscratch` |
| 176 | 4 | reset type |
| 180 | 4 | reset reason |
| 184 | 8 | reserved |

Phases are `SETUP=0`, `ARMED=1`, `TRACE=2`, `RESET=3`, `ERROR=4`.
Owner codes are `UNAVAILABLE=0`, `BOOT=1`, `PRIMARY=2`, `THREAD=3`,
`EMERGENCY=4`. Stop codes are `ACTIVE=0`, `END=1`, `BOUNDARY=2`,
`LIMIT=3`, `OWNER=4`, `INVALID_FRAME=5`, `INVALID_PC=6`,
`NONMONOTONIC=7`.

The counters start at zero. Work status starts at `UINT32_MAX` and
becomes the real finish result at the reset observation. The unchanged
3,056-byte EV02 snapshot is a separate statically owned object named
`micros_panic_trace_test_work_snapshot`. A snapshot call must be bound
to that object and its actual extent, not the 192-byte observation.

Trace entry records the actual coordinates and CSR values once.
The before-first-read stop is reached only after those writes and the
owner/cursor decision and after the trace header was physically emitted.
A completed trace records count/stop only after
the end record was physically emitted. A UART failure while emitting
the end record cannot publish successful completion.

The reset hook runs after shared SRST admission, takes the real work
snapshot and records the real reset tuple/CSRs before the actual
instruction. It does not claim reset or change the selected arguments.
SIE/SEIE must remain clear. A repeated report cannot overwrite the
first-entry coordinates or manufacture a completed trace.

## Closed target controller

The new controller exposes only ordinary current execution and
`--baseline`. It accepts no arbitrary checkout, executable, source,
QEMU path, case list, timeout, stack range, address or debugger command.
All inputs come from the current clean staged candidate and the fixed
Accepted design.

Retain the PLAT02 private Unix listener/reverse QEMU connection model,
packet checksum/acknowledgment validation, bounded reads, actual ELF
instruction binding, owned process cleanup and absolute deadline.
Pure packet framing may be reused without broadening PLAT02's `Remote`
policy or observation schema. No generic debugger framework is needed.

The closed register inventory is SP, `s0`, a0, a1, a6, a7 and PC.
The only write is the four-byte test selector once at `kernel_main`.
No code, register, CSR, frame, metadata or MMIO write is admitted.
There are no debugger MMIO reads.

Hardware breakpoints are limited to actual bound instruction addresses
for kernel entry, fixture armed/before-read/error stops, the shared
SRST `ecall`, and the second public panic entry for cases 15/16.
Only the selected case's declared stops may be armed.

Read-only memory access is limited to the exact typed observation and
snapshot, the four-byte existing panic-control object, the fixture's
declared inputs, and one expected linked physical stack allocation at
the before-read stop. The stack is selected independently from the
case and actual stopped SP; a reported bound never authorizes a read.
Thread cases admit exactly slot one, not the whole thread bank.
Read chunks are at most 1,024 bytes.

For a complete trace, an independent host model walks the captured
stack bytes with the fixed RV64/owner/root rules. Compare the exact
PC/frame sequence, stop reason, count and primitive work to actual
serial and guest observations. Derive required case-1 return sites
from the actual immutable fixture call instructions, not by accepting
the candidate's printed PCs as an expected list.

For cases 15-17, require the exact actual interruption witness, first
report/trace counts, incomplete completion state, bounded work and
actual one-reset exit. A missing end record alone does not establish
correct fault containment.

The baseline has typed absence of new trace functions/objects, not
fake implementations or a fabricated successful empty capture.
Its immutable fixture observation and final work snapshot still exist;
trace-specific fields remain their genuine initial values.

## Fixed baseline and independent authority

Baseline execution compiles runtime commit
`e12b2d6c163ac92282d380eff5be419fad7f7a30`/tree
`53d900f539d0e766b2998ff57aeea990ac25e30a`. Apply only the declared
test/observation overlays and target fixture integration. Do not copy
the new production trace owners into that archive or substitute empty
stubs. Baseline kernel compiler policy remains its fixed original
policy; test-only call-chain retention can be specified on the
immutable fixture alone.

Every altered original file inverses to its exact pinned Git blob and
mode, including the complete recipe. The current recipe's three
kernel frame-chain options are an independently declared production
addition, not an excuse to rebaseline the old recipe expectation.

Before any production edit, freeze all new fixtures, models,
source/recipe/effect declarations, controller and actual tool/firmware
inputs. Execute all eighteen baseline VMs. The original reports,
histories and natural reset outcomes must pass their existing
requirements, while the independent trace properties fail because
the trace mechanism is actually absent. Case 18 is a positive control.
Do not let unavailable trace stops become a protocol timeout.

The four named baseline oracles are:

| ID | Independent required property |
| --- | --- |
| `panic-trace-current-chain` | Actual current C caller PCs and streamed bounded sequence |
| `panic-trace-owned-epoch` | Exact boot/primary/thread/emergency selection, generation and root-boundary confinement |
| `panic-trace-frame-work` | Native/target malformed/depth outcomes, exact actual word reads and accepted advances, constant auxiliary storage |
| `panic-trace-terminal-precedence` | One first trace and failure-reset owner across actual fault, explicit reentry and stuck UART |

Each must demonstrate its own expected failure on the real old runtime,
not merely inherit an aggregate exit status. Missing output is a
qualifying divergence only after the old execution completed, the
original acceptance passed and all expected fixture/absence identities
were authenticated. Baseline exit 1 denotes all required expected
failures; current exit 0 denotes all cases/properties passing; every
execution/closure/internal failure uses exit 2.

Current execution uses the same frozen tests/controller and a fresh
private configure/clean-first build. The first-commit author comparison
requires exact immutable source, actual tool and firmware manifests
before and after both modes. Any frozen-input change requires renewed
baseline evidence. A later ordinary current workflow has no dependency
on the author's old local baseline log.

The later [current-scope study](../research/panic-trace-current-scope-blocker.md)
found that the implemented ordinary path nevertheless enforces the original
whole-repository membership and noncandidate production freeze.
[Accepted ADR-0099](../adr/0099-panic-trace-execution-scopes.md) and its
[execution-scope contract](panic-trace-execution-scopes.md) isolate the
correction: historical author/baseline checks remain exact, while ordinary
current execution binds every current input for the complete run.
This proposal is not an implemented relaxation or a renewed baseline pass.

### Nine new facts, no retirement

Before source-owner or extractor edits, independently declare exactly
nine facts in one `panic-trace-baseline` family:

| Owner | Fact class and scope |
| --- | --- |
| `panic_trace_evidence.py` | One fixed merged-design/runtime/MINIX/psABI anchor, transport identity |
| `panic_trace_evidence.py` | The four separately named baseline oracles above |
| `panic_trace_source.py` | Exact six-candidate/eight-evidence-owner membership, permanent invariant |
| `panic_trace_source.py` | Recipe, frame/layout, observation and output contracts, artifact contract |
| `panic_trace_source.py` | Owner/epoch admission and complete finite effect register, permanent invariant |
| `panic_trace_source.py` | Fixed source/annotation/recipe additions and exact historical inverses, transport identity |

The result is 3,938 facts in fourteen families. Preserve every complete
predecessor fact, all thirteen predecessor fingerprints and both prior
claims. Preserve PLAT02's nine literals and every still-active
assertion; the new adapter runs before its historical source view.
The new module is a dependency leaf so composing the inverses does not
introduce an import cycle.

No new retirement claim or author transition journal is introduced.
The panic whole-source key was already retired by PLAT01; that does
not retire any other identity or its permanent invariants. Test-only
guard/overlay changes must restore complete original source/ASTs.
Discovery of another invalidated active identity blocks implementation
until a separate corrective design is reviewed and merged.

## Finite source/object/linked proof

Extend the current fatal certificate to version 4 only to add the
closed trace evidence register. Retain the same four UART/latch
qualifiers and exactly the same four infeasible sites. No additional
panic edge is excluded and no qualification releases a frame.

The required trace register binds:

| Site/owner | Required independent proof |
| --- | --- |
| New call in `panic_report_claimed` | First claim and actual console seizure dominate the call; correct original-record/history placement |
| Frame-register assembly helper | Exact `s0` return, no frame/memory/call/CSR effect; actual caller result custody |
| Word-load assembly helper | Exactly one admitted eight-byte load and return; exact operand binding at both ordered caller sites |
| Portable owner/cursor helpers | Fixed scalar storage, declared phase/error/progress order, no address dereference or protected-state mutation |
| Target owner adapter | Exact fixed registry and linker-symbol custody, selected handle/generation bounds, no full validator or slot scan |
| Existing selected resolvers | Fixed source and actual complete effects, output stores only to the proved caller-owned local cells |
| Read loop | Admission dominates both loads; addresses remain in the one epoch; root/depth/error checks prevent a further pair |
| Formatter | Only declared UART calls and fixed input/string reads; no history insertion, reset claim or recursive formatter |
| Trace test hooks and EV02 | Exact test-owned extents/arguments and post-primitive observations; no protected-state alias or borrowed snapshot buffer |
| Reset test observer | Actual tuple and snapshot binding after the retained shared admission; unchanged other tuples and firmware return |
| Compiler hooks | Existing zero-stack/register-preserving observer contract in the retained observed images |

Case 15 additionally binds the actual test-only invalid-load instruction
and its real trap-entry stack pivot. The already latched control state
survives that hardware transition without another first-body admission.
Retain the live interrupted frames in their original epoch and the
new capture/handler frames in the destination epoch. Case 16 has no
such pivot and must retain both panic captures on the same live stack.
Neither test receives a fictitious frame release or a generic
exception-edge exclusion.

Every new transitive call in a qualified path must have complete
source, actual object and linked evidence. Unknown effects remain
failures. Neither a source token match, a function name, a passing
serial record nor a marker-based graph rewrite substitutes for actual
load/address/branch/argument evidence.

Both original 48-byte panic captures, every report frame, trap capture,
synthetic fixture reservation and read/formatter continuation remain in
stack composition. The complete 44/113 static matrix and original ten
observed scopes must pass unchanged 4 KiB/16 KiB budgets.

## Acceptance and phase barrier

After the Accepted design and its link-order clarification separately
merge, implementation proceeds in this
order:

1. independently freeze the new facts and complete immutable evidence
   inputs while preserving every old authority;
2. demonstrate portable owner/frame/work regressions and the real
   fixed-runtime trace Red;
3. implement the six production candidates and exact recipe additions;
4. pass native valid/malformed/overflow/guard cases and all eighteen
   actual current target cases with matching immutable inputs;
5. preserve original core/trap/history/terminal assertions and complete
   version-four source/object/linked effects;
6. run the complete fresh author plan, now 50 commands, including
   44 configurations/113 ELF outputs and all ten observed scopes;
7. provide B01-B12/B14 affected-unit evidence from that complete plan,
   retaining B13-E007 as `BLOCKED`;
8. obtain independent implementation review before the first permanent
   commit, verify committed/remote/squash tree equality and ordinary
   journal-free prior authority, then merge and clean only the task refs.

The separately merged design phases ran only documentation/structure and
three-layer diff checks. Their acceptance did not claim a trace, target
pass, source-authority transition or new stack certificate. Implementation
evidence is produced by the [documented current commands](../development/building.md#panic-trace-baseline-and-current-evidence);
development-only static pairs do not replace the complete gate above.

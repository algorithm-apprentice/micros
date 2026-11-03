# ADR-0096: Bounded Kernel Panic Stack Trace

- Status: Accepted
- Date: 2026-11-03
- Refines: ADR-0014, ADR-0015, ADR-0019, ADR-0022, ADR-0080, ADR-0082,
  ADR-0083, ADR-0084, ADR-0086, and ADR-0095

## Context

PLAT02 implementation #201 is independently reviewed and merged with
identical author-reviewed, published and squash trees. It supplies
bounded UART waits, separate first-entry/reset ownership and terminal
console repair. It closes B01-F001/F003/F005, not the absent panic trace.

PLAT03 is the next preferred dependency-ready correction and owns
B01-F004/B01-E004. The
[fixed-source study](../research/minix-panic-stack-trace.md) traces the
MINIX current-kernel frame-chain algorithm, raw PC output, null/progress
termination and `O(s)` time/`O(1)` storage.

RV64 uses negative CFA offsets and optional `s0` frame records. More
importantly, `micros` has boot, primary, thread and emergency stack
epochs. A user-supplied `s0` may numerically point into its saved kernel
trap frame. A broad kernel-RAM or whole-stack bounds check is therefore
insufficient authority for following the link at a raw assembly entry.

## Decision

After this Accepted design separately merges, implement the
[bounded trace contract](../architecture/panic-stack-trace.md).

### Preserve the existing failure path

The trace belongs only to `panic_report_claimed`, after the original core
and optional trap records and before diagnostic history. Keep PLAT02's
first claim, one failure-reset request and no-console termination.
Preserve all original capture/context ABIs and returning-SRST fallback.

No repeated entry traces again. UART exhaustion terminates rather than
printing another failure through the same dependency.

### Reproduce a bounded frame-chain walk

Walk the current kernel continuation's psABI records, not an arbitrary
process or interrupted user stack. Stream each valid raw PC before
evaluating its successor, preserving the reference's ordering.

Use at most 32 records and fixed cursor storage. Admit each two-word
read before touching memory, require aligned monotonic progress and
kernel-text PCs, and report a distinct terminal status. There is no
array of previous frames, recursion, allocation, symbolization, DWARF
engine, pointer scanning or recovery framework.

Kernel C compilation explicitly retains frame pointers, including leaf
frames, and disables sibling-call elimination. The actual relevant
prologues, return sites and private load helper must be bound to the
compiler/object/linked evidence; source flags alone are insufficient.
Other target/native compiler policy is unchanged.

### Authenticate one stack and its entry boundary

Use actual SP, `sscratch`, immutable linker bounds and the existing
selected registry/hart/thread/process generation relations. Do not use
panic arguments, a saved user pointer or caller-supplied ranges as
authority. Owner lookup must not scan the registry or call a complete
validator.

After emitting the authenticated root C frame, stop without following
its inherited predecessor. A primary/thread trap root ends immediately
below the fixed 288-byte trap capture. Boot, idle and emergency roots
likewise end at their actual assembly-entry boundary.

This prevents a numerically in-range user `s0` from turning saved trap
registers into a kernel call chain without changing register restoration,
the original panic capture or scheduling assembly.

### Fail explicitly without manufacturing recovery

Owner failure or malformed chain emits its defined trace-stop record,
then follows the unchanged panic tail. It does not repair metadata or
resume execution. An actual synchronous read fault uses the existing
latched panic/reset path; wired trap-entry/emergency-stack invariants
remain prerequisites, not newly recoverable corruption.

### Preserve prior evidence

Keep all 3,929 predecessor facts, thirteen fingerprints and both prior
retirement claims. No new retirement is assumed. The exact new
source/recipe and machine-effect additions require independent
declarations before implementation owner/extractor edits.

Existing PLAT02 counters, twelve actual terminal cases, original
diagnostic history, full workflow set and all stack budgets remain
required. New trace work must not silently enter an old EV02 count
vector or disappear from actual physical UART accounting.

## Non-goals

No user/process stack trace, cross-epoch unwind, debug server, arbitrary
memory-read API, symbol table, crash dump, general exception recovery,
SMP, runtime enable/depth option, ordinary TTY change, hosted CI waiver,
other MINIX correction or node-Q implementation.

## Phase and acceptance

This Accepted design is documentation only. The paired implementation
todo remains pending. The canonical contract fixes eighteen real target
cases, a 192-byte observation ABI, four separately discriminating
oracles, nine independently declared new facts and the complete
version-four finite effect register.

The new trace markers use a separate `MICROS_TRACE_*` namespace so
the original exact five-record panic classifier remains unchanged.
The full implementation gate expands to 50 fresh commands, 44 static
configurations and 113 ELF outputs while retaining the same ten
observed scopes and blocked B13 admission.

Implementation must start with independent portable and real target Red,
then prove actual normal/malformed/fault/recursive outcomes, bounded
word-load work and storage, unchanged historical authorities, complete
current stack evidence, the full fresh author plan and independent
implementation review before its first permanent commit.

## Design review disposition

Independent read-only GPT-6.1 Sol review inspected the complete nine-file
design, fixed MINIX/RV64 references, current ownership/capture/source
boundaries and exact author documentation evidence. It reported no
substantive design or acceptance issue and no optional preference.
No targeted reproduction or routine validation rerun was needed.

Acceptance records that clean design result. It does not claim that the
new trace owners, target fixture/controller, compiler policy, nine-fact
family or expanded certificate exist. The dependent implementation
remains pending until this design is separately merged.

## Alternatives

- Reuse x86/ARM word widths and offsets: violates RV64 LP64.
- Trust any address in kernel RAM or a stack bank: loses exact owner and
  entry-epoch authority.
- Clear user `s0` on trap entry: unnecessary ABI/assembly expansion when
  the authenticated root can stop the walk.
- Rewrite the original panic capture to include `s0`: unnecessary;
  the first claimed reporter can obtain its current frame register
  without changing the original 48-byte ABI.
- Use a full unwinder or symbolize addresses: not the reference
  mechanism and not needed for this correction.
- Add a frame vector, recursive walker or larger stack budget: obscures
  the reference's constant auxiliary-storage property.

## References

- [Canonical trace contract](../architecture/panic-stack-trace.md)
- [Fixed MINIX and RV64 study](../research/minix-panic-stack-trace.md)
- [Terminal-failure prerequisite](../architecture/terminal-failure.md)
- [B01 findings](../research/minix-complete-baseline-b01-platform.md)
- [Sequential correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)

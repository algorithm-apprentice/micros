# Runtime Memory Primitives

## Status and scope

This Accepted COPY01 contract follows merged EV04 #189 and
[ADR-0090](../adr/0090-minix-word-based-runtime-memory.md).
It owns only B05-F006 and its runtime work-evidence row B05-E005.
The [fixed MINIX ledger](../research/minix-runtime-memory-primitives.md)
defines the selected generic word family and all required adaptations.

Independent design review is clean; implementation remains pending separate
design merge.
Nothing in this document claims the future tests, driver or transition record
already exists.

## Stable interfaces and preconditions

Keep `lib/runtime/memory.h`, `memory_symbols.c`, the prefixed function names
and their exact signatures. The standard symbols remain thin forwarders; no
additional compiler-support or public memory symbol is introduced.

For nonzero length, the caller provides accessible source bytes and writable
destination bytes for the complete range. Copy ranges do not overlap, in
accordance with the existing `restrict` contract. The functions neither
validate virtual mappings nor acquire cross-process pointer authority.

Length zero returns the original destination without a memory access,
alignment assumption or pointer arithmetic, including the existing prefixed
functions' null-pointer/no-access case. Fill converts its integer argument
to one unsigned byte. Every defined call returns the original destination.

Only requested bytes may be read or written. No aligned-down read, trailing
word overread, read-modify-write outside the range, speculative prefetch,
MMIO semantics, error result or fault-recovery path is admitted.

## Copy algorithm

The copy word width is eight bytes, matching the generic LP64 `long` word.
There is no retained state across calls.

1. Return immediately for zero length.
2. If the source and destination residues modulo eight differ, copy exactly
   the requested bytes through the byte path.
3. Otherwise, copy the byte prefix needed to reach their common aligned
   boundary, bounded by the remaining length.
4. Transfer complete eight-byte words while at least eight bytes remain.
5. Copy the remaining bytes.

Traversal remains forward in the existing nonoverlap domain. Pointer-to-
integer conversion is used only for low alignment bits, not for authority,
range validation or cross-object ordering. Length and cursors advance by
the actual primitive width, never by unchecked end-address construction.

## Fill algorithm

The fill word width is four bytes, matching the generic `u_int` word.

1. Return immediately for zero length.
2. Convert the fill value to one unsigned byte.
3. For lengths below twelve, write exactly those bytes and return.
4. Replicate the byte into one aligned `uint32_t` scalar with the reference
   shift/OR construction, once per call.
5. Write the byte prefix needed to align the destination to four bytes.
6. Transfer the pattern in complete four-byte words.
7. Write the remaining bytes.

No eight-byte fill optimization, multiword unroll, vector or size-mode
algorithm variant is added in this baseline correction.

## Exact work and storage

For copy, let `h=min(n, (8 - destination_residue) mod 8)`, when the residues
match; let `q=(n-h)//8` and `r=(n-h)%8`. Its exact counts are:

```text
COPY_UNIT   = h + q + r
COPIED_BYTE = n
```

When residues differ, `COPY_UNIT=n`. Zero length has zero work. These are
executed primitive counts, not a cost estimated from a caller's requested
length.

For fill below twelve bytes, `CLEAR_UNIT=n`. Otherwise use
`h=(4-destination_residue) mod 4`, `q=(n-h)//4`, and `r=(n-h)%4`:

```text
CLEAR_UNIT   = h + q + r
CLEARED_BYTE = n
```

All unrelated owners/kinds remain zero in these isolated captures.
The independent oracle computes the number of complete aligned intervals
inside the supplied range, rather than calling the production branch/loop
helper. Native byte expectations use separate storage and a reference that
does not call the prefixed implementation under test.

Best, average, worst and amortized work are linear in `n`; there is no
cross-call amortization. Retained storage is zero. Auxiliary and stack
storage are fixed scalar cursors, lengths and one four-byte pattern, `O(1)`.
There is no heap, VLA, length-sized temporary, writable production global or
recursion. Actual frame and downstream chain bounds must fit the existing
budgets; no standalone guessed frame size replaces EV03.

## Compiler and observation boundary

The only new compiler-specific primitives are:

- `__builtin_memcpy_inline` with a literal size of eight for copy or four
  for the fill pattern;
- `__builtin_assume_aligned` with the corresponding literal alignment,
  only after the actual address and remaining-length guards; and
- compile-time feature checks rejecting an unsupported compiler.

The fill pattern has explicit four-byte alignment. No arbitrary object is
accessed through a cast wide lvalue. These builtins preserve memcpy-style
object-representation semantics and cannot lower the primitive to an
external call. Genuine byte paths retain volatile character accesses so
their loops cannot become recursive memory-library calls.

Do not change existing `-fno-builtin`, optimization, ISA, ABI, linker,
stack-observer or image-policy options. Target evidence covers both the
ordinary debug service mode and existing `-Os` application mode, plus their
metadata/observed forms. Generated IR must bind constant width/alignment,
and actual RV64 object/linked evidence must identify the corresponding
non-stack load/store transfers, not merely count arbitrary stack spills or
find an unrelated load opcode. Recursion, an unexpected memory helper or
unsupported lowering fails.

Use the existing `MICROS_WORK_OWNER_RUNTIME` and these source-local
annotations:

- byte copy: one `COPY_UNIT`, one `COPIED_BYTE`;
- word copy: one `COPY_UNIT`, eight `COPIED_BYTE`;
- byte fill: one `CLEAR_UNIT`, one `CLEARED_BYTE`;
- word fill: one `CLEAR_UNIT`, four `CLEARED_BYTE`.

The primitive executes before its matching observations. Sites bind the
actual statement, enclosing loop/branch and stage using EV02. Each site has
literal fixed units; a caller does not report inclusive or estimated work.
Enabled compilation is private to native evidence. Ordinary production
contains neither the collector nor its state/symbols.

## Native and target evidence

Use the existing native discovery mechanism, not a new public workflow or
production compile switch. A task-owned C fixture links the real prefixed
bodies and, only in its enabled variant, the existing EV02 collector.
The existing `runtime_test` cases remain unchanged and continue to run.

The future owners are `tests/host/runtime_memory_fixture.c`,
`runtime_memory_evidence.py`, `runtime_memory_authority.py` and
`test_runtime_memory.py`, plus the author entry point below. Python discovery
builds the fixture in private temporary storage; no CMake or preset change
is needed. The native compiler is the existing host-debug cache's selected
absolute `CMAKE_C_COMPILER`; missing configuration fails with the documented
`cmake --preset host-debug` prerequisite. RV64 checks use the existing
explicit `MICROS_CLANG`/`MICROS_LD_LLD` selection. Both compiler identities,
versions and effective flags are retained, and the required builtins must be
supported. Native semantic runs retain warnings-as-errors and ASan/UBSan.

The native matrix covers:

- all 64 copy residue pairs and all four fill residues;
- zero and every short size through the first word/threshold boundaries;
- word-adjacent, 63/64/65, 255/256/257 and 4095/4096-byte cases;
- representative larger aggregate transfers without automatic fixture arrays;
- original destination, signed/oversized fill conversion and exact bytes;
- disjoint ranges in both address orders, without adding overlap semantics;
- typed objects and alignment/range guards under sanitizers;
- canaries and protected-page boundaries, including zero-length no access;
- identical enabled/disabled semantics and default observer absence; and
- complete capture identity, count shape and source-site admission.

Two independently declared replacement oracles must discriminate against the
old byte algorithm: aligned 64-byte copy requires eight units, and aligned
64-byte fill requires sixteen units. Old bodies produce 64 units for each.
Build failure, malformed output, a skipped case or an incidental semantic
failure is not that expected Red.

Actual IR/object checks and the unchanged runtime/service/application QEMU
gates complement native evidence. They do not replace full stack closure.
Wall-clock speedup is not an oracle and no hosted timeout is waived.

## Exact authority transition

Use transition `COPY01-runtime-memory`, owner task `COPY01`, primary unit
`B05`, finding set exactly `{B05-F006}`. The sole planned retirement is:

```text
tests/host/test_trap_syscall_source_layout.py:data-entry:STABLE_HASHES/lib/runtime/memory.c
```

Keep the original `STABLE_HASHES` entry and its reviewed value and source
pin. The other stable artifacts, source owners, ABI, recipes, structural
checks and all unrelated inventory facts remain active.
The affected and planned-retirement sets contain exactly that one key;
the plan's retained subset is empty. Every fact outside the affected set
remains active, including the other 22 entries in the stable-artifact table.

Replacement oracle IDs are:

```text
copy01-copy-aligned-word-work
copy01-fill-aligned-word-work
```

Their fixed test IDs are
`tests.host.test_runtime_memory.RuntimeMemoryWorkTest.test_copy_aligned_work`
and
`tests.host.test_runtime_memory.RuntimeMemoryWorkTest.test_fill_aligned_work`.
Each actual execution reports every registered oracle separately, together
with complete source-local EV02 captures. An aggregate exit code alone is
insufficient. The ordinary host suite also tests the larger boundary,
semantic, access and negative matrix without treating already-green semantic
cases as the discriminating Red oracles.

Both replace that exact file identity and derive their properties from the
fixed generic MINIX trace plus this Accepted adaptation. Their code, model,
input geometry, source admissions and command declarations are frozen before
Red. They are not reconstructed from the corrected production output.

The driver loads task/command authority from the separately merged Accepted
design, with its exact commit/tree/document. Red's candidate is that pinned
pre-correction source. It compiles the original project-owned memory body
from Git into private temporary evidence storage, adding only declared
test-only observations. Projection must recover its original exact bytes
and hash; it cannot substitute a hand-written byte-loop model.

Green compiles the actual clean staged implementation. Candidate projections
include the affected identity owner, production payload and declared
affected-unit inputs. Immutable oracle/authority inputs remain disjoint and
unchanged. All core ADR-0082 event ordering, coverage, projections and digest
checks apply without a new state or relaxation.

### Closed input and declaration binding

The only mutable candidate paths during a Red-to-Green attempt are:

```text
lib/runtime/memory.c
tests/host/test_trap_syscall_source_layout.py
```

The first is the corrected algorithm; the second owns its narrow assertion
adapter and retained structural checks. The task authority declares these
two paths for each affected unit's candidate projection. All other execution
dependencies are covered by the immutable envelope, not omitted because the
unit map is small.

Materialize that envelope from the merged design tree's complete tracked
file inventory, plus exactly the five prospective implementation files named
above, excluding the two candidate paths. It is a closed path set, not an
open glob: missing, extra, linked or untracked inputs fail. Existing ownership
registration and implementation documentation must therefore be finalized
before the recorded Red; changing them later requires a fresh attempt.
Current oracle/fixture/driver bytes are frozen at Red. Accepted authority
documents are read from their pinned merged Git objects, not reconstructed
from a mutable design claim.

Selected compiler/linker/runtime identities, effective compile flags, and the
fixed native cache inputs form separately named immutable external inputs.
No environment value or arbitrary host file is accepted as command/source
authority. Generated native fixtures, phase logs and journals are outputs
under the fixed private namespace and do not become expected-value inputs.

The external task authority is materialized from this merged contract:
COPY01/B05/B05-F006, the one affected key, both exact oracle/test IDs,
the command table and the complete recertification-unit set. Its base
HEAD/tree are the design merge's actual Git objects. The implementation
cannot choose a different base or redefine these fields to match itself.

Add one closed `runtime-memory-baseline` authority family owned by
`tests/host/runtime_memory_authority.py`. Its finite declaration comprises
the merged-design anchor and the two independently defined replacement
oracles. Design anchoring is transport authority; the two work properties
are baseline-oracle authority. They are derived from the fixed merged design,
not observed candidate counts. Preserve every previous family's individual
facts and fingerprint; the old memory identity stays in the inventory even
when its assertion is retired. Ordinary measured record hashes are execution
data, not additional historical-source identities.

## Author transaction and ordinary assertions

The task-specific author entry point is prospective
`tools/check_runtime_memory.py`. Its only recording intents are:

```text
<MICROS_PYTHON> -E -S -B tools/check_runtime_memory.py --red
<MICROS_PYTHON> -E -S -B tools/check_runtime_memory.py --green
<MICROS_PYTHON> -E -S -B tools/check_runtime_memory.py --recertify
```

The selected interpreter is resolved and checked through the existing runtime
owner. There is no arbitrary command, source directory, finding, retirement
set, input artifact, expected outcome or success-file option.

Each attempt has fixed project-owned data/log storage below
`build/evidence-transitions/COPY01/`. An OS-backed lock excludes concurrent
writers. Actual compile, oracle and unit outcomes produce strict records;
the synthetic EV01 event constructors never certify a real transition.
Source/tool/input identities are checked around execution and publication.
Earlier events are immutable. Red-to-Green production changes are expected;
an oracle change, or a candidate change after recorded Green, starts a new
explicit attempt rather than rewriting the old history. Beginning an attempt
invalidates earlier current-success selection before execution.

The complete Green oracle set is rerun inside recertification. Only after
actual Green and all source/plan checks may that transaction provisionally
suppress the single planned file identity while executing retained
invariants. That scope is process-local, derived from the validated plan,
not an environment flag or caller-supplied mask. It is cleared on every exit.
No ordinary gate can borrow it.

Affected units and their fixed actual commands are:

| Unit | Canonical recertification command |
| --- | --- |
| B05 | `cmake --workflow --preset test-qemu-user-runtime` |
| B06 | `cmake --workflow --preset test-qemu-bootstrap-launcher` |
| B07 | `cmake --workflow --preset test-qemu-vm-scratch-preemption` |
| B08 | `cmake --workflow --preset test-qemu-pm-service` |
| B09 | `cmake --workflow --preset test-qemu-tty` |
| B10 | `cmake --workflow --preset test-qemu-ramfs` |
| B11 | `cmake --workflow --preset test-qemu-vfs` |
| B12 | `cmake --workflow --preset test-qemu-elf-spawn` |
| B14 | The actual `--recertify` driver invocation above |

B14's command owns the in-process retained structural/authority checks and
replayed replacement oracles; it does not claim those Python checks were
separate subprocess argv. Each other unit records the real fixed subprocess,
exit status, complete log and matching candidate/input projection.
Every required unit must pass before atomic `AUTHOR_RECERTIFIED` publication.
Failure or interruption publishes no new terminal event or usable retirement.

The assertion adapter distinguishes prior committed registry authority from
a newly staged claim. A new claim is not ordinary retirement merely because
its planned key appears in data: before the first reviewed commit it needs
the valid current-candidate terminal journal, or the original assertion
remains active. The author transaction alone has the provisional exception.
After the reviewed outcome is committed, its unchanged committed registry
entry supplies prior retirement to ADR-0082's ordinary no-plan mode.
It does not relabel old events as current executions or require their
temporary author journal in every later checkout.

Committed provenance is read from the exact HEAD Git blob, not inferred from
a staged flag or an event's claimed state. The local terminal journal stays
outside the tracked source tree, avoiding a candidate-tree/self-reference
cycle. The first permanent commit may include the completed registry claim
only after the actual transition, post-terminal author gate and independent
review are clean. This is the repository's existing trusted reviewed-source
boundary, not a cryptographic substitute for review or a way to approve an
unreviewed commit.

Independent review verifies the actual journal and exact terminal candidate
before the first permanent commit. The resulting source registry is trusted
reviewed project content, not an imported local/remote success artifact.
Future edits still run the independent replacement oracle and retained
invariants; they do not restore the retired byte-loop authority.

Required negatives include premature/staged-only retirement, changed
oracle/primitive placement, false/missing Red, compiler failure presented as
Red, changed Green source, incomplete unit coverage, altered log/record,
wrong tree or command, failed post-publication recheck, and attempted
suppression of any other stable artifact. A fresh ordinary checkout must
retain the completed retirement without manufacturing execution evidence.

## Bounded implementation owners

After design merge, implementation owns only:

- the two bodies and their passive annotations in `lib/runtime/memory.c`;
- task-specific native fixtures, tests, source admissions and compiler/artifact
  checks under `tests/host/`, using existing discovery and EV02 storage;
- a narrow runtime-memory authority adapter and author driver reusing EV01;
- the one existing stable-file assertion's transition hook, with its old
  literal and all unrelated checks preserved;
- directly required Python/authority ownership registration; and
- directly related B05/B14, build and acceptance documentation.

No generic execution framework, CMake redesign, new target gate, copied
MINIX/NetBSD code or unrelated source refactor is included. Default/native
fixture compiler selection must remain explicit and supported; unsupported
intrinsics fail rather than choosing an unproved alternative.

## Completion

After actual granular retirement, run the unchanged complete fresh author
matrix, including full native discovery, all ordinary QEMU/image workflows
and all current stack evidence. Corrected source must preserve original
markers, timeouts, image limits and B13's blocked future admission.

Only complete current-candidate author evidence and clean independent review
permit implementation merge and B05-F006/B05-E005 re-certification.
GRANT03, BOOT03, memory-validation corrections and hosted CI remain separate
outcomes with their own dependencies and acceptance.

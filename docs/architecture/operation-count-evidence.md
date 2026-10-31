# Operation-Count Evidence

## Status and goal

This is the Accepted EV02 design contract for B14-F002 under
[ADR-0083](../adr/0083-test-only-operation-count-evidence.md).
EV01 design #176 and implementation #177 are merged. Implementation of this
contract remains pending until its separate design PR merges.

Provide one portable, test-only mechanism for exact primitive-work
measurement, complete cross-owner composition, and transport-preserving
annotation admission. A correction must be able to observe the audited
divergence before changing its production algorithm.

## Non-goals

EV02 does not:

- install annotations in current production mechanisms;
- correct a runtime algorithm, ABI, lifecycle, or authority;
- retire or rebaseline a real historical identity;
- replace semantic tests, models, QEMU, or independent review;
- measure elapsed time, CPU cycles, stack peaks, or live-storage peaks;
- instrument target assembly or supply EV03 stack tooling;
- add a counter syscall, telemetry IPC, remote cache, or production registry;
- implement node Q, recovery, storage, networking, or POSIX breadth;
- build a general C interpreter or source-rewriting framework.

Fixture algorithms are test evidence only, not prototypes for later production
implementation. Peak storage is not an additive work dimension and remains
the owning design's separate proof obligation.

## Measurement vocabulary

### Source owners

Owner IDs are dense, fixed in this order, and distinct from runtime identities:

| ID | Owner | Source responsibility |
| ---: | --- | --- |
| 0 | `KERNEL_PLATFORM` | Platform/FDT/UART/PLIC/timer C mechanisms |
| 1 | `KERNEL_DIAGNOSTIC` | Kernel diagnostics and panic |
| 2 | `KERNEL_MEMORY` | Frame ownership/allocation and address-space C mechanisms |
| 3 | `KERNEL_OBJECTS` | Process/thread/hart objects |
| 4 | `KERNEL_SCHEDULER` | Scheduler mechanisms and return planning |
| 5 | `KERNEL_ENDPOINT` | Endpoint representation, profiles, and lifecycle |
| 6 | `KERNEL_IPC` | IPC queues, tokens, matching, notification, and cancellation |
| 7 | `KERNEL_GRANT` | Grant authority, planning, and checked copies |
| 8 | `KERNEL_TRAP` | C trap/syscall dispatch |
| 9 | `KERNEL_EXECUTION` | Saved execution-context mechanisms |
| 10 | `KERNEL_BOOT` | Static preparation and bootstrap control |
| 11 | `KERNEL_VM` | Privileged VM/handoff control |
| 12 | `KERNEL_PM` | Privileged PM control |
| 13 | `KERNEL_TTY` | Console and IRQ ownership mechanisms |
| 14 | `RUNTIME` | Shared freestanding runtime primitives |
| 15 | `LIB_VM` | Shared VM planning/validation primitives |
| 16 | `LIB_SPAWN` | Shared spawn encoding and layout |
| 17 | `VM_SERVICE` | VM service policy/adapters |
| 18 | `PM_SERVICE` | PM lifecycle/coordinator/adapters |
| 19 | `TTY_SERVICE` | TTY service/UART/line/output work |
| 20 | `RAMFS_SERVICE` | RAMFS state, namespace, blocks, and seed |
| 21 | `VFS_SERVICE` | VFS process, descriptor, vnode, routing, and transaction work |
| 22 | `APPLICATION_CLIENT` | Later node-Q application I/O/PM clients |
| 23 | `USER_COMMON` | Later reviewed shared user helpers |
| 24 | `INIT` | Later production init |
| 25 | `SHELL` | Later production shell |
| 26 | `COMMANDS` | Later echo/cat/ls/ps work |

The future node-Q labels do not claim that those sources or measurements
already exist. A source's actual owner and admitted primitive sites are fixed
by its owning correction design. Shared code retains its source owner;
separate producer labels distinguish its executions in different processes.

B12 composes work; B14 implements evidence. Neither is an additional primitive
owner whose inclusive total is added to its children.

### Work kinds

Kinds are dense, fixed in this order:

| ID | Kind | One recorded unit |
| ---: | --- | --- |
| 0 | `COMPARISON` | One scalar equality/relational comparison |
| 1 | `RECORD_VISIT` | One visit to a logical record defined by the owner ledger |
| 2 | `INDEX_PROBE` | One candidate/index probe |
| 3 | `LINK_TRAVERSE` | One traversed logical link |
| 4 | `BITMAP_WORD_LOAD` | One bitmap-word load used by the measured mechanism |
| 5 | `PTE_VISIT` | One page-table entry visit |
| 6 | `QUEUE_RECORD_MOVE` | One moved queue/order record |
| 7 | `COMPLETE_VALIDATOR_CALL` | One invocation at a complete validator's own entry |
| 8 | `COPY_UNIT` | One executed copy assignment or owner-defined block primitive |
| 9 | `COPIED_BYTE` | One byte actually moved by that primitive |
| 10 | `CLEAR_UNIT` | One executed clear assignment or block primitive |
| 11 | `CLEARED_BYTE` | One byte actually cleared |
| 12 | `RANGE_VISIT` | One retained/planned range visit |
| 13 | `HASH_MULTIPLY` | One executed multiplication in a hash transform |

The owner ledger specifies the primitive and granularity. For example, one
8-byte move records one `COPY_UNIT` and eight `COPIED_BYTE` units. These are
different dimensions, not two costs to add together. A caller cannot record
the callee's work again, infer units from requested bytes not actually moved,
or change granularity between Red and Green.

## C17 observation interface

The planned shared header is `include/micros/work_observation.h`.
With `MICROS_TEST_WORK_COUNTS` undefined, it exposes only annotation macros,
no collector types, functions, or state. Statements discard every argument;
comparison wrappers evaluate only their original expression once.

With the macro explicitly equal to `1`, the same header exposes the owner/kind
enums and the observation call used by the annotations. Other defined values
fail compilation. Enabled definitions are target-private, never a global
CMake option or inherited production mode.

The two admitted forms are:

```text
MICROS_TEST_WORK_HIT("site-id", OWNER, KIND, positive_decimal_units);
MICROS_TEST_WORK_COMPARE("site-id", OWNER, original_comparison)
```

Owner/kind arguments use the exact declared enum identifiers. A site ID is
plain ASCII matching `[a-z][a-z0-9-]{0,63}` with no escapes. Statement units
are exactly `1` for event-count kinds. Only `COPIED_BYTE` and `CLEARED_BYTE`
may use a canonical decimal literal in `1..4096`, matching the actual fixed
width of the observed primitive. No variable, call, macro, assignment,
increment, or pointer read supplies observation arguments.
`COMPARE` records one `COMPARISON` and returns the original result.

An admitted comparison contains exactly one scalar equality/relational
operator and no logical conjunction/disjunction, conditional, assignment,
increment/decrement, or comma operator. Calls and memory reads in its
unchanged operands retain their original single evaluation. Observation
constructs cannot nest.

The private test API under `tests/work/` provides:

```text
micros_test_work_begin(uint64_t producer_label,
                       uint64_t capture_label,
                       uint32_t owner_mask) -> micros_test_work_status
micros_test_work_record(micros_work_owner owner,
                        micros_work_kind kind,
                        uint64_t units) -> void
micros_test_work_finish(micros_test_work_snapshot *output)
    -> micros_test_work_status
```

`record` is also directly usable by the collector's native fault tests;
production annotation sites use only the two admitted forms above.
The observer never calls production helpers or changes production state.

Statuses are exactly `OK=0`, `ARGUMENT=1`, `STATE=2`, `OWNER=3`, `KIND=4`,
and `OVERFLOW=5`. The private snapshot fields, in order, are:

```text
uint32_t version = 1
uint32_t reserved0 = 0
uint64_t producer_label
uint64_t capture_label
uint32_t owner_mask
uint32_t reserved1 = 0
uint64_t counts[27][14]
```

This is an internal test contract, not a packed IPC or user ABI.

### Storage and concurrency

Each executable has one private collector with a 27-by-14 vector of 64-bit
atomic counts, active/fault state, and the measurement labels/mask. The
collector and each output snapshot must individually fit one 4 KiB page.
They are BSS objects or fixture-owned resident storage, never large automatic
objects. No heap, TLS, callbacks, or dynamically sized registry is added.

64-bit counter and fault updates must be lock-free on the selected native and
RV64 compilers, with no external atomic runtime. Recording uses a constant
number of abstract atomic operations, not a promised instruction count.
Interrupting updates cannot lose increments.

Start and finish are quiescent fixture operations: no update may be in flight
or start concurrently with them. Kernel fixtures enforce that boundary;
one-thread service fixtures finish outside an active call/copy/report path.
The portable collector does not change interrupt state or claim to validate
hart/process authority.

Future enabled target variants must fit their Accepted image/stack budgets.
If instrumentation changes a guest deadline, use native work evidence plus
the unchanged target semantic gate; do not increase a timeout to manufacture
success.

## Capture state and failure

Labels are nonzero unsigned 64-bit observation data, not PIDs, generations,
endpoints, or capabilities. The owner mask is nonempty and contains only the
27 known bits. A fixture supplies the expected labels/mask independently.

### Begin

1. An already active capture returns `STATE` and latches failure on that
   capture; it cannot subsequently finish successfully.
2. Invalid labels/mask, or a capture label not strictly greater than the last
   accepted begin in that executable, return `ARGUMENT` and do not open a
   capture. Invalid begin does not consume a label.
3. A valid quiescent begin clears every count and fault, installs the labels
   and mask, consumes the capture label, and enables recording.

A later explicit begin may start another measurement after finish, but cannot
reuse a consumed capture label even when the earlier measurement failed.
There is no wrap or in-process label reset; accepting `UINT64_MAX` leaves no
later valid begin. These observation checks confer no kernel lifecycle
authority. The fixture and event record bind each measurement to its exact
command, producer, and operation, so an unchanged older output cannot stand in
for a failed newer capture.

### Record

- In the undefined compile mode, no record call or argument evaluation exists.
- An enabled but inactive collector ignores observation noise.
- During capture, an unknown/unselected owner latches `OWNER` before indexing.
- An unknown kind latches `KIND` before indexing.
- Zero units still validate the active dimensions.
- A valid update is atomic. Overflow latches `OVERFLOW`; wrapped values are
  invalid internal state and can never be exported as successful evidence.

Faults are sticky for the capture. Recording never returns a policy decision
to the instrumented algorithm.

### Finish

Finish requires quiescence and a caller-owned snapshot object.
Null output returns `ARGUMENT` and closes any active capture without output.
An inactive finish returns `STATE`. A valid finish disables recording before
reading any count. Latched faults use precedence
`STATE`, `OWNER`, `KIND`, `OVERFLOW`.

On every failure, the caller's output remains unchanged. Success publishes
the complete version-1 snapshot: labels, owner mask, zero reserved fields, and
all 27-by-14 counts, including zeros outside the mask. There is no failure
after output publication begins. Snapshot construction and observer work are
outside the measured scope.

## Hosted composition

`tests/host/work_count_evidence.py` owns the planned hosted facade, with
private modules separated by snapshot/composition and source-projection
responsibility if needed. This is not a telemetry protocol or CLI.

Composition requires an external expected capture label and a complete mapping
from producer label to expected owner mask. For the current one-hart/64-process
scope there are at most 65 producer records: one kernel execution producer and
the process producers declared by the fixture.

It accepts exactly one successful, complete snapshot for each expected
producer and no others. Every scalar is an exact unsigned 64-bit integer,
never a Boolean, float, negative number, or coercible string. Version, shape,
dimensions, labels, mask, reserved zeros, and unselected-row zeros are checked
before arithmetic.

Shared-source owners may occur in different producer records. The unique
leaf key is the producer label; the capture label binds the whole operation.
Inclusive parent aggregates are not snapshot inputs. Repeated producer
records, even with disjoint masks, are rejected.

Only after full validation does composition add each matching owner/kind cell
once, with checked unsigned arithmetic. It returns complete immutable
per-owner vectors and matching-kind totals, or an explicit error; no partial
total is usable. It does not mutate snapshots or derive expected provenance
from the observed producer list.

Expected/worst work is `O(P * O * K)` / `O(P^2 + P * O * K)` with ordinary
hosted keyed producer validation; storage is `O(P * O * K)`, outside the
guest. Here `P <= 65`, `O = 27`, and `K = 14`. These bounds are explicit even
though the current capacities are fixed.

## Finite annotation transport projection

The hosted helper projects a specified repository source path using an
externally declared inventory. Each site binds:

```text
repository source path
function name
site ID
STATEMENT or COMPARISON form
source owner
kind
literal units (statement only)
source stage and its immutable placement locator
enclosing compound-region selectors
primitive token fragment and zero-based occurrence
BEFORE, AFTER, WRAPS, or FUNCTION_ENTRY placement relation
```

The owning correction design declares the exact inventory and any single
top-level `#include "micros/work_observation.h"` admission. Expected admission
is not discovered from the candidate. Every declared site must occur exactly
once in its exact function, and every observed annotation must be declared.

### Primitive occurrence and placement

Function membership and a site label are insufficient: a statement moved from
inside a loop to after it would otherwise project to the same original body
while reporting one visit instead of all iterations.

Each source stage therefore has an immutable logical-source locator. A region
selector contains an exact token fragment ending at a compound's opening
brace and a zero-based occurrence within its parent region. The ordered
selector chain starts at the function body and identifies the complete
enclosing compound stack, including loops and conditional branches.

Within the innermost region, the declaration identifies the measured primitive
by its exact token fragment and zero-based occurrence at that region's direct
depth. A statement primitive is a complete, non-control statement ending in
its semicolon. A comparison primitive is the unchanged scalar expression.
Fragments are finite acceptance-specification source anchors, not executable
design-phase prototypes or a complete planned production body.

`COMPLETE_VALIDATOR_CALL` uses the separate `FUNCTION_ENTRY` locator: the
function's own entry, before its first original source token, with the function
body as its complete enclosing stack. It has no statement fragment or
occurrence, and exactly one such invocation site is allowed for that function.
This observes early-rejected and empty-body calls without inventing a
non-control statement to serve as their primitive.

For `BEFORE` or `AFTER`, the observation and primitive must have the same
declared enclosing stack and be immediately adjacent on that side, with no
intervening non-observation tokens. `COPIED_BYTE` and `CLEARED_BYTE` sites use
`AFTER`, so they report completed movement. `WRAPS` replaces exactly the
declared comparison occurrence. Comments and whitespace do not affect these
token locators.

Relocating either an observation or its primitive outside the declared loop or
branch, attaching it to another repeated occurrence, or changing the placement
side fails admission. Equal projected source is not enough to waive that check.
The owning Accepted design supplies separate immutable RED/GREEN locators when
the correction changes control structure. The canonical evidence command
selects its declared source stage; stage is never inferred from counts or
candidate shape. EV02's own synthetic fixtures use a fixed fixture stage.

The helper may construct a provisional annotation-free token view solely to
resolve these locators and map observation positions into that view. That view
is not a successful projection or identity result. It returns projected source
only after every schema, occurrence, enclosing-region, placement, and include
binding has passed.

The projection:

- reuses the existing bounded C lexical/function-record helpers;
- ignores comments/strings as potential call sites;
- removes only an admitted complete observation statement in a braced
  compound, or unwraps only its admitted comparison expression;
- checks its immutable stage-specific primitive occurrence, complete enclosing
  stack, and exact adjacency/wrapping relation before accepting the projection;
- rejects unbraced-control-body, return/declaration/initializer, nested, or
  otherwise misplaced statements;
- preserves the original comparison operand source;
- removes only the one explicitly admitted top-level header include;
- rejects source-side definition/undefinition of the observation mode,
  annotation macros, or owner/kind identifier namespace;
- preserves all other source and fails on unknown or unsupported forms.

It never executes source or callbacks, regenerates a digest, deletes another
statement, or removes an unapproved include. The existing transport gate still
compares the projected result to its original immutable expectation.
Permanent owner, storage, include-direction, ABI, recipe, case, and physical
source-size checks remain independently active.

Projection is testing transport, not baseline correctness. Its conservative
work bound is `O((F + A) * S + A^2)` with existing function discovery, source
size `S`, functions `F`, and admitted sites `A`; auxiliary space is
`O(S + F + A)`. It runs on the host, not a production hot path.

## Connection to ADR-0082 and later corrections

A later correction design fixes its actual primitive definitions, source
sites, owner masks, producer authentication, capture adapter, and independent
expected work before implementation.

Capture begins before the operation's first production entry, including its
preflight, and finishes only after the complete selected success or rejection,
publication, cleanup, and rollback path. Fixture setup, reference-model work,
expected-result calculation, and assertions are outside capture. A surrogate
backend's observations cannot be described as target kernel work.

The counter implementation, vocabulary, expected model/bounds, compiler mode,
and accepted admission declaration are immutable oracle inputs during that
correction's Red/Green events. The declaration may explicitly cover both the
old and planned new source sites; the applicable candidate stage is bound by
the accepted design, never guessed from the observed counts.

Production annotations are candidate source. Before correction, projecting
them must preserve every active historical identity. After an algorithm
changes, only ADR-0082's planned replacement-oracle and author-recertification
transaction may retire its affected identity. Observer projection grants no
retirement, rebaseline, or source-size waiver.

Reference models compute expected work independently from input and the fixed
MINIX/Accepted-adaptation ledger. They never call the observer or use production
snapshots to generate expectations. Target capture adapters must authenticate
producer identity through existing exact test-report ownership; payload labels
alone confer nothing. EV02 adds no such target protocol.

## EV02 implementation owners and evidence

Independent GPT-6.1 Sol design review identified one missing intra-function
placement binding. The corrected contract adds immutable primitive,
enclosing-region, and placement anchors with relocation negatives. Re-review
of the corrected design reported no substantive issue remaining. Reviewers
ran no builds, suites, models, QEMU, or documentation gates.

Implementation is limited to the following planned owners and directly
dependent registration:

- `include/micros/work_observation.h`: disabled/enabled annotation contract,
  with no production-visible collector declarations;
- `tests/work/work_counts.h` and `tests/work/work_counts.c`: private native
  collector/snapshot contract and implementation;
- bounded native fixture/test sources under `tests/host/`;
- `tests/host/work_count_evidence.py` and narrowly split private helpers:
  complete composition and finite projection;
- `tests/host/test_work_count_evidence.py`: independent positive/negative
  snapshot, transport, and portability evidence;
- the existing CMake native graph, authority/fixture-input inventories, and
  directly related documentation.

The native graph adds `work-counts` and `work-observation-disabled` CTests to
both fast and complete workflows without a new QEMU workflow. All old cases,
models, seeds, gates, markers, budgets, and classifications remain required.
The new shared header is B14-owned compile-time evidence support, not a new
runtime ABI. Existing historical inventories remain pinned; current B14
recertification appends the new inputs rather than rewriting the old manifest.

### Required native cases

- Empty, one-item, `63/64/65`, early/middle/late/missing, sorted, reverse,
  and equal-key fixture inputs produce exact independent counts.
- A nested second source owner is recorded at its own primitive sites; no
  caller estimate is added.
- Enabled/disabled fixtures have identical semantic results.
- Disabled observation arguments are not evaluated; a comparison expression
  is evaluated once in both modes.
- Invalid labels/masks/dimensions, overlapping begin, inactive/null finish,
  stale/reused/exhausted capture labels, and overflow cannot publish a usable
  snapshot.
- Complete composition accepts separate producers executing shared code and
  rejects duplicate, omitted, extra, wrong-capture, malformed, nonzero foreign
  row, and overflow records.
- An independently declared original fixture remains transport-identical
  after projection; omitted/doubled/redefined/nested annotations and unrelated
  source changes fail. Moving a loop-local observation after the loop, across
  a conditional branch, to another identical primitive occurrence, or to the
  opposite side of its primitive fails even if the projected original body
  would otherwise be identical.

### Portability and production absence

The existing native compiler runs warnings-as-errors, ASan, and UBSan.
The selected target Clang compiles the enabled collector freestanding for the
existing `rv64imac_zicsr_zifencei` / `lp64` target, and the object must have no
external atomic-runtime dependency.

The disabled fixture must have no `micros_test_work_` symbol/state, and the
current option-free kernel and production service/application artifacts must
retain observer absence. Neither mode changes target assembly or claims new
hardware acceptance.

### Author and review gates

Design runs the existing documentation and staged/worktree/branch-range
checks on the exact complete staged tree, then independent design review.
It commits no executable artifact.

After design merge, implementation starts with native Red, keeps permanent
commits green, runs the narrow new evidence, then the authoritative fresh
milestone plan. Shared/unknown build or header ownership keeps full-tier
coverage. No reusable-profile or coverage rule is relaxed to obtain a pass.
Independent implementation review follows complete current author evidence.

EV02 completion supplies tooling only. B14 and the correction register may
record that foundation outcome, but inherited B01-B13 work gaps remain open
until their owning correction and affected-unit recertification.

## References

- [ADR-0083](../adr/0083-test-only-operation-count-evidence.md)
- [Fixed MINIX study](../research/minix-operation-count-evidence.md)
- [B14 evidence audit](../research/minix-complete-baseline-b14-evidence.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)
- [Evidence authority transition](evidence-authority-transition.md)
- [Testing strategy](../testing-strategy.md)
- [Build and author validation guide](../development/building.md)

# Bounded Validation Feedback

## Status and outcome

This is the Accepted contract for
[ADR-0105](../adr/0105-bounded-validation-feedback.md).
The paired tasks are `validation-efficiency-design` and
`validation-efficiency-implementation`. Complete and Acceptance-only design
reviews are clean. Separate design #217 merged as
`7fe20afa839bc396b243537bb5a75b4c5f5d59ed`, with identical reviewed and squash
tree `51fb987e80b8a7d25dc69113c7e64827326b86f0`.
The dependent implementation supplies the interfaces below; its complete
author gate and independent implementation review still precede publication.

The [fixed-source investigation](../research/validation-feedback-cost.md)
records the approximately 86-second local identity query, repeated complete
proof, identical Python fast/full selection and withheld progress.
The outcome is usable feedback without converting omitted or stale evidence
into success. It changes development tooling, not operating-system behavior.

## Test layers and claims

| Layer | What it establishes | When it belongs |
| --- | --- | --- |
| Portable unit/property tests | Algorithm results, state transitions, permissions, bounds and failure atomicity | Smallest affected development loop |
| Persistent native models | Long mixed transition sequences and independent state comparison | Explicit model work and complete native acceptance |
| Structural/ABI checks | Current ownership, dependency direction, layout and bounded storage | Focused structural work and complete native acceptance |
| Historical transport checks | Preservation of a particular reviewed source move or unchanged historical subject | Their explicit maintenance/authority boundary, not a substitute for behavior |
| Actual-source/work evidence | Real compiled source, primitive work, ordinary-mode absence and independent expectations | The owning algorithm correction and complete acceptance |
| QEMU components | Real privilege, registers, traps, IRQs, MMU and fences | The affected target mechanism |
| Cross-service integration | Composed IPC, grants, lifecycle, rollback and resource release | The affected ready peers and PR/milestone acceptance |
| End-to-end scenarios | Complete user-visible service composition | The applicable integrated milestone |

No layer can claim another layer merely because both return zero.
Static source equality is not a functional, complexity or hardware oracle.
The implementation must not delete all structural checks or remove integration
on the premise that a kernel can be tested only through host units.

The minimum PR gate and full milestone selection remain under ADR-0032.
This outcome does not make `test-unit-fast` merge evidence, remove a QEMU
workflow, shorten a model or claim the future B13 shell gate.

## Checked source-view ownership

### One explicit owner

Add a private host-side source-view owner in
`tests/host/evidence_authority_scope.py`. Its public use is limited to:

- the repository host-test runner;
- the CLI entry of the existing bootstrap-allocation fixture generator;
- the CLI entry of the existing kernel-mapping fixture generator; and
- the same entry in the restarted MEM03 fixture generator.

Library calls consume an existing view or use their ordinary cold behavior.
They do not silently open nested views. A second owner in the same process,
a foreign root, or an attempted cross-process use is rejected.
No environment variable, CLI flag, file or caller-supplied key list grants
verified authority.

The owner may establish the view lazily at the first protected source read.
A run that performs only independent unit tests need not load the real
repository's complete authority state or toolchain.

Fast/focused behavioral iteration does not acquire a new up-front staging
requirement. A committed source view may preserve an ordinary dirty tracked
snapshot when the cold APIs already permit it, but that is not reusable or
final author evidence. An uncommitted author journal retains its existing
exact clean-staged-candidate requirement. Source-view capture does not waive
untracked-input, namespace or missing-source refusals.

The existing cold authority APIs remain available and independently tested.
In particular, testing the authority validator itself must not accidentally
test only a remembered successful answer.

Expose the checked view through source-consumer adapters, not by replacing
the cold API with an implicit process-global cache. The existing
`identity_required` and `memory_identity_required` correctness tests retain
their direct cold meaning. The initial protected consumers are the kernel
logical/external source readers, the runtime-memory source assertion and the
memory-native source-admission readers. The pending MEM03 adapters join only
on its separately validated restart.

### Closed providers

The initial implementation admits the existing COPY01, PLAT01, MEM01 and
MEM02 authority owners explicitly. It does not discover executable providers
by filename glob, arbitrary import string or plugin registration.

The restarted MEM03 implementation adds its exact fifth provider under this
contract. It must not be treated as committed authority merely because its
source files or completion declaration are staged.

A provider supplies its existing declared task, design, keys and complete
verification mechanism. The view owner may share one independently
materialized current inventory, but it does not invent a different registry
or derive oracle expectations from candidate output.

For each provider:

1. absent authority retains the original transport requirement;
2. committed authority follows the existing literal/HEAD agreement and
   complete registry validation, without depending on an author journal;
3. an uncommitted terminal claim requires the actual complete ordered
   Red/Green/recertification chain and all existing input/oracle/unit checks;
4. an incomplete author attempt grants no additional retirement; and
5. a malformed claim or journal is an error, not absence or a cache miss
   that can manufacture success.

Existing provisional author transactions remain separate. Their exact PID,
root, candidate, scope and cleanup checks continue to run. The ordinary view
cannot turn a provisional result into a terminal or committed result.

### Immutable materialized view

The view contains only:

- the owning PID and resolved repository/worktree/Git identity;
- the captured HEAD, index/source identity and source membership;
- immutable source bytes, modes and admitted link identities;
- the complete current inventory and validated per-key result;
- the exact provider/declaration/design bindings;
- complete tool, environment and external-input identities required by
  any admitted author proof; and
- the selected journal plus record/log identities when a journal is required.

It contains no mutable success flag supplied by a caller and no authority
to write an event, retire a key in a persistent registry, run a privileged
operation or publish an execution receipt.

Retired-key results and source bytes are immutable while the view is open.
Its lifetime is one owned invocation, not a timeout, TTL, file timestamp,
process-global forever cache or disk cache. Closing or unwinding destroys
the view. Forked children cannot inherit usable verification.

The source consumer asks a question about this captured view, not a newly
authorized question about arbitrary live filesystem bytes. Protected source
reads must use the captured bytes and preserve the existing logical/physical
path mapping. Undeclared paths inside the protected namespace are errors.
Independent temporary fixture roots remain outside the view and retain cold
read/validation behavior.

### Entry and exit proof

Before any result becomes available, perform the complete existing
source/inventory/declaration/claim or author-journal verification for the
captured inputs. Recheck the capture before exposing the view so a source,
selector or external-input change during opening cannot create a usable
result.

A journal reader and its caller must not both semantically replay the same
three records. Give one owner responsibility for that validation and return
its typed result along with the validated journal. Record bytes and event
chains remain checked; this is not removal of one independent oracle.

Keep the existing `read_journal` interface for callers that need only its
journal. An internal verified-reader result can contain both the journal and
validation result; neither wrapper may replay the same record a second time.
Do not expose a caller-supplied "already verified" flag.

Before the owning invocation reports overall success, compare the complete
current source, tool, environment, selector and record/log identities with
the verified view. A changed input, missing file, changed mode/link/member,
foreign process/root, malformed selector, failed close or interrupted owner
invalidates the invocation.

The exit comparison may use complete byte identities for already validated
immutable records; it need not semantically replay identical records again.
It must not substitute `mtime`, size alone, a version string or a successful
test result for that comparison.

This changes the observation boundary for read-only source consumers:
complete external-tool closure is checked at the owned invocation boundary,
not before each question about its immutable snapshot. The final acceptance
boundary is not weakened. It retains the existing before/after consistency
model and does not claim an atomic filesystem snapshot or protection against
an adversary controlling the checker process.

### Fail-closed publication and test isolation

Only the owner can close the view successfully. A test's local `ok` result is
not overall acceptance while the view is open. The host runner must complete
view closure before its final successful summary and exit.

Any exception clears the active view. Failed opening or closing leaves no
usable result for a later invocation. There is no recovery path that
continues with the old verified data after detecting changed inputs.

Unit tests of the proof machinery use its cold APIs or explicit isolated
fixture views. A test fixture may temporarily suspend the real source view
while modeling a different claim or mocked authority; that test-only helper
cannot be exposed as an execution option or used by production source
consumers. Original negative inputs and assertions remain required.
New tests must separately exercise the scoped consumer so isolation cannot
conceal a scope invalidation defect.

The view cannot write the author's journal or completion claim. Author
drivers retain their current one-shot phase and publication ownership.

Scope queries are read-only and never enter an author recertification mode.
In particular, the host runner cannot obtain a provisional retirement
merely to make source tests pass faster.

## Deterministic work bounds

Let `q` be local identity queries, `s` the closed source/input bytes, `f` the
current inventory size and `j` the selected journal/replay work.

The rejected implementation repeatedly performs complete work proportional
to `q * (s + f + j)`, including duplicate replay in a single query.

An owned source view instead performs complete capture/validation and exit
comparison at its boundary. Within it:

- repeated selected-key lookup performs no context capture, tool probe,
  inventory reconstruction or execution-log replay;
- lookup uses the already validated key set;
- repeated source reads return immutable captured bytes;
- each selected author record is semantically replayed once per view; and
- the complete inventory is materialized once for the view rather than once
  per family/key.

The boundary still pays the necessary source/tool identity cost. Its storage
is explicit host-side snapshot/inventory storage, proportional to the closed
inputs, not new kernel BSS or automatic target stack storage.

Measure these call/work counts directly in deterministic tests.
Report cold-view, repeated-query, exit-verification and complete-suite
wall-clock costs separately; do not add overlapping inclusive timings.

## Host-test runner and tiers

### Interfaces

Add `tools/run_host_tests.py` with these mutually exclusive intents:

```text
${MICROS_PYTHON} -E -S -B tools/run_host_tests.py --tier fast
${MICROS_PYTHON} -E -S -B tools/run_host_tests.py --tier full
${MICROS_PYTHON} -E -S -B tools/run_host_tests.py --test test_module.Class.method
```

`--test` may repeat for a bounded explicit selection. It is focused
development evidence, not final PR coverage. Names must resolve to actual
repository `tests/host/test_*.py` modules and their unittest classes/methods,
not arbitrary executable imports.

An optional `--list` reports the selected test IDs without running test cases,
fixtures, source-view proof or workflows. It provides no execution receipt.
Normal Python module loading is not claimed to be a sandbox.

The runner derives its repository root from its checked-in location, requires
the documented root invocation, validates the selected runtime, and preserves
`-E -S -B`. No `PYTHONPATH`, site initialization or launcher-variable rewrite
is introduced. Every owned pre-exec boundary keeps ADR-0075's rejection.

### Complete discovery

Full mode uses the existing unittest discovery semantics and ordering:

```text
start directory: tests/host
pattern:         test_*.py
```

Every discovered test runs. A duplicate identity, unresolved selection,
discovery/import error, empty selection or omitted selected test fails.
No timing threshold chooses which tests to execute.

The runner preserves assertions, subtests, cleanup and failure reporting.
Required skipped/not-run tests are not successes. It must not catch an
exception and emit a passing aggregate.

The complete native CTest inventory and all persistent model identities stay
in `test-unit`. This outcome adds runner/scope regressions, not a replacement
for an existing behavioral suite.

### Fast selection

The initial fast Python module set is the independently measured existing
set below plus the new runner/scope unit tests:

```text
test_check_docs
test_check_elf_sections
test_embed_ramfs_seed
test_python_host_runtime
test_stack_reports
test_stack_observer_state
test_validation_evidence
test_evidence_authority_scope
test_host_test_runner
```

The first seven currently pass 64 tests in 3.188 seconds. The latter two do
not yet exist; their deterministic fixture tests must not invoke a real
complete repository proof or toolchain.

Fast mode loads only its reviewed unit modules rather than importing every
slow module first. Every listed module must exist and supply runnable tests.
New unlisted modules remain included by full discovery; they are not silently
classified as fast or dropped from complete acceptance.

The nonslow native CTests remain selected through the existing `slow` label.
Do not reduce any model's transition count. The six-product MEM03
orchestration is labeled slow and is not a dependency of the fast target on
restart. Its full target, CTest, corpus and focused launcher regression remain.

Keep the six-second incremental fast budget as a review signal and record
actual cold/incremental costs separately. Do not claim that the measured
Python subset establishes the complete target budget.

## Observable execution

### Per-test progress

Before a selected test starts, print and flush its ordinal, total and ID.
After it finishes, print its actual outcome and observed duration.
At completion, show a bounded slowest-ten summary with deterministic
tie-breaking by ID.

The source-view owner similarly reports proof opening and closing as
separate phases. It must not disguise proof preparation as a hung first
test or count it as another behavioral execution.

Durations use a monotonic clock. Tests of the reporter use an injected clock
and controlled results, not sleeps or wall-clock pass thresholds.
The final successful summary follows both test completion and source-view
closure. Partial progress is not a completed native gate.

### Live workflow logs

`tools/validation_execution.py` keeps its fixed canonical argv and the
existing evidence-store lock, invalidation and publication order.

After opening the owned attempt log, stream combined stdout/stderr from the
one workflow child. Write each raw byte exactly once to that log and display
progress promptly. Do not wait for workflow exit or a full text line before
making available output visible.

The persisted raw log must equal the child's complete combined byte stream.
Use incremental text decoding for display so a multibyte character split
between chunks is not corrupted by per-chunk decoding. Display replacement
of invalid text must not change retained raw bytes.

After actual exit, retain the existing complete-log identity and postflight.
Do not replay already displayed bytes a second time. Receipt consumption
and its attributed `REUSED` result remain distinct from streaming a fresh
execution.

A write/read/display error, interruption or incomplete stream cannot publish
a success receipt. The executor owns the exact child/process group it starts
and must terminate/reap that owned work on exceptional exit; never terminate
unrelated processes. Cleanup deadlines are not larger test deadlines.
The implementation reuses existing process-lifecycle patterns rather than
adding an unbounded watcher or a detached helper.

On the supported POSIX hosts, start the workflow in its own session/process
group and retain the child's exact identity. Exceptional cleanup and interrupt
relay target only that group, then reap the child with bounded escalation.
Never signal the caller's inherited process group or find a process by name.

## Build, source and authority boundaries

Only the two native Python invocations in `CMakeLists.txt` change to the
exact full/fast runner commands. CTest commands, native product paths,
target/compiler options, guest workflows and image/stack budgets remain.

Keep the historical recipe value
`tools/validation_context.py:data-entry:RECIPE_DIGESTS/CMakeLists.txt`
unchanged. Its narrow payloads are `CMakeLists.txt` and
`tools/validation_context.py`, as verified on the fixed reference.
Admit the two new invocation forms through one finite inverse before the
existing recipe projection. It must reject a missing/duplicate replacement,
unknown runner, extra arguments, changed runtime prefix or outside edit.

The inverse proves unchanged historical recipe content outside the accepted
invocation adaptation. It does not certify the new selection or execution
semantics; the independently specified tests below do that. Keep every
existing historical fact/value and fingerprint. No runtime identity is
retired and no candidate-derived digest is substituted.

The historical inverse accepts the exact old pair or the exact new pair,
never a mixed/partial pair. Current runner-admission checks require the new
pair, so historical parsing cannot silently permit a current build to fall
back to full discovery in its fast target.

Use the same finite adaptation at the existing kernel logical CMake reader,
work/stack recipe projection, and MEM01/MEM02 native recipe inverses. Their
direct fixed-reference comparisons retain every other byte and negative
control. Do not scatter permissive string replacements across test modules
or change the native executable/CTest argument policy.

The new source-view/runner implementation is ordinary executable policy, not
another frozen copy of its own expected output. Register actual Python
runtime ownership and explicit non-identity discovery treatment where the
existing inventory requires it. Do not bypass family discovery broadly.

Expected code owners are:

| Owner | Responsibility |
| --- | --- |
| `tests/host/evidence_authority_scope.py` | One checked read-view lifetime, provider composition and immutable lookup |
| Existing four authority owners | Their original proof rules and typed single-validation result |
| Existing source/fixture readers | Consume a checked view or retain their cold behavior |
| `tools/run_host_tests.py` | Exact selection, unittest execution, progress and final close/result |
| `tools/validation_execution.py` | Live workflow forwarding and unchanged success publication boundary |
| Existing recipe projection helper | Exact runner, source-reader and streaming historical inverses |
| `CMakeLists.txt` | Existing native workflows delegated to the correct tier |
| Focused host tests | Work-count, invalidation, selection, progress and lifecycle regressions |

No C/assembly, public runtime header, server, application or QEMU scenario
change belongs to the prerequisite implementation.

## Failure matrix

The implementation must reject, rather than reuse a passing source view, when:

- source bytes, mode, kind, link or namespace membership change;
- HEAD, index/worktree identity or required cleanliness changes;
- declarations, inventory ownership, fingerprints or design binding change;
- a tool, environment, native cache, firmware or external input changes;
- selector, attempt, event order, record bytes, log bytes or unit results
  differ from the verified proof;
- a record is missing, malformed, incomplete, borrowed or prematurely claims
  retirement;
- a caller uses a foreign root/PID, nests another owner or keeps a view after
  close; or
- opening, tests, cleanup, closing or log finalization fails.

Test source modifications that preserve timestamps and sizes, not just
ordinary writes. Verify unregistered/incomplete authority keeps old
identities active. Verify committed authority needs no author journal.
Preserve original negative controls when isolating proof-engine fixtures.

## Implementation and acceptance sequence

After the separate design merge:

1. Add deterministic Red evidence for duplicate proof work and identical
   fast/full selection using the existing implementations and actual
   discovered identities; a missing new file alone is insufficient.
2. Implement the checked source view and single journal-validation ownership,
   retaining cold APIs and all proof/invalidation negatives.
3. Add the exact runner, finite native invocation adaptation, closed fast set
   and complete full discovery.
4. Stream workflow output while preserving exact logs, failure cleanup and
   receipt postflight/publication.
5. Run narrow affected tests, then complete native and current author
   acceptance; inspect all current inventory, recipe and profile eligibility.
6. Obtain independent GPT-6.1 Sol implementation review before the first
   permanent commit and publish only the identical reviewed tree.

Deterministic tests require:

- one proof per immutable view, zero repeated complete proof for many local
  questions, and complete rejection after changed inputs or lifetime;
- exact old/new final authority decisions and unchanged unrelated facts;
- all discovered tests in full, the exact nonempty fast set, focused
  selection validation and no hidden execution from listing;
- failing, skipped, errored and interrupted cases cannot become a successful
  aggregate;
- progress is visible before a controlled child/test completes, with no sleep;
- partial UTF-8, stderr, write failure, nonzero exit and interruption preserve
  byte-exact logs and correct publication/cleanup; and
- pre-exec/runtime Python protections and the unchanged native artifact
  parser remain effective.

Record cold proof, repeated-query, fast subset, complete native and full
producer timings. The current committed foundation can prove its four
providers and controlled author-journal fixtures; it must not claim that
the archived MEM03 five-hour case is fixed before the restarted real
MEM03 acceptance measures it.

## MEM03 restart and remaining work

The archived runtime candidate is not cherry-picked as accepted work.
After this prerequisite implementation merges, create a fresh MEM03 branch
from current `main`.

Preserve the fixed #214 runtime reference, both Accepted MEM03 decisions,
forty keys, six immutable adapters, 99-case matrix, complete helper authority
and original work/semantic expectations. Independently redeclare the actual
new base membership and required source-reader/build adaptations before
renewed production Red/Green.

This design explicitly permits that new validation-prerequisite base and
the full-only MEM03 orchestration placement; it does not allow unrelated
runtime changes or borrowed author evidence. Current source, external inputs
and command records must agree through the restarted transaction.

The resumed real current native/full gate must demonstrate that per-key
proof replay no longer multiplies into hours. The prerequisite can complete
its own committed-foundation and controlled author-proof acceptance before
that restart; it must distinguish this from a measured speedup of the real
uncommitted MEM03 case. If that later case violates the deterministic work
bound, stop and correct its integration rather than claiming the original
problem is solved by a synthetic fixture alone.

Hosted CI remains a separate blocked implementation. These local tooling
changes do not supply a hosted green run, node Q, STACK13, RECERT01 or POSIX
completion.

## Implementation map and evidence boundary

The source-view owner captures immutable source bytes, candidate/namespace
identity, environment and the validated running Python boundary. Repository
invocation prefixes are checked before their already admitted spellings are
normalized, as in the earlier author proofs; the standard-library roots remain
bound. An outside import root is an error, not normalization.

Committed registry resolution shares one inventory. A terminal author view
uses the inventory already owned by its verified author context, semantically
replays the ordered journal once and retains exact selector/record/log
identities. Closing verifies the complete external inputs and source
snapshot without materializing another inventory or replaying identical logs.
Journal input capture rechecks the ordinary namespace and selected attempt
through the existing ownership helpers before and after reading, so linked
parents cannot preserve authority with identical leaf bytes. Failed opening
is sticky even if a test catches the first exception.

The four existing authority owners now expose a verified journal/result pair
internally while retaining their original `read_journal` interface. Their cold
queries no longer replay the same proof again at the caller. Kernel logical
and external readers and the runtime-memory source assertion consume the
owned view. Directory, grant, object and TTY source assertions use the same
adapter instead of rebuilding complete authority for each historical key.
Ordinary cold APIs and their correctness tests remain independent.

The two memory fixture CLI entries own a view around generation and close it
before successful exit. Their library functions do not open nested owners.
The host runner selects the exact fast set or complete unittest discovery,
preserves focused/list semantics and publishes final `OK` only after closing.

Canonical workflow policy remains in `tools/validation_execution.py`.
The small `tools/validation_stream.py` leaf owns only the output pipe and its
child group: it retains raw bytes, decodes display text incrementally and
observes exit without reaping. Scoped cancellation handlers defer interruption
until a spawned child is owned, then route cancellation through cleanup.
The leader remains unreaped until its group has been terminated, including
any remaining descendants at ordinary completion; prior signal handlers are
restored afterward. Nondefault child-reaping policy is rejected before
launch. It makes no coverage, source, postflight or receipt decision.

Darwin returns `EPERM` when the private group contains only its unreaped
zombie leader. That result is admitted only after a non-reaping exited-child
observation and a successful system `libproc` query with a fixed two-PID
buffer proves the complete group is exactly that same leader. A live child,
extra member, truncated/failed query or other permission failure remains an
error. This read-only check neither signals another process nor relaxes a
permission boundary, and the child is reaped even when cleanup fails.

The historical execution-source reader admits only three exact reversible
edits: the streaming import, the same canonical child call through that leaf,
and removal of duplicate completed-log replay. These forms were declared and
round-tripped before editing the executor. The original complete execution
identities remain unchanged; the new leaf's behavior is independently tested,
not inferred from that historical inverse.

The existing recipe/transport helper owns that inverse and the finite
snapshot-reader adaptation. The latter restores only the declared import,
captured-byte reads and execution-adapter call before the unchanged
MEM02/MEM01/PLAT03/PLAT02/PLAT01 source-gate chain. Complete historical source
and AST comparisons still reject outside edits; partial or mixed reader
forms fail rather than bypassing an old assertion. The four source-assertion
consumers admit only their exact single import substitution.

Focused evidence covers duplicate replay, immutable reads, mode/namespace/
environment/runtime/selector/log changes, process/lifetime isolation, sticky
failure, exact selection, progress before completion, split/invalid UTF-8,
nonzero exit, write failure, interruption and owned cleanup. A precise
non-identity exception identifies the execution regression module as a
consumer of already registered identities, not another identity owner.

The first current fast workflow rebuilt 220 native steps, then passed all
37 nonslow CTests in 16.29 seconds and 90 Python unit cases in 4.797 seconds.
The separately observed incremental run passed the same 37/90 cases in
10.84 and 5.182 seconds. Compilation and execution are distinct costs.
This is useful seconds-scale feedback, but the complete fast target is still
above the six-second review signal; no case is hidden or relabeled to claim
that budget.

Those observations are not complete native/target acceptance or proof of the
archived MEM03 speedup. Complete author evidence and independent review are
separate publication requirements.

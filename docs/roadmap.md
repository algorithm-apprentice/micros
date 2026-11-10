# Roadmap

## Delivery model

Development proceeds through sequential pull requests. A later milestone does
not begin until the previous pull request has been reviewed and accepted.
Architecture or scope changes are recorded in ADRs before implementation.
Implementation follows the repository's documentation-first, test-first,
independently reviewed AI-native workflow. Each dependency-ready subsystem
first reproduces the documented MINIX logical structures, algorithm family,
complexity envelope, and behavior; optional optimization follows only after
baseline integration passes.

The structural-baseline transition defined by ADR-0080 blocks every runtime
correction and feature implementation while the pinned B01-B14 complete audit
is in progress. The completed
[findings register and correction DAG](research/minix-complete-baseline-findings-and-correction-dag.md)
assigns all blocking findings to one sequential correction queue. Each
correction merges through its design/implementation pair and affected-unit
re-certification. Node-Q remains blocked through `BASE01`; Step 16 and
ordinary feature work remain blocked through `RECERT01`.

The evidence barrier is merged as EV04 #189. COPY01 design #190 and
implementation #191 close only B05-F006/B05-E005 with real primitive-work
evidence and granular source-authority retirement. PLAT01's source-authority
study first found the
[packed-identity prerequisite](architecture/packed-evidence-identities.md).
EV01-C01 design #192 and implementation #193 are merged. The resumed
PLAT01 study then found a distinct
[relocated-source binding gap](architecture/relocated-evidence-payloads.md).
Its EV01-C02 design #194 and independently reviewed implementation #195
are merged. PLAT01 then obtained the independently reviewed Accepted
[diagnostic-history design](architecture/diagnostic-history.md), covering
both histories, exact TTY transfer/notification and panic consumption.
Its design phase authored no runtime code before the separate design merge.
The remaining runtime queue and hosted CI are not declared complete.

Design #196 and collector-source correction #197 merged. The
first uncommitted PLAT01 implementation subsequently hit the
[startup IRQ-retrigger blocker](research/minix-diagnostic-history.md#observed-startup-irq-retrigger).
It was abandoned before the independently reviewed
[Accepted ADR-0094 correction](adr/0094-retrigger-safe-diagnostic-probe.md)
separately merged as #198. A later valid claim can appear after the first IRQ completion;
the large target probe must defer until that actual pending event is
consumed and completed.

Restarted PLAT01 implementation #199 is now independently reviewed and
merged. Actual Red/Green, thirteen-unit recertification and all 48 fresh
author commands passed, including 42 static configurations, 111 ELF outputs
and ten observed stack scopes. The reviewed, published and squash trees
are identical; a fresh committed checkout preserves COPY01 and PLAT01
authority without an author journal. This closes only B01-F002.
The following [PLAT02](research/minix-terminal-failure-baseline.md)
outcome covers bounded UART polling, recursive panic and corrupt-console
fallback. Its [Accepted ADR-0095](adr/0095-bounded-uart-and-terminal-panic.md) and
[terminal contract](architecture/terminal-failure.md) have passed
independent design/correction review and separately merged as #200.
Implementation #201 is now independently reviewed and merged. All 49
current author commands passed fresh, with actual matching baseline/current
terminal evidence, 43 static configurations/112 ELF outputs and ten
observed scopes. The reviewed, published and squash trees are identical;
a fresh committed checkout preserves all prior authority without journals.
This closes only B01-F001/F003/F005 and makes PLAT03 dependency-ready.

The [PLAT03 trace design](architecture/panic-stack-trace.md) follows
[Accepted ADR-0096](adr/0096-bounded-kernel-panic-stack-trace.md).
Its [fixed-source study](research/minix-panic-stack-trace.md) traces
the current-kernel frame chain, RV64 layout and authenticated entry-epoch
barriers. Clean independent design and Acceptance review preceded its
separate merge as #202.

The first uncommitted implementation stopped before production changes
after its initial declaration misread case 8's shorthand stop result.
The [Accepted ADR-0097 clarification](adr/0097-panic-trace-link-rejection-precedence.md)
separately merged as #203, retaining the already defined progress-before-range
order and unblocking the restarted PLAT03 implementation. The private
owner/cursor, target reader, streamed trace and version-four fault/stack
evidence are now implemented. Acceptance requires matching immutable
eighteen-case baseline/current evidence, all 50 fresh author commands,
44 static configurations/113 outputs, ten observed scopes and independent
implementation review. Implementation #204 passed those gates and merged as
`ddca7661d4fa7e13acf0aaa5f0ba55b91f9c9916`, with identical reviewed,
committed, remote and squash tree
`3bf57ef9d12345f7b72e082a5843c97a2476f23e`. A fresh committed checkout
preserves prior authority without author journals. This closes only
B01-F004/B01-E004.

MEM01's initial documentation-only proposal received an independent High
design finding: one standalone frame-validator hash inherits 797 payload
paths, colliding with independent immutable CMake/planner inputs. The author
verified the actual inventory and unchanged `PROJECTION` rejection.
That proposal is preserved as reference, not accepted implementation
authority, and no memory implementation has started.
EV01-C03 follows
[Accepted ADR-0098](adr/0098-standalone-frame-validator-evidence-binding.md)
and its [one-key binding contract](architecture/standalone-frame-validator-binding.md).
EV01-C03 changes only independently declared payload/unit metadata while
preserving all other facts, historical values and disjointness. MEM01 design
resumes only after the separate prerequisite implementation merges.

Design #205 is merged. EV01-C03's initial implementation was preserved
when its full author run passed native checks and seven commands,
then the ordinary panic-trace gate rejected the three already committed
design documents against PLAT03's original file set. This same rejection
also affected clean main at #205; it was not a trace-runtime failure.
[Accepted ADR-0099](adr/0099-panic-trace-execution-scopes.md) and the
[execution-scope contract](architecture/panic-trace-execution-scopes.md)
define the separate PLAT03-C02 prerequisite. Design #206 and independently
reviewed implementation #207 have now merged with identical reviewed,
committed, remote and squash tree
`6928ae70be64082d236508986c003c25a29b7c6b`. The renewed actual historical
pair and complete current author gate passed without weakening either
boundary. EV01-C03 then restarted, passed all 50 fresh author commands and
independent implementation review, and merged as #208,
`bcace37522e5beb101ef5278889a2f87742f5721`. Its reviewed, committed,
remote and squash tree is `36110108d7839460b2f384761bb9aaa540e82444`;
fresh committed checks preserve ordinary authority without author journals.

MEM01 implementation #210 is now independently reviewed and merged under
[Accepted ADR-0100](adr/0100-minix-bootstrap-allocation.md) and the
[bootstrap allocation contract](architecture/bootstrap-allocation.md).
The separately reviewed design #209 retains one lower-bound bitmap cursor, lowest-address
selection, complete mutation/rollback coherence and original whole-interval
FDT safety while removing repeated per-frame work. Its six actual fact
closures now fit the eight candidate paths without immutable overlap.
Actual pre-production Red, current Green, thirteen-unit recertification,
all 50 fresh author commands and independent first-commit review passed.
The reviewed/committed/remote/preview/squash tree is
`08f31a8b1dfc639c8b826ea65d092327fea159a1`, merged as
`3f7e33026f92c8c1765ce00c2b03b5c2d6855197`. Fresh committed checks preserve
ordinary authority without author journals, and completed task refs are
removed. This closes only B01-F006/B02-F001.

MEM02 implementation #212 is now independently reviewed and merged. Accepted
[ADR-0101](adr/0101-minix-kernel-mapping-topology.md) and its
[kernel-map contract](architecture/kernel-mapping-topology.md) define
largest legal kernel leaves, direct checked table identities, exact mapping
domains and real native/hardware evidence. Independent design and Acceptance
reviews are clean; separate design #211 merged as
`e7d936d6d26e372a8794bc5fd8feb6d976fcd529`, with identical reviewed and squash
tree `4f520e80e7f7a7490f67d438029c5d7ff77abcc0`.
Actual pre-production Red, current Green, thirteen-unit recertification,
all fifty fresh author commands and independent first-commit review passed.
The reviewed/committed/remote/preview/squash tree is
`910e04a2129a692ec74ede4c34ff19c8f18e2397`, merged as
`bf4eef8f0c59a9d06ae3a20d4a52c5f07943c2ee`. Journal-free committed checks and
exact task-ref cleanup passed. This closes B02-F003/B02-F004, leaving
66 pre-node runtime findings open.

The next MEM03 design found a distinct
[current-native source authority gap](research/memory-native-source-authority-gap.md)
before implementation. Accepted
[ADR-0102](adr/0102-granular-memory-native-source-authority.md) and the
[EV01-C04 contract](architecture/memory-native-source-authority.md) define
precise identities for 328 existing MEM01/MEM02 source-admission clauses
while preserving original records and current checks. Separate design #213
is merged as `52c9b3ab44acdd3b80429f98a4e15cd3e064f63c`.
Implementation #214 passed real coverage Red, complete current fifty-command
acceptance and independent first-commit review, then merged as
`8927f9956eb270962517b19715a12bc93a5fa466`, with identical reviewed and squash
tree `9f7802dfdfc84034b626068b5c160b90a645dad7`.
Fresh committed/actual-main journal-free checks and exact cleanup passed.
All old facts, fingerprints, guards and runtime source remain unchanged.

MEM03 design #215 is separately merged. Accepted
[ADR-0103](adr/0103-selected-frame-ownership-validation.md) and its
[selected ownership contract](architecture/selected-frame-ownership.md)
define active metadata, selected-record plans, whole-active transitions and
explicit complete diagnostics. The
[fixed-source study](research/minix-selected-frame-ownership.md) records
the 26 current direct audit calls and the active obligations that cannot be
removed with the global scan. The first uncommitted implementation passed
actual native Red/Green and all twelve QEMU recertification commands, but
B14 rejected an accidentally prefix-wide token assertion while both complete
scan helpers remained unchanged.

The first implementation was archived without a permanent commit or retirement.
The corrective design under Accepted
[ADR-0104](adr/0104-precise-complete-validator-token-authority.md) defines the
accepted token-scope repair.
The [scope investigation](research/complete-validator-token-scope-gap.md)
defines the two added identities and one frozen adapter, yielding the
40-key/27-path envelope without changing runtime behavior or old hash values.
Independent design review was clean and the correction separately merged as
#216, `80941ff270b5d75d7edfce65ca96f28e6501f20d`.
The restarted uncommitted implementation completed actual Red/Green,
recertification and one full author gate. Independent review then found a
Python launcher-boundary omission; its test-first correction passed renewed
Red/Green and recertification.

The subsequent full run exposed impractical validation feedback: 43 CTests
passed in 214.22 seconds, while the Python stage repeatedly reconstructed
complete source/tool and journal proofs for individual source queries.
The [cost investigation](research/validation-feedback-cost.md) measured one
such query at approximately 86 seconds. The user prioritized correcting this
before more runtime development. That candidate is preserved, its full run
was intentionally stopped, and no implementation commit or merge occurred.

Accepted [ADR-0105](adr/0105-bounded-validation-feedback.md) and its
[feedback contract](architecture/validation-feedback.md) passed complete and
Acceptance-only independent review and separately merged as #217,
`7fe20afa839bc396b243537bb5a75b4c5f5d59ed`.
The validation implementation #218 merged as
`dedf9a8d9d72f153519ea07150e50594a6618c6b`, with reviewed, committed, remote,
preview and squash tree `3afdc8e5281fe5199cb2fd45cc4af3a12eaba1f0`.
All fifty fresh author commands and clean independent review preceded
publication; journal-free committed/main checks and exact ref cleanup passed.
It supplies checked source-view reuse, distinct host tiers and live byte-exact
progress, without removing behavioral, target or integration evidence.

MEM03's fresh restart from #218 independently renewed actual Red/Green,
thirteen-unit recertification, all fifty fresh author commands and clean
independent first-commit review. Implementation #219 merged as
`da258293f507c745c8e157cd84bd13b2fb4b5e77`, with identical reviewed,
committed, remote, preview and squash tree
`5793c993338c22cf30aa552c9397c8aa234c33cc`.
Its exact forty-key transaction preserves all predecessor records and
diagnostic boundaries. Journal-free committed/main authority checks and
completed-task ref cleanup passed. B02-F002/B02-E002 are closed, leaving
65 pre-node runtime findings open.

That real workload also verified the validation improvement: complete Python
discovery ran 1,526 tests in 499.049 seconds, versus the earlier 1,470 tests
in 18,841.945 seconds. The fast workflow took 16.64 seconds while preserving
37 CTests and 98 Python cases. This does not claim the six-second signal or
hosted-CI success; all complete native, target, integration and stack gates
remain required.

The following MEM04 design found the
[MEM03 native-source authority gap](research/selected-ownership-native-source-authority-gap.md).
Accepted [ADR-0106](adr/0106-granular-selected-ownership-native-source-authority.md)
and [EV01-C05](architecture/selected-ownership-native-source-authority.md)
define a separate 484-clause metadata prerequisite, preserving all 4,290
predecessor facts, eighteen fingerprints and existing checks. Separate design
#220 merged as `1d9eab05afab16a4caff519a84790541036ee240`.
Its independently frozen declaration and closed materialization supply
4,774 facts in nineteen families without any new retirement. Implementation
#221 completed real coverage Red, all fifty fresh author commands and clean
independent first-commit review, then merged as
`f28377decc0d20533e05330f2f2211db3f230910`.
The identical reviewed/committed/remote/preview/squash tree is
`4b56076479ac53092c4c83d9c9b52829b66139ba`.
Fresh committed/actual-main authority checks and protected task-ref cleanup
passed.

Accepted [ADR-0107](adr/0107-selected-user-address-space-validation.md) and
the [MEM04 contract](architecture/selected-user-address-space.md) now define
checked selected lookup, translation, allocation preflight and root
preparation/activation. Bounded VM/TTY binding inspection removes hidden
global validation without replacing command authorization. The proposal
also preserves complete diagnosis and fixes the directly coupled allocator
cursor compensation gap. Design #222 is separately merged. Its uncommitted
implementation reached actual Red and current native evidence, but the
canonical successful QEMU output omitted the semantic witnesses required
for Green. The candidate is archived without a Green event, retirement or
implementation publication.
[Accepted ADR-0108](adr/0108-qemu-accepted-transcript-reporting.md) defines
the separate MEM04-C01 reporting prerequisite. Its design and implementation
must merge before MEM04 restarts from current main with renewed evidence.
Design #223 is merged as `123034f6f5cb0bb2617b98b3e2022ceca2bb106e`,
with identical reviewed and squash tree
`44d9a1f0c13b7a817049fd5b8f1fc86987f4d788`.
Implementation #224 is independently reviewed and merged as
`9f2eb7db4d6397d7760bf0ba1674d465c9a1fab5`, with identical reviewed,
committed, remote, preview and squash tree
`56f6e9eccae4803c7fc53148d0122ba9e7ccdcc5`.
It supplies successful diagnostic output from one accepted QEMU result.
All fifty fresh author commands and three same-run reusable profiles passed,
as did independent review, committed/main journal-free checks and exact
task-ref cleanup. It preserves all 4,774 predecessor facts and nineteen
fingerprints. Complete Python discovery ran 1,543 cases in 489.779 seconds;
the unchanged fast Python set ran 98 cases in 6.629 seconds.

The fresh MEM04 restart recorded actual native/target Red, then the current
handoff target failed at stage 5: removing the global PC/SP audit exposed
`OWNERSHIP` where the existing handoff contract returns `STATE`. The actual
failure and read-only debugger diagnosis are recorded in the
[fixed-source study](research/minix-selected-user-address-space.md#indirect-handoff-preflight-error-dependence).
The candidate is archived without Green or retirement.
[Accepted ADR-0109](adr/0109-handoff-preflight-error-ownership.md) adds only
explicit normalization at the owning complete handoff checks. Its design is
independently reviewed; implementation remains blocked until separate merge.
No fixture expectation or shared
authority rule is relaxed.
Corrective design #225 separately merged as
`36b26b658abef6a29e7b9ffc8340b3576ea3ccd7`, tree
`ef91993f3c5aea07158df9fbb5ba1fe1ba77cb45`.
The next candidate completed actual native/target Red, Green and all thirteen
recertifications. Its complete producer passed 44 CTests, all 1,559 Python
cases, all forty QEMU workflows and four standalone-image workflows, then
stopped at the kernel-visitor whole-header check in stack certification.
The only header change is the already Accepted ordinary preparation prototype.
The candidate is archived without final acceptance or publication.
[Accepted ADR-0110](adr/0110-kernel-visitor-extension-admission.md) defines
the exact fixed-source header extension and five qualified callback-free
mapping helpers without changing existing callback authority, old facts,
stack budgets or required coverage. Independent design review identified
the following function-inventory boundary before another implementation
restart. Complete correction review is clean and design #226 separately
merged as `593b910cdaaff2a89018b0ffe7cd4e4bf853972c`, with identical reviewed
and squash tree `315baff3f1d62776b36956dc583fa40e6388fa03`.
The fresh candidate passed actual native/target Green. A single fresh
ordinary/metadata stack check passed repaired visitor binding, then failed
the fatal source-unit guard on the ordinary TTY inspector prototype.
This caught the next boundary before recertification and the full producer.
The candidate is archived without final acceptance.
[Accepted ADR-0111](adr/0111-fatal-tty-header-extension.md) has clean
independent design review; it adds one exact header admission and one
immutable existing owner without changing runtime scope or stack budgets.
Corrective design #227 separately merged as
`60e8b8e160b1c063e448cd32b0418535dd63bbb7`, with identical reviewed,
committed, remote, preview and squash tree
`28a01d02f4c12a148627f23d31cb0de512c657f4`.
The fresh implementation independently freezes its 1,497-member base.
Its [evidence map](architecture/selected-user-address-space.md#implementation-and-evidence-map)
retains seventeen production subjects, 29 candidate paths and the original
33 keys, with three exact immutable stack-admission owners. Complete renewed
acceptance and independent implementation review remain required.
Those gates have now passed. MEM04 implementation #228 merged as
`8bd33e58a8e661cff78a8e4aad9d6cbc6a406120`, with identical reviewed,
committed, remote, preview and squash tree
`fb74bc2458a4a3bf4fce45b4e396fa7fb0cde465`.
The final actual attempt passed all thirteen recertification units; B14
ran 503 cases in 377.115 seconds. All fifty final commands passed fresh,
including 44 CTests, all 1,570 Python cases in 568.821 seconds, all forty
QEMU workflows, four standalone images and the complete 44/113/ten-scope
stack gate. Three current same-run receipts, independent review,
journal-free committed/main checks and exact task-ref cleanup passed.
The 33-key transition preserves every predecessor record and reaches
103 total retired identities; later audit owners remain unchanged.

MEM05's early read-only design census then found the
[MEM04 native/source-authority gap](research/selected-address-space-native-source-authority-gap.md).
[Accepted ADR-0112](adr/0112-granular-selected-address-space-native-source-authority.md)
and its [EV01-C06 contract](architecture/selected-address-space-native-source-authority.md)
name 477 already enforced function/file/definition/skeleton/observation
clauses. The design is the sole active prerequisite; it changes no guard,
runtime behavior or retirement. MEM05 design resumes only after the
separate metadata design/implementation pair is reviewed and merged.
The design #229 has separately merged as
`e98f92290e88d928ba6ff4478dd6e33d00718d46`, with reviewed and squash tree
`0fbefc0997e873c2b02f1e904d626c05baa0b4ba`.
Its [metadata implementation map](architecture/selected-address-space-native-source-authority.md#implementation-map)
records the independently frozen 477-row addition, genuine missing-key
Red and bounded historical/current compatibility. All 4,782 predecessor
records, twenty fingerprints, six claims and 103 retirements remain
unchanged. Complete current acceptance and independent implementation
review are still required; no MEM05 runtime correction is claimed.
Hosted CI and later runtime findings retain their separate owners.

EV01-C06 implementation #230 has now completed those gates and merged as
`5748ec20dcdb568a5ccfca2eccfcda09c6dd3f8c`, with identical reviewed,
committed, remote, preview, squash and main tree
`21b1b87ca2a5053d2d909d84cfd3507499949fd6`.
All fifty final commands passed fresh: 44 CTests, all 1,578 Python cases
in 560.893 seconds, forty QEMU workflows, four standalone images and the
complete 44/113/ten-scope stack gate. Same-run receipts, independent
review, journal-free committed/main checks and protected ref cleanup passed.
All six claims and 103 retirements remain unchanged.

Resumed MEM05 design found the distinct
[MEM03 observation-authority gap](research/selected-ownership-observation-authority-gap.md):
its mutable-function guard can accept a source change that the separate
primitive/coverage predicate rejects, without an individual identity.
[Accepted ADR-0113](adr/0113-granular-selected-ownership-observation-authority.md)
and the [EV01-C07 contract](architecture/selected-ownership-observation-authority.md)
name 120 existing current function contracts, including 57 empty sets.
This documentation-only prerequisite is now the sole active outcome.
MEM05 design is blocked; its implementation and algorithm remain unstarted.
No old fact, source guard, runtime finding or hosted-CI result is changed.

EV01-C07 design #231 has separately merged as
`1725b9d70d630c566bb6d9945f29729a47d710a5`, with identical reviewed and
squash tree `ae74883c3c94f4ead680ceb6a32ba97952fa71da`.
Its [metadata implementation map](architecture/selected-ownership-observation-authority.md#implementation-map)
records the independently frozen 120-row declaration, three genuine
missing-key Red failures and focused Green. The 5,379-record current catalog
preserves every old record, twenty-one predecessor fingerprints, six claims
and 103 retirements. No guard is relaxed and no identity retires.
Full current acceptance and independent implementation review remain
required; MEM05 stays blocked until that separate merge.

EV01-C07 implementation #232 has now passed that acceptance and merged as
`abe05cdd7bced5e77e86c1ec12754d017f920cab`, with identical reviewed,
committed, remote, preview, squash and main tree
`2c34dac3c259a4975d1c213e3e14c18d8a075658`.
The final producer passed all fifty commands fresh, 44 CTests, all 1,584
Python cases in 572.589 seconds and complete current target/image/stack
coverage. Same-run receipts, independent review, journal-free main authority
and protected task-ref cleanup passed without a new retirement.

MEM05 design is complete through the
[fixed-source study](research/minix-selected-release-and-wired-handoff.md) and
[Accepted ADR-0114](adr/0114-selected-release-and-wired-handoff-plans.md).
The [contract](architecture/selected-release-and-wired-handoff.md) defines
process-owned indexing, selected-word sets and one protected handoff plan,
with exact source roles, native bounds and truthful target witnesses.
Independent design/correction review is clean.
Its separate merge still precedes implementation; no MEM05 code, test,
model or executable prototype is present in this design phase.

Design #233 has now separately merged. During pre-Red fixture preparation,
the author verified a conflict between the new whole-slot count contract
and one retained same-slot/different-generation MEM03 guard.
The [fixed-source probe](research/minix-selected-release-and-wired-handoff.md#mem05-c01-same-slot-count-evidence-conflict)
confirmed the original `OK`/zero result; the Accepted consumed-chain check
requires `INVARIANT` with output preservation.
The incomplete scaffold is archived before production edits or formal
Red. [Accepted ADR-0115](adr/0115-selected-count-generation-evidence-scope.md)
defines the exact current-fixture exception while preserving historical
results, runtime scope and all final gates. MEM05 implementation is
blocked until that separate documentation-only correction is reviewed,
Accepted and merged. B02-F006/F007 and hosted CI remain open.

Corrective design #234 separately merged as
`7b5aa73cd42a2742cea014c8773879845f78ccd4`, tree
`3b2bacb362577a1538cc4e0fc3c89bef31df6e27`.
The fresh implementation completed its current native executions and the
user-address-space QEMU gate, then the existing handoff corruption fixture
failed at stage 8. Its direct owner/count mutation is no longer a valid
indexed ledger, although that case still requires complete ownership `OK`.
The [fixed-source investigation](research/minix-selected-release-and-wired-handoff.md#mem05-c02-handed-off-corruption-fixture-conflict)
records the actual failure and preserved candidate.

[Accepted ADR-0116](adr/0116-handed-off-leaf-fixture-coherence.md) defines
one additional frozen target owner and an existing-interface replacement
for that fixture setup. The unaccepted implementation is archived.
This documentation-only MEM05-C02 correction has clean independent design
review and is the sole active outcome; runtime implementation restarts
only after its separate merge. No Green event, new retirement, runtime finding
closure or hosted-CI acceptance is claimed.

MEM05-C02 design #235 has now separately merged as
`8f3b19ccae21a06345d05415708cd2dc1db32e76`, with identical reviewed,
committed, remote, preview, squash and main tree
`1a7500dac64cf8605faa2b699c68b2caf6217f6b`.
Complete and Acceptance-only reviews are clean. Fresh committed and
actual-main inspections preserve all prior authority without journals;
completed design refs are removed and purposeful archives preserved.
The fresh [MEM05 evidence map](architecture/selected-release-and-wired-handoff.md#implementation-and-evidence-map)
uses the independently frozen 1,518-file base and thirty-path domain.
It must renew all actual phases and final acceptance before any runtime
finding closes.

MEM05 implementation #236 has now completed those requirements and merged
as `aba02c1f8a583866a85c3012d20c6007b9445a22`, with identical reviewed,
committed, remote, preview, squash and main tree
`7609a9578e8010c735310923a90953833c93e3d5`.
Its final actual attempt passed all thirteen affected units; B14 ran
568 cases in 537.810 seconds. All fifty final commands passed fresh:
45 CTests, all 1,635 Python cases in 729.431 seconds, forty QEMU workflows,
four standalone images and complete 44/113/ten-scope stack coverage.
Same-run receipts, clean independent review, fresh journal-free
committed/main authority and exact completed-ref cleanup passed.
This closes only B02-F006/F007.

The next preferred OBJ01 design found the
[MEM05 native-source authority gap](research/selected-release-handoff-native-source-authority-gap.md).
[Accepted ADR-0117](adr/0117-granular-selected-release-handoff-native-source-authority.md)
and [EV01-C08](architecture/selected-release-handoff-native-source-authority.md)
name 453 existing source comparisons while preserving all 5,387 current
facts, twenty-three fingerprints, seven claims and 185 retirements.
This documentation-only prerequisite is the sole active outcome.
OBJ01 design is blocked until its separate reviewed design/implementation
pair merges; no OBJ01 runtime correction or hosted-CI acceptance is claimed.

EV01-C08 design #237 separately merged as
`d047b387c54e53a7455596052c119ec82a3f2825`, with identical reviewed and
squash tree `2f851fce3e990a047e198d5e8d0f8b4932f91df3`.
Its [metadata implementation map](architecture/selected-release-handoff-native-source-authority.md#implementation-map)
records the independent pre-edit freeze, eleven actual missing-identity
subcase failures and focused Green. The 5,840-record catalog preserves all
5,387 predecessors, twenty-three old fingerprints, seven claims and
185 retirements. Complete current acceptance and independent
implementation review remain required; OBJ01 design stays blocked until
the separate metadata implementation merges.

EV01-C08 implementation #238 has now passed those gates and merged as
`89d3b85314754ae225f01fd16f257745bf19ce71`, with identical reviewed,
committed, remote, preview, squash and main tree
`ce73f3875dd354ef6c8452808d53ab2b9e14d522`.
All fifty final commands passed fresh: 45 CTests, all 1,643 Python cases
in 715.852 seconds, forty QEMU workflows, four standalone images and
complete 44/113/ten-scope stack coverage. Independent review, same-run
receipts, fresh journal-free committed/main authority and protected
completed-ref cleanup passed.

Resumed OBJ01 research found the separate
[object and panic source-authority gap](research/object-and-panic-source-authority-gap.md).
[Accepted ADR-0118](adr/0118-precise-object-and-panic-source-authority.md)
and [EV01-C09](architecture/object-and-panic-source-authority.md) define
fifty precise object metadata repairs and eleven names for existing panic
source comparisons. Only that documentation-only design is active.
OBJ01 design is blocked until its separately reviewed prerequisite pair
merges; no OBJ01 runtime algorithm, retirement or finding closure is claimed.

EV01-C09 design #239 has separately merged as
`92cbd2b45f96df3bda616daab75262d36a2180b7`, with identical reviewed and
squash tree `ebb45050815ba7e6dc7b185c83676492020e5e06`.
Complete correction and Acceptance-only review, fresh journal-free
committed/main checks and protected completed-ref cleanup passed.
Its [metadata implementation map](architecture/object-and-panic-source-authority.md#implementation-map)
records the independent pre-edit freeze, 61 actual missing-binding/key
subcase failures and focused Green. The 5,851-record/25-family candidate
changes only fifty old metadata tuples and adds eleven existing-policy
names. Full current acceptance and independent implementation review
remain required before its separate merge and OBJ01 design resumption.

EV01-C09 implementation #240 has now completed those requirements and
merged as `9b880468192e61177ffe57ec69079fe398160a42`, with identical
reviewed, committed, remote, preview, squash and main tree
`80b93b111b5a1387d812da87c539e56da042777b`.
All fifty final commands passed fresh: 45 CTests, all 1,650 Python cases
in 727.215 seconds, forty QEMU workflows, four standalone images and
complete 44/113/ten-scope stack coverage. Same-run profiles, independent
review, fresh journal-free committed/main authority and protected cleanup
passed. The seven claims and 185 retirements are unchanged.

The resumed [OBJ01 design](architecture/selected-object-and-hart-validation.md)
and [fixed-source study](research/minix-selected-object-and-hart-validation.md)
follow [Accepted ADR-0119](adr/0119-selected-object-and-hart-validation.md).
They define selected scheduler preflight, direct hart/current identity,
regular/exceptional stack proof and the exact ninety-four-key evidence
transition. Only design is active. No runtime code, test, model or
executable prototype precedes separate review, Acceptance and merge.

That design separately merged as #241. The uncommitted implementation
reached its declared native/target transaction, but the full native gate
found one active scheduler-validator neighbor hash missing from that plan.
Its inherited B14 subset also omitted the complete object-layout and two
affected fixture owners. The candidate is archived without implementation
review, permanent commit or full author acceptance.
[Accepted ADR-0120](adr/0120-obj01-complete-source-authority.md) and
[OBJ01-C01](architecture/selected-object-and-hart-validation.md#obj01-c01-complete-source-authority)
define the separate ninety-five-key correction and exact fixture/coverage
boundaries. It changes no runtime algorithm or candidate path. Only this
documentation-only correction is active; OBJ01 restarts after its reviewed
design merge. Hosted CI and later MINIX/POSIX outcomes remain open.

Corrective design #242 has now separately merged after clean independent
review. The fresh [OBJ01 implementation map](architecture/selected-object-and-hart-validation.md#implementation-and-evidence-map)
records its new 1,538-file base, complete ninety-five-key partition,
468 native predicates and corrected immutable fixture/coverage inputs.
All runtime evidence must be renewed; this preparation closes no finding
and does not substitute for the complete current author and review gates.

OBJ01 implementation #243 has now passed those gates and merged as
`34125516b60e9e0a9e5362ff2ee60555691adb99`, with identical reviewed,
committed, remote, preview, squash and main tree
`1de53de9dcf5102dbef3e76d59b7f2fef28cee9e`.
All thirteen recertification units and fifty final fresh commands passed;
complete Python discovery ran 1,670 cases in 864.109 seconds.
The [acceptance record](architecture/selected-object-and-hart-validation.md#implementation-acceptance)
also retains complete current target/stack coverage, same-producer reuse,
independent review, journal-free authority and protected cleanup.
Only B03-F001/F003/F004 close. The next preferred
[OBJ02 design study](research/minix-common-scheduler-return.md) is active;
its implementation, IPC01, hosted CI and later MINIX/POSIX work remain open.

[Accepted ADR-0121](adr/0121-bounded-common-scheduler-return.md) and its
[common-return contract](architecture/common-scheduler-return.md) now specify
that bounded outcome, including current-corruption scope, plan representation
observations and exact context-proof lifetime. Complete independent design
review is clean. This remains documentation-only until the separate design
merge; implementation has not started.

That design separately merged as #244. Its uncommitted implementation obtained
actual native/target Green, but complete B14 recertification exposed three
omitted historical test owners and existing adapter integration errors.
The [observed conflicts](research/minix-common-scheduler-return.md#obj02-c01-observed-consumer-scope-conflicts)
and preserved candidate are not final acceptance.
[Accepted ADR-0122](adr/0122-obj02-evidence-consumer-scope.md) is the sole active
correction; it retains the runtime and exact retirement scopes. A fresh OBJ02
implementation follows its separately reviewed merge. The broader workflow
simplification remains after OBJ02 rather than becoming parallel work.

Corrective design #245 has now independently merged as
`cc6e0eee984ea1731eca9fc02bc4132ead1d2e2c`, with identical reviewed,
committed, remote, preview, squash and main tree
`5d2a9d6022b08b20cba75b6b1c33da5f6df4db09`.
Journal-free committed/main authority and completed-ref cleanup passed.
The fresh [implementation map](architecture/common-scheduler-return.md#implementation-and-evidence-map)
freezes the actual 1,551-member base and corrected consumer inputs.
No prior execution is borrowed; complete renewed acceptance remains required.

Current Milestone 2 progress includes trap recovery, supervisor timer
interrupts, the bootstrap frame allocator, the Sv39 kernel address space, and
generation-safe process/thread/hart identity tables with per-hart trap and
timer ownership. A typed per-frame ledger now binds allocator geometry to exact
kernel/process-generation owners and provides the staged one-way VM-handoff
gate. Generation-safe per-process Sv39 roots now share immutable kernel
entries while owning private, typed user mappings. U-mode entry, saved
execution contexts, and thread-owned kernel stacks now complete the first real
user round trip. The portable MINIX-style queues and accounting model now drive
repeated two-address-space switching under real timer interrupts, including a
real no-runnable idle transition and timer wake. Endpoint privileges and
generation-safe lifecycle gates are complete. The fixed message ABI and
dormant IPC state are present, and sender/receiver queue topology is validated.
Failure-atomic held-thread enqueue and matching delivery commit are
implemented. Incoming held senders and receivers consume the first compatible
FIFO peer, preserve unmatched peers, stage the canonical message, retain call
senders in reply wait, and make ordinary participants runnable through the
scheduler. Consuming a queued ordinary sender also stages its canonical
no-message `OK` completion, while a consumed call remains completion-free in
reply wait. A failure-atomic portable helper stages canonical no-message
success or dead-endpoint completion for target integration. Portable ordinary
send and receive now enforce exact active
endpoints, operation and target-profile authorization, canonical snapshots,
immediate matching before blocking, and specific or `ANY` FIFO selection.
Portable call now allocates monotonic nonzero reply tokens from fixed kernel
state, binds each token to the exact caller thread and callee generation,
delivers it through immediate or queued request paths, and retains the caller's
reply buffer while only reply wait remains. Portable reply now resolves an
opaque token with a bounded thread scan, requires the exact active callee and
reply operation, stages a canonical token-zero response, consumes the
one-shot right, and wakes the caller only when no independent run-time flag
remains. Another thread owned by the callee endpoint may use the token only
after the token-bearing request completion returns; an early attempt preserves
all state and reports a reply-token error. Validation rejects duplicate staged
copies carrying one call token. The token bypasses only the ordinary
send-target mask. These operations currently consume scheduler-held
non-current callers. Portable
`reply_receive` now shares reply's complete token and message preflight,
requires its distinct operation authority, validates a specific source or
`ANY` plus the receive buffer, and commits the caller reply together with
either one immediate FIFO sender or one receiver-queue insertion. Scheduler
and queue failures preserve both the token and complete state. Portable
notification now requires a nonzero event mask, exact active source and
destination generations, and notify-profile authority. It never blocks:
the first compatible specific or `ANY` receiver outside reply wait is staged
and awakened immediately, while all other events OR-coalesce by source slot.
Ordinary receive and `reply_receive` select the lowest matching pending source
before FIFO senders, revalidate its live generation, and stage the canonical
kernel envelope with a zero token and payload tail. Validators preserve the
bitmap/event relation and prevent source reuse while an event remains
pending. Bounded v0.1 deadlock detection now runs only after matching fails and
before send, receive, call, or `reply_receive` blocking mutation. It follows
`SEND`, then `REPLY`, then specific `RECEIVE`, treats `ANY` as no dependency,
resolves exact endpoint/process generations through the checked sole-live-
thread projection, rejects candidate returns as deadlock, and treats repeated
intermediate threads as corruption. `reply_receive` checks the graph after
simulating reply completion. Endpoint close now preflights every exact-
generation queue, token, notification, and staged message reference before one
non-failing cancellation commit. It unlinks held closing-process threads,
wakes foreign senders, specific receivers, and reply waiters with
`DEAD_ENDPOINT`, cancels caller/callee reply rights, clears pending
notifications in both directions, and preserves unrelated `ANY` receivers.
An owned staged `DEAD_ENDPOINT` result from an earlier close is discarded when
its endpoint later closes, allowing cancellation to cascade. Successfully
staged messages, notifications, or no-message completions must instead be
drained before close; their rejection is failure-atomic. The final commit
finishes through the ADR-0029 lifecycle close without exposing a partial
transition. ADR-0030's portable
acceptance evidence now includes one persistent replayable 8,192-transition
model with three multithreaded model processes. It compares complete IPC and
scheduler state after every mixed operation, denial, malformed input, close,
and reuse. An isolated QEMU gate exercises the same production portable
mechanism with three exact process generations and trusted kernel-owned
messages, then restores the object, endpoint, and scheduler baseline. Target
endpoint lifecycle and IPC component paths now share one authoritative,
failure-atomic, one-shot registry runtime. Portable no-message, message, and
failure completion shapes are now canonical. One shared selected-thread return
path preflights and commits them across scheduler start, ordinary U-trap,
captured IPC return, and idle wake. It performs bounded user-buffer copy,
retains the exact prevalidated physical chunks through commit, patches stable
`a0` results, stores the exact selected context, clears the completion, and
only then commits scheduler selection after timer/accounting preparation.
Malformed residual non-pending completion state is an invariant failure.
Production U-mode `ecall` routing now captures the six stable operation
numbers, advances `sepc`, preflights target arguments and buffers, executes the
current-thread guard transaction, maps stable results, stages immediate
completion, and selects ordinary or captured return. A focused QEMU gate proves
real instruction entry, successful notify commit, unauthorized-send rollback,
upper endpoint rejection, register preservation, and baseline restoration.
The complete three-address-space syscall gate now executes all six operations,
blocking/wakeup, token routing, atomic reply/receive, notification coalescing
and reply-wait exclusion, cross-page request/reply buffers, priority wakeups,
stable negative results, deferred completion across a timer-selected peer,
exact register results, and full teardown. An isolated image revokes an
accepted return buffer and proves the
exact `invalid-bootstrap-ipc-buffer` panic before scheduler commit.
ADR-0037's kernel-origin IPC mechanism now injects source-`NONE` event masks,
wakes `ANY` receivers without a notifier thread, OR-coalesces deferred masks,
and returns them through the shared selected-thread completion path.
Arbitrary-address
generation-bound translation and bounded two-page IPC message snapshot/write
are now implemented for that target boundary. The scheduler now provides a
reversible current-thread IPC guard with exact ready-queue and trap-stack
rollback plus head-preserving immediate commit.
ADR-0038's generation-safe kernel grant registry and lifecycle are now
implemented with exact participant authority, revoke-time generation advance,
terminal quarantine, failure-atomic endpoint cancellation, an authoritative
target runtime, a 4,096-transition model, and endpoint QEMU evidence. Checked
copy is the next separate design-implementation pair. The user-runtime syscall
ABI remains a later reviewed task.
ADR-0039's one-page-bounded checked copies are now implemented using exact
grant participants, retained at-most-two-page physical plans, explicit
direction and bounds, failure-atomic commit, native/model evidence, and a
three-address-space QEMU grant gate. This implementation is
initially bootstrap-phase-only; the separate ADR-0040 outcome supplies its
post-handoff mapping authority. The user syscall/runtime ABI remains outside
this outcome.
ADR-0040's replacement is now implemented for the statically embedded service
set:
every live bootstrap leaf must be staged `VM_WIRED` before ownership commit,
and read-only root validation, lookup, translation, and activation then use
exact wired process-generation ownership. Transferable-frame mapping, VM fault
delivery, and mutation remain later VM work. Native owner classification and
an isolated QEMU gate prove failure-atomic handoff, handed-off IPC/grant access,
revoked mutation, bootstrap-only generic context preparation, and a real
handed-off user return.
ADR-0041's unified RISC-V `ecall` boundary is now implemented. IPC operations
1 through 6 remain unchanged, while grant create, revoke, copy-from, and
copy-to use operations 7 through 10 with fixed register layouts, stable
generic results, and exact current-process authority. Native ABI tests, a
three-process bootstrap cleanup gate, and retained-state handed-off
integration prove real ecalls, stable failures, register preservation,
generation reuse, and both copy directions.
ADR-0042's freestanding user-service runtime is now implemented. It supplies
one fixed standalone ELF at the existing user window, loader-owned BSS and
external stack initialization, a no-argument service entry, one raw RV64
`ecall` function, typed wrappers for operations 1 through 10, direct stable
results without `errno`, and only `memcpy`/`memset` compiler support. Native
raw-stub tests prove exact argument, result, output, and token preservation.
The repository-owned ELF checker rejects forbidden segments, sections,
relocations, undefined symbols, startup mutations, and noncanonical raw-stub
instructions. The isolated three-process QEMU image proves the checked ELF
loader contract, startup, stack/data/BSS/rodata behavior, blocking IPC,
register preservation, grant lifecycle and copies, accidental-return trapping,
and complete cleanup.
ADR-0043's static bootstrap launcher is now implemented. The kernel validates
the pointer-free manifest and generated images, prepares every static service
while held, publishes exact profiles/endpoints/scheduler state atomically,
enforces token-bound readiness and guest-owned deadlines through operation 11,
and seals launcher authority through exact `SOURCE_ONLY` state. Native models
and three isolated QEMU workflows prove success, timeout, cyclic-manifest
failure, and stable diagnostics.

ADR-0045's VM server and one-way ownership handoff core are now implemented.
The kernel constructs one fixed VM boot-information/frame database, stages
every static user frame wired, validates exact VM-only operation 12, publishes
ownership irreversibly before generic VM readiness, and retains wired IPC and
grant-copy authority. A real launcher/VM/probe QEMU workflow proves independent
kernel/VM database agreement, post-handoff return, launcher sealing, and a
bidirectional checked-copy grant exchange. Running- and sealed-phase VM
self-fault workflows prove exact identity/register diagnostics, bootstrap
service-fault ordering, and the post-seal no-bootstrap-failure path. This
completes the static Step 9 boundary. Dynamic mappings and non-VM page-fault
delivery remain outside that minimal boundary.
PM's later tokenized preparation transition remains the only post-handoff
context-creation replacement.

ADR-0046 defines the dependency-ready PM model boundary. PM owns a bounded
application table, monotonic semantic PIDs, parent/child and init-reaper
relationships, spawn metadata, exit, zombie, and wait state. Launcher sealing
delivers one exact kernel-origin event that enables runtime PM policy.
ADR-0050 now extends PM-only kernel control across hidden process reservation,
token-bound execution preparation and rollback, atomic activation, ordered
exit detach, and empty-process release. An isolated launcher/VM/PM QEMU fixture
loads and runs a synthetic four-page child and returns every child resource to
baseline. The production PM/VFS/VM coordinator now adds ordinary protocol
composition, exact rollback, immediate PM publication commits, and ordered
exit/wait completion with a separate mixed native reference model. Its
[implementation map](architecture/pm-spawn-coordinator.md) records ownership
and validation. The checked application runtime is merged as #127;
the final dynamic PID-1 spawn/wait fixture now composes the dependency-ready
Step 14 production mechanisms.

The application-runtime slice is merged after the separately
reviewed ELF-checker correction #126. Its
[implementation map](architecture/application-runtime.md) fixes the separate
startup/decoder/normal-return/image owners and native/component evidence
under existing ADR-0050. Code now proves checked application entry and normal
return through the existing PM component, plus standalone image/seed parity.
Implementation #127 passed the complete author gate and independent review
and merged with the identical reviewed tree. The
[final ELF spawn integration map](architecture/elf-spawn-integration.md)
records the implemented six-production-service dynamic PID-1 gate.
Implementation #128 runs real post-freeze descriptor rollback, inherited
I/O, child normal exit and one-shot wait, complete post-init resource
equality and physical UART drain. Its merge requires the current complete
author gate and clean independent review; production init/shell remain later.

ADR-0047's static TTY boundary is now implemented. VM installs one exact
post-handoff UART device leaf before TTY release; TTY then initializes the
NS16550A, commits console ownership through operation 14, and acknowledges
readiness under the original begin-time deadline. The kernel claims PLIC
source 10, notifies only the bound TTY generation, and retains the source in
service until TTY drains the device and explicitly completes it. The real TTY
service provides bounded canonical input, interrupt-driven output, strict
request identifiers, explicit completion collection, and exact-direction
VFS grants. A five-service QEMU gate injects serial input only after an exact
TTY marker and proves canonical erase/CR handling, checked grants, retained
claim/completion evidence, physical UART drain, and clean shutdown. This
completes the Step 11 boundary; production VFS descriptors, RAMFS, init, and
shell integration remain later steps.

ADR-0048 defines the dependency-ready RAMFS boundary. One exact VFS peer
mounts a validated build-generated seed into a fixed writable node and sparse
data-block arena, then uses versioned lookup, create, mkdir, read, write,
getdents, and putnode calls. Generation-safe node handles, separate link and
reference counts, root-confined whole-path traversal, exact-direction
one-page grants, all-or-error writes, and complete-record directory cursors
preserve the selected MINIX behavior without adding a block layer. The design
is implemented with native seed/state tests, a replayable 8,192-transition
model, one six-service QEMU scenario, a standalone image check, and fail-closed
validation ownership.

ADR-0049 defines the dependency-ready production VFS boundary. VFS mounts
RAMFS once, owns fixed per-process descriptors, shared open-file descriptions,
vnode/backend-reference aggregation, root and working-directory routing, and
one synthetic console object. Application open, close, read, write, getdents,
mkdir, and chdir calls use one application grant hop and one separately
authorized RAMFS or TTY hop through two resident pages. A non-copying
grant-range preflight protects consuming reads, and VFS translates backend
directory records into a stable application format. The design requires a
replayable 8,192-transition model, a bounded standalone image, and one isolated
seven-process QEMU application probe while the production manifest remains
six services. Capacity seven preserves a 128-byte service configuration and
moves the VM boot object to an exact 364,704-byte layout.

ADR-0051 defines responsibility-based source organization before the next VFS
lifecycle increment. The portable VFS core must be decomposed by state
validation, ownership, transfer, namespace, regular I/O, console, process, and
spawn authority before Step 14 exit cleanup is added. The refactor preserves
the accepted VFS behavior. The VFS decomposition is now merged, and the
[repository source-module audit](architecture/source-module-audit.md)
classifies the remaining in-scope implementation/test source review signals
into separate subsystem design and implementation pairs rather than one
repository-wide rewrite. ADR-0052 defines the first correction: preserve the
existing VFS test executions while separating fixture, core, spawn, service,
model, target, kernel-hook, and fixture-generation source ownership before
exit-cleanup coverage is added. That correction and the exit-cleanup slice
are merged. The ADR-0053 design and implementation are also merged, applying
the exact
[kernel address-space source-organization contract](architecture/kernel-address-space-source-organization.md).
ADR-0054 defined the frame-ownership correction: apply the exact
[frame-ownership source-organization contract](architecture/frame-ownership-source-organization.md).
That design and implementation are merged. ADR-0055 defined the following
correction: apply the exact
[kernel VM-handoff source-organization contract](architecture/kernel-vm-handoff-source-organization.md)
before later PM and VM coordination extends operation 12 and its lifecycle
evidence. That design and implementation are merged. ADR-0056 defined the
following correction: apply the exact
[kernel PM-control source-organization contract](architecture/kernel-pm-control-source-organization.md)
before the cross-service coordinator extends operation 13 and its lifecycle
evidence. That design and implementation are merged. ADR-0057 defined the
following correction: apply the exact
[VM service source-organization contract](architecture/vm-service-source-organization.md)
before the coordinator extends VM loading and release behavior. That design
and implementation are merged. ADR-0058 defines the PM correction:
apply the exact
[PM service source-organization contract](architecture/pm-service-source-organization.md)
before the PM/VFS/VM coordinator. Its reviewed design is Accepted and merged.
The implementation now separates the specified lifecycle, adapter, native
evidence, kernel report, and generator owners, with empty migrations and
fail-closed structural enforcement. It does not add the later coordinator.

The next shared-mechanism audit correction is the kernel IPC design in
Accepted
[ADR-0059](adr/0059-kernel-ipc-source-decomposition.md) and its exact
[source contract](architecture/kernel-ipc-source-organization.md).
It is a behavior-preserving maintenance outcome over the already implemented
Step 6/7 mechanisms, not a new runtime dependency node. Its independently
reviewed design is Accepted and merged. The implementation separates the
specified portable, captured-syscall, native/reference-model, and target
owners, with empty migrations and fail-closed structural enforcement. It
adds no new IPC behavior or execution.

The bootstrap maintenance correction follows Accepted
[ADR-0060](adr/0060-kernel-bootstrap-source-decomposition.md), with the exact
[bootstrap source contract](architecture/kernel-bootstrap-source-organization.md).
It separates immutable validation, privileged preparation/rollback,
diagnostics, captured control, boot entry, launcher and native evidence
without changing the runtime graph or bootstrap behavior. Its independently
reviewed design is Accepted and merged. The implementation now exposes the
specified source owners and semantic phases with empty migrations, preserved
evidence and fail-closed structural enforcement. It adds no bootstrap behavior,
application runtime or dynamic PID-1 integration.

The reviewed trap/syscall correction follows Accepted
[ADR-0061](adr/0061-trap-and-syscall-source-decomposition.md) and its exact
[trap/syscall source contract](architecture/trap-syscall-source-organization.md).
It preserves the current trap/call algorithm, register custody, and runtime
evidence while exposing named authority and task phases. Its independently
reviewed design is Accepted and merged as #121. The implementation now
separates the specified owners with empty fourteen-symbol/nine-function
migrations, unchanged architecture and runtime surfaces, and fail-closed
source/build/evidence enforcement. It adds no application runtime or dynamic
PID-1 behavior.

The next selected high-risk dependency-ready correction is Accepted
[ADR-0062](adr/0062-kernel-scheduler-source-decomposition.md) and its exact
[scheduler source contract](architecture/kernel-scheduler-source-organization.md).
The design separates queue, IPC transaction, validation, planning/commit,
accounting, target completion/return/interrupt/idle, and independent evidence
owners while retaining all current behavior. It names 38 C sources, six
private contracts, six source sets, 138 moving definitions, and twelve
semantic long-function splits. Its independently reviewed design merged as
#123. Implementation now separates all specified portable, target-runtime,
and evidence owners with empty owner/function migrations and retained
execution identities. Its complete author gate and independent read-only
implementation review passed, and the implementation merged as #124 with an
identical validated/reviewed squash tree.
This does not add application startup, dynamic PID-1 integration, or a new
runtime dependency.

The next selected tooling correction is Accepted
[ADR-0063](adr/0063-user-elf-checker-source-decomposition.md) and its exact
[user ELF checker source contract](architecture/user-elf-checker-source-organization.md).
It separates parsing, structure, service policy, instructions, CLI, fixtures,
and case discovery without changing accepted service images or diagnostics.
The correction precedes application-checker reuse in `elf-spawn-app-runtime`;
it is not a new runtime-DAG node or an application-runtime implementation.
Independent design review is clean after correction of one image-inheritance
gap, and the design merged separately as #125. Implementation now separates
all specified owners and both semantic phase roots with literal-empty
migrations, unchanged fixture/case identities, and added CLI contracts.
Implementation #126 passed full author validation and clean independent
implementation review and is merged. Application
startup is not part of this source-only outcome.

The selected RAMFS maintenance correction is Accepted
[ADR-0066](adr/0066-ramfs-source-decomposition.md), with the exact
[RAMFS source contract](architecture/ramfs-source-organization.md).
It exposes portable state/codec/ownership/namespace/I/O/directory work,
native and independent-model evidence, target peer/report, and generator
phases without changing Step 12 behavior. Its documentation-only design and
clean bounded correction review merged as #133. Implementation now exposes
all specified source owners and semantic outlines, with both active
migrations empty and retained native/model and real QEMU evidence.
Implementation #134 passed the complete 47-command fresh author gate,
bounded actual three-profile reuse acceptance and independent read-only
review, then merged with the identical validated/reviewed squash tree.
This correction adds no production init, shell, command, or runtime-DAG node.

The endpoint maintenance correction follows Accepted
[ADR-0067](adr/0067-kernel-endpoint-source-decomposition.md) and its exact
[endpoint source contract](architecture/kernel-endpoint-source-organization.md).
It defines representation, profile, complete validation, publication,
source-only/close, and native/model/target owners over the existing Step 6/7
mechanisms. Design #135 and implementation #136 are independently reviewed
and merged. All 98 original definitions have their required owners, both
migrations are empty, and 151 current C definitions satisfy the required
task boundaries. The full 47-command fresh author plan and additional actual
three-profile reuse passed before exact reviewed-tree squash and branch
cleanup. No endpoint behavior or runtime dependency was added.

Kernel-object maintenance follows Accepted
[ADR-0068](adr/0068-kernel-object-source-decomposition.md), with the exact
[object source contract](architecture/kernel-object-source-organization.md).
It names 23 C owners, three private headers/sets, 72 moves, eight long and
four shorter task dispositions over the existing 76-definition object
family. Public layouts and the cohesive runtime remain unchanged; the
scheduler/base-validator direction, original storage and full source/recipe
oracles are explicit. Independently reviewed design #137 is merged.
Implementation #138 is independently reviewed and merged with all 23 real
owners and twelve semantic dispositions, both migrations literal empty and
128 definitions bounded by 92 physical lines. Full fresh author acceptance,
actual same-producer three-profile reuse, identical reviewed squash tree and
local/remote branch cleanup close this maintenance outcome.

The grant maintenance correction follows Accepted
[ADR-0069](adr/0069-grant-data-plane-source-decomposition.md), with the exact
[grant data-plane source contract](architecture/grant-data-plane-source-organization.md).
It preserves the cohesive grant registry and existing Step 7 authority while
defining range/copy, captured syscall, native/reference-model and real
target-script boundaries: 22 C owners, three private headers/sets, four
consumer operands, 65 moves and fourteen long plus five shorter dispositions.
This independently reviewed design contains documentation only and merged
separately as #139. Implementation #140 has all 22 real owners, 217 definitions
bounded by 115 physical lines, all 65 original moves and nineteen semantic
dispositions, and literal-empty migrations under immutable original-body/type
facts. Complete current 47-command fresh author acceptance, actual
same-original-producer three-profile reuse and independent review passed.
Implementation #140 is merged with identical reviewed squash tree, clean
main fast-forward and exact local/remote branch cleanup.

The next single maintenance design is Accepted
[ADR-0070](adr/0070-tty-function-and-evidence-decomposition.md), with the exact
[TTY source contract](architecture/tty-source-organization.md).
It preserves cohesive terminal/UART/control behavior while defining runtime,
service, model, native-double and generator responsibilities: 26 C owners,
five private headers/sets, eight consumer operands, 116 moves and eight
over-limit C/Python dispositions. Design #141 is independently reviewed and
merged. Implementation #142 is blocked and abandoned because the original
complete model validator needs a reference-owner edge omitted by the graph.
Accepted [ADR-0071](adr/0071-tty-model-validation-reference-dependency.md)
has independently passed complete and acceptance-only design review and
separately merged as #143 with identical reviewed tree. Implementation has
restarted from that current `main` through test-first green slices; abandoned
results remain historical rather than current evidence.
These corrections add no runtime node,
clock/deadline, application startup or shell behavior.

Independently reviewed implementation #144 completes all 26 real TTY source owners,
116 original moves and eight required dispositions under immutable
body/type/data facts. Both migrations are literal empty and all current
C/renderer functions are at most 118 physical lines. Complete current
47-command fresh author acceptance and actual same-original-full-producer
three-profile reuse passed. It is merged as `34f1048` with identical reviewed
tree, clean main fast-forward and exact obsolete branch cleanup.
This is not v0.1 completion.

The next single maintenance task is Accepted
[ADR-0072](adr/0072-qemu-runner-source-decomposition.md) and the exact
[QEMU runner source contract](architecture/qemu-runner-source-organization.md).
It exposes existing outcome/report, argv, process/serial, CLI and host-case
owners without changing accepted markers, commands or budgets. The exact
source-family declaration transports the existing closed argument builder;
no gate, receipt, default-config or cross-HEAD rule is relaxed.
Complete independent design/correction and acceptance-only reviews are clean.
Design #145 separately merged as `934faa9` with its exact reviewed tree and
obsolete branches removed. Implementation #146 is independently reviewed
and merged as `f524a23`, including the justified direct-script namespace
correction. Complete current fresh author and actual same-original-producer
reuse passed; the squash tree equals the reviewed tree, main fast-forwarded
clean and exact obsolete branches are removed.

The kernel directory maintenance outcome follows Accepted
[ADR-0073](adr/0073-kernel-directory-layout.md) and its
[exact source contract](architecture/kernel-directory-layout.md).
Complete independent design review is clean and design #147 is separately
merged. Implementation #148 moved all 373 files into fourteen production and
sixteen target-evidence directories, preserves logical source owners,
diagnostics and evidence, and finishes its physical-owner migration empty.
Its complete fresh producer, bounded same-run reuse, independent correction
review, identical squash tree, main fast-forward, and exact branch cleanup are
verified. It adds no runtime-DAG node. Production init/shell remains the later
node Q, not part of this maintenance task.

The kernel-directory acceptance also exposed avoidable validation
orchestration latency: a complete 47-command pass could not publish two
required receipts because their profiles were not eligible before execution.
Accepted
[ADR-0074](adr/0074-fail-fast-validation-producer-readiness.md) defines a
separate fail-fast producer-readiness design. Independent design/correction
review is clean. It changes no runtime-DAG node, coverage, receipt identity,
or default fresh behavior. Implementation #150 is separately merged with its
exact reviewed tree, real blocked-producer evidence, current producer/reuse
evidence, main fast-forward, and branch cleanup.

The next host-tooling design follows the user-selected latest stable Python
policy. Accepted
[ADR-0075](adr/0075-cpython-3-15-host-automation-floor.md) pins CPython 3.15
final rather than a rolling latest version, replaces the Xcode Python reuse
profile with a closed Homebrew 3.15 runtime, disables site/OpenSSL/environment
inputs, closes every child interpreter, and defines a stable project-owned AST
evidence format without digest rebaselining. It adds no runtime-DAG node.
Independent design review is clean after closure of OpenSSL, child-process,
AST-evidence, and consumed-launcher boundaries. Design #151 is separately
merged. The dependent implementation now centralizes exact CMake/runtime
selection, advances reusable evidence to
`darwin-arm64-ninja-python315-v2`, closes the Homebrew/OpenSSL/child-process
inputs, and preserves every prior AST digest through one shared serializer.

The Python implementation gate then exposed a measured target hot-path defect:
the ELF-spawn image can cross its timeout while the hart repeatedly scans the
fixed 262,144-entry frame ledger from user translation. Accepted
[ADR-0076](adr/0076-bounded-complete-frame-ownership-validation.md) preserves
complete corruption detection and capacity while replacing per-frame inactive
bitmap/helper work with a bounded word-aware scan. The Python implementation
was blocked until that separately reviewed implementation passed the no-retry
VFS-to-ELF-spawn reproducer. Independent design/correction review is clean.

The dependent implementation now uses bitmap-word active traversal and a
direct inactive owner/target plus bitmap-word tail scan. Native semantic
guards, the frame-ownership QEMU component, and the no-retry VFS-to-ELF-spawn
reproducer, complete producer/reuse evidence, and clean independent review
passed. Implementation #153 is merged with the exact reviewed tree and its
task branches are removed.

The resumed Python implementation then exposed a second distinct threshold in
the same final acceptance sequence. The unchanged VM service could pass or
hit its 10-second guest readiness deadline while executing
`vm_core_boot_database_is_immutable()`. Accepted
[ADR-0077](adr/0077-bounded-complete-vm-boot-database-validation.md) preserves
the exact boot digest and complete VM-state validation while replacing
262,144 per-frame bitmap helper calls and linear inactive-zero hashing with a
bitmap-word active scan, complete inactive tail checks, and logarithmic FNV
zero-run advancement. The Python implementation was blocked until the separate
design and implementation passed the exact no-retry acceptance sequence.

Implementation planning then found one design-evidence defect before code:
ADR-0077's valid managed-count-1 case cannot construct the existing complete
two-space/three-mapping VM fixture, whose first free frame is index 6.
Accepted
[ADR-0078](adr/0078-valid-vm-boot-database-boundary-evidence.md) supersedes only
that native case with valid count 6 while retaining 63/64/65, the observed
shape, maximum boundaries, and coherent zero/maximum-plus-one corruption. It
changes no production contract.

The dependent implementation now uses segmented immutable-prefix hashing, one
cached initial-free bitmap word per active chunk, exact inactive byte/bitmap
tail checks, and logarithmic FNV zero-run advancement. The structural Red,
sanitizer-backed VM-core semantics, VM-handoff component, and exact one-shot
VFS-to-ELF sequence, complete producer/reuse evidence, and independent review
passed. Implementation #156 is merged with the exact reviewed tree. The Python
implementation has resumed from current `main`.

## v0.1 completion goal

The independent validation-efficiency task follows Accepted
[ADR-0064](adr/0064-fail-closed-validation-evidence-reuse.md) and its
[exact evidence contract](architecture/validation-evidence.md).
Original design #129 is merged. Implementation #130 was abandoned after a
concrete default-configuration closure omission. Corrective design #131 and
Accepted [ADR-0065](adr/0065-closed-tool-default-configuration-for-validation.md)
are merged. Implementation #132 is separately merged and follows both
contracts and the canonical source/evidence map. Default and full validation
remain fresh; opt-in reuse requires exact current-candidate/current-context
evidence and never reduces mandatory coverage. RAMFS #134 subsequently
demonstrated real native, smoke and TTY-image reuse after its bounded source
recipe maintenance. Endpoint #136 also preserves that real eligibility
after its separately reviewed bounded recipe update. This does not alter the
runtime DAG.

`micros` v0.1 is complete when a clean checkout builds with the documented
LLVM/LLD/CMake/Ninja toolchain, boots under RISC-V64 QEMU `virt` through
OpenSBI, and reaches a shell that runs `echo`, `cat`, `ls`, and `ps`.

The system must enforce user isolation and preemption and must include
generation-aware IPC, one-shot reply rights, privilege profiles, direct grants,
the static launcher, VM, PM, TTY, RAMFS, VFS, ELF spawn, init, and automated
native/QEMU/end-to-end tests.

Although v0.1 runs one user thread per process on one hart, process, thread,
endpoint, and hart state must remain distinct. No v0.1 implementation may make
multithreading or SMP require replacing the process identity, scheduler object
model, IPC reply model, or global current-execution representation.

## Long-term completion goal

Accepted
[ADR-0081](adr/0081-complete-posix-compatible-system-goal.md) defines the final
product as a complete POSIX-compatible operating system comparable in breadth
to MINIX. v0.1 remains the first integrated checkpoint, not the final feature
boundary.

Every v0.1 omission of process, thread, signal, credential, terminal, storage,
memory, IPC, networking, libc, shell-language, or utility behavior is a staged
future outcome. A mandatory requirement is conditionally inapplicable only
when POSIX itself says so and the exact condition does not hold. MINIX remains
the internal mechanism baseline; POSIX.1-2024 defines the final portable
interface and behavior.

The closed
[staged-omission manifest](research/posix-staged-omission-manifest.md) owns
every current deferral exactly once.

## Milestone 0: design baseline

### Deliverables

- architecture overview;
- MINIX dependency analysis;
- development DAG;
- testing strategy;
- initial ADR set;
- repository documentation index.

### Exit criteria

- every initial ADR is reviewed and either Accepted, revised, or Rejected;
- the shell MVP boundary is unambiguous;
- the implementation order contains no dependency cycle;
- no implementation files are committed before this gate.

## Milestone 1: reproducible boot

### Deliverables

- RISC-V cross-compilation configuration;
- linker script and boot image;
- OpenSBI/QEMU launch command;
- bounded FDT parser for memory and reserved ranges;
- UART output;
- panic path;
- deterministic test shutdown.

### Exit criteria

- a clean checkout builds with one documented command;
- QEMU prints a versioned boot marker;
- an intentional panic prints location and machine state;
- the host gates distinguish success, explicit failure, panic, unexpected exit,
  and timeout.

## Milestone 2: privileged kernel mechanisms

### Deliverables

- trap entry and exception decoding;
- timer interrupts;
- bootstrap frame allocator;
- typed bootstrap frame ownership and atomic handoff classification;
- kernel and user page tables;
- user-mode entry;
- separate process, thread, endpoint, and hart objects;
- thread execution contexts;
- preemptive round-robin scheduler.

### Exit criteria

- allocator and typed-ownership invariants pass randomized host tests;
- expected exceptions return safely;
- U-mode cannot access kernel-only pages;
- two user threads can be preempted repeatedly without register corruption.

## Milestone 3: protected communication

### Deliverables

- process slots and generation-aware endpoints;
- immutable privilege profiles;
- one-shot reply tokens bound to caller threads;
- blocking send, receive, call, reply/receive, and notification;
- deadlock-chain detection;
- direct grants and safe-copy operations;
- unified user grant syscalls;
- wired post-handoff address resolution for initial services;
- one fixed standalone service ELF and freestanding user-service runtime.

### Exit criteria

- stale endpoints and unauthorized IPC are rejected;
- every blocking transition has a tested wakeup path;
- deadlock tests terminate deterministically;
- grant direction, bounds, overflow, endpoint, and lifetime checks pass;
- real U-mode grant create, revoke, and both copy directions preserve stable
  results and non-result registers;
- wired service roots retain activation, IPC-buffer, and checked-copy access
  after the irreversible ownership handoff;
- one independent user ELF has exact RX, R, and RW/NX load closure, no
  relocation or hosted-libc dependency, loader-zeroed BSS, and an external
  aligned stack;
- startup establishes the reviewed `gp`/`tp` policy, enters the no-argument C
  service function, and traps deterministically if that function returns;
- host-stub tests cover typed wrappers for operations 1 through 10, and one
  real QEMU image proves those production operations through the standalone
  runtime while preserving registers, stack state, initialized data, BSS, and
  read-only data;
- no protocol relies on raw pointers crossing an address space.

## Milestone 4: bootstrap and memory service

### Deliverables

- pointer-free, versioned embedded service manifest with seven-entry storage
  and an exact six-entry production chain;
- generated immutable references to already checked service images;
- deterministic explicit-prerequisite topology and cycle rejection;
- kernel preparation of every static root, image, stack, context, reserved
  endpoint, and held thread before launcher entry;
- bootstrap launcher with manifest-bound release authority only;
- atomic exact-profile installation, endpoint publication, and thread release;
- one exact-generation readiness call and token-bound acknowledgment;
- guest-owned readiness deadlines and stable fatal diagnostics;
- exact manifest privilege, UART-page, and IRQ-source assignments;
- irreversible launcher authority revocation after the last static service;
- VM server;
- one-way ownership handoff from bootstrap memory to VM.

### Exit criteria

- manifest validation rejects unknown versions, malformed entries, missing
  profiles or images, invalid resource fields, missing prerequisites, and
  cycles before manifest-directed service mutation;
- capacity-seven consumers agree on the exact 128-byte service configuration
  and 364,704-byte VM boot-information offsets, active counts, zero tails, and
  digest extent;
- launcher order is deterministic and independent from source array order;
- unreleased endpoints remain hidden and held services remain off ready queues;
- release installs only the exact named immutable profile and publishes one
  endpoint generation atomically with exact scheduler admission;
- malformed, foreign, early, duplicate, and stale readiness cannot release a
  dependent;
- a missing readiness response fails inside the guest with the exact service
  identity and no host-timeout dependence;
- failure before publication preserves or rolls back hidden state, while
  failure after publication is fatal with no recovery fallback;
- VM's generic ready transition follows the irreversible ownership handoff;
- TTY release and readiness retain the ADR-0012 begin/commit gates;
- final completion holds the launcher, clears the exact controller binding,
  and makes bootstrap authority impossible to reintroduce while its retained
  source-only endpoint rejects new destinations until later teardown;
- every usable physical frame has exactly one owner;
- user mapping changes after handoff require VM authority;
- VM's complete fault-handling working set remains wired;
- VM failure while launcher bootstrap is running is a fatal bootstrap error;
  after launcher sealing, VM self-fault uses its exact non-bootstrap fatal
  diagnostic and never recurses into VM.

## Milestone 5: core user-space services

### Deliverables

- PM with application identity, hidden process reservation, spawn metadata,
  exit, zombie, wait, and rollback;
- interrupt-driven TTY with early-console and PLIC handoff;
- RAMFS;
- VFS with descriptors, a synthetic console object, pathname routing, and a
  root mount.

### Exit criteria

- PM starts through the static launcher after VM handoff and receives one
  exact bootstrap-sealed runtime event;
- semantic PIDs, parent/child relationships, init reparenting, zombie
  retention, exact-child and any-child wait, and one-shot wait replies pass a
  deterministic native model;
- PM's initial kernel authority can reserve and abort only one empty hidden
  process, releases every live reservation resource without changing
  endpoint/scheduler/ownership state, and leaves exactly one consumed process
  generation and transaction stale;
- no Milestone 5 PM outcome claims a successful spawn or allocates post-handoff
  user memory before the reviewed VM mapping protocol;
- TTY release follows console begin and one exact VM-installed UART device
  leaf, while TTY readiness follows ownership commit;
- one guest-owned manifest deadline covers the complete console begin through
  readiness sequence without a pre-release timeout gap;
- PLIC source 10 remains in service from kernel claim through exact TTY
  acknowledgment after UART draining;
- deterministic QEMU input and output prove canonical CR/LF and erase
  processing, empty-to-nonempty transmit start, interrupt-driven
  receive/transmit, and no routine kernel UART output after begin;
- service protocols reject malformed types and payload lengths;
- client termination releases server-owned state;
- terminal data and file data use exact-direction grants;
- non-transitive data paths use bounded resident bounce buffers;
- init receives working descriptors 0, 1, and 2 without RAMFS device nodes;
- RAMFS validates a reproducible seed before readiness and accepts one exact
  VFS mount;
- RAMFS generation, parent/name, link/reference, block ownership, sparse-file,
  capacity, and failure-atomic invariants pass deterministic native tests;
- a replayable RAMFS model covers at least 8,192 mixed mount, namespace, I/O,
  directory-cursor, release, malformed, stale, capacity, and grant-failure
  transitions;
- one QEMU integration proves the real RAMFS service's seed lookup, writable
  sparse file, complete-record directory cursor, exact grants, balanced
  references, VFS readiness, launcher sealing, and TTY-routed pass marker;
- VFS resolves absolute and relative paths and enforces exact process-local
  descriptor ownership;
- shared open-file descriptions own regular-file offsets and directory
  cursors, while vnode-local and RAMFS reference counts remain balanced;
- application read destinations are completely prevalidated before a
  consuming TTY hop;
- non-copying grant validation preserves checked-copy error precedence, and
  global second-hop grant exhaustion returns `NO_SPACE` without backend
  submission;
- file and terminal data cross application-to-VFS and VFS-to-backend grants
  through resident pages without transitive authority;
- complete RAMFS directory records become complete fixed application records
  with no exposed backend handle;
- pending console reads, completions, cancellation, and writable retry leave
  the VFS receive loop available, including exact
  `CANCEL/REQUEST -> COLLECT/OK` cleanup;
- a replayable VFS model covers at least 8,192 mixed lifecycle, descriptor,
  path, I/O, directory, console, malformed, capacity, and injected-failure
  transitions; and
- one isolated QEMU application probe proves both grant hops, descriptor
  access, absolute/relative routing, translated directories, canonical console
  input, TTY output, a no-authority resident-output drain barrier, zero live
  grants, and clean launcher sealing without claiming production spawn.

## Milestone 6: shell MVP

Milestone 6 is internally gated. The ELF spawn path is implemented and
reviewed first with an isolated dynamic PID-1 fixture and child application.
Production init, shell policy, and commands begin only after that fixture
proves failed-spawn rollback, successful activation, normal exit, wait, and
complete child-resource release.

### Deliverables

- ELF-spawn gate:
  - bounded static RISC-V ELF loading;
  - post-handoff VM allocation, scratch initialization, mapping, freeze,
    rollback, and release for one reserved child;
  - target-proven VM scratch preemption, inactive-root validation,
    reactivation, and stale-translation removal;
  - PM/VFS/VM spawn transaction with explicit descriptor duplication and
    complete preactivation rollback;
  - PM-only thread, endpoint, application-profile, context, and activation
    transitions;
  - separate argv/env application startup and normal-return exit;
  - RISC-V instruction-fetch synchronization before a child runs; and
  - deterministic dynamic-parent/child QEMU resource-baseline proof;
- shell gate:
  - production init;
  - interactive shell;
  - `echo`, `cat`, `ls`, and `ps`; and
  - automated serial end-to-end scenario.

Accepted
[ADR-0079](adr/0079-production-init-shell-and-basic-commands.md) and the
[node-Q architecture](architecture/init-shell.md) define this shell gate.
The bounded outcome uses the existing direct spawn/wait path: PM creates PID 1
after bootstrap sealing, init restarts one console shell, the shell executes
foreground application commands, and PM publishes an application-only process
snapshot for `ps`. Fork, signals, getty/login, full shell grammar, pipes,
redirection, job control, and static-service enumeration remain later work.

### Exit criteria

- the ELF-spawn gate proves one post-freeze failed spawn leaves no resource,
  one valid child receives exact argv/env and descriptors, and child
  exit/wait returns to the post-init baseline;
- a QEMU component preempts VM with the scratch leaf present, activates a
  distinct Sv39 root, reactivates VM through the ordinary scheduler path, and
  proves `SCRATCH_COMMIT` removes both the PTE and usable translation;
- the complete system boots from a clean build;
- the required command scenario succeeds without manual timing assumptions;
- process exit and wait leave no process, frame, descriptor, or inode leak;
- repeated scenario runs produce the same externally visible result.

## Milestone 7: resilient control plane

### Deliverables

- DS endpoint publication;
- user-space scheduler policy;
- RS lifecycle management;
- service crash injection and restart.

### Exit criteria

- service identity changes invalidate stale endpoints;
- dependencies observe a controlled restart notification;
- injected restart tests preserve or explicitly discard state according to the
  service contract;
- bootstrap-only authority is not reintroduced.

## Milestone 8: persistent storage

### Deliverables

- VirtIO block layer;
- persistent filesystem with POSIX metadata and link semantics;
- durable mount and recovery boundaries;
- file-backed VM interface.

### Exit criteria

- file data and metadata survive deterministic reboot scenarios;
- pathname, permission, link, timestamp, locking, and descriptor behavior map
  to the POSIX capability matrix;
- VFS/VM file-backed cycles use an explicit asynchronous protocol.

## Milestone 9: POSIX system semantics

### Deliverables

- `fork`, complete `exec`, process groups, sessions, and job control;
- MINIX-baseline copy-on-write fork mappings and write-fault separation;
- POSIX threads and synchronization;
- signals, clocks, timers, credentials, users, and groups;
- pipes, FIFOs, shared memory, memory mapping, and required IPC;
- complete terminal and pseudo-terminal behavior; and
- sockets and required networking interfaces;
- public POSIX headers, libc entry points, error/`errno` behavior, and ABI
  wrappers delivered with each interface family.

### Exit criteria

- every unconditional mandatory System Interfaces row has deterministic
  evidence, and every conditionally mandatory row passes when its
  standard-defined condition holds;
- deferred v0.1 substitutions are removed without weakening the MINIX
  structural/algorithmic baseline;
- fork copy-on-write sharing, write-fault isolation, and release pass native
  and target evidence;
- representative multithreaded and multiprocess applications run unchanged.

## Milestone 10: POSIX shell and utilities

### Deliverables

- complete POSIX shell language;
- mandatory utilities;
- remaining shell/utility-facing libc and userland support;
- explicitly reviewed userland provenance and maintenance policy.

### Exit criteria

- shell grammar, expansion, redirection, pipelines, jobs, and scripts pass
  conformance evidence;
- mandatory utilities pass their capability-matrix rows;
- representative third-party POSIX source builds without project-private
  source changes.

## Milestone 11: complete POSIX conformance

### Deliverables

- versioned POSIX.1-2024 capability matrix;
- claimed option-group profile;
- complete native, QEMU, integration, shell/utility, and application evidence.

### Exit criteria

- every unconditional mandatory row, every applicable conditionally mandatory
  row, and every claimed optional/XSI group passes;
- no required behavior exists only in prose;
- independent review finds no unsupported complete-compatibility claim.

## Further platform milestones

These require separate ADRs and are not part of the shell MVP:

- SMP;
- physical RISC-V hardware.

## Major risks

| Risk | Consequence | Mitigation |
| --- | --- | --- |
| Hidden bootstrap cycle | A service cannot start without one of its successors | Validate explicit manifest prerequisites, reject cycles, and release in one deterministic topological order |
| ABI churn | Every service changes at once | Accept endpoint, message, and grant ADRs before service code |
| Assembly bugs | Corruption appears far from the cause | Dedicated QEMU context and trap tests with known register patterns |
| Memory ownership ambiguity | Leaks or double allocation | Central ownership ledger and assertions before/after VM handoff |
| Slow feedback | Kernel bugs become expensive to isolate | Keep pure logic host-testable and QEMU smoke tests short |
| Unbounded or accidental baseline scope | One task imports unrelated architecture or later POSIX breadth | Reproduce complete structures/algorithms through the sequential DAG; treat milestone omissions as staged, not as permission for one broad change |
| Future concurrency requires redesign | Thread or SMP work replaces process and IPC foundations | Separate process/thread/hart objects and reply rights in v0.1 |
| Silent service failure | Boot hangs without a diagnosis | Use one guest-owned counter deadline, exact service identity, and a stable fatal bootstrap record |
| Host-only assumptions | macOS build works but CI or target behavior differs | Separate host and target toolchains and test on Linux CI |

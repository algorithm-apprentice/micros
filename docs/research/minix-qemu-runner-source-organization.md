# MINIX QEMU Runner Responsibility Baseline

## Reference and scope

The fixed read-only local reference is
`4db99f4012570a577414fe2a43697b2f239b699e` at
`/Users/alephling/Repo/minix`. Its HEAD and selected
`minix/tests/Makefile`/`minix/tests/run` paths were verified unchanged before
this design trace. No MINIX runner, source fragment, cleanup command or shell
expansion is copied or executed.

The existing [validation-driver study](minix-validation-driver-baseline.md)
is the shared ADR-0025 baseline. This narrower study explains responsibility
boundaries for the Proposed
[QEMU runner contract](../architecture/qemu-runner-source-organization.md),
not another kernel/subsystem design.

## Actual trace and classification

`minix/tests/Makefile:23-27,78-103,134-142` provides explicit named program
and shared-support ownership. `minix/tests/run:49-91` separates option
selection, executable-test counting and TAP plan publication.
`run:111-179` separates execution, per-test result collection and final
success/failure reporting.

| Observed responsibility | ADR-0025 classification | Preserved `micros` boundary |
| --- | --- | --- |
| Named programs/support and one outcome per selected test | Baseline parity | Keep all native/model/QEMU/image identities and one original test discovery. |
| Selection and report publication separate from the program under test | Baseline parity | Keep CLI, command construction, execution and outcome acceptance separate. |
| Hosted executable/privilege runner instead of OpenSBI/RV64 QEMU | Required adaptation | Existing exact `virt` argv, subprocess/serial ownership, guest markers and TAP remain authoritative. |
| Byte-exact ordered guest records, fault PCs and deterministic serial trigger | Compatible extension | Preserve the existing actual parser, symbol, timeout and input semantics, not a new generic protocol. |
| Exact candidate/context receipts and closed tool/default inputs | Compatible extension | ADR-0064/0065 retain authority; this source-only correction transports their reviewed code declaration. |
| Missing/privilege-dependent tests silently skipped | Divergence requiring correction | Never import that hosted-suite behavior; unsatisfied `micros` gates block acceptance. |

MINIX does not define this Python runner's regex vocabulary, process API,
serial event loop, exception boundaries, default-configuration closure or
execution receipts. Those come from Accepted `micros` decisions and the
fixed current implementation. Source organization cannot use MINIX as
authority to simplify them.

## Required adaptations, not optimization

The current `micros` runner already distinguishes classification from
acceptance: a panic marker can classify a run as panic while timeout,
nonzero exit, incomplete records or unexpected context still reject it.
The preserved process/serial loop owns its actual pipes, selector, deadline,
byte buffer, trigger and cleanup as one cohesive mechanism.

The exact 22-element guest command and all help/errors/markers/budgets remain
unchanged. The source split introduces no `-no-user-config`, new serial
retry, broader cleanup, missing-gate fallback or new identity check.
New helper-source/loader admission is a bounded development-code closure
transport, not a MINIX runtime feature.

The baseline is the currently merged TTY outcome
`34f1048449c221b014530484bfec4b3c0ab51142`: 2,087 runner lines/46 functions/
two classes/43 assignments, and 2,492 test lines/six classes/85 methods/
36 independent fixtures. This design records those facts only. Executable
oracles, models, harnesses, prototypes and source transport begin after
independent review, acceptance and separate design merge.

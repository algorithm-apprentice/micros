# ADR-0061: Trap and Syscall Source Decomposition

- Status: Proposed
- Date: 2026-10-14
- Related:
  [ADR-0025](0025-minix-behavioral-baseline-before-optimization.md),
  [ADR-0032](0032-fail-closed-change-aware-validation.md),
  [ADR-0033](0033-reviewed-design-before-implementation.md),
  [ADR-0034](0034-author-validation-before-review.md),
  [ADR-0015](0015-supervisor-trap-entry.md),
  [ADR-0022](0022-user-execution-contexts-and-u-mode-entry.md),
  [ADR-0027](0027-return-boundary-timer-rearming.md),
  [ADR-0035](0035-riscv-ipc-syscall-and-bootstrap-buffers.md),
  [ADR-0041](0041-unified-risc-v-grant-syscalls.md),
  [ADR-0042](0042-freestanding-user-service-runtime.md),
  [ADR-0051](0051-responsibility-based-source-modules.md),
  [source-module audit](../architecture/source-module-audit.md),
  [exact source contract](../architecture/trap-syscall-source-organization.md),
  [fixed MINIX baseline](../research/minix-trap-syscall-source-organization.md)

## Context

The bootstrap correction is reviewed and merged as PR #120. Among the remaining
dependency-ready source-audit units, trap/syscall routing is a high-risk
authority and register-custody boundary.

At base `09271b80de386870b7ecadcf0368576e46da56ae`, the six audited sources
contain 4,797 physical lines and nine functions above the 120-line review
signal. The 924-line trap dispatcher mixes exact routed-context validation,
user accounting/capture, faults, user and supervisor interrupts, many
compile-time evidence hooks, syscall return, and fatal diagnostics.
The coupled runtime evidence mixes static fixture state, image access, setup,
register capture, trap hooks, cleanup, role scripts and native raw capture.

The fixed MINIX trace separates architecture save/restore, exception policy,
IPC/kernel-call authority and common selected-user return. Accepted `micros`
RISC-V, distinct-object, immutable-profile, captured-result, bootstrap and
console choices remain authoritative. Source extraction must not turn a
kernel-origin interrupt into a scheduling point or save a supervisor stack
from a returning helper.

## Decision

Apply the exact ownership, phase, storage, build, migration and acceptance
contract in
[Trap and Syscall Source Organization](../architecture/trap-syscall-source-organization.md).
Implementation begins only after independent design review, Acceptance, and
the separate design merge.

The paired tasks remain sequential:

```text
trap-syscall-decomposition-implementation
    -> trap-syscall-decomposition-design
```

Only design is active in this phase. No executable implementation artifact is
part of the design pull request.

### Preserve authority and entry/return custody

Retain the supported trap/syscall/runtime declarations, route classifier,
architecture frame/entry/restore, raw/startup assembly, memory/runtime
implementations and linker contract byte-for-byte.

Separate routed context, ordered user routing, interrupt handling, syscall
bridging, fault classification, armed supervisor tests and stateless
compile-time evidence hooks. Keep the public install/dispatch/nested entries
and generic unexpected-supervisor-exception call site in the original facade.
Use the existing scheduler selectors; add no router-local selection or state.

Preserve user accounting before capture, all hook/predicate/short-circuit
order, direct S-origin return, VM/bootstrap/TTY fatal ordering, and exact
normal/captured selection. Top-level syscall extraction preserves argument/
PC checks, one scalar capture and `sepc + 4`, ordered early control decode,
authoritative current-context resolution, then existing operation dispatch.

### Explicit existing evidence storage

Consolidate only the three current armed-trap statics and seventeen current
kernel runtime-test statics into their separate private contexts. Preserve
volatile fields and the nonzero `returned_actor` initializer.
The native raw capture remains its one existing eight-field fixture.

Three kernel supervisor-save ABI globals remain distinct and their inline
`sp`/`gp`/`tp` capture remains in the original non-returning entry invocation.
Seven standalone image ABI objects retain exact symbols, types, qualifiers,
sizes and initial bytes. Client/server message, token, grant and volatile
canary state stays automatic. No new mutable kernel state or evidence
registry is introduced.

### Exact source and phase enforcement

Use the five complete source sets and five declarations-only private headers
in the source contract. The implementation starts with one structural Red
gate, exactly fourteen moving function symbols and nine long-function
semantic migrations. Both active collections must be empty before review.
A rename, move or formatting compression does not discharge a long function.

The generator remains one module/CLI and renders identical complete bytes
for identical checked inputs. All current native, ELF, QEMU, marker, timeout,
instruction and budget identities remain unchanged.

### Validation and review

Preserve exact path ownership, including full-tier/all-37 trap ownership,
the syscall twenty-workflow/IPC-model union, and user-runtime target
classification for the currently known native evidence.

Use narrow current evidence per extraction and one complete final author
PR plan. Bind that evidence to the staged tree and clean task state before
independent read-only implementation review. Reviewers inspect rather than
routinely rerun the author suite. Fix only justified findings and rerun only
invalidated checks before bounded re-review.

## Non-goals

No new trap, syscall, privilege, profile, frame, scheduling, timer, interrupt,
fault delivery, paging, signal, retry, recovery, runtime, application startup,
process exit, image, protocol, test execution, directory layout, validation
deduplication, concurrency or compatibility behavior.

The grant, endpoint, object, scheduler, TTY, QEMU runner and ELF-checker
corrections remain separate outcomes.

## Alternatives considered

- Leave the router mixed: rejected because the existing capture, authority,
  hook and return ordering is already difficult to inspect.
- Move the dispatcher unchanged: rejected because it retains the non-atomic
  task hierarchy and fails the explicit long-function contract.
- Add a callback/test registry: rejected because compile-time evidence needs
  ordered named phases, not runtime generality.
- Move register capture into setup: rejected because a returning helper cannot
  own the saved supervisor continuation's stack.
- Rework scheduler or syscall policy while moving source: rejected; any
  behavioral defect requires its separately reviewed corrective design.

## Consequences

The source owners expose exact trap/call authority and return custody while
retaining existing behavior and evidence. Costs are cross-owner visibility,
preprocessor-order risk and test-storage migration. Fixed context inventories,
unchanged architecture artifacts, semantic phase gates, full current evidence
and independent review bound those risks.

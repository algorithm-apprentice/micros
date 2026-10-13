# ADR-0059: Kernel IPC Source Decomposition

- Status: Accepted
- Date: 2026-10-12
- Related:
  [ADR-0025](0025-minix-behavioral-baseline-before-optimization.md),
  [ADR-0030](0030-minix-baseline-blocking-ipc.md),
  [ADR-0032](0032-fail-closed-change-aware-validation.md),
  [ADR-0033](0033-reviewed-design-before-implementation.md),
  [ADR-0034](0034-author-validation-before-review.md),
  [ADR-0035](0035-riscv-ipc-syscall-and-bootstrap-buffers.md),
  [ADR-0036](0036-reply-authority-begins-at-request-return.md),
  [ADR-0037](0037-kernel-origin-ipc-notification-injection.md),
  [ADR-0051](0051-responsibility-based-source-modules.md),
  [source-module audit](../architecture/source-module-audit.md),
  [IPC source contract](../architecture/kernel-ipc-source-organization.md),
  [MINIX baseline](../research/minix-kernel-ipc-source-organization.md)

## Context

The merged PM correction completes the immediate source-organization
predecessors of the Step 14 coordinator. Other audited correction units
remain dependency-ready. Kernel IPC is the highest-risk remaining shared
mechanism correction and is addressed as one sequential design/implementation
pair, without developing other tasks in parallel.

At base `11c1c43a3fff4c3104eac3f5eb656ae27185a61a`, the eleven audited IPC
files contain 21,066 physical lines, 363 functions, and 42 functions above
the 120-line review signal. They combine portable operations, raw linkage,
completion, notification and token preparation, close cancellation, syscall
adaptation, mutable fixtures, long target scripts, and the complete native
reference model.

Accepted IPC, scheduler, endpoint, bootstrap, and exit behavior already
exists. This decision changes source and task ownership, not those contracts.

## Decision

Apply the exact source sets, helper ownership, dependency direction, storage,
evidence identities, and function dispositions in
[Kernel IPC Source Organization](../architecture/kernel-ipc-source-organization.md).

### Portable mechanisms

Separate common identity/message primitives, raw queue primitives,
completion shaping, token preflight, notifications, deadlock, rendezvous,
close, reply/receive, and send/receive/call orchestration.

One declarations-only private header carries only the required cross-owner
helpers and the existing automatic pending-notification/reply-plan shapes.
Complete endpoint/object validation and the authoritative target runtime
remain with their existing owners.

Preserve every argument/authority/state/error check, nested validation,
matching-before-blocking rule, FIFO order, token publication, scheduler
preflight/commit, and success-only output. Source extraction does not remove
apparently repeated validation or add rollback after a non-failing commit.

### Captured syscall boundary

Separate diagnostic/result shaping, request/buffer preflight and portable
dispatch, and captured-current orchestration. Preserve controller reply
restriction, stored context, current-thread guard, rollback, immediate
completion, and captured return.

The generic syscall dispatcher, shared selected-thread return, IPC buffer
implementation, ABI mapper, and target registry remain unchanged. No new
operation, result, physical-buffer authority, or retry policy is introduced.

### Native evidence

Retain all existing case names, labels, order, executables, and CTests.
Separate each signaled case family's fixture from its behavior cases; the
notification and close model remain in their existing executions.

Consolidate only the existing mutable static storage into one context per
family. Automatic snapshots and reference tables stay automatic. Reference
queries, scheduler/IPC primitives, rendezvous, notification, and lifecycle
decisions remain independent of production helpers.

The persistent model retains its exact prelude, total 8,192 transitions,
seed, one random draw after each prelude step, action selection/recovery,
trace encoding, expected-before-production execution, complete comparison,
and coverage requirements. Preserve its existing address-independent trace
hash `65c2014aad08f70b`.

The notification model retains 4,096 steps and two draws per step, including
draws before its forced prefix. The close scenarios retain 512 cases and
three draws per case. No model workflow or transition count is changed.

### Target evidence

Separate the trusted IPC scenario, ecall-core setup/report, and syscall
script's fixture, setup, command construction, transition phases, hooks,
and report.

Each has one context for its original mutable static storage. Its target
registry pointer remains borrowed from the authoritative runtime. The six
assembly-required saved-register globals remain separate ABI objects.

Keep supervisor register capture in the original non-returning public entry
frame, before the same scheduler entry. Keep exact assembly, payload labels,
21-command syscall sequence, priority changes, deferred timer completion,
negative-state normalization, failure stages, markers, and timeouts.

### Build and validation

Eleven exact ordered source sets replace repeated raw split-source entries
in their current consumers. New paths inherit the complete original
classification, including slow-model ownership and unknown/full fallback.

The implementation begins with one fail-closed structural Red gate. It
tracks an exact 70-symbol public/case/hook owner migration and an exact
42-function long migration; both may only shrink and must be empty at final
review. An unchanged long body moved or renamed is not a completed split.

The gate covers exact families/consumers, stable hashes, declarations-only
private contracts, retained helper ownership, recursive dependency direction,
file-scope/static-local storage and ABI exceptions, case/model/script order,
trace, markers, compile guards, and validation inheritance.

No split creates another executable, CTest, model execution, QEMU workflow,
marker, timeout, assembly hook, user image, or CI job.

### Phase barrier

Implementation remains pending until this design is independently reviewed,
Accepted, and squash-merged. Follow the contract's small green slices, narrow
existing evidence, one authoritative final PR plan, and independent
implementation review. Reviewers inspect author evidence rather than
repeating suites.

## Consequences

- Queue, completion, token, event, close, and syscall boundaries become
  readable without changing the shared mechanism.
- Target scripts expose their command and timer phases without replacing real
  hardware or scheduler evidence.
- Native reference decisions remain independently traceable.
- Mutable evidence storage has an explicit owner without adding another
  runtime registry.
- The move is large because evidence and shared consumers are large; exact
  inventories and sequential green slices constrain that risk.
- Future endpoint, scheduler, grant, trap, and directory corrections remain
  separate tasks rather than additions to this PR.

## Alternatives considered

### Decompose all shared kernel mechanisms together

Rejected. It would combine unrelated owners, defeat attribution, and violate
the single-focus sequential workflow.

### Leave the model and target scripts monolithic

Rejected. They are part of the audited IPC authority/evidence unit and would
continue hiding state, action, comparison, and cleanup ownership.

### Place raw queues, notifications, and complete operations in one helper

Rejected. It would recreate a central mixed file and can introduce a
queue/notification cycle. Raw linkage sits below both.

### Let the model reuse production private helpers

Rejected. That would make matching production and reference outcomes
meaningless as independent evidence.

### Eliminate nested validation during extraction

Rejected. Validation optimization is a different measured behavior-bearing
decision, not part of source movement.

### Move supervisor capture into a reusable returning setup helper

Rejected. The later resume would name a helper frame whose lifetime already
ended. The current public entry frame remains the capture owner.

## Non-goals

No ABI, layout, enum, error, authority, queue/token/notification policy,
allocation, scheduling, generation, teardown, or buffer-fault change. No
asynchronous IPC, timeout, retry, priority inheritance, multihart support,
production lifecycle feature, directory migration, unrelated decomposition,
compatibility, recovery, or weakened/replaced evidence.

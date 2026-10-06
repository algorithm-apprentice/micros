# ADR-0024: Preemptive Round-Robin Scheduler

- Status: Accepted
- Date: 2026-10-06

## Context

The kernel can now:

- create generation-safe processes and threads;
- prepare one saved user context and supervisor stack per thread;
- enter U-mode and capture a user-origin trap;
- deliver and rearm supervisor timer interrupts;
- switch process roots with full local invalidation.

What remains missing is scheduling. A thread can be entered only through the
single-thread bootstrap API, there is no runnable state or queue, and every
ordinary U-origin timer trap is still fatal after context capture.

ADR-0011 requires scheduling to operate on threads rather than processes.
ADR-0019 reserved hart-local preemption and reschedule fields. ADR-0022
established a distinct kernel stack per thread so an outgoing thread's trap
frame remains valid while another thread becomes current.

The first scheduler must remain small enough to audit directly while
preserving a future path to blocking IPC and SMP. It must not add priorities,
timeslices in wall-clock units, work stealing, affinity policy, or a user
scheduler before the one-hart round-robin mechanism is proven.

## Decision

### Superseded accepted details

This ADR supersedes only these earlier details:

- ADR-0016's handled U-origin timer tick returning through an unchanged frame;
  spurious ticks and S-origin mechanism handling remain unchanged;
- ADR-0019's two-state `INACTIVE`/`RUNNING` lifecycle and invariant that every
  non-running live thread is inactive;
- ADR-0022's production `micros_user_execution_enter` authority and rule that
  every non-test U-origin trap is fatal after capture.

The low-level bind/clear helpers are redefined atomically:

- bind requires an attached `INACTIVE` context and simultaneously installs the
  current handle, changes the thread to `RUNNING`, and selects its thread
  stack;
- clear requires the exact `RUNNING` current thread and simultaneously restores
  the idle stack, clears current, and changes the thread to `INACTIVE`.

The separate public select/restore sequencing from ADR-0022 is superseded and
becomes an internal implementation detail. Production and object tests cannot
observe a current-thread/idle-stack or no-current/thread-stack intermediate
state.

ADR-0022's context layout, stack ownership, capture rules, and status/mapping
validation remain unchanged. ADR-0023 remains the canonical status contract.

### Thread states

The portable thread state becomes:

```text
INACTIVE -> RUNNABLE -> RUNNING
```

- `INACTIVE`: live, not queued, not current;
- `RUNNABLE`: linked in exactly one hart run queue, not current;
- `RUNNING`: current on exactly one hart, not queued.

Context preparation still attaches to an `INACTIVE` thread. Detach and thread
release still require `INACTIVE`.

A later IPC ADR adds blocked states. Blocking removes the exact running or
runnable thread from scheduler ownership before linking it into an IPC wait
queue; wakeup returns that exact thread to `RUNNABLE`. It does not replace
these identities or queue fields.

The existing low-level bind/clear operations remain available to focused
object component tests under the stricter attached-context precondition.
Production scheduling uses only the new queue transitions. The former
production `micros_user_execution_enter` becomes test-only; target scheduler
entry uses the preflight/commit interface defined below.

### Intrusive per-hart FIFO

Each thread record adds:

- `run_queue_linked`;
- exact owning hart handle while linked;
- previous and next thread handles.

Each hart record adds:

- exact run-queue head and tail handles;
- runnable count.

Handles include generations. Generation zero is the null link. The queue is a
bounded intrusive doubly linked FIFO over the fixed thread table; it allocates
no memory.

The portable API is:

```c
micros_thread_make_runnable(objects, hart, thread);
micros_thread_make_inactive(objects, hart, thread);
micros_hart_peek_runnable(objects, hart, thread);
micros_hart_dispatch_next(objects, hart, thread);
micros_hart_rotate_current(objects, hart, previous, next);
micros_hart_rotate_pending_current(objects, hart, previous, next);
micros_hart_stop_current(objects, hart, thread);
```

`make_runnable` requires:

- an exact live `INACTIVE` thread;
- an attached execution context;
- an exact registered hart;
- no queue links and no current-hart reference.

It appends at the tail and changes the state to `RUNNABLE`.

`make_inactive` removes an exact `RUNNABLE` thread from its owning queue,
clears every link, and changes it to `INACTIVE`.

`peek_runnable` resolves the exact head without mutation and preserves its
output on an empty or corrupted queue.

`dispatch_next` requires an idle hart whose trap anchor selects its immutable
idle stack. It removes the head, changes that thread to `RUNNING`, installs it
as current, and selects its kernel stack in the hart anchor. An empty queue
returns `EMPTY` without mutation.

`rotate_current` requires one exact current `RUNNING` thread and its selected
stack. If the queue is empty it returns the same thread as both previous and
next without changing queue or anchor state. Otherwise it:

1. preflights the complete queue, current thread, and next head;
2. appends the current thread at the tail as `RUNNABLE`;
3. removes the old head as the next `RUNNING` thread;
4. changes the hart current handle;
5. selects the next thread's kernel stack;
6. publishes exact previous and next outputs.

No fallible check occurs after the first state mutation.

`stop_current` restores the idle stack, clears the exact current handle, and
changes the thread to `INACTIVE` in one prevalidated transition.

`rotate_pending_current` has the same queue behavior as `rotate_current`, but
requires `preempt_disable_count = 0` and `reschedule_pending = true` and clears
that pending bit only in the successful rotation or singleton commit.

All failed operations preserve the registry and outputs byte for byte.

### Queue and registry invariants

Full validation additionally requires:

- every `RUNNABLE` thread occurs exactly once in one hart queue;
- queue reachability is exactly equivalent to
  `state == RUNNABLE && run_queue_linked`;
- every reachable node records the exact walked owning hart;
- every queued handle resolves the exact live generation;
- head has null previous and tail has null next;
- forward and reverse walks agree and terminate within thread capacity;
- stored runnable count equals both walks;
- no thread is queued on two harts;
- `RUNNING` threads are current on exactly one hart and have null queue links;
- every non-runnable thread has `run_queue_linked = false`, a null owning hart,
  and null previous/next links;
- `INACTIVE` threads have null queue/current ownership;
- every queued or running thread has an attached execution context;
- a hart with no current thread selects its idle stack;
- a hart with a current thread selects that exact thread stack;
- free and quarantined slots contain no queue links.

The validator never repairs a queue.

### Preemption exclusion and pending reschedule

The hart's existing fields become active mechanism state:

- `preempt_disable_count`;
- `reschedule_pending`.

The portable API is:

```c
micros_hart_preempt_disable(objects, hart);
micros_hart_preempt_enable(objects, hart, should_reschedule);
micros_hart_request_reschedule(objects, hart);
```

Disable rejects counter overflow and increments exactly once. Request sets the
pending bit idempotently. Enable rejects underflow, decrements, and sets the
output true only when the count reaches zero and a pending request is eligible
for a safe point. It does not clear the pending bit. Pending is consumed only
inside a successful preflighted pending-rotation commit, including a singleton
no-switch result. Failed preflight preserves pending.

The single-hart target uses SIE-clear critical sections around queue and
preemption operations. These fields are not substitutes for interrupt state:
SIE controls delivery, while the count controls whether a captured user frame
may be replaced with another runnable context.

### Target scheduler

The target wrapper owns the production registry and exposes:

```c
micros_scheduler_initialize(interval);
micros_scheduler_make_runnable(thread);
enum micros_scheduler_error micros_scheduler_start(void);
micros_scheduler_handle_timer(hart, frame);
micros_scheduler_reschedule_if_pending(hart, frame);
```

Initialization:

- requires one registered boot hart, no current thread, and an empty queue;
- initializes the hart timer;
- records a nonzero counter-tick quantum;
- does not enable global SIE.

`make_runnable` resolves the exact prepared production thread and appends it to
the boot-hart queue.

`start` requires at least one runnable thread. With SIE clear it:

1. non-mutatingly peeks the queue head;
2. preflights that exact context, stack, process root, and idle hart;
3. starts the timer while the hart and queue remain unchanged;
4. performs the prevalidated non-failing dispatch and stack selection;
5. activates the selected root through a non-failing commit helper whose
   impossible readback mismatch is fatal;
6. enters through the audited restore assembly.

If timer start rejects overflow or SBI programming, `start` returns an explicit
error with queue, thread, hart, anchor, and root unchanged. The successful path
does not return. After dispatch begins, no recoverable failure is returned.
User status has `SPIE = 1`, so supervisor timer delivery is enabled after
`sret`.

The target test has a deterministic fail-next-timer-program hook. It requires
`start` to return `TIMER` with byte-exact queue, thread, hart, anchor, timer,
root, and output preservation before the ordinary successful start.

### Context-switch preflight and commit

User execution adds an internal scheduler interface:

```c
micros_user_execution_preflight_switch(
    hart,
    outgoing_thread,
    next_thread,
    live_frame,
    switch_plan
);
micros_user_execution_commit_switch(
    switch_plan,
    live_frame
);
```

The preflight runs before portable queue/current mutation. It:

- verifies the live frame lies on the exact outgoing stack and that outgoing
  remains attached;
- validates the complete next saved status under ADR-0023;
- validates the freshly captured outgoing context before it can be requeued;
- validates next PC and SP mappings against the explicit next process root
  without requiring that root or thread to be current;
- validates the exact next stack and target `satp`;
- copies the next context and expected root into bounded wired plan storage.

Only after that succeeds may the scheduler rotate portable ownership. Commit:

1. copies the prevalidated next context into the still-wired outgoing frame;
2. installs the stable routed `hart_context` and canonical trap-only return
   fields;
3. writes the prevalidated next `satp` through the audited full-fence sequence;
4. treats an impossible readback mismatch as a fatal invariant;
5. returns to the ordinary epilogue.

Commit performs no scratch acquisition, tree walk, owner lookup, callback, or
recoverable check. For scheduler start, an equivalent preflight/commit pair
validates an explicit runnable head and enters it after the timer is armed.

### Invalid bootstrap user contexts

This slice runs only trusted statically embedded bootstrap payloads and has no
PM/page-fault fault-delivery protocol. If a freshly captured outgoing context
or queued next context fails ADR-0022 status, PC, or stack validation, the
scheduler:

1. detects the failure before any queue/current/anchor mutation;
2. emits trap-aware panic reason `invalid-bootstrap-user-context`;
3. never enqueues, selects, or returns through that invalid context.

An isolated QEMU image deliberately installs an invalid user `sp`, takes a real
timer trap, and requires that exact fatal reason with unchanged scheduler
ownership in diagnostic test state.

This bounded fatal policy must be superseded before untrusted applications are
released. The later process-fault ADR atomically removes/faults the exact
thread, reports it to PM, and selects another runnable thread without changing
the queue representation established here.

### Timer-trap ordering

For a U-origin supervisor timer interrupt, dispatch performs this exact order:

1. route the hart and verify the live frame lies on its current thread stack;
2. capture the outgoing user context under ADR-0022;
3. call the timer mechanism and reject inactive, overflow, or rearm failure;
4. if the indication is spurious, validate and return the unchanged frame;
5. if preemption is disabled, set `reschedule_pending`, validate and return the
   unchanged frame;
6. otherwise preflight the current thread, queue, next context, next root, and
   outgoing frame storage;
7. build the complete user-execution switch plan while outgoing remains
   current and its root remains active;
8. atomically rotate portable scheduler ownership;
9. commit the prevalidated frame image and next process root without a
   recoverable operation;
10. return through the ordinary epilogue.

The outgoing frame remains safe through `sret` because its thread is
`RUNNABLE`, its context stays attached, and its stack cannot be detached or
released. The hart anchor already names the next thread stack, so the next
trap routes correctly. SIE remains clear throughout dispatch. After the
portable rotation begins, the commit contains only bounded wired memory
copies, CSR operations, and non-failing stores.

If the queue is empty, rotation selects the same current thread and no root or
anchor switch is needed.

### S-origin timer interrupts

The scheduler does not switch a user context from an S-origin frame. An
accepted S-origin timer tick sets `reschedule_pending` and returns through the
unchanged supervisor frame. The next explicit scheduler safe point consumes
the request after preemption is enabled.

This avoids replacing a kernel continuation before general kernel blocking and
switch continuations exist.

### Safe-point rescheduling

`micros_scheduler_reschedule_if_pending` requires:

- a U-origin frame already captured for the exact current thread;
- `preempt_disable_count = 0`;
- a pending request.

It consumes the request and performs the same preflighted rotation and return
frame installation as direct timer preemption. The portable pending-rotation
transition verifies and clears `reschedule_pending` in the same non-failing
commit that rotates, or clears it in the singleton no-switch commit.

Preempt-enable only reports that a safe point is required; it does not switch
without an explicit live user frame.

### Initial policy

The policy is strict FIFO round robin:

- enqueue at tail;
- select from head;
- one accepted timer quantum produces at most one rotation;
- no priorities or dynamic quantum changes;
- no compensation for delayed ticks;
- no migration between harts in production v0.1.

The quantum is expressed only in platform counter ticks, preserving ADR-0016.

### Future scheduler-server handoff

The kernel initially operates in `AUTONOMOUS_ROUND_ROBIN` policy mode. A later
Accepted ADR defines a tested one-way transition to `EXTERNAL_POLICY`:

- the kernel retains authoritative thread states, queue membership, context
  validation, timer mechanism, and final context-switch commit;
- autonomous FIFO selection and autonomous quantum rotation are disabled;
- timer and runnable events are reported to the authorized scheduler server;
- the server submits an exact runnable next-thread handle;
- the kernel validates that decision against its queues before switching;
- server failure has explicit fatal or fallback behavior defined by that later
  ADR and cannot silently reactivate autonomous policy.

That later slice tests revocation of autonomous decisions. This ADR does not
implement the server or the handoff, but fixes the replacement boundary
required by ADR-0003.

### Native tests

Portable tests cover:

- empty, singleton, and multi-thread queues;
- deterministic FIFO order and wraparound through slot reuse;
- stale head, tail, previous, next, and owning-hart generations;
- duplicate enqueue and cross-hart enqueue rejection;
- remove-head, remove-tail, remove-middle, dispatch, rotate, and stop;
- singleton rotation without duplicate queue membership;
- required attached contexts and exact stack selection;
- preempt-disable overflow, enable underflow, nested counts, idempotent request,
  and one-shot pending consumption;
- byte-exact state/output preservation on every failure;
- corruption of every link, count, state, current handle, and anchor relation;
- independent corruption of `run_queue_linked`, owning hart, previous, and
  next metadata versus actual reachability;
- seeded model traces with several threads and two harts.

### QEMU component test

A separate `MICROS_BUILD_SCHEDULER_TEST` image:

1. creates two processes, roots, RX loop pages, RW counter pages, RW stacks,
   contexts, and threads;
2. writes distinct relocation-free payload parameters and executes `fence.i`;
3. enqueues both threads and proves the queue order before entry;
4. injects one timer-program failure, proves byte-exact failed-start
   preservation, then starts one real supervisor timer quantum source and
   enters the first thread;
5. on each accepted U-origin tick, verifies the exact current thread,
   process root, selected kernel stack, every x1-x31 value under its documented
   per-payload evolution, and monotonic private counter;
6. proves the exact sequence A, B, A, B, A, B across six rotations;
7. injects one test-only disabled-preemption tick, proves the current thread and
   frame remain unchanged with `reschedule_pending = true`, then consumes it at
   a user-frame safe point and performs exactly one deferred rotation;
8. proves both counters advanced and neither thread lost register state;
9. disarms the timer, activates the kernel root, stops the current thread,
   removes the remaining runnable thread, and returns through a test-only
   interrupt-disabled S-mode continuation;
10. detaches/releases both contexts and threads, destroys both roots, releases
    both processes, and restores object/ownership state to baseline.

Only that complete sequence emits:

```text
MICROS_SCHEDULER_TEST_PASS switches=round-robin fairness=observed registers=preserved deferred=honored
```

The host gate requires the exact pass record after ownership readiness, clean
SBI shutdown, no panic or explicit failure, and no timeout. Parser regressions
reject missing, duplicate, malformed, unterminated, or out-of-order records.

Two isolated expected-panic workflows prove both preflight inputs:

- one runs a user payload with an invalid captured stack pointer;
- one corrupts the already-enqueued second thread's saved status, PC, or SP,
  starts the valid first thread, and reaches the invalid queue head on a real
  timer tick.

Both require `MICROS_PANIC reason=invalid-bootstrap-user-context` before any
switch and independently snapshot unchanged current, queue, anchor, and root
ownership in test diagnostics.

## Consequences

- Runnable ownership and fairness become explicit before IPC adds blocking.
- Timer interrupts can preempt U-mode without replacing process or thread
  identity.
- Per-thread stacks make outgoing-frame switching safe without a separate
  scheduler stack.
- The one-hart policy remains a checked target configuration; native models
  exercise multiple queues.
- Later scheduler-server policy can choose enqueue decisions without replacing
  the kernel queue/context-switch mechanism.

## Alternatives considered

### Use one global run queue

One hart needs only one queue, but putting ownership directly in the hart keeps
future SMP from replacing queue membership and current-thread invariants.

### Switch from an S-origin timer frame

That requires resumable kernel continuations and general blocking semantics.
Deferring until a captured U frame preserves a clear first mechanism.

### Copy the outgoing frame to the next thread stack

The ordinary epilogue can safely consume the wired outgoing frame after the
next context is installed. A second frame copy adds no correctness and creates
another stack-layout transition.

### Add priorities now

Priority ordering, donation, inversion, and scheduler-server policy are not
needed to prove preemption or the shell MVP mechanism.

### Add IPC blocking in the same slice

Blocking introduces wait queues, endpoint ownership, wakeups, reply rights,
and deadlock chains. The runnable state machine should be independently
observable first.

## Specification basis

- [RISC-V Privileged Architecture, Supervisor-Level ISA](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html)
- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0010: Testing and Observability](0010-testing-and-observability.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0016: Supervisor Timer Interrupts](0016-supervisor-timer-interrupts.md)
- [ADR-0019: Kernel Object Identity and Ownership](0019-kernel-object-identity-and-ownership.md)
- [ADR-0021: Generation-Safe User Address Spaces](0021-generation-safe-user-address-spaces.md)
- [ADR-0022: User Execution Contexts and U-Mode Entry](0022-user-execution-contexts-and-u-mode-entry.md)
- [ADR-0023: Canonical User Status Summary Bits](0023-canonical-user-status-summary-bits.md)
- [Development dependency DAG](../architecture/development-dag.md)

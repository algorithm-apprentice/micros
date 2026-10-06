# ADR-0022: User Execution Contexts and U-Mode Entry

- Status: Accepted
- Date: 2026-10-06

## Context

The kernel now has:

- generation-safe process and thread objects;
- private generation-bound process address spaces;
- typed user and page-table frame ownership;
- complete supervisor trap entry and return;
- one registered hart with explicit current-thread ownership.

The next dependency-DAG step must create the first real U-mode execution
without also implementing run queues, timer preemption, IPC, ELF loading, or
the PM/VFS/VM spawn transaction. It must establish the durable representation
and ownership rules that those later mechanisms consume.

ADR-0011 requires every thread to own a saved user context and a kernel stack.
ADR-0019 deliberately left those fields unattached. The existing trap entry
always uses one per-hart primary stack, which is sufficient before user threads
run but cannot retain one blocked or preempted kernel continuation per thread.

The frame ledger has process-generation owner kinds for user and page-table
frames. Kernel stacks are different: they are privileged, never transferred
to VM, and need a stable virtual address in every process root. Extending the
handoff ledger for a fixed-capacity kernel-internal stack pool would add
cross-subsystem owner kinds without improving physical-memory policy.

## Decision

### Scope

This slice implements:

- a portable saved integer user-context representation;
- exact thread attach, capture, inspect, detach, and release ordering;
- one fixed kernel-stack slot per representable thread slot;
- hart switching between its idle trap stack and the current thread's kernel
  stack;
- validated first entry to U-mode;
- capture and resume of a user trap through the existing mutable trap frame;
- a test-only return to a supervisor continuation so the component test can
  clean up and report.

This slice does not implement runnable queues, context switching between two
threads, timer preemption, blocking, syscalls, IPC, endpoints, executable
loading, mapping-generation seals, privilege profiles, or user-service
startup.

### Saved user context

The portable context contains x1-x31 in the same order and at the same offsets
as the first 248 bytes of `struct micros_trap_frame`, followed by `sstatus` and
`sepc` at offsets 248 and 256:

```c
struct micros_user_context {
    uint64_t ra;
    uint64_t sp;
    uint64_t gp;
    uint64_t tp;
    uint64_t t0;
    uint64_t t1;
    uint64_t t2;
    uint64_t s0;
    uint64_t s1;
    uint64_t a0;
    uint64_t a1;
    uint64_t a2;
    uint64_t a3;
    uint64_t a4;
    uint64_t a5;
    uint64_t a6;
    uint64_t a7;
    uint64_t s2;
    uint64_t s3;
    uint64_t s4;
    uint64_t s5;
    uint64_t s6;
    uint64_t s7;
    uint64_t s8;
    uint64_t s9;
    uint64_t s10;
    uint64_t s11;
    uint64_t t3;
    uint64_t t4;
    uint64_t t5;
    uint64_t t6;
    uint64_t sstatus;
    uint64_t sepc;
};
```

Its size is 264 bytes. C static assertions bind every shared field offset to
the trap-frame assembly ABI. Trap-only `scause`, `stval`, and `hart_context`
are not persisted as user execution state.

Each live thread record adds:

- `context_attached`;
- exact kernel-stack bottom and top;
- one saved `micros_user_context`.

Free and quarantined slots contain zero context state. Creation starts
detached. Release requires an `INACTIVE` thread with no attached context and
no hart reference.

The portable object API is:

```c
micros_thread_attach_execution_context(
    objects,
    thread,
    kernel_stack_bottom,
    kernel_stack_top,
    context
);
micros_thread_capture_execution_context(objects, thread, context);
micros_thread_inspect_execution_context(
    objects,
    thread,
    context,
    kernel_stack_bottom,
    kernel_stack_top
);
micros_thread_detach_execution_context(objects, thread);
```

Attach requires an exact live `INACTIVE` thread, no existing context, a
16-byte-aligned stack range of at least 16 KiB, and caller-provided context.
The candidate half-open stack range must not overlap any other attached thread
stack or any installed hart idle-primary or emergency stack.
Capture requires that exact thread to be `RUNNING`, current on exactly one
hart, and already attached. Detach requires `INACTIVE` and not current. All
failed operations preserve the complete registry and every output argument.

The portable bind primitive remains an object-lifecycle mechanism and does not
itself validate RISC-V user mappings or status bits. The target U-mode entry
wrapper requires an attached, target-validated context before binding and
running a thread. The scheduler ADR will define the later runnable-state gate.

### Fixed kernel-stack pool

The target owns:

```text
MICROS_THREAD_CAPACITY * 16 KiB = 2 MiB
```

of page-aligned static BSS in a dedicated linker-kept
`.thread_kernel_stacks` section. Stack slot `n` belongs exclusively to live
thread slot `n`; the full thread generation remains the authority for every
operation on that slot.

The pool lies inside `[__kernel_start, __kernel_end)`, is excluded before frame
allocator geometry is derived, and is mapped supervisor-only RW/NX through
every process root. It is not allocator-managed and does not participate in
the VM handoff. This is the same fixed-capacity tradeoff already used for the
object tables and supports every representable live thread without a
single-thread structural shortcut.

Before attaching a context, the target clears all 16 KiB of that slot. A
released slot retains no metadata, and reuse clears the complete stack again
before publication. The MVP has checked bounds but no unmapped guard pages;
guarded or dynamically allocated kernel stacks require a later ADR without
changing thread identity or the context API.

The target wrapper derives the only accepted bounds directly from the exact
thread slot and rejects any mismatch before clearing or publishing them.

Compile-time assertions require:

- one stack per thread slot;
- 4096-byte slot alignment;
- 16-byte top alignment;
- exact 16 KiB size;
- no overlap with boot, hart-idle, or emergency stacks in target validation.

### Hart idle and thread stack selection

Each installed hart records immutable idle-primary stack bounds outside its
assembly-visible trap anchor. Installation initializes both the idle bounds
and the anchor's current primary bounds.

The portable registry adds exact operations to:

- select the current thread's attached kernel stack as the anchor's primary
  stack;
- restore the immutable idle-primary stack.

Selection requires:

- an installed exact hart;
- that hart's exact current `RUNNING` thread;
- an attached context;
- stack bounds distinct from every hart idle/emergency stack.

Restore requires the same current thread and exact selected bounds. Neither
operation changes `sscratch`, because it already points to the stable hart
anchor. The first user trap therefore allocates its frame on the current
thread's kernel stack, while nested traps continue to use the hart emergency
stack.

The validator requires:

- idle bounds remain valid and immutable after installation;
- an idle hart anchor selects its idle-primary stack;
- an anchor selecting a non-idle primary stack matches the exact current
  thread's attached bounds;
- every attached thread-stack range is pairwise disjoint;
- every attached thread stack is disjoint from every installed hart
  idle-primary and emergency stack;
- no selected or idle stack overlaps an emergency stack or another hart's
  stack.

### Target preparation

The target API prepares one exact inactive thread:

```c
micros_user_execution_prepare(thread, initial_context);
micros_user_execution_inspect(thread, context);
micros_user_execution_detach(thread);
```

Preparation executes with SIE clear and requires:

- typed resolution through the production object registry;
- one exact owning process with a valid bootstrap-phase address space;
- no existing thread context;
- `sepc` page mapped user RX or X;
- `sepc` two-byte aligned, matching the target's compressed-instruction
  IALIGN;
- `sp` 16-byte aligned, inside or exactly one byte past a mapped user RW page,
  with `sp - 1` resolving to that page;
- every user mapping and process owner passing ADR-0021 validation.

The caller does not supply privileged status policy. Its
`initial_context.sstatus` must be zero or preparation rejects the request.
Preparation reads live `sstatus`, requires `UXL = 2` (64-bit user mode), and
derives the saved status with this exact contract:

```text
USER_CONTROL_MASK =
    SIE | SPIE | SPP | UBE | VS | FS | XS | SUM | MXR
USER_REQUIRED_SET = SPIE
USER_REQUIRED_CLEAR =
    SIE | SPP | UBE | VS | FS | XS | SUM | MXR
UXL_MASK = 3 << 32
UXL_REQUIRED = 2 << 32
```

The saved value is:

```text
(live_sstatus & ~USER_CONTROL_MASK) | USER_REQUIRED_SET
```

and must satisfy `(saved & UXL_MASK) == UXL_REQUIRED`. This preserves
read-only and WPRI fields from the live CSR, selects RV64 user execution, and
installs exactly:

- `SPP = 0`;
- `SIE = 0`;
- `SPIE = 1`;
- `SUM = 0`;
- `MXR = 0`;
- `FS`, `VS`, and `XS` off;
- little-endian user memory;
- no unsupported extension state.

An ordinary U-mode return revalidates the same complete control mask, UXL
value, two-byte-aligned `sepc`, executable user mapping, and 16-byte-aligned
mapped user `sp`. A handler may change GPRs or `sepc`, but it cannot modify
preserved fields outside `USER_CONTROL_MASK`.

The prepared context is published only after full stack clearing and all
mapping/status checks succeed.

The API is bootstrap-only in this slice. A later PM-only executable
preparation transition supersedes its caller authority and adds mapping
generation, load-complete token, seal, and `fence.i` rules. The isolated test
may copy a relocation-free payload through the supervisor direct map and issue
a local `fence.i` before preparation.

### First U-mode entry

The production entry operation is non-returning:

```c
_Noreturn micros_user_execution_enter(thread);
```

With SIE clear, it preflights:

- the exact prepared inactive thread and owner process;
- the process address space and complete saved-context policy;
- an idle boot hart with no current thread;
- the thread stack and hart idle/emergency bounds.

Commit then performs only prevalidated non-failing changes:

1. bind the thread to the boot hart, changing it to `RUNNING`;
2. select its kernel stack in the hart trap anchor;
3. activate its process root with ADR-0021's full ASID-zero fences;
4. restore the saved context and execute `sret`.

The audited assembly entry uses one temporary register as the context base,
writes validated `sstatus` and `sepc`, restores x1-x31 with user `sp` and the
base register last, and executes `sret`. It performs no memory access after
restoring the final base register.

`sscratch` remains the hart-anchor pointer. User `gp` and `tp` are restored
from the context; trap entry saves them before installing kernel `tp`.

### User-trap capture and resume

When trap dispatch observes `SPP = 0`, it requires:

- an exact routed hart;
- one exact current `RUNNING` thread;
- the frame to lie inside that thread's selected kernel stack;
- the current process root to match the thread owner's root.

Before component-specific dispatch, it copies every GPR plus saved `sstatus`
and `sepc` from the frame into the thread context through the portable capture
operation. Live SUM is already clear under ADR-0021.

Returning to the same user thread uses the mutable frame and existing trap
epilogue. A handler may change validated user GPRs or `sepc`; it may not set
`SPP`, `SIE`, `SUM`, `MXR`, or extension state. The dispatcher validates the
complete return status and user PC/stack mappings before returning.

Until syscall and page-fault handlers exist, every production U-origin trap is
fatal after capture. Only the isolated component test arms recoverable user
fault and environment-call states.

### Test-only supervisor return

The component test needs cleanup after proving U-mode entry. Its assembly
wrapper saves the supervisor caller state before invoking the production
non-returning entry sequence.

On the test's final environment call, the test handler:

1. verifies and captures the second user context;
2. activates the kernel root with ADR-0021's complete fence sequence;
3. restores the hart idle-primary stack;
4. clears the exact current thread back to `INACTIVE`;
5. selects a validated S-mode continuation and saved supervisor stack;
6. derives a separate supervisor-return status with `SPP = 1`, `SIE = 0`,
   `SPIE = 0`, `SUM = 0`, `MXR = 0`, extension state off, `UBE = 0`, and the
   live `UXL = 2`/preserved fields.

The ordinary epilogue consumes that frame and `sret` returns to the test-only
supervisor continuation with interrupts still disabled. The continuation
restores the saved supervisor status and caller state before returning to C.
No production syscall path uses this escape.

### Native tests

Portable object tests cover:

- exact context attach, inspect, capture, and detach;
- stale process/thread generations;
- duplicate attach and wrong-state rejection;
- stack alignment, size, and overlap boundaries;
- pairwise thread-stack and hart-stack overlap rejection;
- capture only while exact-running/current;
- release rejection while context remains attached;
- output and full-registry preservation on every failure;
- validator corruption of flags, bounds, context bytes, and current-stack
  relationships;
- a seeded model containing attach, capture, detach, bind, clear, and release.

Layout tests bind every user-context offset to the trap-frame ABI and verify
the exact 264-byte size.

### QEMU component test

A separate `MICROS_BUILD_USER_EXECUTION_TEST` image runs after normal object,
FDT, allocator, Sv39, ownership, and trap readiness. It:

1. creates one process root with one RX code page and one RW stack page;
2. copies a relocation-free payload, zeros the rest of both pages, and executes
   `fence.i`;
3. creates one thread and rejects bad PC permissions, bad/misaligned stack
   pointers, odd PCs, nonzero caller `sstatus`, unsafe status attempts,
   duplicate preparation, and stale handles without state/output changes;
4. fills the thread's static kernel-stack slot with a pattern, prepares it, and
   verifies every byte is cleared before entry;
5. enters U-mode with distinct x1-x31 values;
6. proves a user load from kernel text raises an exact U-origin load page
   fault, advances only to the next payload instruction, and resumes U-mode;
7. handles a first environment call, verifies every register and saved
   context, changes one return register and `sepc`, and resumes U-mode;
8. handles a second environment call and proves the changed values survived
   the real `sret` path, while an odd handler-selected resume PC is rejected
   before the valid return is installed;
9. returns through the test-only S-mode continuation, restores the hart idle
   stack, clears current-thread ownership, and verifies the kernel root is
   active;
10. inspects the captured context, detaches and releases the original thread,
    recreates the same slot at the next generation, prepares it against the
    still-live process root, and proves complete 16 KiB stack clearing;
11. detaches and releases the replacement thread, destroys the now-inactive
    process root, releases the process, and returns object/ownership state to
    the baseline.

Only that complete sequence emits:

```text
MICROS_USER_EXECUTION_TEST_PASS mode=entered faults=isolated context=preserved stack=owned return=resumed
```

The host gate requires exactly one pass record after ownership readiness, clean
SBI shutdown, no panic or explicit failure, and no timeout. Parser regressions
reject missing, duplicate, malformed, unterminated, or out-of-order records.

## Consequences

- User register state and kernel continuation storage become thread-owned
  before scheduling exists.
- Every representable thread has a stable supervisor-only stack without adding
  VM-handoff owner classes.
- The hart anchor can route traps to the current thread stack while retaining
  its stable address and emergency stack.
- The first real U-mode round trip validates the context ABI, privilege bits,
  address-space isolation, and trap capture path.
- Scheduler work can add queues and switching without replacing process,
  thread, stack, or context identity.
- Static stacks reserve two MiB even when few threads are live; this is the
  explicit cost of the fixed-capacity MVP.

## Alternatives considered

### Allocate kernel stacks from the frame ledger

This would require a thread-generation owner kind, VM-handoff validation, and
thread-aware batch teardown for memory that is permanently kernel-reserved.
The fixed thread table already establishes a bounded capacity, so a parallel
static stack pool is smaller.

### Continue using one hart primary stack

One stack cannot retain separate blocked or preempted kernel continuations and
would force scheduler work to replace the trap ownership model.

### Embed a complete trap frame in each thread

`scause`, `stval`, and `hart_context` are trap observations, not persistent user
state. A dedicated context keeps the saved ABI exact without preserving stale
trap metadata.

### Add scheduler queues in the same slice

That would combine privilege entry, stack switching, context capture, timer
preemption, runnable ordering, and queue invariants. First proving one
thread/hart round trip keeps failures attributable.

### Add the PM/VM load-complete protocol now

Executable authority, mapping generations, seals, and tokens require IPC and
the VM/PM services. The test-only payload establishes mechanism without
inventing that protocol early.

## Specification basis

- [RISC-V Privileged Architecture, Supervisor-Level ISA](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html)
- [ADR-0001: Target Platform](0001-target-platform.md)
- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0006: VM Bootstrap and Handoff](0006-vm-bootstrap-and-handoff.md)
- [ADR-0010: Testing and Observability](0010-testing-and-observability.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0015: Supervisor Trap Entry](0015-supervisor-trap-entry.md)
- [ADR-0019: Kernel Object Identity and Ownership](0019-kernel-object-identity-and-ownership.md)
- [ADR-0021: Generation-Safe User Address Spaces](0021-generation-safe-user-address-spaces.md)
- [Development dependency DAG](../architecture/development-dag.md)

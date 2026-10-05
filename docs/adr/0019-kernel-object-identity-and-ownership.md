# ADR-0019: Kernel Object Identity and Ownership

- Status: Accepted
- Date: 2026-10-06

## Context

Development DAG Step 5 now has a validated Sv39 kernel address space, but the
kernel still has no process, thread, or hart objects. Trap diagnostics use a
standalone hart ID, timer state lives in a module-global object, and no typed
ownership relationship exists for the future user execution context.

ADR-0011 requires process, thread, endpoint, and hart identities to remain
distinct even though v0.1 permits one thread per process on one hart. ADR-0004
also fixes the future endpoint ABI at a 12-bit process slot plus a 20-bit
nonzero generation. The first object implementation must therefore establish
stable stale-reference and ownership rules before user-mode entry, context
switching, scheduling, or IPC is built on accidental pointer identity.

This slice does not yet create user address spaces, allocate user/kernel thread
stacks, install register contexts, schedule threads, or allocate endpoints. It
provides the bounded identity and ownership substrate those mechanisms will
consume.

## Decision

### Portable registry

The portable kernel-object module owns one caller-provided registry containing
fixed arrays of:

- 64 process slots;
- 128 thread slots;
- eight hart slots.

The process capacity is an implementation bound, not the endpoint ABI limit.
It is deliberately no greater than ADR-0004's 4096 encodable process slots and
can be enlarged without changing handles, endpoint packing, or ownership
rules. Having more thread slots than process slots keeps the representation
structurally capable of multithreading even though the production policy limit
is one live thread per process. Multiple hart slots likewise keep current-thread
ownership and hart-ID uniqueness real invariants rather than singleton claims.

Initialization accepts checked `max_threads_per_process` and `max_harts`
policies. Production uses `1` for both. Native model tests initialize registries
with larger values, exercise several threads owned by one process, and route
threads between two registered harts. The v0.1 limits are therefore policy
rather than process/thread equivalence or singleton storage.

Initialization is one-shot for zero-initialized registry storage. An invalid
policy leaves all bytes unchanged. A second initialization returns
`ALREADY_INITIALIZED` without mutation, and there is no reset operation during
the registry's lifetime. This prevents a reset from reissuing generation one
and resurrecting a stale handle.

The portable module contains no UART, panic, architecture CSR, dynamic
allocation, or hidden interrupt manipulation. A target wrapper owns the one
static production registry and supplies explicit IRQ-save critical sections.

### Typed handles and stale-reference protection

Process and thread references are typed value handles, never raw object
pointers stored across operations:

```c
struct micros_process_handle {
    uint16_t slot;
    uint32_t generation;
};

struct micros_thread_handle {
    uint16_t slot;
    uint32_t generation;
};
```

Generation zero is invalid. A handle resolves only when its slot is in range,
the object is live, and the complete generation matches.

Process generations are limited to `0x000fffff` so the same slot and
generation can later be packed into ADR-0004's endpoint representation without
translation or truncation. A process slot starts at generation zero; its first
allocation uses generation one and each reuse increments it. Before allocation,
the module packs the candidate slot/generation value using ADR-0004's exact
layout. It quarantines the slot instead of issuing a candidate that wraps or
equals reserved endpoint `NONE` (`0xfffffffe`) or `ANY` (`0xffffffff`). A live
slot already at its maximum usable generation is quarantined when released.

Thread generations use the full nonzero 32-bit range and follow the same
increment-and-quarantine rule. Thread handles are internal and are not exposed
as endpoint, process, or public thread IDs.

Allocation selects the lowest available non-quarantined slot. Determinism
makes native traces and early boot behavior reproducible; it is not a future
process-allocation policy.

### Process ownership

A live process record owns:

- its stable process handle;
- its count of live threads;
- the future address-space, endpoint, privilege, grant, and accounting fields
  assigned by later dependency-ready slices.

This slice initializes the future-owned fields to invalid values and does not
invent attachment APIs before their lifecycle contracts exist. Process
creation therefore reserves kernel identity only.

A process can be released only when its live-thread count is zero and no hart
can reach one of its threads. Release invalidates the old handle before the
slot can be reused. PM parent/child, exit, and wait semantics remain outside
the kernel object.

### Thread ownership

A live thread record owns:

- its stable thread handle;
- the exact owning process handle;
- an explicit lifecycle state, initially `INACTIVE`;
- future execution-context, kernel-stack, queue-link, continuation, fault, and
  reply-wait fields added by their later slices.

Thread creation resolves the owner, enforces the registry's
`max_threads_per_process`, selects the lowest available thread slot, and
increments the process's live-thread count atomically within one registry
operation.

Thread release is rejected while any hart names that thread as current.
Otherwise it decrements the matching live owner count and invalidates the
thread handle. A stale or mismatched owner is an invariant error rather than a
silent decrement.

Binding an `INACTIVE` thread to a hart atomically changes it to `RUNNING`.
Clearing the exact current thread atomically returns it to `INACTIVE`. Binding
any other state and releasing a non-`INACTIVE` thread are rejected without
mutation. The object module does not yet perform runnable/blocked transitions
or queue operations; later scheduler work extends this lifecycle without
moving state onto the process object.

### Hart-local state

The production policy registers exactly the OpenSBI boot hart ID and rejects a
duplicate registration or a second distinct hart. A model registry can register
up to its configured hart policy. Each hart record owns:

- the hardware hart ID;
- an optional current-thread handle;
- primary and emergency trap-stack bounds plus trap-installation state;
- interrupt nesting, preemption, and reschedule placeholders;
- the one-hart timer mechanism state previously held in `kernel/timer.c`.

There is no global `current_process`, `current_thread`, or standalone trap hart
ID. A current process is derived only by resolving the current thread's owner.
Binding requires a registered hart with no current thread and a live
`INACTIVE` thread. It rejects an occupied destination and a thread already
current on another hart without changing either thread or hart. Clearing
requires the exact `RUNNING` thread currently bound.

Each installed hart begins with this fixed assembly-visible trap anchor:

```c
struct micros_hart_trap_anchor {
    uintptr_t primary_stack_top;
    uintptr_t primary_stack_bottom;
    uintptr_t emergency_stack_top;
    uintptr_t emergency_stack_bottom;
    uintptr_t entry_t0;
    uintptr_t entry_t1;
    uintptr_t entry_t2;
};
```

The anchor is the first member of the hart record, so the anchor address and
hart-record address are identical. C static assertions verify every assembly
offset and that the anchor begins at offset zero. Outside dispatch, `sscratch`
contains this exact address rather than a stack top.

ADR-0019 supersedes ADR-0015's installation steps 2-3, exact regular prologue,
nested-stack selection, exact epilogue, and zero-valued final frame field. The
288-byte frame size and all field offsets remain unchanged; the final field is
renamed `hart_context`.

The replacement regular prologue is exact:

1. atomically swap `sp` and `sscratch`, yielding the hart anchor in `sp` and
   the interrupted stack pointer in `sscratch`;
2. reject a zero anchor as the nested-trap sentinel before any stack access;
3. save interrupted `t0`, `t1`, and `t2` in the anchor scratch words;
4. retain the hart pointer in `t2`, load the primary stack top through it, and
   allocate the 288-byte frame;
5. copy the saved `t0`-`t2`, interrupted `sp`, and interrupted `tp` into their
   frame fields;
6. write the hart pointer to `hart_context` and install the same pointer in
   kernel `tp`;
7. write zero to `sscratch`;
8. save the remaining GPRs and trap CSRs.

The anchor scratch stores, primary-stack load, frame allocation, and frame
stores through step 6 are required wired, aligned, non-faulting accesses.
Kernel `tp` is usable before the zero sentinel is armed. Afterward, a nested
trap swaps zero into `sp`, obtains the hart from `tp`, loads that hart's
emergency stack top, and enters the non-returning nested handler.

The C dispatcher remains frame-only. Before any component-specific handling,
it verifies that `hart_context` resolves the registered hart, the live frame
address lies within that hart's primary stack bounds, and `sscratch` is zero.

The replacement regular epilogue is exact:

1. restore validated `sstatus` and `sepc`;
2. restore every GPR except `t0`, `t1`, `t2`, `tp`, and `sp`;
3. write the frame's validated `hart_context` to `sscratch`;
4. restore interrupted `t0`, `t1`, `t2`, and `tp`;
5. load interrupted `sp` directly from the wired frame as the final memory
   access;
6. execute `sret`.

The final hart-context, register, and stack-pointer frame loads are required
wired and non-faulting. After return, `sscratch` again contains the exact hart
anchor while the interrupted `tp` and every other GPR are restored.

Trap installation records the linker-defined stack bounds in the boot hart,
initializes its anchor, and programs `sscratch` and `stvec`. Trap dispatch and
nested panic use only the routed hart record for the diagnostic ID. Timer
initialization, start, stop, tick inspection, and interrupt handling accept or
resolve that hart's timer state; the interrupt dispatcher passes its routed
hart explicitly. The legacy standalone `micros_trap_hart_id` and module-global
`timer_state` symbols are removed.

Later scheduler work will replace only the current-thread and scheduling
placeholders, not the hart identity, trap routing, or timer ownership boundary.

### Registry invariants

The portable validator checks:

- stored live counts equal the arrays' actual live objects;
- every live handle has a nonzero in-range generation and matching slot;
- every live thread resolves one live owning process;
- each process's thread count equals the number of live threads that name it;
- no live process exceeds the configured thread policy;
- every current-thread handle resolves one live `RUNNING` thread;
- every `RUNNING` thread is current on exactly one hart;
- every other live thread is `INACTIVE`;
- one thread is current on at most one hart;
- every registered hart ID is unique;
- the number of registered harts does not exceed the configured hart policy;
- each installed trap anchor points back to its exact registered hart;
- free and quarantined slots contain no live ownership or current-thread state;
- generation values never exceed their object limit.

Every failed initialization, registration, create, release, resolve, bind, or
clear operation leaves the registry unchanged. The validator is called after
production initialization and by target self-tests; native tests check it
after every model operation.

### Boot integration and observability

`kernel_main` initializes the production registry before trap installation,
registers the passed OpenSBI hart ID, and installs the trap stacks into that
hart. Once serial output is available, every image emits exactly one:

```text
MICROS_OBJECTS_READY processes=0x0000000000000000 threads=0x0000000000000000 harts=0x0000000000000001 max-threads=0x0000000000000001 max-harts=0x0000000000000001 boot-hart=0x0000000000000000
```

All fields are fixed-width lowercase hexadecimal. Every ordinary image has no
live process or thread yet, exactly one registered hart, production thread
and hart limits of one, and the actual OpenSBI boot hart ID. The record appears
after `MICROS_BOOT` and before `MICROS_TRAP_READY`. Every target workflow
requires that exact shape and ordering.

An initialization failure before trap installation uses the explicit boot hart
argument in the existing panic path. After installation, trap-aware failures
use the hart object.

### Native tests

The native suite compiles the production portable registry under ASan and
UBSan. Deterministic cases cover:

- lowest-slot process and thread allocation;
- nonzero generation and generation advance after reuse;
- stale process and thread rejection;
- process-release rejection while threads are live;
- thread-release rejection while current on a hart;
- exact thread-count maintenance;
- production one-thread policy rejection with no state change;
- a model registry with several threads owned by one process;
- atomic `INACTIVE`/`RUNNING` bind and clear transitions;
- two-hart current-thread routing and cross-hart duplicate rejection;
- occupied-destination bind rejection with both threads and hart unchanged;
- production duplicate-hart and secondary-hart rejection;
- process and thread generation exhaustion and quarantine;
- reserved endpoint candidate rejection at slots `0x0ffe` and `0x0fff`;
- rejected invalid and repeated registry initialization without mutation;
- all argument, capacity, state, ownership, and stale-handle errors.

A seeded model compares create/release/bind/clear operations against a compact
reference state. It validates the complete registry after every operation and
prints the seed and failing step.

### QEMU component test

A separate `MICROS_BUILD_OBJECT_MODEL_TEST` image passes through production
object initialization and trap installation. It then:

1. verifies the boot hart ID and both recorded trap-stack ranges;
2. creates one process and one thread;
3. rejects a second live thread for that process under the production policy;
4. binds the `INACTIVE` thread, verifies it is `RUNNING`, and resolves it as the
   boot hart's current thread;
5. rejects release while current, clears it back to `INACTIVE`, and releases
   the thread;
6. releases and reallocates the same process slot;
7. verifies the generation advanced and the stale process handle no longer
   resolves;
8. releases the replacement and verifies zero live processes and threads with
   one registered hart.

Only then does it emit:

```text
MICROS_OBJECT_MODEL_TEST_PASS process-generation=advanced stale=rejected thread-limit=enforced hart-local=preserved
```

The host gate requires exactly one valid object-ready record and one exact pass
record in order, plus normal FDT, allocator, and MMU readiness, clean SBI
shutdown, no panic or explicit failure, and no timeout. Host regressions reject
missing, duplicated, malformed, unterminated, or early records.

The regular trap component test also verifies that the live frame lies within
the registered primary stack bounds and, after `sret`, that `sscratch` contains
the exact hart anchor pointer.

An additional isolated nested-trap image installs a test-only emergency stack
in the registered boot-hart anchor. Its bounds are distinct from the
linker-default emergency stack. The initial test assembly loads a poison value
into interrupted `tp` and raises an expected exception. In this build only,
trap entry deliberately raises a second illegal instruction immediately after
installing kernel `tp` and clearing `sscratch`, before any later frame stores or
C dispatch. The nested path must recover the registered hart through kernel
`tp`, preserve the outer frame pointer placed in `sscratch` by the nested swap,
clear `sscratch` again, switch to the test-only emergency stack, and enter the
non-returning nested handler. The handler validates:

- the routed hart ID;
- that its live stack address lies strictly within the configured emergency
  bounds;
- that the preserved outer frame lies within the same hart's primary bounds;
- that the outer frame's `hart_context` identifies the routed hart;
- that the outer frame's saved `tp` equals the injected poison value.

Only then does it emit:

```text
MICROS_NESTED_TRAP_TEST_PASS hart=routed emergency-stack=selected
```

It then performs clean SBI shutdown. The host gate rejects a panic, a return to
normal dispatch, the linker-default emergency stack, or any missing,
duplicated, malformed, unterminated, or out-of-order pass record. This target
proves that interrupted `tp` is saved before kernel `tp` is installed, kernel
`tp` precedes the sentinel, and per-hart nested routing is usable at the first
instruction after sentinel arming.

All pre-existing target workflows require object readiness. Trap, trap-panic,
timer, and MMU component tests validate a nonzero registered `hart_context` in
every delivered frame. The timer component test also snapshots its boot
hart-owned active/deadline/tick state after real interrupts. The mandatory ELF
audit rejects the legacy `micros_trap_hart_id` and `timer_state` symbols. These
checks make it impossible to satisfy acceptance by mirroring the new hart
object beside the old standalone state.

## Consequences

- Process, thread, and hart identity become explicit before user execution or
  IPC can depend on them.
- Stale internal references are rejected from the first object allocation.
- The one-thread and one-hart MVP bounds remain checked policy.
- The first hart object becomes authoritative for trap and timer mechanism
  state.
- Fixed arrays make exhaustion explicit and native tests deterministic.
- User address spaces, contexts, stacks, scheduling, endpoints, privileges,
  IPC queues, and process semantics remain later work.

## Alternatives considered

### Use object pointers as identities

Pointers are convenient but make reuse silently validate stale references and
couple identity to storage layout. Typed slot/generation handles make lifetime
checks explicit.

### Allocate one thread object inside each process

This enforces the MVP limit structurally and would require replacing ownership,
queue, hart-current, and reply-routing representations for multithreading.

### Allocate all 4096 endpoint-encodable process slots immediately

The ABI permits 4096 slots, but the shell MVP does not need that much static
metadata. A checked 64-slot implementation limit preserves the ABI and can be
raised deliberately.

### Add execution contexts and scheduling in the same pull request

That would mix identity reuse, ownership, RISC-V privilege return, kernel-stack
switching, queues, timer preemption, and context selection. Establishing the
portable object invariants first keeps later target failures attributable.

## Specification basis

- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0004: IPC and Endpoint ABI](0004-ipc-and-endpoint-abi.md)
- [ADR-0010: Testing and Observability](0010-testing-and-observability.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0015: Supervisor Trap Entry](0015-supervisor-trap-entry.md)
- [ADR-0016: Supervisor Timer Interrupts](0016-supervisor-timer-interrupts.md)
- [ADR-0018: Sv39 Kernel Address Space](0018-sv39-kernel-address-space.md)
- [Development dependency DAG](../architecture/development-dag.md)

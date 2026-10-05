# ADR-0006: VM Bootstrap and Handoff

- Status: Accepted
- Date: 2026-10-05

## Context

The VM server needs memory and an address space before it can manage memory.
The kernel also needs page tables and frames before user IPC exists. Treating
kernel and VM as ordinary runtime peers during startup creates a dependency
cycle.

## Decision

The kernel owns a bootstrap memory subsystem that:

- reserves firmware, kernel, image, stack, page-table, and device ranges;
- creates initial kernel and service mappings;
- allocates the frames required to start IPC, the launcher, and VM;
- records every reservation and allocation in a handoff ledger.

The VM server starts from an embedded image. It receives:

- the usable physical memory map;
- all reserved ranges;
- all existing user address spaces and mappings;
- the kernel-owned frame pool;
- the frame pool eligible for VM ownership;
- the ranges wired for VM's own fault-handling working set.

VM builds its frame database and sends `VM_READY` with the handoff version,
range count, and total managed-frame count. The kernel validates that summary
against its ownership ledger and then atomically:

1. marks the handoff complete;
2. rejects further bootstrap user-frame allocations;
3. designates VM as the only authority for user mapping policy;
4. enables page-fault delivery to VM.

The handoff is one-way during the MVP. The kernel retains map/unmap validation,
page-table mechanics, and its reserved frame pool. VM cannot map kernel-owned
frames into user space.

Before `VM_READY`, the kernel wires VM's complete code, read-only data, writable
data, stack, frame database, IPC buffers, grant buffers, and page-table working
set. VM may not make those mappings pageable or release their frames in v0.1.
The kernel does not deliver a VM-originated fault back to VM; such a fault is a
fatal invariant violation with full trap diagnostics.

VM reserves a bounded scratch virtual-address window. During spawn it maps
newly allocated child frames into that window, initializes them from a VFS
grant, unmaps the scratch aliases, and only then maps the frames into the child.
VM then invokes a mapping-complete operation. The kernel freezes that child
mapping generation and returns an opaque load-complete token, which VM sends to
PM. Any subsequent mapping change invalidates the token. VM has no authority
to initialize the child context, publish the executable, activate the endpoint,
or run the thread.

PM is the sole caller of the kernel executable-preparation transition. It
presents the load-complete token with the initial context; the kernel validates
the frozen generation, performs required page-table invalidation and a local
`fence.i`, initializes the thread, stores the validated mapping generation,
seals the address space against VM mapping operations, and leaves the thread
held in a prepared state.

Only PM may activate or abort that prepared process. Activation revalidates the
stored generation and seal before making the thread runnable. Abort clears the
prepared context and seal so VM can release or replace mappings. A requested
mapping change while prepared is rejected rather than silently invalidating
the instruction-synchronization point; PM must abort and repeat load completion
and preparation.

Before handoff, an unrecoverable allocation failure is fatal. After handoff,
VM returns explicit allocation errors. VM restart and ownership reconstruction
are deferred to the recovery milestone.

## Consequences

- Kernel and VM can be implemented in development-DAG order.
- Every physical frame has an explicit owner at the transition.
- Bootstrap-only allocation paths can be disabled and tested.
- VM failure remains fatal until RS and recovery protocols exist.
- VM cannot deadlock waiting for itself to resolve its own page fault.
- The ledger and transition require strong invariants and QEMU tests.

## Alternatives considered

### Keep physical allocation permanently in the kernel

This removes the startup cycle but places long-term memory policy in privileged
code.

### Start VM with an ad hoc untracked pool

This is easy initially but makes it impossible to prove that kernel and VM
ownership are disjoint.

### Make handoff reversible immediately

Reversal requires recovery, state reconstruction, and concurrent protocol
design before a working VM exists.

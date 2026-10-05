# ADR-0009: Spawn Before Fork

- Status: Accepted
- Date: 2026-10-05

## Context

POSIX `fork` requires address-space duplication, open-file inheritance,
process-table coordination, and eventually copy-on-write. Implementing it
before VM, PM, and VFS have stable ownership would create a multi-service
transaction with no simpler operation to validate each participant.

## Decision

The first general process-creation API is `spawn`.

PM coordinates the transaction:

1. reserve an unpublished process, its first thread, and an endpoint;
2. use PM's narrow kernel capability to install the immutable `APPLICATION`
   privilege profile;
3. ask VFS to resolve, authorize, and buffer the bounded executable;
4. have VFS create a read-only direct grant from that resident buffer to VM;
5. ask VM to allocate child frames, map them into its scratch window, load and
   validate ELF segments, remove the scratch aliases, build and freeze the
   child mappings, and return a kernel-issued load-complete token;
6. ask VFS to apply explicit initial descriptor actions; init receives the
   synthetic console object as descriptors 0, 1, and 2, while later children
   may duplicate selected parent descriptors;
7. have PM present the load-complete token and initial context to the kernel's
   executable-preparation operation; the kernel validates the frozen mapping
   generation, executes a local `fence.i`, initializes the thread, and leaves
   it held with the validated address space sealed against mapping changes;
8. commit the prepared child to PM and VFS state;
9. have PM invoke the final activation operation, which atomically makes the
   endpoint visible and the first thread runnable only after revalidating the
   sealed mapping generation.

Each step returns a resource token that PM can release in reverse order if a
later step fails. No partially created process becomes visible to ordinary
clients. If failure occurs after preparation, PM first invokes abort to clear
the prepared context and mapping seal; VM may change or release mappings only
after that acknowledgment.

The first API supports executable path, argument vector, environment vector,
initial descriptor actions, parent identity, exit status, and wait. The exact
wire protocol is defined before implementation.

Descriptor actions are explicit and bounded. v0.1 supports installing init's
three console descriptors and duplicating selected parent descriptors; it does
not perform implicit unrestricted descriptor inheritance.

An `exec` operation may reuse the loader and replacement transaction after
spawn is stable. `fork`, copy-on-write, unrestricted descriptor inheritance,
and full POSIX semantics are deferred.

## Consequences

- The first process transaction has explicit ownership and rollback.
- The shell can launch programs without `fork`.
- Common Unix shell implementation patterns are initially unavailable.
- PM remains the coordinator, avoiding VFS-to-VM callbacks during initial
  executable loading.
- Buffering an executable in VFS adds a copy and temporary memory cost that is
  accepted for the MVP.
- Installing one fixed application profile avoids giving PM arbitrary
  privilege-management authority.
- VM proves that mappings are complete, but PM alone controls preparation and
  activation of the process transaction.
- Adding fork later requires a new ADR and conformance tests.

## Alternatives considered

### Implement fork first

This couples PM, VM, VFS, and kernel state before any simpler process-creation
path has proven their interfaces.

### Kernel-only process creation

This would move executable, descriptor, and process policy into privileged
code.

### Combine PM and VM

Combining services reduces one protocol but obscures the intended separation
between process semantics and memory policy.

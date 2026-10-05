# ADR-0005: Direct Memory Grants

- Status: Accepted
- Date: 2026-10-05

## Context

Fixed-size IPC cannot efficiently transfer terminal buffers, executable data,
or file contents. Passing virtual addresses directly would let a confused or
malicious server access memory beyond the caller's intent.

## Decision

The MVP uses kernel-managed direct grants.

A process creates a grant containing:

- one authorized grantee endpoint;
- a base virtual address in the grantor;
- a byte length;
- read, write, or read/write permission;
- a live generation managed by the kernel.

Read permission allows the grantee to copy from the grantor into the grantee's
own memory. Write permission allows the grantee to copy from its own memory
into the grantor.

The returned grant identifier is an opaque 32-bit token. Its internal
slot/generation split is not part of the user ABI.

The grantee invokes checked copy operations with:

- the grantor endpoint;
- grant identifier;
- grant-relative offset;
- local address;
- requested length;
- copy direction.

The kernel verifies:

- live grantor and grantee endpoint generations;
- grant lifetime;
- requested direction;
- integer overflow;
- grant bounds;
- local and remote user ranges;
- resident mappings for the full operation.

An MVP safe-copy operation does not invoke VM to resolve a fault. If either
range is not resident and valid, the operation fails with an explicit error.
This rule prevents a kernel/VM callback cycle in the first implementation.
The kernel validates the complete range before copying. The MVP operation
either copies every requested byte or returns an error; it does not report
partial success.

Grants are revoked explicitly or automatically when either participating
process exits. Indirect grants, magic grants, arbitrary shared mappings, and
implicit pointer copying are deferred.

Direct grants are non-transitive. A grantee cannot use another process's grant
as authority for a third process.

### Three-party data paths

VFS owns bounded resident bounce buffers, initially one page per active
transfer slot:

- For an application write, VFS copies from the application's read grant into
  a VFS buffer, then grants RAMFS or TTY read access to that VFS buffer.
- For an application read, VFS grants RAMFS or TTY write access to a VFS
  buffer, then copies from that buffer into the application's write grant.
- Grant identifiers received from an application are never forwarded to a
  backend service.

For executable loading, VFS buffers the bounded executable image in resident
memory and grants VM read access. VM allocates child frames, temporarily maps
them into a VM-only scratch window, copies from the VFS grant into that window,
removes the scratch mappings, and then maps the initialized frames into the
child. Safe-copy never treats a child address as if it belonged to VM. VM asks
the kernel to freeze the resulting mapping generation and issue an opaque
load-complete token to PM. PM consumes that token in the kernel preparation
transition, which executes a local `fence.i` before the child can become
runnable.

## Consequences

- Every bulk transfer has a bounded, reviewable authority.
- Kernel-managed metadata is simpler and avoids grant-table time-of-check/
  time-of-use races in the MVP.
- The kernel consumes bounded state per active grant.
- Services must arrange resident buffers before I/O.
- Three-party transfers perform an additional bounded copy in the MVP.
- Zero-copy mappings and restartable faulting copies can be evaluated later
  without changing message size.

## Alternatives considered

### User-managed grant tables

This resembles MINIX and reduces grant-creation syscalls, but it requires safe
table traversal, update rules, and fault behavior before VM integration is
stable.

### Shared memory only

Shared mappings can be efficient but require lifetime, revocation, and
concurrent mutation policy for every transfer.

### Copy arbitrary remote addresses

This provides no capability boundary and makes pointer mistakes cross process
isolation boundaries.

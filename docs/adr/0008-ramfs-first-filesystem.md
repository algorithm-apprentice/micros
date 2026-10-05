# ADR-0008: RAMFS-First Filesystem

- Status: Accepted
- Date: 2026-10-05

## Context

A persistent root filesystem introduces VFS, filesystem, block-driver,
discovery, cache, and recovery dependencies at once. Those dependencies are
not required to test process isolation, IPC, grants, path lookup, descriptors,
or a shell.

## Decision

The first filesystem is a user-space RAMFS server.

- VFS and RAMFS remain separate services to exercise the filesystem protocol.
- RAMFS supports regular files, directories, lookup, create, open, read,
  write, close-related reference handling, and directory enumeration.
- The root is initialized from a build-generated in-memory image.
- The seed-image format is an internal build format, not a stable disk ABI.
- RAMFS becomes writable after initialization.
- File data moves through direct grants.
- VFS mediates application/backend transfers with resident bounce buffers
  because direct grants are non-transitive.
- VFS mounts RAMFS at `/` through a fixed bootstrap endpoint.
- There is no block layer, persistent metadata, journaling, page cache, or DS
  discovery in the shell MVP.

VFS owns descriptors and namespace routing. RAMFS owns filesystem objects and
file data.

VFS also owns one synthetic console object bound to the fixed TTY endpoint.
It is not a RAMFS inode or a generally discoverable device node. During init
spawn, VFS installs this object as descriptors 0, 1, and 2 with the appropriate
read/write modes. Later spawn operations may explicitly duplicate a parent's
console descriptors into the child. Applications continue to call VFS; they
never receive direct TTY authority.

## Consequences

- The initial filesystem path remains fully user-space without requiring a
  storage driver.
- Reboot discards changes.
- VFS/filesystem protocol design is exercised before an on-disk format is
  frozen.
- Executables and initial files must be included in the build-generated image
  or embedded boot image.
- Persistent storage can later be added behind VFS rather than changing
  applications.
- The shell has standard streams without requiring device nodes, DS, or a
  general driver namespace.

## Alternatives considered

### Port MFS and a memory block driver

This more closely follows MINIX but adds an on-disk format, block protocol, and
driver discovery before the basic server architecture is proven.

### Put RAMFS inside VFS

This is smaller but avoids the IPC and grant boundary that future filesystem
servers must use.

### Host-backed QEMU filesystem

A host bridge would obscure the target filesystem protocol and create a
development-only dependency.

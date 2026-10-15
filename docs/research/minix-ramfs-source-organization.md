# MINIX RAMFS Source-Organization Baseline

## Reference and scope

```text
MINIX: 4db99f4012570a577414fe2a43697b2f239b699e
micros: 4290ca21c06ac88b299d694c236b774896cb6171
```

The fixed local MINIX checkout was inspected read-only. No MINIX code is
copied or executed. This note classifies organization for the separate
RAMFS source correction in [ADR-0066](../adr/0066-ramfs-source-decomposition.md).
The existing [VFS/MFS protocol study](minix-vfs-mfs-filesystem-protocol.md)
remains the sole ADR-0025 behavioral ledger for Step 12. Its adaptations,
and Accepted ADR-0048, are not replaced by this source-only outcome.

## Fixed source trace

| MINIX source | Observed responsibility | Required micros disposition |
| --- | --- | --- |
| `minix/lib/libfsdriver/fsdriver.c:18-66,82-105` | The driver authenticates VFS, gates ordinary operations on mount, dispatches, forms a reply, and owns the receive loop. | Separate the portable codec/facade from state handlers; retain the existing actual service adapter rather than importing a driver framework. |
| `minix/fs/mfs/table.c:13-46` | One table names mount, lookup, counted putnode, regular I/O, getdents, create, and mkdir implementations. | Give the already supported operations named responsibility owners; retain the current explicit bounded switch and exact operation set. |
| `minix/fs/mfs/Makefile:2-9` | The server links responsibility-specific inode, mount, open, path, read, and write sources. | Define one complete portable source set and expand it once in every actual consumer. Do not copy the broader MINIX file list or block-driver libraries. |
| `minix/fs/mfs/proto.h:15-69` | Private prototypes identify allocation/reference, mount, creation, and pathname boundaries. | Introduce one narrow portable private contract; do not put extraction-only helpers into the stable public RAMFS headers. |
| `minix/fs/mfs/inode.c:17-79` | Counted putnode and inode lifetime belong to inode ownership rather than message transport. | Keep generation resolution, reference checks, root pin, and counted release with fixed-node ownership. |
| `minix/fs/mfs/open.c:17-116` | Create/mkdir coordinate parent/name validation and inode/directory changes. | Name namespace preflight, grant staging, and publication while preserving the already Accepted volatile-table semantics and error order. |
| `minix/lib/libfsdriver/lookup.c:118-284` | Grant-backed path processing and temporary traversal references are explicit protocol work. | Keep complete path-byte validation, confinement, traversal, and final reference publication visible; retain ADR-0048's existing differences, not MINIX's temporary-reference machinery. |

## Difference classification

| Difference | Classification | Reason and evidence boundary |
| --- | --- | --- |
| Responsibility-specific codec, node, namespace, I/O, and directory owners | Baseline parity | Preserve the same separation of authority without changing supported operations. Native semantics and actual grant/IPC evidence remain required. |
| One fixed in-memory table, one exact VFS peer, generation handles, bounded direct grants, and one scratch page | Required adaptation | Already classified by the behavioral ledger and Accepted ADR-0048; source movement cannot import persistent inodes, block caches, worker synchronization, or MINIX wire layouts. |
| Explicit state arguments and private family declarations instead of MINIX service globals | Required adaptation | ADR-0051 requires visible ownership and an acyclic private contract. Production state remains owned by its caller; test contexts contain only pre-existing storage. |
| Independent native reference/projection, source reconstruction, and target report owners | Compatible extension | These organize existing development evidence, not filesystem behavior or kernel authority. Reference math remains independent and the kernel still authenticates the real reporting thread and PC. |
| Exact source-equivalence metadata and reviewed CMake recipe declaration maintenance | Compatible extension | Required to preserve micros' existing validation coverage and closed reuse eligibility after moving sources. It authorizes no new external input or reusable workflow. |

## Design conclusion

Follow the [canonical source map](../architecture/ramfs-source-organization.md),
not MINIX's exact granularity. Keep the cohesive seed parser and real service
adapter intact. Preserve all existing request, staging, commit, failure,
reference, and readiness behavior. New RAMFS semantics require a later
separately reviewed design and are not part of this correction.

# ADR-0051: Responsibility-Based Source Modules

- Status: Accepted
- Date: 2026-10-12
- Owners: micros maintainers
- Related:
  - [ADR-0025](0025-minix-behavioral-baseline-before-optimization.md)
  - [ADR-0033](0033-reviewed-design-before-implementation.md)
  - [ADR-0049](0049-vfs-service-and-application-io-protocol.md)
  - [ADR-0050](0050-post-handoff-elf-spawn.md)
  - [Source organization](../architecture/source-organization.md)
  - [MINIX VFS source-organization baseline](../research/minix-vfs-source-organization.md)

## Context

The portable VFS implementation was built incrementally while its descriptors,
namespace, console, and spawn protocols were still being established. The
resulting `servers/vfs/vfs_core.c` has 6,043 lines and 100 function
definitions. It owns several independently testable state machines and
resource lifecycles in one translation unit.

The next Step 14 work adds exit cleanup. Adding that behavior to the same file
would further mix process lifecycle, descriptor ownership, device completion,
namespace operations, and spawn rollback.

MINIX VFS provides the behavioral baseline and also demonstrates a useful
organizational property: its receive loop, descriptor table, vnode ownership,
path traversal, regular I/O, character-device handling, process lifecycle,
executable handling, and filesystem request wrappers live in
responsibility-specific modules. `micros` must not import MINIX's worker,
locking, recovery, or multi-filesystem machinery, but it should preserve the
same clarity of authority.

The repository also contains other large implementation and test files.
Solving all of them in one change would violate the single-focus development
workflow. A general rule and a staged adoption path are required.

## Decision

`micros` organizes source by cohesive authority, resource lifecycle, protocol
layer, or algorithm. A source file must not become the default owner for
unrelated behavior merely because it already has access to the subsystem
state.

The canonical operational rules and review signals are documented in
[Responsibility-Based Source Organization](../architecture/source-organization.md).
Line count and function count are review signals, not definitions of
correctness. A large cohesive algorithm may remain intact; a smaller file that
mixes independent authorities is still incorrectly organized.

### Public and private boundaries

Production subsystems use:

- a small public facade for supported entry points;
- private responsibility modules for implementation;
- one explicit private header for the narrow cross-module contract;
- explicit state parameters rather than new mutable global context;
- file-local functions unless another responsibility requires the helper.

Public headers must not accumulate private declarations merely to support
source movement.

### Function boundaries

Non-algorithmic handlers separate request validation, reservation, commit, and
rollback when those are distinct phases. Helper extraction must preserve exact
error precedence, authority checks, and no-mutation-on-failure behavior.

A function larger than 120 physical lines requires explicit review. It should
be split unless it implements one intrinsically cohesive algorithm or state
transition whose correctness would be obscured by further extraction.

The complete current VFS disposition is recorded in
[Responsibility-Based Source Organization](../architecture/source-organization.md).
Every current over-limit VFS function is a required split; none receives an
algorithmic exception.

### VFS decomposition

The first implementation of this decision splits the portable VFS core into
the exact ownership areas defined in the source-organization architecture:

- facade and dispatch;
- complete state validation;
- fixed-table and reference ownership;
- checked transfer and grant primitives;
- root-mount and namespace operations;
- regular application I/O;
- console pending I/O;
- process lifecycle;
- spawn transaction;
- one private internal header.

`vfs_core.h` remains the stable public state and API contract.
`vfs_service_core.c` and `vfs_service_core.h` remain the byte-exact
message-codec layer. `vfs_service.c` remains the production IPC/backend
adapter. The new `vfs_internal.h` is private to the portable-core modules and
is not included by either service layer.

The decomposition is behavior-preserving. It does not authorize exit cleanup,
wire changes, state-layout changes, new allocation, new concurrency, or a new
filesystem abstraction.

### Staged repository adoption

The repository adopts this decision sequentially:

1. complete and review this design;
2. decompose VFS without changing runtime behavior;
3. audit the remaining review-signal files;
4. create separate design and implementation tasks for each justified
   subsystem correction.

No implementation task may expand into a repository-wide cleanup.

### Validation

The VFS implementation begins with a structural test that names the complete
portable-core and existing service-layer source set and verifies that every
VFS consumer builds from the required files.

Each extraction commit must keep the focused VFS native state, service, and
model tests green. The final implementation must also pass the VFS image,
QEMU, and complete pull-request validation gates defined by the testing
strategy.

An independent reviewer must inspect the complete move, cross-module
visibility, ownership direction, error ordering, and build integration. A
passing runtime suite is necessary but does not prove that the resulting
module boundaries are coherent.

## Alternatives considered

### Keep one VFS core until v0.1

Rejected. Exit cleanup would increase the number of authorities in an already
mixed file and make both implementation and review harder.

### Adopt MINIX's exact file list

Rejected. MINIX's modules reflect multiple workers, locking, several
filesystem and device classes, recovery, and a much broader POSIX surface.
Copying that granularity would add unused abstractions rather than preserve
the relevant baseline.

### Enforce a hard repository-wide line limit

Rejected. Physical size does not identify authority by itself and would
encourage mechanical splits or fragmented algorithms. The chosen limits are
review signals coupled to responsibility analysis.

### Refactor every oversized file now

Rejected. It would combine unrelated subsystems, prevent small independent
reviews, and delay the dependency-ready Step 14 path.

### Split files without a private internal contract

Rejected. Adding implementation helpers to public headers would enlarge the
supported API, while ad hoc `extern` declarations would make dependencies
implicit and fragile.

## Consequences

Positive consequences:

- ownership and transition boundaries become visible in source layout;
- further VFS lifecycle work has an explicit home;
- reviewers can inspect smaller responsibility-specific changes;
- private dependencies remain explicit without changing the public contract;
- the same rule can guide later subsystem audits without requiring an
  immediate rewrite.

Costs and risks:

- the initial VFS move touches build lists and many source lines despite
  preserving behavior;
- cross-module visibility mistakes can appear during extraction;
- careless helper extraction can reorder validation or rollback;
- review signals require engineering judgment rather than a simple size gate.

The implementation mitigates those risks through a structural Red test,
small green extraction commits, existing model and QEMU evidence, and an
independent review of the complete change.

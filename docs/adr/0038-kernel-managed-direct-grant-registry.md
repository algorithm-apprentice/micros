# ADR-0038: Kernel-Managed Direct Grant Registry

- Status: Accepted
- Date: 2026-10-08
- Refines: ADR-0005

## Context

ADR-0005 selects direct, non-transitive memory grants as the shell-MVP bulk
data authority. A grant binds one bounded grantor range to one exact grantee
and direction. The kernel later uses that authority for checked
cross-address-space copy.

The fixed MINIX baseline uses user-managed grant tables, direct/indirect/magic
entries, and index/sequence identifiers. Revocation invalidates a token and
advances its sequence before the slot returns to a LIFO free list.

`micros` already chooses a smaller authority model:

- active grant records are kernel-managed;
- only direct non-transitive grants exist;
- exact endpoint generations replace wildcard authority;
- stale generations never become valid again;
- checked copy and its target evidence receive a separate design only after
  grant identity and lifetime are implemented.

The canonical evidence is
[the MINIX direct grant and safe-copy study](../research/minix-direct-grants-and-safecopy.md).

This decision defines exactly one dependency-ready outcome: the direct-grant
registry and lifecycle. It does not authorize checked-copy implementation.

## Decision

### Scope

This slice defines:

- one fixed-capacity portable grant registry;
- opaque token packing and stale-token rejection;
- exact grantor/grantee authority;
- create, inspect, and revoke operations;
- endpoint-cancellation preflight and commit;
- one authoritative target grant runtime;
- full registry/endpoint/object validation;
- native deterministic, replayable model, and QEMU lifecycle evidence.

It does not define:

- safe-copy address translation or byte movement;
- copy-from or copy-to interfaces;
- copy length, physical plans, mapping faults, or copy errors;
- a U-mode syscall number, register ABI, or user-runtime wrapper;
- indirect, magic, wildcard, delegated, self, or transitive grants;
- vectored, scatter/gather, asynchronous, or partial copy;
- VM fault callbacks, page pinning, or shared mappings;
- service protocols or SMP synchronization.

After this implementation merges, a separate checked-copy design task may
consume the accepted registry interface. That later task has its own paired
implementation PR.

### Stable types and permissions

The kernel grant interface defines:

```c
typedef uint32_t micros_grant_t;
```

`MICROS_GRANT_NONE` is `UINT32_MAX` and is never allocated.

Permissions are:

```text
MICROS_GRANT_READ   = 1 << 0
MICROS_GRANT_WRITE  = 1 << 1
```

At least one permission is required. Unknown bits are rejected.

Permissions are named from the grantee's perspective:

- `READ`: a later checked-copy operation may read grantor memory;
- `WRITE`: a later checked-copy operation may write grantor memory.

This slice stores and validates those bits but does not copy memory.

The token is opaque to users. No protocol may extract or depend on its current
slot/generation packing.

### Fixed registry and token identity

The v0.1 registry has:

```text
MICROS_GRANT_CAPACITY = 64
MICROS_GRANT_SLOT_BITS = 6
```

The current internal token packs:

```text
bits 0..5    registry slot
bits 6..31   nonzero generation
```

Generation zero and `MICROS_GRANT_NONE` are invalid tokens.

Each slot has lifecycle:

```text
FREE -> ACTIVE -> FREE
ACTIVE -> QUARANTINED
```

Initialization places every slot in canonical `FREE` state with generation
one and zero authority fields. The generation stored by a free slot is the
generation to use on its next allocation.

Allocation selects the lowest slot in canonical `FREE` state and publishes an
`ACTIVE` record using that already stored generation. It does not advance any
generation during search or commit.

Revoke and endpoint cancellation clear authority and then:

- advance generation and enter `FREE` when the next token is nonzero,
  packable, and not `MICROS_GRANT_NONE`;
- otherwise enter `QUARANTINED` with the terminal generation retained.

This aligns generation advance with the MINIX revoke boundary while preventing
wrap or reserved-token resurrection.

A failed allocation, including complete capacity exhaustion, preserves every
registry byte. No free slot may contain a generation whose next active token is
already invalid; that state is corruption rather than an allocation-time
repair.

Capacity and packing are current implementation bounds, not user ABI. Changing
them later must preserve outstanding-token invalidation.

### Grant record

Each active record stores:

- slot state and generation;
- exact grantor process handle;
- exact grantor endpoint;
- exact grantee endpoint;
- grantor virtual base;
- byte length;
- permission bits.

The record stores virtual authority only. It never retains a physical address,
page-table entry, mapping generation, user pointer, or thread identity.

### Creation authority

A grant belongs to one process generation, not one thread.

Creation requires:

- initialized externally serialized grant, endpoint, and object registries;
- one exact live grantor process;
- the grantor's exact active primary endpoint;
- one exact active grantee endpoint distinct from the grantor;
- a nonzero known permission subset;
- a nonzero base within the user window;
- a nonzero length with no overflow and complete range inside the user window;
- a non-null output token.

The output remains unchanged on failure.

Creation validates range shape but not current page residency. The later
checked-copy design must revalidate every touched mapping and permission and
must not retain physical authority in this record.

No destination-profile mask is consulted. The grantor explicitly exposes only
its own bounded range to one exact grantee. A later service protocol decides
whether creating that grant is appropriate.

Self-grants, `ANY`, `NONE`, reserved endpoints, stale generations, and inactive
endpoints are rejected.

### Portable lifecycle interface

The portable interface is equivalent to:

```c
micros_grant_registry_initialize(registry);
micros_grant_create(
    registry,
    endpoint_registry,
    objects,
    grantor_process,
    grantee_endpoint,
    base,
    length,
    permissions,
    grant_out
);
micros_grant_inspect(
    registry,
    endpoint_registry,
    objects,
    grantor_process,
    grant,
    record_out
);
micros_grant_revoke(
    registry,
    endpoint_registry,
    objects,
    grantor_process,
    grant
);
micros_grant_registry_validate(
    registry,
    endpoint_registry,
    objects
);
```

Inspection returns a value snapshot, never mutable registry authority. It
requires the exact live grantor process; it is not a grantee introspection
capability.

Every operation resolves exact endpoint and process generations. A malformed
or stale token never aliases a different active record.

### Authoritative target runtime

The target owns one static grant registry for the boot lifetime. It exposes
internal equivalents of:

```c
micros_grant_runtime_initialize();
micros_grant_runtime_registry();
micros_grant_runtime_validate();
```

Initialization is one-shot and has no reset. It prepares a canonical empty
candidate, validates that candidate against the authoritative endpoint/object
runtimes, and publishes readiness only after success. Failed initialization
preserves every runtime byte.

Target create, inspect, revoke, and cancellation use this same registry. Host
tests may pass caller-owned registries. Later checked-copy and user-runtime
tasks must not instantiate another target registry.

### Inspect and revoke

Token parsing first rejects:

- `MICROS_GRANT_NONE`;
- generation zero;
- slot outside `MICROS_GRANT_CAPACITY`.

For a well-formed token:

- `FREE` or `QUARANTINED` slot state is `STALE_GRANT`;
- generation mismatch is `STALE_GRANT`;
- exact active record with a different grantor is `UNAUTHORIZED`.

Inspect copies the complete record only after every check succeeds.

Revoke requires the exact live grantor process and active token. On success it
clears authority and performs the `FREE` or `QUARANTINED` transition defined
above. Revoking a grant does not inspect or modify either process's mappings.

### Endpoint cancellation

Every active grant references two endpoint generations. Endpoint teardown must
cancel grants where the closing endpoint is either:

- the grantor endpoint; or
- the grantee endpoint.

The grant layer provides a bounded plan:

```c
micros_grant_prepare_endpoint_cancel(..., endpoint, plan);
micros_grant_commit_endpoint_cancel(..., plan);
```

The plan contains:

- the exact endpoint being canceled;
- one bit per grant slot;
- the expected active generation for every selected slot;
- one-use plan state that detects caller reuse or foreign-registry commit.

Preflight:

- rejects special or malformed endpoints;
- resolves the exact active endpoint;
- validates the complete grant/endpoint/object relation;
- records every matching active grant;
- performs no mutation.

Commit:

1. verifies plan ownership, one-use state, and every selected slot/generation
   before the first mutation;
2. consumes the plan;
3. clears every selected record;
4. advances each selected generation to `FREE` or `QUARANTINED`;
5. cannot fail after mutation begins.

The future process/endpoint lifecycle owner executes, with SIE clear:

1. grant cancellation preflight;
2. `micros_ipc_endpoint_close`;
3. non-failing grant cancellation commit;
4. process release.

If IPC close fails, grants remain unchanged. Once IPC close commits, the
prevalidated grant commit cannot fail. This preserves failure atomicity without
making the lower IPC layer depend on grants.

Until that lifecycle owner exists, native and QEMU registry tests invoke this
sequence explicitly. A grant becomes unusable immediately when either stored
endpoint generation stops resolving, even before cancellation reclaims its
slot.

### Registry invariants

Full validation requires:

- every active record token repacks to its exact slot/generation;
- every active grantor handle resolves one live process generation;
- stored grantor endpoint equals that process's exact active primary endpoint;
- stored grantee resolves one distinct exact active endpoint;
- permissions are a nonzero known subset;
- base plus length is nonwrapping and inside the user window;
- free records contain zero authority fields and one packable nonzero next
  generation;
- quarantined records contain zero authority fields and are never allocatable;
- a quarantined token is stale;
- active tokens are unique;
- live count equals the number of active records;
- no active record uses generation zero or `MICROS_GRANT_NONE`.

Validation detects corruption and never repairs it.

### Operation-specific error precedence

#### Initialize

1. null storage: `ARGUMENT`;
2. already initialized: `STATE`;
3. unavailable or invalid authoritative endpoint/object runtime: `INVARIANT`;
4. otherwise success.

#### Create

1. null output, unknown/zero permissions, zero base/length, special grantee,
   arithmetic overflow, or range outside the user window:
   `ARGUMENT` or `RANGE`;
2. grant registry, endpoint registry, or object corruption: `INVARIANT`;
3. stale grantor process or stale grantee endpoint: `DEAD_ENDPOINT`;
4. missing active grantor endpoint, reserved/inactive grantee, or self-grant:
   `STATE` or `UNAUTHORIZED` as appropriate;
5. no canonical free slot: `CAPACITY`;
6. otherwise success.

#### Inspect and revoke

1. null output for inspect or malformed token packing: `ARGUMENT`;
2. registry relation corruption: `INVARIANT`;
3. stale grantor process or missing active grantor endpoint: `DEAD_ENDPOINT`;
4. free/quarantined slot or generation mismatch: `STALE_GRANT`;
5. active token owned by another grantor: `UNAUTHORIZED`;
6. otherwise success.

#### Endpoint cancellation

Prepare precedence is:

1. null plan or special endpoint: `ARGUMENT`;
2. grant/endpoint/object relation corruption: `INVARIANT`;
3. stale endpoint generation: `DEAD_ENDPOINT`;
4. reserved, inactive, or otherwise non-active endpoint: `STATE`;
5. otherwise success with no mutation.

Commit precedence is:

1. null plan: `ARGUMENT`;
2. inactive, already consumed, or foreign-registry plan: `STATE`;
3. malformed, duplicate, or out-of-range selected slot: `INVARIANT`;
4. selected slot not active at the exact prepared generation:
   `INVARIANT`;
5. otherwise consume the plan and perform the non-failing mutation pass.

Commit deliberately does not re-resolve the endpoint or run the complete
active-endpoint relation validator: the required lifecycle has already closed
that endpoint between prepare and commit. With SIE clear, any selected
slot/generation disagreement is corruption or caller misuse, not an ordinary
stale-token race.

Capacity is a create-only result. A token naming a quarantined slot is always
`STALE_GRANT`.

Tests combine invalid conditions to prove this precedence. No failure changes
an output token, output record, registry byte, endpoint, object, or plan.

### Serialization and resource bounds

Portable grant lifecycle operations require external serialization. The
one-hart target holds SIE clear across:

- complete grant/endpoint/object validation;
- token allocation and revoke;
- cancellation planning and commit;
- target runtime publication.

The mechanism:

- uses 64 fixed records and no dynamic allocation;
- scans at most `MICROS_GRANT_CAPACITY` records for allocation, validation, or
  cancellation;
- runs the existing fixed-capacity endpoint/object validator;
- stores no physical authority.

This is not an SMP locking design. Future multihart support must preserve token
identity and no-use-after-revoke semantics.

## Test-first evidence

### Native deterministic tests

Tests must cover:

- token pack/unpack boundaries, generation zero, `NONE`, malformed slots, and
  the slot-63 reserved-token edge;
- canonical generation-one initialization;
- lowest-slot allocation and complete capacity exhaustion;
- mutation-free failed allocation;
- exact grantor and grantee generations;
- read, write, and read/write permissions;
- zero/unknown permission rejection;
- base/length overflow and user-window boundaries;
- self, `ANY`, `NONE`, reserved, inactive, and stale grantee rejection;
- exact grantor-only inspect and revoke authority;
- revoke-time generation advance;
- stale token rejection before and after reuse;
- `ACTIVE -> QUARANTINED` at the last usable generation;
- quarantined-token `STALE_GRANT`;
- output and complete-registry preservation on every failure;
- cancellation by grantor and grantee endpoint;
- IPC-close failure leaving a prepared cancellation uncommitted;
- one-use/foreign-plan commit rejection before mutation;
- validator corruption of count, token, owner, endpoint, range, permission,
  free generation, active state, and quarantine state;
- dual-invalid inputs for every operation-specific precedence branch.

### Replayable model

A model runs at least 4,096 create, inspect, revoke, cancel,
endpoint-close/reuse, denial, malformed-token, quarantine, and exhaustion
transitions.

It compares:

- complete grant slot state and generations;
- active count;
- exact grantor/grantee endpoint generations;
- cancellation plans and consumed state;
- endpoint lifecycle and reuse;
- output preservation after every failure.

Failures print the seed and recent complete operation trace.

### QEMU endpoint/grant-registry component

The existing endpoint QEMU image extends its target-runtime evidence:

1. initialize the authoritative grant runtime once;
2. create exact client, server, and peer generations;
3. create read and write grants between active endpoints;
4. inspect and revoke with exact grantor authority;
5. reject wrong-owner and stale tokens;
6. prepare cancellation for either participant, close the endpoint, then
   commit the prevalidated cancellation;
7. reuse a process/endpoint generation and prove old tokens remain stale;
8. restore grant, endpoint, object, and hart baselines.

Its existing pass record is extended with:

```text
grants=generation-safe
```

No user copy, grant syscall, or memory-transfer behavior is claimed by this
gate.

### Validation ownership

Grant registry and runtime files select:

- complete native tests and the replayable grant model;
- `test-qemu-endpoint`;
- existing object, IPC lifecycle, and full shared-header gates selected by the
  fail-closed planner;
- documentation and all three diff checks.

## Consequences

- Direct-grant identity and lifetime become generation-safe before byte copy
  or user ABI work begins.
- Kernel-managed records avoid mutable user-table validation.
- Endpoint teardown gains one preflight/commit cancellation participant.
- The later checked-copy design receives a stable direct-grant record and
  error model.
- More design/implementation PRs are required, but each review remains bounded
  to one outcome.

## Alternatives considered

### Combine registry and checked copy in one implementation

Rejected under ADR-0033. Token lifecycle/cancellation and cross-address-space
copy have distinct state, failure, QEMU, and review evidence.

### Reproduce user-managed MINIX grant tables

Rejected for v0.1. It adds table registration, concurrent mutation, copy-in
validation, live-update identity, and fault-marker behavior before a service
runtime exists.

### Advance generation during allocation

Rejected. Exhausted free slots would need quarantine while allocation may
ultimately fail, violating mutation-free failure. Advancing during revoke or
cancellation matches the baseline lifetime boundary.

### Preserve MINIX LIFO reuse

Rejected as a compatible deterministic extension. Lowest-slot allocation makes
models and diagnostics stable without changing valid authority semantics.

### Add indirect or magic grants

Rejected. They make authority transitive and are unnecessary with the accepted
VFS bounce-buffer and VM scratch-window paths.

### Define user syscalls now

Rejected. Registry authority can be proven before the freestanding runtime
chooses a unified syscall namespace.

## Specification basis

- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0004: IPC and Endpoint ABI](0004-ipc-and-endpoint-abi.md)
- [ADR-0005: Direct Memory Grants](0005-direct-memory-grants.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0029: Endpoint and Privilege Substrate](0029-endpoint-and-privilege-substrate.md)
- [ADR-0033: Reviewed Design Before Implementation](0033-reviewed-design-before-implementation.md)
- [MINIX direct grant and safe-copy study](../research/minix-direct-grants-and-safecopy.md)
- [Development dependency DAG](../architecture/development-dag.md)

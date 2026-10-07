# ADR-0039: Page-Bounded Checked Grant Copy

- Status: Accepted
- Date: 2026-10-08
- Refines: ADR-0005 and ADR-0038

## Context

ADR-0038 provides one authoritative generation-safe direct-grant registry.
Each active record binds:

- one exact grantor process and endpoint generation;
- one exact grantee endpoint generation;
- one bounded grantor virtual range;
- read and/or write authority.

The next dependency-ready outcome is checked byte transfer under that
authority.

The fixed MINIX baseline verifies a grant token, exact grantee, direction, and
grant-relative bounds before copying between the grantor and the caller. MINIX
may involve VM for ordinary faults, supports `CPF_TRY`, and offers vectored
operations that may complete earlier entries before a later failure.

ADR-0005 already chooses a smaller bootstrap contract:

- resident mappings only;
- direct non-transitive grants only;
- scalar operations;
- complete local and remote validation before copying;
- all requested bytes or no bytes;
- no VM callback.

The completed address-space substrate can translate an arbitrary user byte
address into an exact process-owned physical address, permissions, and bytes
remaining in that page. The IPC buffer layer demonstrates retained two-page
plans for fixed 64-byte messages, but checked grant copy has different range,
direction, error, and participant authority and does not change that IPC API.

The canonical baseline evidence is
[the MINIX direct grant and safe-copy study](../research/minix-direct-grants-and-safecopy.md).

## Decision

### Scope

This slice defines:

- native-testable grant copy authorization and bounds preflight;
- scalar `copy_from` and `copy_to` kernel interfaces;
- one-page maximum copy length;
- local and remote user-range translation plans;
- complete direction, mapping, permission, ownership, and overlap validation;
- non-failing retained-physical-plan commit;
- target-runtime integration with the authoritative grant registry;
- deterministic native/model tests and one QEMU grant-copy component.

It does not define:

- U-mode syscall numbers, registers, or user-runtime wrappers;
- vectored, scatter/gather, asynchronous, partial, or restartable copy;
- indirect, magic, wildcard, delegated, self, or transitive grants;
- VM fault callbacks, page pinning, retry, or recovery;
- shared mappings or zero-copy transfer;
- service protocols;
- SMP synchronization.

This implementation is explicitly bootstrap-phase-only. It does not define
post-handoff mapping authority.

The freestanding user-runtime design later exposes these completed operations
only after a separate reviewed mapping-authority design replaces the
bootstrap translator for handed-off address spaces. That replacement must not
change direction, bounds, identity, or failure atomicity.

### Direction

Direction remains named from the grantee's perspective:

- `copy_from`: read grantor memory into grantee-local memory and require
  `MICROS_GRANT_PERMISSION_READ`;
- `copy_to`: read grantee-local memory into grantor memory and require
  `MICROS_GRANT_PERMISSION_WRITE`.

The opposite grant permission never substitutes. A read/write grant permits
both operations.

### Interfaces

The portable target-facing interfaces are equivalent to:

```c
micros_grant_copy_from(
    grant_registry,
    endpoint_registry,
    objects,
    grantee_process,
    grantor_endpoint,
    grant,
    grant_offset,
    local_address,
    length
);

micros_grant_copy_to(
    grant_registry,
    endpoint_registry,
    objects,
    grantee_process,
    grantor_endpoint,
    grant,
    grant_offset,
    local_address,
    length
);
```

They add no token, endpoint, mapping, or completion state.

An internal native-testable authority helper is equivalent to:

```c
micros_grant_prepare_copy_authority(
    grant_registry,
    endpoint_registry,
    objects,
    grantee_process,
    grantor_endpoint,
    grant,
    grant_offset,
    length,
    required_permission,
    authority_out
);
```

The output contains only value state needed by target translation:

- exact grantor process handle;
- exact grantee process handle;
- resolved remote virtual address;
- local length;
- required direction.

It contains no physical address. The output remains unchanged on failure.

The target interfaces use the same authoritative registry published by
`micros_grant_runtime`; no second target grant registry exists.

### Copy size

One operation accepts:

```text
0 <= length <= MICROS_GRANT_COPY_MAX
MICROS_GRANT_COPY_MAX = MICROS_SV39_PAGE_SIZE
```

Larger protocol transfers repeat page-bounded operations.

A nonzero page-sized unaligned range spans at most two pages. Each process plan
therefore retains at most two chunks.

Zero-length copy is a successful no-op only after token, participant,
direction, and grant-offset validation. It ignores `local_address` and performs
no translation or memory access.

### Authority and bounds preflight

For nonzero and zero-length operations, authority preflight validates:

- registry/object/output presence;
- one exact required permission;
- exact live grantee process and endpoint;
- token shape and active generation;
- exact stored grantee;
- exact supplied and stored grantor;
- required direction;
- grant-relative bounds and remote-address arithmetic.

The helper returns the exact stored grantor process and resolved remote
address. It does not inspect page tables. The sole normative error precedence
is defined below.

The supplied grantor endpoint remains part of the protocol, matching the
baseline safe-copy call. Knowing a token without both exact participant
generations is insufficient.

### Bootstrap phase boundary

The first implementation calls the existing bootstrap
`micros_user_address_space_translate()` mechanism and requires the typed frame
ownership phase to remain `BOOTSTRAP`.

If ownership has entered handoff or handed-off state, checked copy returns:

```text
MICROS_GRANT_ERROR_PHASE
```

before any user-range translation or byte access, including for zero-length
requests.

This is an ADR-0025 staged substitution. It allows grant and copy mechanisms
to be completed in DAG order without inventing VM callbacks. Before the
launcher or any post-handoff service can use checked copy, a separate reviewed
mapping-authority design must provide generation-bound translation in the
handed-off phase. That task may replace only range resolution and ownership
validation; grant tokens, participants, directions, bounds, and commit
atomicity remain unchanged.

### User-range planning

For nonzero bootstrap-phase copies, target preflight validates:

- nonzero local address;
- nonwrapping local address plus length;
- complete local and remote ranges in
  `[MICROS_USER_VIRTUAL_BASE, MICROS_USER_VIRTUAL_END)`;
- every touched leaf resolves to the exact process generation;
- every touched frame has exact `PROCESS_USER` ownership;
- required page permissions.

Permissions are:

| Operation | Grantor range | Grantee-local range |
| --- | --- | --- |
| `copy_from` | readable | writable |
| `copy_to` | writable | readable |

Writable Sv39 user mappings are already required to be readable by the
address-space layer; checked copy still requests only the permission needed for
the relevant side.

The target planner calls `micros_user_address_space_translate()` page by page
and retains:

```c
struct micros_grant_copy_chunk {
    uint64_t physical_address;
    size_t length;
};

struct micros_grant_copy_range_plan {
    struct micros_grant_copy_chunk chunks[2];
    size_t chunk_count;
};
```

Grant-copy planning is separate from the fixed-size IPC buffer plan. The first
implementation may share small internal helpers only when doing so does not
change IPC behavior or broaden this task.

### Error classification for translation

Address-space outcomes caused by the supplied ranges map to
`MICROS_GRANT_ERROR_FAULT`:

- user-range failure;
- absent mapping;
- insufficient read or write permission.

These remain `MICROS_GRANT_ERROR_INVARIANT`:

- stale process after exact participant preflight;
- missing prepared root for a live participant;
- foreign frame ownership;
- malformed PTE;
- scratch/reentrancy failure;
- uninitialized address-space runtime;
- any address-space invariant.

`MICROS_USER_ADDRESS_SPACE_ERROR_PHASE` maps only to
`MICROS_GRANT_ERROR_PHASE`.

There is no fallback, partial result, implicit zero fill, or VM request.

### Physical nonoverlap

Before commit, every source chunk is compared against every destination chunk.
Any physical overlap is an invariant failure.

Every physical chunk end is checked for address overflow before interval
comparison.

Current typed ownership should already prevent distinct live process
generations from sharing `PROCESS_USER` frames. The explicit check preserves
copy semantics if later mapping mechanisms become more general and prevents a
future self-grant or alias from silently requiring `memmove`.

### Commit

Preflight produces one source and one destination range plan plus total length.

Commit:

1. starts at the first source and destination chunk;
2. copies the minimum remaining bytes in those chunks;
3. advances exhausted chunks;
4. stops only after exactly `length` bytes.

It uses kernel-accessible physical addresses and never dereferences a user
virtual pointer or enables SUM.

It does not repeat grant, endpoint, process, page-table, permission, or
ownership validation. With one hart and SIE clear, no mapping, revoke, or
endpoint transition can race the retained plans.

After preflight, commit consists only of bounded loads/stores and cannot return
a recoverable error. Every successful operation copies exactly all requested
bytes. Every recoverable failure copies zero bytes.

### Error model and precedence

ADR-0038 errors remain authoritative and add:

```text
MICROS_GRANT_ERROR_FAULT
MICROS_GRANT_ERROR_PHASE
```

The following is the sole normative operation order:

1. null registry/object/output arguments, required permission other than
   exactly `READ` or `WRITE`, or special grantor endpoint:
   `ARGUMENT`;
2. `length > MICROS_GRANT_COPY_MAX`, nonzero local range overflow, local range
   outside the user window, or `grant_offset + length` overflow:
   `RANGE`;
3. malformed grant token packing: `ARGUMENT`;
4. grant, endpoint, or object registry corruption: `INVARIANT`;
5. stale/dead grantee process or missing active grantee endpoint:
   `DEAD_ENDPOINT`;
6. free/quarantined slot or token generation mismatch:
   `STALE_GRANT`;
7. active token naming another grantee: `UNAUTHORIZED`;
8. stale/dead supplied grantor endpoint: `DEAD_ENDPOINT`;
9. active but wrong grantor endpoint or missing required direction:
   `UNAUTHORIZED`;
10. grant-relative end beyond grant length or remote-address arithmetic/range
    failure: `RANGE`;
11. ownership phase other than `BOOTSTRAP`: `PHASE`;
12. zero length: success without translation;
13. prepare both remote and local range plans;
14. if either plan reports structural ownership, PTE, root, scratch, or
    runtime corruption: `INVARIANT`;
15. otherwise, if either plan reports absent mapping, user range, or
    insufficient permission: `FAULT`;
16. physical chunk overflow or overlap: `INVARIANT`;
17. otherwise commit success.

Both plans are prepared before translation errors are classified. An
`INVARIANT` from either side takes precedence over a `FAULT` from the other;
there is no source-before-destination ambiguity.

Tests combine invalid conditions to prove precedence. No failure changes
registry state, endpoint/object state, page tables, output authority, source
bytes, destination bytes, or surrounding canaries.

### Serialization

Portable copy operations require external serialization. The one-hart target
holds SIE clear across:

- authority validation;
- both range translations;
- physical overlap checks;
- retained-plan commit.

Grant create, revoke, cancellation, endpoint close, and address-space mutation
cannot interleave.

This is not an SMP locking or page-pinning design. Future multihart support must
preserve no-copy-after-revoke and retained-plan mapping stability.

### Resource bounds

One copy:

- allocates no memory;
- validates one grant record plus fixed-capacity registries;
- translates at most two pages per participant;
- performs at most four physical interval-overlap checks;
- copies at most one 4 KiB page;
- retains no authority after return.

The full registry and address-space validator costs are consciously accepted
for v0.1. Optimization requires measurement after baseline integration.

## Test-first evidence

### Native authority and plan tests

Native deterministic tests must cover:

- exact `copy_from`/read and `copy_to`/write direction;
- read/write grants permitting both;
- malformed, free, quarantined, stale-generation, and revoked tokens;
- wrong grantee and wrong active grantor;
- stale grantee and stale grantor endpoint generations;
- explicit non-bootstrap `PHASE` rejection before translation;
- zero-length copy and `grant_offset == grant.length`;
- maximum length;
- oversized length;
- grant offset/length overflow and exact end boundaries;
- remote address overflow;
- unchanged authority output on every failure;
- dual-invalid inputs for every precedence boundary;
- pure source/destination chunk pairing for page-local, source-crossing,
  destination-crossing, and both-crossing plans;
- physical overlap rejection before commit;
- copy-loop exact byte count across unequal chunk boundaries.

The replayable grant model adds authorize-from, authorize-to, denial, stale
token, revoke, cancel, endpoint reuse, and bounds operations. It compares the
complete grant registry, participant generations, authority output, and
unchanged byte arrays after every modeled failure.

### QEMU grant-copy component

A dedicated `test-qemu-grant` image uses three exact address spaces:

1. grantor and grantee map distinct resident data pages;
2. a third process acts as wrong grantee;
3. read and write grants are created through the authoritative target runtime;
4. `copy_from` and `copy_to` succeed for page-local ranges;
5. both directions succeed when both local and remote ranges cross pages;
6. copied bytes match exactly and surrounding canaries remain unchanged;
7. wrong grantee, wrong direction, wrong active grantor, stale token, stale
   endpoint, overflow, out-of-bounds, oversized, unmapped, and permission
   failures preserve every byte and registry/object state;
8. revoke and prepare-close-commit cancellation immediately remove authority;
9. endpoint/process generation reuse cannot resurrect copy authority;
10. grants, endpoints, objects, roots, frames, and allocator counts return to
    baseline.

Only then may it emit:

```text
MICROS_GRANT_TEST_PASS identity=generation-safe directions=checked bounds=validated copies=atomic phase=bootstrap cleanup=complete
```

No user ecall or service protocol is claimed by this gate.

This gate proves bootstrap-phase copy only. Post-handoff copy cannot be claimed
until the later mapping-authority design and its own integration evidence
merge.

### Validation ownership

Checked-copy implementation, grant authorization, grant runtime, address-space
translation, or physical-plan files select:

- complete native tests and the grant model;
- `test-qemu-grant`;
- `test-qemu-user-address-space`;
- existing endpoint, frame-ownership, IPC-buffer, and shared-header gates
  selected by the fail-closed planner;
- documentation and all three diff checks.

## Consequences

- Checked copy becomes a bounded capability operation rather than arbitrary
  remote-pointer access.
- Large data paths repeat page-sized copies through resident bounce buffers.
- Failure cannot expose partial destination state.
- Physical plans remain operation-local and cannot outlive revocation.
- The first implementation is unavailable after VM handoff until a reviewed
  mapping-authority replacement exists.
- User-runtime syscall design remains a separate small task.

## Alternatives considered

### Unbounded scalar copy

Rejected. Retaining an unbounded physical plan would require dynamic
allocation, page pinning, or revalidation during commit.

### Reuse the 64-byte IPC buffer API directly

Rejected. Grant copy has variable page-bounded length, two participants,
direction authority, overlap checks, and different errors. Hidden coupling
would make both mechanisms harder to review.

### Copy page by page while validating incrementally

Rejected. A later mapping or permission failure would expose partial success.

### Enable SUM and copy through user virtual pointers

Rejected. It would depend on one active root, bypass exact participant
translation, and weaken nested-trap safety.

### Ask VM to resolve absent pages

Rejected until VM-safe fault delivery is dependency-ready.

### Define user syscalls now

Rejected. Target mechanism evidence precedes the separately reviewed
freestanding runtime ABI.

## Specification basis

- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0005: Direct Memory Grants](0005-direct-memory-grants.md)
- [ADR-0020: Typed Bootstrap Frame Ownership](0020-typed-bootstrap-frame-ownership.md)
- [ADR-0021: Generation-Safe User Address Spaces](0021-generation-safe-user-address-spaces.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0033: Reviewed Design Before Implementation](0033-reviewed-design-before-implementation.md)
- [ADR-0038: Kernel-Managed Direct Grant Registry](0038-kernel-managed-direct-grant-registry.md)
- [MINIX direct grant and safe-copy study](../research/minix-direct-grants-and-safecopy.md)
- [Development dependency DAG](../architecture/development-dag.md)

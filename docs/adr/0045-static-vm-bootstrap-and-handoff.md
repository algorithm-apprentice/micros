# ADR-0045: Static VM Bootstrap and One-Way Handoff

- Status: Accepted
- Date: 2026-10-08
- Refines: ADR-0003, ADR-0006, ADR-0017, ADR-0020, ADR-0021,
  ADR-0025, ADR-0040, ADR-0043, and ADR-0044
- Supersedes in part:
  - ADR-0006's statement that the ownership commit immediately enables
    non-VM page-fault delivery. Step 9 establishes exact VM policy authority,
    but dynamic mapping and fault delivery remain unavailable until a reviewed
    post-handoff mapping protocol exists.

## Context

Development-DAG Step 8 is complete:

- every static service image, root, stack, context, endpoint, and scheduler
  policy is prepared before launcher entry;
- the launcher releases one exact manifest service at a time;
- VM is the first non-launcher production role;
- every release has one guest-owned readiness deadline;
- one exact ready call is acknowledged through its reply token; and
- final launcher completion seals bootstrap authority.

The kernel already owns the mechanisms required for the one-way memory
transition:

- a bounded FDT-derived frame allocator;
- a dense typed frame-ownership ledger;
- exact process-generation `PROCESS_PAGE_TABLE` and `PROCESS_USER` owners;
- staged `VM_WIRED` and `VM_TRANSFERABLE` targets;
- complete private-root reachability validation;
- an irreversible ownership commit;
- post-handoff read-only validation, lookup, translation, activation, IPC
  buffers, checked grants, and scheduler return through exact `VM_WIRED`
  leaves; and
- phase rejection for generic allocation, release, map, unmap, root
  destruction, and execution-context preparation after handoff.

What is missing is the user-space handoff owner. VM must consume one complete
kernel memory snapshot, construct and validate its private frame database,
prove that it is ready to own policy, and trigger the irreversible commit
before the launcher may acknowledge VM's ordinary readiness.

The fixed MINIX baseline is documented in
[the VM bootstrap and handoff study](../research/minix-vm-bootstrap-and-handoff.md).
MINIX makes VM an exceptional boot process, supplies kernel boot memory and
process metadata, initializes a VM-owned free-page bitmap, reconstructs VM's
pre-mapped page tables from wired spare pages, and leaves privileged mapping
mechanics in the kernel. A VM-originated page fault is fatal.

The stable MINIX VM protocol is much broader than the dependency-ready Step 9
outcome. Fork, exec, page-fault delivery, sharing, file-backed mappings,
mapping caches, live update, and recovery require later PM, VFS, DS, RS, and
storage milestones. Pulling them into this task would recreate the runtime
cycle that the development DAG intentionally broke.

## Decision

### Scope

This outcome implements:

- one real statically embedded VM service ELF;
- one fixed pointer-free VM boot-information and frame-database ABI;
- kernel construction and inactive-root patching of that object after every
  static service mapping exists;
- atomic staging of every static `PROCESS_USER` frame as `VM_WIRED`;
- a VM-side validator and deterministic frame-state summary;
- syscall operation 12, authorized only to the exact manifest VM generation;
- failure-atomic summary validation and the existing irreversible wired
  ownership commit;
- one exact VM handoff runtime binding;
- the ADR-0043 VM role gate before ordinary readiness acknowledgment;
- fatal VM-originated fault handling;
- native transition/model evidence and isolated QEMU handoff evidence; and
- fail-closed validation ownership.

It does not implement:

- post-handoff frame allocation or release;
- map, unmap, page-table growth, aliases, shared mappings, copy-on-write, or
  scratch aliases;
- non-VM page-fault delivery, suspension, retry, or fault replies;
- `VM_TRANSFERABLE` as live PTE authority;
- mapping generations, load-complete tokens, prepared-address-space sealing,
  PM executable preparation, or spawn;
- file-backed mappings, paging, overcommit, reclaim, swap, or cache policy;
- live update, restart, reconstruction, recovery, SMP synchronization, ASIDs,
  or remote TLB shootdown;
- PM, VFS, TTY, RAMFS, DS, scheduler-server, or RS protocols; or
- a production six-service image.

A later mapping design must precede the first post-handoff mapping mutation or
page-fault delivery. PM may model metadata in its next DAG task, but no process
may receive pageable or newly allocated user memory until that VM mapping
authority is reviewed and implemented.

## Fixed VM boot-information ABI

### Exported object

The VM ELF exports one global, 4096-byte-aligned, writable, all-zero object:

```c
struct micros_vm_boot_info {
    struct micros_vm_boot_header header;
    struct micros_vm_physical_range
        memory_ranges[MICROS_VM_MAX_MEMORY_RANGES];
    struct micros_vm_physical_range
        reserved_ranges[MICROS_VM_MAX_RESERVED_RANGES];
    struct micros_vm_managed_range
        managed_ranges[MICROS_VM_MAX_MANAGED_RANGES];
    struct micros_vm_address_space
        address_spaces[MICROS_VM_MAX_STATIC_ADDRESS_SPACES];
    struct micros_vm_mapping
        mappings[MICROS_VM_MAX_STATIC_MAPPINGS];
    uint8_t frame_states[MICROS_VM_MAX_MANAGED_FRAMES];
};
```

The fixed capacities are the existing kernel capacities:

```text
MICROS_VM_MAX_MEMORY_RANGES  = 16
MICROS_VM_MAX_RESERVED_RANGES = 80
MICROS_VM_MAX_MANAGED_RANGES = 96
MICROS_VM_MAX_STATIC_ADDRESS_SPACES = 6
MICROS_VM_MAX_STATIC_MAPPINGS = 4096
MICROS_VM_MAX_MANAGED_FRAMES = 262144
```

The complete object is exactly 364672 bytes, fits in 90 4-KiB pages, contains
no pointer, and lies wholly inside VM's RW/NX segment.

The 192-byte header is equivalent to:

```c
struct micros_vm_boot_header {
    uint32_t version;
    uint32_t header_size;
    uint32_t total_size;
    uint32_t physical_range_size;
    uint32_t managed_range_size;
    uint32_t memory_range_capacity;
    uint32_t memory_range_count;
    uint32_t reserved_range_capacity;
    uint32_t reserved_range_count;
    uint32_t managed_range_capacity;
    uint32_t managed_range_count;
    uint32_t frame_state_capacity;
    uint32_t frame_state_size;
    uint32_t service_id;
    uint32_t self_endpoint;
    uint64_t managed_frame_count;
    uint64_t free_frame_count;
    uint64_t kernel_frame_count;
    uint64_t vm_self_wired_frame_count;
    uint64_t service_wired_frame_count;
    uint64_t transferable_frame_count;
    uint64_t digest;
    uint32_t ownership_phase;
    uint32_t flags;
    uint32_t address_space_entry_size;
    uint32_t address_space_capacity;
    uint32_t address_space_count;
    uint32_t mapping_entry_size;
    uint32_t mapping_capacity;
    uint32_t mapping_count;
    uint64_t reserved[5];
};
```

Version 1 requires:

```text
version             = 1
header_size         = 192
total_size          = 364672
physical_range_size = 16
managed_range_size  = 24
address_space_entry_size = 32
mapping_entry_size  = 24
frame_state_size    = 1
ownership_phase     = BOOTSTRAP
flags               = 0
```

Every unused header, range, and frame-state byte is zero. Compile-time size and
offset assertions close the ABI.

### Range records

Physical memory and reservation records are:

```c
struct micros_vm_physical_range {
    uint64_t base;
    uint64_t size;
};
```

The kernel copies the canonical memory and reserved ranges used by the
bootstrap frame allocator. Reservation records include firmware, the kernel
image and static BSS, boot stacks, FDT reservation-map entries,
`/reserved-memory` ranges, and every other permanent range excluded before
managed-frame derivation. They explicitly exclude the reclaimed raw FDT blob;
its complete frames follow the accepted ADR-0017 reclamation rule.

Managed records are:

```c
struct micros_vm_managed_range {
    uint64_t base;
    uint64_t frame_count;
    uint64_t frame_index;
};
```

They exactly match the allocator and ownership-ledger range snapshots. Active
ranges are page-aligned, nonempty, sorted, nonoverlapping, and cover exactly
`managed_frame_count` state bytes.

### Existing address spaces and mappings

The snapshot contains every live static address space:

```c
struct micros_vm_address_space {
    uint32_t service_id;
    uint32_t endpoint;
    uint32_t process_generation;
    uint16_t process_slot;
    uint16_t mapping_count;
    uint32_t mapping_index;
    uint32_t flags;
    uint64_t root_physical_address;
};
```

Version 1 has at most six records. Each record binds one exact manifest service,
process generation, endpoint generation, private root, and one contiguous slice
of the mapping table. `flags` is zero.

Every reachable static user leaf has one record:

```c
struct micros_vm_mapping {
    uint32_t process_generation;
    uint16_t process_slot;
    uint8_t permissions;
    uint8_t role;
    uint64_t virtual_address;
    uint64_t physical_address;
};
```

The fixed mapping capacity is 4096 pages. A VM-enabled manifest whose actual
prepared leaf count exceeds that bound is rejected before allocation. Records
are grouped by address space and sorted by ascending virtual address. They
carry exact stored `R`, `W`, and `X` permissions without the implicit user bit.

`role` is:

```text
1  VM_SELF_WIRED
2  SERVICE_WIRED
```

For every record:

- process slot and generation equal its enclosing address-space record;
- virtual and physical addresses are page aligned;
- virtual address lies in the accepted user window;
- physical address resolves to the corresponding managed frame index;
- the PTE and mapping permissions agree;
- the current owner is exact `PROCESS_USER` for that process;
- the staged target is `VM_WIRED`; and
- the frame-state byte carries the matching self/service wired class.

Every live process root appears exactly once, every reachable user leaf appears
exactly once, and no mapping record points to a free, kernel, transferable,
foreign, stale, duplicate, or orphan frame.

This inventory is the complete Step 9 adoption of all existing static address
spaces and mappings required by ADR-0006. Later mapping operations extend this
same VM database rather than reconstructing kernel-only bootstrap state.

### Frame states

Each managed frame has one state byte:

```text
0  UNUSED
1  FREE
2  KERNEL
3  VM_SELF_WIRED
4  SERVICE_WIRED
5  TRANSFERABLE
```

For active frame indices:

- `FREE` means the allocator bit is clear and the owner is `FREE`;
- `KERNEL` means the frame is allocated and remains a kernel-reserved or
  page-table mechanism frame;
- `VM_SELF_WIRED` means the current owner is VM's exact `PROCESS_USER`
  generation with staged target `VM_WIRED`;
- `SERVICE_WIRED` means another exact static process owns the
  `PROCESS_USER` frame with staged target `VM_WIRED`; and
- `TRANSFERABLE` means one legal owner has staged target
  `VM_TRANSFERABLE`.

Version 1 ordinary boot stages every `PROCESS_USER` frame wired and therefore
has zero transferable frames. The state remains defined so the snapshot can
reject an unexpected plan and so a later ADR can supersede the zero-count
policy explicitly.

The count fields independently equal a complete scan, and:

```text
free + kernel + vm_self_wired + service_wired + transferable
    = managed_frame_count
```

VM requires a nonzero `vm_self_wired_frame_count`.

### Digest

The digest is 64-bit FNV-1a over all 364672 bytes with the digest field treated
as zero. Kernel and VM use separate implementations and compare the exact
result.

The digest is corruption evidence, not authority. Kernel-owned allocator,
ownership, object, page-table, and bootstrap state remain authoritative.

## Kernel snapshot construction

### Image-catalog extension

The generated image catalog adds optional VM boot-object metadata:

```text
vm_boot_info_address
vm_boot_info_size
vm_boot_info_initially_zero
```

Exactly one active `VM` role requires:

- a nonzero 4096-byte-aligned address;
- exact size 364672;
- the complete object inside the image's writable segment;
- all canonical initial bytes zero; and
- no overlap with the generic 128-byte service configuration.

Every non-VM image requires all VM boot-object fields zero.

### Atomic wired-plan staging

After every static image, stack, configuration, manifest view, and thread
context is prepared, but before launcher publication, the address-space owner
constructs one fixed bitmap of every reachable `PROCESS_USER` frame.

A new portable ownership operation preflights the complete set:

- ownership phase is `BOOTSTRAP`;
- allocator geometry, bits, owners, plans, and counts validate;
- every selected frame is exact `PROCESS_USER`;
- every `PROCESS_USER` owner is selected exactly once;
- no selected frame already has a handoff target;
- no selected frame is duplicated or out of range; and
- resulting plan-count arithmetic is valid.

Only after full preflight does one non-failing pass set every selected target
to `VM_WIRED`. Failure changes no plan byte.

The address-space wrapper proves that the bitmap is exactly the set of user
leaves reachable from all live static roots and that every such process has
one prepared thread. It then invokes the portable batch operation.

No free, page-table, kernel, or temporary frame receives a target in ordinary
version-1 boot.

### Snapshot patch and validation

With the complete wired plan staged, the kernel:

1. resolves the exact manifest VM binding;
2. validates the VM image metadata and zero boot object;
3. copies memory, reservation, and managed ranges;
4. walks every live static root and writes exact address-space and mapping
   records;
5. scans every managed frame and derives one frame state;
6. cross-checks every mapping against its frame state and owner;
7. independently computes all counts and the digest;
8. writes the complete object through VM's inactive root;
9. reads it back and requires byte-exact equality;
10. stores the exact expected summary plus VM process, thread, endpoint, image,
   and object identities in fixed kernel VM-handoff state; and
11. validates complete object, ownership, root, endpoint, and bootstrap
   relationships.

Any failure occurs before launcher publication and uses ADR-0043's reverse
preparation rollback. The boot object confers no kernel authority.

## VM-side initialization

The VM service starts through ADR-0042 and validates:

- generic bootstrap configuration and exact VM role identity;
- every header constant, count, capacity, flag, and reserved byte;
- canonical physical and managed ranges;
- exact address-space roots and mapping slices;
- ascending virtual mappings, exact physical frames, and permissions;
- complete frame-state values and zero tail;
- all independently recomputed counts;
- exact self endpoint and service ID;
- a nonzero wired self working set; and
- the independent FNV-1a digest.

VM treats the object itself as its initial fixed-capacity frame database. It
does not allocate a second copy, use a heap, infer physical memory outside the
described ranges, or mark a kernel/wired frame free.

The initial database is immutable until the handoff succeeds. Later allocation
policy may mutate VM-owned state only after a separate mapping ADR defines the
kernel transition that mirrors the change.

## VM handoff syscall

### Unified namespace

Operation 12 is:

```text
MICROS_SYSCALL_ABI_VM_HANDOFF = 12
```

Operations 1 through 11 remain unchanged. The generic ADR-0042 runtime is not
expanded; VM uses one private raw-syscall wrapper.

The sole version-1 command is:

```text
MICROS_VM_HANDOFF_READY = 1
```

Registers are:

```text
a0  command
a1  boot-information version
a2  address-space count in bits 31..16, managed-range count in bits 15..0
a3  managed-frame count
a4  free-frame count
a5  mapping count
a6  64-bit digest
a7  MICROS_SYSCALL_ABI_VM_HANDOFF
```

`a0` through `a5` require zero upper 32 bits. The digest uses all 64 bits.
Both packed `a2` counts must fit their fixed 16-bit fields.

### Authority

The operation is authorized only when:

- the exact current process and thread equal the recorded VM binding;
- its active endpoint is the exact manifest VM generation;
- its immutable profile contains kernel-operation bit 1,
  `MICROS_KERNEL_OPERATION_VM_HANDOFF`;
- the bootstrap launcher is `RUNNING`;
- the manifest VM entry is the sole `STARTING` service;
- VM-handoff phase is `PREPARED`; and
- ownership phase is `BOOTSTRAP`.

No caller supplies a process, thread, root, image, profile, or pointer.

### Failure ordering

The returning preflight order is:

1. unknown command, width violation, or malformed scalar shape:
   `MICROS_SYSCALL_ABI_ARGUMENT`;
2. impossible current-thread resolution: invariant failure;
3. exact VM profile and binding authority:
   `MICROS_SYSCALL_ABI_UNAUTHORIZED`;
4. launcher, service, handoff, or ownership phase mismatch:
   `MICROS_SYSCALL_ABI_STATE`;
5. complete VM-handoff, object, endpoint, scheduler, root, allocator, and
   ownership validation: corruption is fatal;
6. summary version, range counts, address-space count, mapping count, frame
   count, free count, or digest mismatch: kernel-originated nonreturning
   `ready-role-gate` bootstrap failure;
7. complete wired-handoff preflight; an impossible failure is the same
   nonreturning role-gate failure; and
8. one non-failing commit.

Every returning failure preserves VM-handoff, bootstrap, ownership, allocator,
PTE, endpoint, scheduler, message, and output state except `a0` and advanced
`sepc`.

VM treats every returned negative result as fatal and deliberately traps. The
existing active-bootstrap service-fault path emits the exact VM service and
endpoint before panic. VM cannot invoke launcher operation 11 and no new
VM-to-launcher failure message is introduced.

Once exact VM authority and phase are established, a summary or handoff
preflight mismatch does not return. The kernel emits the one
`MICROS_BOOTSTRAP_FAILURE reason=ready-role-gate` record for the exact VM
binding and enters `bootstrap-failure` panic. VM never retries with a different
summary.

### Commit

After full preflight:

1. the existing address-space-owned wired handoff commits every staged owner
   transition and changes ownership phase to `HANDED_OFF`;
2. fixed VM-handoff state records `HANDED_OFF` and retains the exact VM
   generation and summary;
3. no further bootstrap allocation, release, plan, root mutation, or generic
   context preparation is possible; and
4. the syscall returns success through the ordinary scheduler path using
   post-handoff `VM_WIRED` authority.

The ownership commit is the publication point. No fallible operation follows
its first owner mutation.

There is no operation that reverses or repeats the handoff.

## Launcher role gate

After successful operation 12, VM sends the ordinary ADR-0043 readiness call.

`ACCEPT_READY` for a VM role requires:

- ownership phase `HANDED_OFF`;
- VM-handoff phase `HANDED_OFF`;
- exact process and endpoint generations equal the manifest VM binding; and
- the caller's token remains bound to that same endpoint.

Only then may the kernel record VM `READY`, stage the launcher acknowledgment,
and permit release of a dependent.

An early generic VM readiness request is `ready-role-gate` failure. The
launcher cannot infer handoff completion from payload data.

## Post-handoff authority

After commit:

- VM's frame database is the user-space policy view for free ordinary frames;
- the kernel allocator bitmap remains the privileged availability mechanism;
- the typed ledger remains the privileged semantic owner mirror;
- kernel page-table code remains the only PTE mutation mechanism;
- all existing static leaves remain exact `VM_WIRED` mappings;
- IPC, grants, activation, trap capture, and scheduler return continue through
  existing wired read authority;
- every bootstrap allocator/root mutation API remains phase-rejected; and
- no live PTE may name `VM_TRANSFERABLE`.

This outcome intentionally exposes no post-handoff mutation syscall. A later
ADR must define exact VM-selected frame allocation, mapping owner identity,
map/unmap validation, generation changes, and failure rollback before PM or
another service can request memory.

## Fault behavior

VM's complete working set is wired. A VM-originated instruction, load, or
store page fault is a fatal invariant violation with:

- exact VM service, process, endpoint, PC, address, and cause diagnostics;
- one stable VM-self-fault reason; and
- ordinary structured panic shutdown.

The kernel first emits exactly one newline-terminated record:

```text
MICROS_VM_SELF_FAULT service=0x<16 hex> process-slot=0x<16 hex> process-generation=0x<16 hex> endpoint=0x<16 hex> scause=0x<16 hex> stval=0x<16 hex> sepc=0x<16 hex> ownership=<bootstrap|handed-off>
```

All numeric fields are lowercase, zero-padded 16-hex-digit values. `ownership`
is exactly `bootstrap` or `handed-off`, matching the authoritative ownership
phase observed at the fault. The record uses the exact current VM binding and
trap frame and precedes every other fatal record.

If launcher bootstrap phase is still `RUNNING`, the kernel then emits:

```text
MICROS_BOOTSTRAP_FAILURE reason=service-fault service=0x<VM service> endpoint=0x<VM endpoint> phase=failed state=<starting|ready> detail=0x0000000000000000
```

followed by the ordinary `MICROS_PANIC reason=bootstrap-failure` records.
The state field is copied from the authoritative VM bootstrap entry: it is
`starting` before generic VM readiness and `ready` if a later service is
starting while VM has already been acknowledged.

If launcher authority is already `SEALED`, no bootstrap failure record is
emitted. The VM record is followed by
`MICROS_PANIC reason=vm-self-fault` and the ordinary trap-context records.

Before operation 12, a VM fault records `ownership=bootstrap`. After operation
12 it records `ownership=handed-off`. Both remain fatal; no path asks VM to
resolve its own fault.

Non-VM page-fault delivery remains disabled in this outcome. All static
service mappings are wired, so a fault in another static service is also fatal
service failure. Fault delivery may be enabled only with the later mapping
protocol that defines suspension, one exact VM request, reply ownership,
mapping mutation, and retry.

## Invariants

- Exactly one active manifest entry has the VM role.
- VM boot information is fixed-size, pointer-free, fully zero-tailed, and
  patched before VM runs.
- Every `PROCESS_USER` frame is planned `VM_WIRED` before launcher
  publication.
- Snapshot ranges and states exactly match allocator and ownership metadata.
- Snapshot address spaces and mappings exactly match every live static root,
  user PTE, permission, physical frame, process generation, and wired state.
- VM summary identity and counts are scalar data, never pointer authority.
- Only the exact current VM generation may invoke operation 12.
- Ownership remains `BOOTSTRAP` until a matching summary completes full
  preflight.
- The handoff is failure-atomic and irreversible.
- Every live static user leaf is exact `VM_WIRED` after commit.
- VM's own wired-frame count is nonzero and its self fault is never delivered
  recursively.
- Generic launcher readiness cannot precede handoff.
- No post-handoff mapping mutation exists without a later Accepted ADR.

## Failure boundaries

### Before launcher publication

Boot-object validation, wired-plan staging, snapshot construction, patching,
and readback are part of static preparation. Any failure rolls every prepared
service back in reverse order and restores allocator, ownership, object,
endpoint, root, and frame baselines before fatal shutdown.

### Before ownership publication

Operation-12 ABI, authority, phase, summary, and complete handoff validation
precede the first owner mutation. Every recoverable failure preserves all
state.

### After ownership publication

Any impossible post-commit scheduler, root, endpoint, or bootstrap relation is
fatal. The kernel does not:

- restore `PROCESS_USER`;
- reopen bootstrap allocation;
- retry a different summary;
- restart VM;
- release a dependent;
- fall back to kernel memory policy; or
- continue with reduced functionality.

Recovery remains at the later RS milestone.

## Test-first evidence

### Native boot-information tests

Native tests cover:

- exact header, range, complete-object sizes and offsets;
- valid minimum and maximum snapshots;
- physical and managed range bounds, order, overlap, and overflow;
- address-space count, root, generation, endpoint, and mapping-slice bounds;
- mapping order, virtual/physical alignment, permissions, duplicates, and
  owner/frame-state agreement;
- every valid and invalid frame state;
- independent count and digest recomputation;
- zero unused entries and state tail;
- exact VM service/endpoint identity;
- duplicate, missing, stale, and corrupt owners;
- VM-self versus other-service wired classification;
- output and state preservation on every failure; and
- malformed generated image metadata.

### Native wired-plan and handoff model

The portable model covers:

- exact reachable-user-frame bitmap construction;
- complete-set equality with all `PROCESS_USER` owners;
- failure-atomic batch staging;
- missing, duplicate, foreign, page-table, kernel, free, and preplanned frame
  rejection;
- summary mismatch;
- authority and phase precedence;
- irreversible commit;
- generic readiness rejection before commit;
- exact readiness acceptance after commit;
- VM self-fault diagnostic selection for `RUNNING+STARTING`,
  `RUNNING+READY`, and `SEALED`; and
- a replayable minimum 4096-transition reference comparison with seed and
  recent trace.

### QEMU success

The implementation adds:

```text
test-qemu-vm-handoff
```

It uses the production launcher, a real VM ELF, one test-only probe ELF, a
three-entry test manifest, the production snapshot and operation-12 paths, and
proves:

1. VM is prepared and released first;
2. its 90-page boot database is mapped RW/NX and wired;
3. every static user frame is staged wired before launcher entry;
4. kernel and VM independently agree on ranges, address spaces, mappings,
   states, counts, and digest;
5. a real operation-12 ecall commits the one-way handoff;
6. bootstrap mutation is rejected afterward;
7. VM continues through ordinary post-handoff user return;
8. VM's generic readiness call is acknowledged only after commit;
9. only after VM readiness does the launcher release and acknowledge the
   probe;
10. the launcher seals in the three-service prefix;
11. the probe creates one bounded RW direct grant to VM and calls VM with the
    token and length as data;
12. VM validates the exact probe endpoint, copies the deterministic bytes
    from the probe, writes the reviewed transformed bytes back through the
    same grant, and replies;
13. the probe validates canaries and transformed bytes, then revokes the
    grant;
14. VM and the probe retain ordinary IPC and checked-copy access through wired
    mappings; and
15. complete bootstrap, VM, object, root, ownership, endpoint, scheduler, and
    frame-database invariants validate.

Only the complete sequence emits:

```text
MICROS_VM_HANDOFF_TEST_PASS snapshot=validated ownership=handed-off vm=wired readiness=acknowledged authority=vm
```

### VM self-fault

The implementation also adds:

```text
test-qemu-vm-self-fault
test-qemu-vm-self-fault-sealed
```

After a successful handoff and before generic readiness, the VM test variant
deliberately accesses one unmapped address. The gate requires the exact
VM-self-fault record, bootstrap service-fault record, trap context, and panic
ordering, forbids both success markers, and rejects host timeout.

The sealed variant first completes ordinary VM readiness and launcher sealing,
then faults from VM. It requires `ownership=handed-off`, forbids any
`MICROS_BOOTSTRAP_FAILURE` record, requires
`MICROS_PANIC reason=vm-self-fault`, and rejects host timeout.

### Retained regression gates

Changes to the VM boot ABI, snapshot builder, ownership batch staging,
operation 12, role gate, post-handoff authority, or fault handling retain:

- complete native tests and models;
- all three bootstrap-launcher workflows;
- frame allocator and ownership gates;
- user-address-space and wired-handoff gates;
- endpoint, IPC, grant, user-runtime, user-execution, scheduler, trap, timer,
  and panic gates selected by shared paths;
- documentation; and
- all three diff checks.

## Implementation sequence

The dependent implementation PR uses four green commits:

1. add the fixed boot-information ABI, independent VM validator, digest,
   snapshot builder, and native model;
2. add atomic all-user-frame wired staging, fixed VM-handoff runtime,
   operation 12, and launcher role gate;
3. add the real VM ELF, generated VM object metadata, and successful QEMU
   handoff gate together with its planner mapping, inventory regression, and
   implemented-matrix documentation; and
4. add both running- and sealed-phase VM self-fault workflows together with
   their planner ownership and directly related documentation.

The implementation does not add PM, process spawn, post-handoff mapping
mutation, or non-VM fault delivery.

## Consequences

- VM becomes the explicit user-space owner of ordinary frame-allocation policy
  before PM work begins.
- The kernel retains only privileged availability, ownership, and page-table
  mechanisms.
- Every static mapping remains usable through exact wired authority.
- The handoff is observable, failure-atomic, and irreversible.
- The fixed database costs 90 wired pages in VM's image.
- Dynamic mapping and page-fault protocols remain explicit later work rather
  than hidden inside bootstrap.

## Alternatives considered

### Give VM a pointer to kernel allocator or ownership storage

Rejected. Cross-address-space pointers are not authority, would expose mutable
kernel state, and would bypass checked snapshot/version boundaries.

### Let VM read the FDT directly

Rejected. Firmware data is reclaimed during kernel bootstrap, and VM also
needs kernel reservations, managed-index geometry, current owners, and staged
targets that the FDT does not describe.

### Keep the frame database permanently in the kernel

Rejected. That preserves the bootstrap cycle break as permanent privileged
memory policy and diverges from the MINIX authority boundary.

### Transfer every free frame into allocated `VM_TRANSFERABLE` ownership

Rejected. It would consume the allocator's free set merely to represent policy
ownership, increase commit cost, and provide no live mapping authority.
VM selects free frames later while the kernel bitmap remains the privileged
availability mirror.

### Add allocation, mapping, and fault delivery now

Rejected. Their exact mapping owner, generation, rollback, suspension, and
client protocols are not required to prove Step 9 and would pull PM/VFS work
into the handoff task.

### Make handoff reversible

Rejected. Reversal requires VM-state reconstruction and service recovery,
which belong to the later RS milestone.

# ADR-0040: Post-Handoff Wired Address-Space Resolution

- Status: Accepted
- Date: 2026-10-08
- Refines: ADR-0006, ADR-0020, ADR-0021, ADR-0035, and ADR-0039

## Context

ADR-0021 implements generation-safe private Sv39 roots while bootstrap frame
ownership is authoritative. Its read and mutation operations require:

- ownership phase `BOOTSTRAP`;
- exact `PROCESS_PAGE_TABLE` ownership for every private table;
- exact `PROCESS_USER` ownership for every user leaf.

ADR-0020 then changes every mapped bootstrap user frame to either:

- `VM_WIRED`, retaining the exact process generation; or
- `VM_TRANSFERABLE`, clearing process identity.

The transition is intentionally irreversible. The current address-space API
therefore rejects validation, lookup, arbitrary-address translation, and
process-root activation after handoff. This also disables:

- scheduler and user-return validation;
- IPC message snapshot and staged delivery;
- checked grant copy;
- any future freestanding service runtime using those mechanisms.

ADR-0006 already requires the launcher and initial services to have their
complete code, data, stacks, IPC buffers, grant buffers, and page-table
working sets wired before handoff. Those mappings have an exact durable
authority available after the transition: `VM_WIRED` plus the same process
slot and generation.

The fixed MINIX baseline keeps safe copy available after VM owns memory policy.
It verifies grant authority, uses the current process mappings, and may suspend
the request while VM resolves a fault. `micros` deliberately defers that fault
callback and dynamic mapping lifecycle, but it must preserve runtime access to
resident initial-service mappings. The canonical evidence is
[the MINIX runtime safe-copy and post-handoff resolution study](../research/minix-post-handoff-wired-address-resolution.md).

This decision defines the smallest dependency-ready replacement: a
wired-only, read-only handed-off address-space authority. It does not implement
VM-controlled mapping mutation.

## Decision

### Scope

This slice defines:

- one production handoff operation that validates every live private root
  before committing frame ownership;
- phase-aware read-only address-space validation;
- phase-aware page lookup and arbitrary-address translation;
- phase-aware process-root activation;
- an explicit bootstrap-only guard for generic execution-context preparation;
- post-handoff IPC-buffer and checked-grant-copy use for exact `VM_WIRED`
  mappings;
- failure and serialization rules for that boundary;
- native classification tests and one isolated QEMU handoff component;
- fail-closed validation ownership for every affected path.

It does not define:

- allocation, release, map, unmap, destroy, or page-table growth after
  handoff;
- a live mapping of `VM_TRANSFERABLE` frames;
- a VM server, `VM_READY` message, manifest, or launcher protocol;
- VM fault callbacks, copy suspension, retry, or paging;
- shared mappings, aliases, copy-on-write, or page pinning;
- mapping generations, load-complete tokens, prepared-address-space sealing,
  or spawn;
- user syscall numbers or freestanding runtime wrappers;
- recovery, restart, SMP locking, ASIDs, or remote TLB shootdown.

The later VM mapping task must define those mutation and lifecycle contracts
before a transferable frame appears in a live user PTE.

### Phase-specific read authority

Private page-table ownership remains unchanged across handoff:

| Phase | Private root/table owner | User-leaf owner |
| --- | --- | --- |
| `BOOTSTRAP` | exact `PROCESS_PAGE_TABLE` | exact `PROCESS_USER` |
| `HANDED_OFF` | exact `PROCESS_PAGE_TABLE` | exact `VM_WIRED` |

Every process-bound owner carries the same exact process slot and generation as
the resolved process object.

Neither a PTE nor a frame-owner record is sufficient alone:

- the PTE proves the current virtual mapping and permissions;
- the owner record proves that the physical frame is in the allowed lifecycle
  class for that exact process generation.

Read authority requires both to agree.

`VM_TRANSFERABLE` has zero process identity and is not valid live-leaf
authority in this slice. A user leaf naming a transferable frame is an
invariant failure, even if the PTE is otherwise structurally valid. A future
VM mapping ADR may add a new exact mapping owner or registry and explicitly
broaden this rule.

### Wired handoff operation

Production ownership handoff is exposed through one address-space-owned
operation equivalent to:

```c
micros_user_address_space_complete_wired_handoff(void);
```

The future VM bootstrap owner invokes this operation only after validating its
own memory-map summary and staging every frame target required by ADR-0006.
Ordinary user code cannot invoke it.

The operation runs with SIE clear and uses the authoritative kernel-object,
frame-ownership, and kernel-address-space runtimes. Before any mutation it:

1. requires ownership phase `BOOTSTRAP`;
2. validates the complete object and frame-ownership registries;
3. acquires the existing bounded address-space scratch state;
4. scans every live process slot;
5. for every process with a private root, validates the complete bootstrap
   root using the existing shared-kernel-subtree rules;
6. requires exactly one live v0.1 thread owned by that process and requires
   that thread to have one already attached execution context;
7. requires every private root and intermediate table to be exact
   `PROCESS_PAGE_TABLE` ownership for that process generation;
8. requires every reachable leaf to be exact `PROCESS_USER` ownership for the
   same generation;
9. requires every reachable user frame's staged target to be
   `VM_WIRED`;
10. rejects duplicate reachability, orphan process-owned frames, foreign
   owners, stale generations, malformed PTEs, live leaves planned
   `VM_TRANSFERABLE`, and any live process state inconsistent with its root;
11. invokes the existing frame-ownership handoff preflight and commit.

A live process without a private root is accepted only when it has:

- no live thread;
- no primary endpoint;
- no process-bound page-table, user, or wired frame.

This permits an otherwise empty reserved object slot without treating it as a
runnable service.

The lower frame-ownership commit remains responsible for:

- rejecting temporary frames;
- requiring every `PROCESS_USER` frame to have a legal target;
- validating allocator/owner/count agreement;
- applying all owner transitions;
- clearing every handoff target;
- changing phase to `HANDED_OFF`.

The address-space operation does not modify a PTE. Once the lower commit begins
owner mutation, every remaining action is a bounded prevalidated store and
cannot return a recoverable error. Thus observers never see a successful
handoff with a reachable user leaf lacking exact handed-off authority.

The lower portable ownership operation remains independently testable, but
production boot orchestration must use the address-space-owned operation.

### Phase-aware read-only interfaces

These existing interfaces become valid in both recognized phases:

```c
micros_user_address_space_validate(process);
micros_user_address_space_lookup(
    process,
    virtual_address,
    physical_address,
    permissions
);
micros_user_address_space_translate(
    process,
    user_address,
    physical_address,
    permissions,
    contiguous_bytes
);
micros_user_address_space_activate(process);
```

They select the required leaf owner from the ownership phase table above.
Their remaining contracts do not change:

- exact live process generation;
- one attached private root;
- root index 1 as the only private subtree;
- every shared root entry exactly equal to the immutable kernel root;
- 4 KiB user leaves only;
- valid `U`, `R`, `W`, and `X` combinations;
- no duplicate private frame reachability;
- complete owner/reachability agreement;
- unchanged outputs on failure;
- full ASID-zero fences and exact `satp` readback for activation.

`micros_user_address_space_activate_kernel()` remains valid in either phase.

These mutation interfaces remain bootstrap-only and continue to return
`MICROS_USER_ADDRESS_SPACE_ERROR_PHASE` after handoff:

```c
micros_user_address_space_create(process);
micros_user_address_space_allocate_page(...);
micros_user_address_space_release_page(...);
micros_user_address_space_destroy(process);
```

No post-handoff caller may modify a PTE directly or reinterpret
`VM_TRANSFERABLE` as mapped authority.

### Execution-context preparation remains bootstrap-only

Phase-aware address lookup is required by scheduler return, but it does not
broaden authority to create a new executable context.

`micros_user_execution_prepare()` must check the ownership phase explicitly
before thread-state or mapping preparation. In `HANDED_OFF` it returns:

```text
MICROS_USER_EXECUTION_ERROR_PHASE
```

without changing:

- the thread object or attached-context state;
- the supplied context;
- the slot-derived kernel stack;
- scheduler or hart state;
- any page table or frame owner.

The handoff preflight therefore rejects a live thread without an already
attached context. Existing prepared threads remain valid: context validation,
trap capture, stored-context updates, scheduler selection, root activation,
and the common user-return path may use the phase-aware read authority.

Only the later PM-only executable preparation transition may create a new
post-handoff context. It must add the load-complete token, mapping-generation
seal, and `fence.i` contract required by ADR-0006 and ADR-0011.

### IPC and grant-copy integration

IPC buffers continue to call
`micros_user_address_space_translate()`. After a valid wired handoff:

- outbound snapshot accepts exact readable `VM_WIRED` leaves;
- inbound copy accepts exact writable `VM_WIRED` leaves;
- page-local and two-page message behavior remains unchanged;
- absent mappings and insufficient permissions retain the existing recoverable
  message-fault result;
- owner, PTE, root, phase, or runtime corruption remains fatal.

Checked grant copy retains every ADR-0039 token, participant, direction,
bounds, planning, overlap, and commit rule. This ADR supersedes only the
bootstrap-only phase restriction:

- `BOOTSTRAP` plans through exact `PROCESS_USER` leaves;
- `HANDED_OFF` plans through exact `VM_WIRED` leaves;
- zero length succeeds in either valid phase after grant authority and bounds
  validation and performs no page translation;
- an unsupported or corrupt phase remains `MICROS_GRANT_ERROR_PHASE` or
  `MICROS_GRANT_ERROR_INVARIANT` according to the existing exhaustive mapping;
- `VM_TRANSFERABLE`, foreign wired ownership, malformed PTEs, and stale roots
  map to `MICROS_GRANT_ERROR_INVARIANT`;
- absent mappings and insufficient permissions remain
  `MICROS_GRANT_ERROR_FAULT`.

Both local and remote plans are still prepared before translation errors are
classified. SIE remains clear across authority validation, both translations,
overlap checks, and commit.

### Full handed-off validation

In `HANDED_OFF`, complete process-root validation requires:

- the ownership ledger itself validates and contains no `PROCESS_USER` or
  `KERNEL_TEMPORARY` frame;
- the exact root and every private non-leaf are distinct
  `PROCESS_PAGE_TABLE` owners for the process generation;
- every user leaf is a distinct exact `VM_WIRED` owner for that process;
- every `PROCESS_PAGE_TABLE` and `VM_WIRED` owner naming the process is
  reachable exactly once in the matching role;
- no exact process owner of another kind exists;
- no live leaf points to `VM_TRANSFERABLE`, `FREE`, a kernel class, another
  process, or a stale generation;
- shared kernel entries remain byte-for-byte identical to the kernel root.

Validation detects corruption and never repairs it.

### Failure model and precedence

The existing address-space error enum remains sufficient. Lookup and
translation return PTE permissions; they do not accept or classify a requested
access mode themselves.

For read-only validation, lookup, and translation, the normative order is:

1. null output or malformed argument: `ARGUMENT`;
2. unavailable runtime: `NOT_INITIALIZED`;
3. unrecognized or unavailable ownership phase: `PHASE`;
4. stale process generation: `STALE`;
5. missing root or invalid object state: `STATE`;
6. supplied virtual-address range or alignment failure where applicable:
   `RANGE`;
7. scratch reentrancy: `BUSY`;
8. complete root, PTE, owner, reachability, ledger, and shared-root
   validation:
   - foreign, transferable, stale, duplicate, or orphan owner:
     `OWNERSHIP`;
   - malformed PTE: `PTE`;
   - registry, count, shared-root, or unexpected relation corruption:
     `INVARIANT`;
9. requested leaf absent after the complete structure validates:
   `NOT_MAPPED`;
10. otherwise success with the stored page permissions.

Complete structural validation therefore outranks a missing requested leaf.
An absent target cannot hide corruption elsewhere in the same root.

IPC-buffer and grant-copy planners classify the returned permissions only
after successful translation:

- insufficient read/write permission becomes the existing recoverable
  message or grant `FAULT`;
- structural translation errors retain their existing invariant behavior.

Activation performs steps 2 through 8, then programs and verifies `satp`.
Programming or readback failure is `SATP`.

The wired handoff operation checks:

1. runtime and phase;
2. object/ledger structure;
3. every root/PTE/owner relation;
4. every required `VM_WIRED` target;
5. the lower ownership handoff preflight;
6. only then the non-failing commit.

Any failure before commit preserves:

- every process and thread object;
- every endpoint and grant record;
- every PTE and root attachment;
- allocator bits and counts;
- frame owners and handoff targets;
- scratch state after release;
- every output argument.

### Serialization

All operations retain the one-hart external-serialization contract.

The target keeps SIE clear across:

- complete root and ownership validation;
- scratch acquisition and release;
- handoff preflight and commit;
- read-only lookup and translation;
- root activation and full fences;
- IPC-buffer and grant-copy plans using returned physical chunks.

No post-handoff mapping mutation exists in this slice, so retained physical
plans cannot race a VM map/unmap transition. The future VM mapping design must
preserve that stability through serialization, pinning, generation
revalidation, or another reviewed mechanism before mutation is enabled.

This is not an SMP design.

### Resource bounds

The design:

- allocates no memory;
- reuses the existing fixed reachability and release scratch bitmaps;
- scans at most `MICROS_PROCESS_CAPACITY` process slots;
- walks each private root's existing three-level Sv39 subtree;
- scans at most the fixed managed-frame capacity for owner/reachability
  agreement;
- retains no physical authority after a read operation returns;
- adds no per-mapping dynamic storage.

The full validation cost is accepted for v0.1 and for the one-time handoff.
Optimization requires measurement after the baseline is integrated.

## Test-first evidence

### Native classification tests

Pure owner/phase classification tests cover:

- bootstrap table and leaf owner acceptance;
- handed-off table and wired-leaf acceptance;
- transferable, foreign, stale, free, kernel, and wrong-phase rejection;
- exact output preservation;
- every error-mapping branch consumed by IPC and grant copy;
- a missing requested leaf combined with unrelated owner or PTE corruption,
  proving structural failure takes precedence;
- caller-side permission denial only after successful translation.

Existing frame-ownership models continue to prove atomic owner transition,
target validation, and phase sealing.

### QEMU wired-handoff component

A dedicated isolated QEMU image must:

1. create at least two exact process generations and private roots;
2. map page-local and cross-page resident buffers with distinct contents and
   permissions;
3. create exact read and write grants between their active endpoints;
4. prove bootstrap lookup, translation, IPC-buffer access, activation, and
   checked copy through the production paths;
5. stage every reachable user leaf as `VM_WIRED`;
6. deliberately stage one reachable leaf as `VM_TRANSFERABLE`, invoke the
   production handoff operation, and prove complete state and byte
   preservation with phase still `BOOTSTRAP`;
7. correct that target and complete the one-way handoff;
8. validate, look up, translate, and activate both roots in `HANDED_OFF`;
9. prove page-local and cross-page IPC snapshot/write through exact wired
   mappings;
10. prove page-local, cross-page, and zero-length grant copy in both
    directions through exact wired mappings;
11. prove absent and permission failures preserve every affected byte;
12. inject foreign wired ownership, a transferable live leaf, and a malformed
    PTE one at a time, requiring invariant rejection before bytes or selected
    state change;
13. prove create, allocate, release, and destroy remain rejected with
    `PHASE`;
14. prove `micros_user_execution_prepare()` returns `PHASE` without changing
    the prepared thread, saved context, or kernel stack, while that thread
    still returns through the ordinary scheduler path;
15. revalidate object, endpoint, grant, ownership, and address-space state.

Because the handoff is irreversible, this image terminates with the wired
processes and roots intentionally retained. Only the complete sequence may
emit:

```text
MICROS_ADDRESS_SPACE_HANDOFF_TEST_PASS phase=handed-off wired=validated ipc=resident grants=atomic mutation=revoked
```

This gate does not claim:

- a VM service or `VM_READY` protocol;
- transferable-frame mapping;
- fault delivery or retry;
- process teardown after handoff;
- user syscall/runtime behavior.

### Validation ownership

The implementation must add one dedicated QEMU workflow. Changes to:

- frame handoff runtime;
- user address-space validation, lookup, translation, or activation;
- IPC buffer planning;
- checked grant-copy planning;
- execution-context preparation;
- scheduler or user-return address-space validation;
- relevant public headers;
- the new handoff component or harness;

select:

- complete native tests and existing persistent models;
- the new wired-handoff QEMU workflow;
- `test-qemu-frame-ownership`;
- `test-qemu-user-address-space`;
- `test-qemu-ipc-syscall`;
- `test-qemu-grant`;
- `test-qemu-user-execution`;
- scheduler gates selected by the existing fail-closed planner;
- documentation and all three diff checks.

## Consequences

- Initial wired services retain IPC and checked-copy functionality after the
  irreversible ownership handoff.
- Scheduler return and root activation no longer depend on bootstrap user-frame
  ownership.
- A live PTE cannot silently turn a zero-identity transferable frame into
  process memory.
- The kernel still owns mapping validation and activation mechanism while VM
  remains the later policy owner.
- Post-handoff mutation remains unavailable, preserving the current
  no-race physical-plan argument.
- User-runtime design can follow this implementation without claiming dynamic
  VM mappings.
- The VM mapping task still needs a separate reviewed owner/mapping-generation
  contract before spawn.

## Alternatives considered

### Trust any live PTE to a `VM_TRANSFERABLE` frame

Rejected. The PTE would identify a virtual mapping, but the owner record would
carry no process identity. Corruption or an unintended alias could cross
process isolation without an independent exact mapping authority.

### Add the complete VM mapping registry now

Rejected. Allocation policy, map/unmap lifecycle, page-table growth, scratch
aliases, mapping generations, prepared-address-space sealing, and fault
delivery belong to the later VM task. Pulling them into this prerequisite
would expand one review into most of Milestone 4.

### Keep checked copy and IPC bootstrap-only

Rejected. The launcher and initial services must remain usable after handoff,
and MINIX preserves runtime IPC and safe-copy access under VM ownership.

### Transfer private page tables to zero-identity VM ownership

Rejected. Exact process-bound page-table ownership is already established,
survives handoff, and is required for generation-safe root validation.

### Ask VM to validate every copy now

Rejected. VM is not dependency-ready, callbacks would recreate the bootstrap
cycle, and a VM-originated fault cannot be delegated back to VM in v0.1.

### Keep every service operation before handoff

Rejected. This would make the one-way transition an artificial end-of-use
barrier and would not provide a runtime architecture for the services that
must continue afterward.

## Specification basis

- [ADR-0003: Kernel Responsibility Boundary](0003-kernel-responsibility-boundary.md)
- [ADR-0005: Direct Memory Grants](0005-direct-memory-grants.md)
- [ADR-0006: VM Bootstrap and Handoff](0006-vm-bootstrap-and-handoff.md)
- [ADR-0011: Process, Thread, and Hart Model](0011-process-thread-and-hart-model.md)
- [ADR-0020: Typed Bootstrap Frame Ownership](0020-typed-bootstrap-frame-ownership.md)
- [ADR-0021: Generation-Safe User Address Spaces](0021-generation-safe-user-address-spaces.md)
- [ADR-0025: MINIX Behavioral Baseline Before Optimization](0025-minix-behavioral-baseline-before-optimization.md)
- [ADR-0035: RISC-V IPC Syscall and Bootstrap Buffers](0035-riscv-ipc-syscall-and-bootstrap-buffers.md)
- [ADR-0038: Kernel-Managed Direct Grant Registry](0038-kernel-managed-direct-grant-registry.md)
- [ADR-0039: Page-Bounded Checked Grant Copy](0039-page-bounded-checked-grant-copy.md)
- [MINIX runtime safe-copy and post-handoff resolution study](../research/minix-post-handoff-wired-address-resolution.md)
- [System overview](../architecture/system-overview.md)
- [Development dependency DAG](../architecture/development-dag.md)

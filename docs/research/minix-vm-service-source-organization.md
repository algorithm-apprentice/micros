# MINIX VM Service Source-Organization Baseline

This note records the fixed MINIX baseline used to decompose the `micros` VM
service, portable loader state, frame selection, image/stack planning, exit
release, service adaptation, and evidence sources. Runtime semantics remain
governed by the Accepted `micros` ADRs, especially ADR-0045 and ADR-0050.

## Reference

The reference tree is the local MINIX checkout at:

```text
/Users/alephling/Repo/minix
```

The inspected revision is the fixed ADR-0025 baseline:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

Representative sources are:

```text
minix/servers/vm/main.c
minix/servers/vm/utility.c
minix/servers/vm/alloc.c
minix/servers/vm/region.c
minix/servers/vm/pagetable.c
minix/servers/vm/exit.c
minix/servers/vm/pagefaults.c
minix/lib/libexec/exec_elf.c
minix/lib/libexec/exec_general.c
minix/servers/vfs/exec.c
```

The inspection used the local tree only. Representative commands were:

```sh
git -C /Users/alephling/Repo/minix rev-parse HEAD
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/servers/vm/main.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/servers/vm/utility.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/servers/vm/alloc.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/servers/vm/region.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/servers/vm/pagetable.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/servers/vm/exit.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/servers/vm/pagefaults.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/lib/libexec/exec_elf.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/lib/libexec/exec_general.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/servers/vfs/exec.c
```

No GitHub or web copy was used.

## Service dispatch and bootstrap

MINIX `servers/vm/main.c` owns the VM service process boundary:

- first-time boot import and VM initialization;
- SEF startup and restart/live-update callbacks;
- one receive loop;
- exact source-endpoint resolution;
- request-number and ACL dispatch;
- special kernel page-fault and RS initialization paths; and
- ordinary reply suppression or publication.

The dispatch loop does not implement physical allocation, region operations,
page-table writes, executable parsing, exit release, or page-fault resolution.
Those responsibilities live in separate modules.

The reusable source rule is:

- service configuration, caller classification, request dispatch, and reply
  publication belong in the service adapter;
- portable frame, mapping, loader, and release state remain below it; and
- scenario- or platform-specific test behavior does not become the general
  receive loop.

`micros` has no SEF, live update, ACL table, or general VM request namespace.
Its current VM service starts from the immutable static launcher, validates one
fixed boot object, performs one-way handoff, and supports only the bounded
PM/TTY/test protocols already accepted.

## Physical allocation

MINIX `alloc.c` owns the physical-page availability bitmap, allocation and
freeing, reserved queues, memory import, and allocation statistics. Callers
request pages through this owner rather than scanning process mappings or
editing page tables themselves.

The reusable behavior is:

- one authoritative availability view;
- deterministic eligibility and capacity checks before publication;
- selection policy separate from page-table installation; and
- release through the same allocation owner.

`micros` adapts this boundary after handoff:

- the VM boot frame-state bytes are VM's local policy view;
- the kernel allocator and typed ledger remain privileged mirrors;
- deterministic selection chooses the lowest eligible initial-free frame;
- four page-table candidates are locally `LOAD_RESERVED`;
- data frames become kernel `VM_TRANSFERABLE` one at a time; and
- VM mirrors every successful kernel owner transition before another request.

The dense fixed database, generation-bound mapping records, and exact
kernel/VM set-equality validation are compatible safety extensions.

## Regions, page tables, and complete state

MINIX separates region policy in `region.c` from architecture page-table
mechanism in `pagetable.c`. Regions own process virtual ranges, backing
objects, copying, extension, lookup, unmap, and process release.
Page-table code owns table allocation, binding, map writes, root changes, and
architecture-specific translation details.

VM's complete runtime state is checked through explicit sanity paths rather
than being made the responsibility of request dispatch.

The reusable source direction is:

1. validate authoritative VM process/mapping state;
2. select policy resources;
3. invoke lower-level page-table and frame mechanisms;
4. publish one completed region/address-space transition; and
5. release through a distinct lifecycle owner.

`micros` does not give user-space VM direct PTE write authority. Its portable
state therefore owns deterministic plans and local mirrors, while operation
12 performs privileged scratch, freeze, abort, commit, and release. Complete
VM-state validation remains read-only below load and exit transitions.

## Executable and stack planning

MINIX executable handling is divided among VFS policy and the reusable
`libexec` loader. `exec_elf.c` validates the ELF header and program-header
ranges, then `exec_general.c` drives segment allocation/copy/clear callbacks.
VFS owns path resolution, executable authorization, argument capture, stack
copy, and the call into VM/kernel mechanisms.

The reusable source rule is:

- byte-level ELF and stack planning are portable algorithms;
- executable path and descriptor policy remain outside VM;
- frame selection remains outside the ELF parser;
- every file range is validated before allocation or publication; and
- image bytes and stack bytes are constructed from immutable snapshots.

`micros` uses a smaller fixed RISC-V ELF64 subset and a pointer-free spawn
request. Separate stack and ELF modules preserve that algorithm boundary while
the load transaction owns copied inputs, frame selection, and lifecycle
publication.

## Exit release

MINIX `exit.c` separates:

- announcement that a process will exit;
- release of regions and page tables;
- clearing the VM process slot; and
- other process-control operations.

PM/VFS coordination occurs above it. `free_proc` releases mappings and page
tables, while `clear_proc` resets the VM slot only after release.

The reusable behavior is:

- execution/lifecycle coordination does not duplicate VM release;
- mapping/page-table release precedes VM slot reuse; and
- release is a separate source responsibility from load construction.

`micros` binds release to exact exit, load, mapping-generation, PID, and
process-generation identities. It mirrors the kernel release into the fixed
frame database and clears one active mapping record before returning the
loader to `IDLE`.

## Page faults are outside this correction

MINIX `pagefaults.c` owns fault classification, region lookup, protection
checks, asynchronous backing work, and kernel resume. It is separate from
ordinary request dispatch and from allocation/page-table primitives.

`micros` has no dependency-ready non-VM page-fault protocol. VM's own working
set is wired and a VM-originated fault remains fatal. Source decomposition
must not create a fault module, retry loop, region abstraction, or recovery
path.

## Source-ownership lesson

The useful MINIX source shape is responsibility direction:

1. service receive/dispatch is separate from VM algorithms and resource
   lifecycles;
2. physical allocation, region policy, page-table mechanism, executable
   parsing, exit release, and fault handling have distinct owners;
3. complete validation is below mutation;
4. executable and stack planning do not select or publish physical frames;
   and
5. test/recovery breadth does not belong in the common request facade.

This supports separate `micros` owners for:

- immutable boot-object validation;
- shared binary-planning primitives;
- stack planning/building;
- ELF planning;
- runtime frame translation and deterministic selection;
- complete portable VM-state validation;
- one load transaction;
- one exit-release transaction;
- service startup/handoff/readiness and scenario adapters;
- host fixtures and behavior cases;
- scratch-preemption fixture/runtime/report/trap phases; and
- fixture descriptor/manifest/address generation.

It does not justify importing MINIX's dynamic regions, direct VM page-table
editing, page cache, reserved queues, asynchronous faults, fork, copy-on-write,
live update, recovery, architecture code, or broad request table.

## Difference classification

The classification follows
[ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md).

| MINIX behavior or difference | Classification | `micros` treatment |
| --- | --- | --- |
| VM service dispatch is separate from allocation, mappings, exit, and faults | Baseline parity | Keep service facade and scenario dispatch above portable VM state owners |
| Physical availability and selection have one authority | Baseline parity | One boot frame database and initial-free bitmap select lowest eligible frames |
| Region/mapping validation precedes publication | Baseline parity | Complete fixed-record and local-frame validation remains below load/exit transitions |
| ELF and stack planning are portable loader algorithms | Baseline parity | Separate fixed ELF and canonical stack owners consume immutable snapshots |
| VM owns address-space memory policy while kernel owns privileged mechanism | Baseline parity | VM selects frames/plans; operation 12 validates and commits owners/PTEs |
| Exit mapping release is separate from process-semantic lifecycle | Baseline parity | Exact VM exit-release transaction precedes PM's final process release |
| MINIX VM edits page tables directly | Required adaptation | Unprivileged VM uses one kernel-controlled scratch alias and kernel-built Sv39 roots |
| MINIX uses dynamic regions and broader executable mappings | Required adaptation | Fixed resident pages and 64 bounded mapping records replace regions |
| MINIX allocator uses general first-fit/contiguous queues | Compatible extension | Deterministic lowest eligible frame selection supports replayable bounded evidence |
| MINIX process/region state uses raw slots and broader shared backing | Compatible extension | Generation-bound process identities, exact record uniqueness, and frame set equality |
| MINIX loader and VFS may copy directly into target memory | Required adaptation | VFS-to-VM grants plus one VM-only scratch page preserve non-transitive authority |
| MINIX separates page-fault handling and cannot recursively resolve VM's own working set | Baseline parity | VM self-fault remains an exact fatal invariant boundary; ordinary non-VM fault delivery is not part of this correction |

No dependency-ready behavior is classified as divergence requiring
correction.

MINIX fork, copy-on-write, dynamic regions, reclaim, file-backed or demand
paging, live update, and recovery are broader components outside this
dependency-ready source-organization parity analysis. They are non-goals and
do not justify a `micros` module, temporary policy owner, or staged-
substitution classification in this correction. Each requires its own later
baseline trace and reviewed design when its DAG node becomes ready.

## Required preservation

The source decomposition must preserve:

- the fixed VM boot ABI, independent counts, digest, and immutable initial
  database;
- exact local frame states, mapping records, loader phases, and set-equality
  validation;
- deterministic frame ordering and one scratch mapping;
- exact ELF, stack, page, freeze, abort, commit, and exit behavior;
- exact service configuration, handoff, readiness, TTY, probe, PM-test, and
  scratch-preemption protocol behavior;
- exact fatal versus stable expected result boundaries;
- current native case order, QEMU scenario order, markers, timeouts, and
  generated fixture bytes; and
- no new mapping, fault, allocation, recovery, or compatibility behavior.

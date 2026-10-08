# MINIX VM Bootstrap and One-Way Handoff Baseline

## Purpose

This study fixes the MINIX behavioral baseline for development-DAG Step 9:
starting the first VM server, constructing its physical-frame database, and
transferring ordinary user-memory policy out of the kernel without making VM
dependent on pageable memory.

It is the sole ADR-0025 classification ledger for the static VM bootstrap and
one-way handoff outcome. Earlier launcher, typed-ownership, private-root, and
wired-resolution classifications remain in their existing canonical studies.

The study does not authorize copying MINIX source. It identifies behavior,
authority, ordering, and failure evidence that `micros` must reproduce or
classify.

## Reference

- Repository: MINIX 3 source tree
- Commit: `4db99f4012570a577414fe2a43697b2f239b699e`
- Commit date: 2018-11-14
- Relevant areas:
  - `minix/kernel/main.c`
  - `minix/kernel/system/do_vmctl.c`
  - `minix/kernel/arch/i386/exception.c`
  - `minix/kernel/arch/earm/exception.c`
  - `minix/servers/vm/main.c`
  - `minix/servers/vm/utility.c`
  - `minix/servers/vm/alloc.c`
  - `minix/servers/vm/pagetable.c`
  - `minix/servers/vm/vmproc.h`

## Fixed baseline

### VM is an exceptional boot process

The kernel creates all boot processes, but only kernel tasks, RS, and VM are
immediately privileged and schedulable. Other system processes retain
privilege, VM, and boot inhibition until their startup owners clear those
flags (`minix/kernel/main.c:192-266`).

VM therefore does not begin as an ordinary client of an already complete
memory service. The kernel has already:

- loaded VM's image;
- established VM's initial mappings;
- assigned VM's early privilege profile;
- made the VM process runnable; and
- retained other boot processes behind VM-related inhibition.

This is the baseline cycle break. VM is special only long enough to become the
ordinary memory-policy owner.

### VM imports kernel boot information

On first initialization, VM retrieves the kernel boot-information structure
through `sys_getkinfo()` and requires a nonempty memory map and boot-module set
(`minix/servers/vm/main.c:428-455`).

`get_mem_chunks()` copies the kernel-provided memory map into a bounded local
array, rounds starts up and ends down to VM pages, and discards empty results
(`minix/servers/vm/utility.c:42-79`).

VM also imports boot-process identities. Each live boot entry initializes one
`vmproc` slot with the exact endpoint and boot-image descriptor
(`minix/servers/vm/main.c:262-285`).

The behavioral contract is:

- kernel boot data is authoritative input;
- VM copies it into bounded private state;
- VM validates counts and alignment before use; and
- process identity remains endpoint-specific rather than inferred from a
  name.

### VM constructs the physical-page database

MINIX VM owns one fixed bitmap spanning the supported physical address range
(`minix/servers/vm/alloc.c:32-45`). `mem_init()` clears the bitmap, inserts
every usable memory chunk, and records total and low/high memory bounds
(`minix/servers/vm/alloc.c:303-335`).

Allocation and release then mutate VM-owned policy state:

- `alloc_mem()` chooses physical pages from the free bitmap, with optional
  alignment and low-memory constraints
  (`minix/servers/vm/alloc.c:239-279,402-459`);
- `free_mem()` returns a page range to VM's free database
  (`minix/servers/vm/alloc.c:286-300,463-480`); and
- `memstats()` derives free-page and largest-run summaries from that same
  bitmap (`minix/servers/vm/alloc.c:348-367`).

The exact scan direction and page cache are implementation details. The
authority boundary is observable: after bootstrap, VM chooses ordinary
user-memory allocation policy.

### VM's own page-table working set is bootstrap-wired

VM cannot depend on its own pageable mapping service while constructing that
service. `pt_init()` uses statically mapped spare pages, resolves their
physical addresses, and builds a private representation of the page tables
that the kernel already established for VM
(`minix/servers/vm/pagetable.c:1086-1311`).

Only after that representation is functional does VM replace bootstrap spare
pages with dynamically allocated pages and rebind its page tables
(`minix/servers/vm/pagetable.c:1311-1335`).

The baseline invariant is stronger than merely making VM runnable:

- VM's code, data, stack, allocator state, page-table state, and IPC working
  set must remain resident while VM handles memory policy; and
- the kernel must not ask VM to resolve a fault in that same working set.

### VM adopts and releases the remaining boot services

After initializing memory and its own page tables, VM creates VM-side process
state for the other boot services, builds their page tables, loads their boot
images, creates minimal stacks, binds the new roots, and clears kernel boot
inhibition (`minix/servers/vm/main.c:288-419,497-520`).

`micros` already performs static image, root, stack, context, and endpoint
preparation before launcher entry. Step 9 therefore does not need to move that
work back into VM merely to resemble MINIX source structure. What must remain
equivalent is:

- VM becomes the final user-memory policy owner before dependents require
  ordinary mapping changes;
- every preexisting mapping is represented in VM's initial database; and
- releasing a dependent never precedes the memory-ownership transition on
  which that dependent relies.

### Kernel retains mapping mechanism

MINIX VM asks the kernel to bind address spaces, inhibit or release processes,
flush translation state, and expose kernel physical mappings through
`SYS_VMCTL` (`minix/kernel/system/do_vmctl.c:112-170` and
`minix/servers/vm/pagetable.c:1184-1421`).

VM owns policy and page-table construction state, while the kernel retains:

- privileged root installation;
- process run inhibition;
- translation invalidation;
- safe access to kernel-owned mappings; and
- validation of privileged operations.

This is not a requirement that `micros` expose MINIX's `SYS_VMCTL` ABI. It is
the authority split to preserve.

### VM-originated faults are fatal

Both the x86 and ARM exception paths explicitly refuse to send VM's own page
fault back to VM. They print the VM fault context and panic
(`minix/kernel/arch/i386/exception.c:96-136`,
`minix/kernel/arch/earm/exception.c:76-116`).

Other process faults are inhibited and delivered to VM as kernel-origin
messages. VM handles those messages without an ordinary reply and later clears
the kernel fault state (`minix/servers/vm/main.c:142-166` and
`minix/servers/vm/pagefaults.c`).

For the Step 9 boundary, only the first rule is dependency-ready: VM's own
fault is fatal. Dynamic mappings and non-VM page-fault delivery require a
reviewed post-handoff mapping protocol and are classified below as a staged
substitution.

### VM enters an ordinary server loop

After initialization, VM receives requests, validates exact caller endpoints,
checks per-call authorization, and sends explicit replies unless an operation
is deliberately suspended (`minix/servers/vm/main.c:110-191`).

The complete MINIX call set includes mapping, sharing, cache, fork, exec, and
file-backed behavior. Step 9 does not pull those future peers into the current
development node. The baseline relevant now is that VM becomes a persistent
user-space authority with explicit caller and operation checks.

## `micros` constraints already fixed

Accepted `micros` decisions establish:

- one-hart RISC-V64 QEMU `virt` with Sv39 and OpenSBI;
- a bounded FDT-derived frame allocator;
- a dense typed frame-ownership ledger with exact process generations;
- `PROCESS_PAGE_TABLE`, `PROCESS_USER`, `VM_WIRED`, and
  `VM_TRANSFERABLE` classes;
- one irreversible `BOOTSTRAP -> HANDED_OFF` phase transition;
- exact private roots and complete reachability validation;
- all static images, stacks, IPC buffers, and contexts prepared before
  handoff;
- a static launcher that releases VM first;
- generic address-space mutation disabled after handoff;
- read-only post-handoff authority only through exact `VM_WIRED` mappings;
- `VM_TRANSFERABLE` forbidden as live PTE authority; and
- a VM readiness gate that must precede the launcher's ordinary readiness
  acknowledgment.

Step 9 must extend these mechanisms rather than introduce a second allocator,
identity model, or page-table representation inside the kernel.

## Sole ADR-0025 classification ledger

| ID | `micros` choice relative to the fixed MINIX baseline | Classification | Reason or replacement point |
| --- | --- | --- | --- |
| VH-01 | VM is a statically embedded, specially privileged early service released before PM | Baseline parity | MINIX makes VM an exceptional boot process before ordinary services become runnable |
| VH-02 | The kernel transports boot memory, reservations, mappings, and process identity through one bounded pointer-free object | Required adaptation | FDT, Sv39, generated images, and the accepted no-argument runtime replace MINIX `sys_getkinfo`, Multiboot, and VMCTL bootstrap structures |
| VH-03 | VM validates kernel boot state into one private frame and mapping database | Baseline parity | MINIX VM constructs a private free-page bitmap, process table, region state, and page-table representation |
| VH-04 | The VM object uses fixed capacities, typed frame states, exact generations, zero tails, and an independent digest | Compatible extension | These checks strengthen provenance and stale-authority rejection without changing VM policy ownership |
| VH-05 | The snapshot contains every existing static address space and mapping | Baseline parity | MINIX VM imports or reconstructs all boot-process mappings before ordinary memory policy begins |
| VH-06 | Every static `PROCESS_USER` frame is staged `VM_WIRED` before VM runs | Required adaptation | ADR-0040 requires exact process-generation wired authority for all already prepared static mappings |
| VH-07 | VM sends one scalar `VM_READY` summary through a dedicated kernel operation | Required adaptation | The target has no MINIX `SYS_VMCTL` boot ABI; a pointer-free summary proves that VM consumed the exact snapshot |
| VH-08 | Successful handoff makes VM the final ordinary user-memory policy owner | Baseline parity | MINIX transfers normal allocation and mapping policy to VM after its exceptional bootstrap |
| VH-09 | Typed full preflight and one irreversible non-failing ownership commit implement the transfer | Compatible extension | Atomic owner/count/phase publication strengthens the baseline handoff without changing its valid result |
| VH-10 | The generic launcher acknowledges VM readiness only after the exact VM generation owns the completed handoff | Required adaptation | The static launcher replaces MINIX's distributed kernel/RS/VM boot inhibition and preserves the same ordering |
| VH-11 | Kernel page-table mechanics and ownership validation remain privileged while VM owns user-memory policy | Baseline parity | MINIX VM chooses policy while kernel VMCTL paths retain privileged mapping and inhibition mechanisms |
| VH-12 | VM's complete image, stack, allocator database, IPC buffers, and page-table working set remain resident | Baseline parity | MINIX uses bootstrap-mapped static spare pages so VM never depends on resolving its own working-set fault |
| VH-13 | Exact `VM_WIRED` process-generation ownership represents that resident working set | Required adaptation | The accepted typed Sv39 authority replaces MINIX architecture-specific bootstrap mappings |
| VH-14 | A VM-originated page fault is fatal with exact diagnostics | Baseline parity | MINIX explicitly panics rather than recursively asking VM to resolve its own fault |
| VH-15 | Non-VM page-fault delivery and post-handoff map/unmap/allocation operations remain unavailable in this outcome | Staged substitution | A later reviewed VM mapping protocol replaces the fatal-only boundary before any pageable or newly mapped process exists |
| VH-16 | VM chooses ordinary free frames after handoff | Baseline parity | MINIX VM owns the free-page bitmap and allocation policy |
| VH-17 | The kernel allocator bitmap and typed ledger remain privileged validation/mechanism mirrors | Compatible extension | User policy cannot mutate privileged availability, ownership, or PTE metadata directly |
| VH-18 | VM identity is bound to exact service, process, endpoint, profile, and generation values | Compatible extension | MINIX validates endpoints and VM slots; independent generation-bound identities reject stale authority |
| VH-19 | Live update, restart, reconstruction, overcommit, paging, sharing, file-backed mappings, and cache policy are absent | Staged substitution | These behaviors return only at their later DS/RS, VFS, persistent-storage, or POSIX milestones |

No unclassified divergence remains for the Step 9 outcome.

## Derived Step 9 boundary

The smallest dependency-ready VM handoff therefore requires:

1. one real fixed-address VM service ELF released by the static launcher;
2. one fixed pointer-free boot-information and frame-database object in VM's
   wired writable segment;
3. complete memory, reservation, allocator-range, address-space, mapping,
   frame-state, count, and digest validation in user space;
4. one exact VM-only kernel summary operation;
5. atomic staging of every static user frame as wired before launcher entry;
6. one failure-atomic irreversible ownership commit after VM summary
   validation;
7. an exact role gate connecting that commit to ordinary launcher readiness;
8. post-handoff execution, IPC, and checked-copy access through existing wired
   mappings;
9. fatal VM self-fault behavior; and
10. no post-handoff mapping mutation until its own reviewed task.

This boundary establishes VM as the policy owner without inventing PM, VFS,
page-fault, mapping-generation, load-token, or recovery protocols early.

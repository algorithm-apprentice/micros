# MINIX Kernel VM-Handoff Source-Organization Baseline

This note records the fixed MINIX baseline used to decompose the `micros`
kernel VM-handoff state, operation-12 adapter, boot snapshot, and associated
native and target evidence. Runtime semantics remain governed by the Accepted
`micros` ADRs, especially ADR-0040, ADR-0045, and ADR-0050.

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
minix/kernel/system/do_vmctl.c
minix/kernel/arch/i386/arch_do_vmctl.c
minix/servers/vm/main.c
minix/servers/vm/utility.c
minix/servers/vm/alloc.c
minix/servers/vm/pagetable.c
minix/servers/vm/exec.c
minix/servers/vm/exit.c
minix/servers/vm/pagefaults.c
```

The inspection used the local tree only. Representative commands were:

```sh
git -C /Users/alephling/Repo/minix rev-parse HEAD
sed -n '1,220p' /Users/alephling/Repo/minix/minix/kernel/system/do_vmctl.c
sed -n '1,120p' /Users/alephling/Repo/minix/minix/kernel/arch/i386/arch_do_vmctl.c
sed -n '90,210p' /Users/alephling/Repo/minix/minix/servers/vm/main.c
sed -n '250,540p' /Users/alephling/Repo/minix/minix/servers/vm/main.c
sed -n '980,1510p' /Users/alephling/Repo/minix/minix/servers/vm/pagetable.c
sed -n '1,180p' /Users/alephling/Repo/minix/minix/servers/vm/exec.c
sed -n '1,150p' /Users/alephling/Repo/minix/minix/servers/vm/exit.c
```

No GitHub or web copy was used.

## Privileged VMCTL adapter

MINIX keeps the privileged kernel adapter separate from VM policy.
`do_vmctl.c` validates the caller and target process, dispatches the selected
VMCTL request, applies process inhibition or fault-state changes, and forwards
architecture operations to `arch_do_vmctl.c`.

The adapter does not become the physical-page allocator, process-region
owner, or executable loader. It is the privileged boundary through which VM
requests root binding, translation invalidation, process release, and other
kernel-only mechanisms.

The reusable source rule is:

- decode and caller authorization belong at the privileged adapter;
- policy and transaction state remain with VM or a portable state owner;
- architecture mutation is below the adapter; and
- the adapter must preserve validation-before-mutation ordering.

`micros` operation 12 is not the MINIX VMCTL ABI. Its register namespace,
generation-bound caller identity, pointer-free fixed objects, and fatal
invariant classification are RISC-V and project adaptations.

## VM bootstrap and steady-state dispatch

MINIX VM startup in `main.c` imports kernel boot information, initializes
physical memory, reconstructs boot-process mappings, and only then enters its
ordinary receive loop. Request dispatch validates the exact source endpoint
and operation before invoking responsibility-specific handlers.

The reusable behavior is:

- kernel boot data is authoritative input;
- VM copies and validates that input into private state;
- ordinary VM authority begins only after bootstrap state is complete; and
- later requests are dispatched by operation without folding every lifecycle
  into the receive loop.

`micros` already prepares static service roots before VM runs. Its bounded
snapshot therefore describes existing mappings rather than asking VM to
rebuild every boot service. That is a required development-DAG adaptation,
not a change in the eventual authority owner.

## Dynamic load and page-table construction

MINIX keeps executable orchestration above lower-level page-table mechanism.
`exec.c` and the exec path coordinate process image replacement, while
`pagetable.c` owns page-table allocation, mapping writes, root binding, and
release.

The useful ordering property is:

1. validate the complete requested image or mapping operation;
2. reserve or allocate every required resource;
3. prepare page-table state before publication;
4. bind or expose the completed result; and
5. release process memory through a distinct exit path.

`micros` direct spawn differs materially:

- VM is unprivileged and cannot write child page tables directly;
- one token-bound scratch alias initializes each selected transferable frame;
- the kernel validates and constructs the exact four-table Sv39 root at
  freeze;
- prepared and activated lifecycle phases are explicit; and
- every abort or exit release uses a complete preflight followed by
  non-failing commits.

These mechanisms are required adaptations and compatible safety extensions.
They do not move frame selection or executable-layout policy into the kernel.

## Exit release

MINIX `exit.c` orchestrates VM process-memory release separately from request
decode, physical allocation, and page-table primitives. It releases process
regions and tables, then clears the VM process slot.

The reusable contract is that exit coordination invokes lower-level mapping
and ownership mechanisms; it does not duplicate those mechanisms in the
dispatcher. `micros` preserves that direction with an exact exit token, load
token, mapping generation, complete address-space teardown preflight, frame
release preflight, and final state clear.

## Fault handling

MINIX separates page-fault receive and resume logic from ordinary request
dispatch. A VM self-fault is fatal because VM cannot recursively resolve its
own working set.

`micros` retains only the dependency-ready subset:

- VM self-fault classification remains fatal;
- ordinary dynamic-fault delivery and retry remain deferred; and
- source decomposition must not invent a page-fault adapter.

## Source-ownership lesson

The useful MINIX source shape is responsibility direction:

1. generic kernel VM control is separate from architecture mutation;
2. VM startup and request dispatch are separate from allocator, page-table,
   exec, exit, and fault mechanisms;
3. executable and exit orchestration depend downward on page-table and frame
   mechanisms; and
4. process lifecycle handlers do not become the common message decoder.

This supports separate `micros` owners for:

- register ABI decoding;
- complete VM-handoff state validation and page-plan digest;
- static boot handoff;
- dynamic load begin;
- scratch map/commit;
- freeze;
- prepared/activated/final commit state;
- abort and exit release;
- fixed user-object translation;
- privileged command adapters;
- boot snapshot preparation and current-state validation; and
- behavior-specific native and target evidence.

It does not justify copying MINIX message layouts, global VM state, VMCTL
commands, dynamic regions, page cache, fault retry, fork, recovery, or
architecture page-table code.

## Difference classification

The classification follows
[ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md).

| ID | `micros` choice relative to the fixed MINIX baseline | Classification | Reason |
| --- | --- | --- | --- |
| KH-01 | VM owns ordinary memory and mapping policy while the kernel retains privileged mechanism | Baseline parity | MINIX preserves the same kernel/VM authority split |
| KH-02 | Operation 12 uses a fixed RISC-V register ABI rather than MINIX messages and VMCTL | Required adaptation | The unified syscall namespace and direct U-mode `ecall` boundary are target constraints |
| KH-03 | Static boot memory and every existing mapping are transported in one bounded pointer-free snapshot | Required adaptation | FDT, embedded ELFs, and prebuilt static roots replace MINIX boot-info and reconstruction mechanisms |
| KH-04 | VM validates and privately adopts the complete boot snapshot before ordinary readiness | Baseline parity | MINIX VM imports and validates kernel boot state before entering steady state |
| KH-05 | Exact generations, role/profile identity, tokens, zero tails, and scalar digests bind authority | Compatible extension | These checks strengthen stale-authority and provenance guarantees |
| KH-06 | VM selects transferable and page-table frames while the kernel constructs the final Sv39 child root at freeze | Required adaptation | The unprivileged VM cannot write page tables directly |
| KH-07 | One exact token-bound VM-root scratch alias initializes each transferable frame | Required adaptation | It replaces direct VM physical mapping while preserving VM content policy |
| KH-08 | Load begin, freeze, prepared, activated, commit, exit-detached, abort, and release are explicit bounded states | Compatible extension | Explicit state and token ownership strengthen the baseline lifecycle |
| KH-09 | Every fallible operation completes full preflight before non-failing authoritative commits | Compatible extension | Failure atomicity strengthens MINIX's preparation-before-publication ordering |
| KH-10 | Exit release is a separate orchestration above address-space and frame mechanisms | Baseline parity | MINIX exit similarly invokes lower-level VM release owners |
| KH-11 | VM self-fault classification remains separate and fatal | Baseline parity | MINIX never asks VM to resolve its own fault |
| KH-12 | Ordinary page-fault delivery, fork, copy-on-write, recovery, and live update remain absent | Staged substitution | These return only at their later dependency-ready milestones |
| KH-13 | Portable state transitions, syscall adapters, snapshots, and evidence use downward acyclic dependencies | Baseline parity | MINIX keeps dispatch above responsibility-specific VM mechanisms |
| KH-14 | Native byte-exact tests and isolated QEMU scenarios remain replayable and deterministic | Compatible extension | Executable evidence strengthens the source move without changing behavior |

No unclassified divergence is introduced by the source decomposition.

## Derived constraints

1. Preserve `vm_handoff_core.h`, `vm_handoff_syscall.h`, `vm_snapshot.h`, and
   `address_space_handoff_test.h` byte for byte.
2. Keep ABI decoding independent from authoritative state mutation.
3. Keep complete state validation read-only and below every transition owner.
4. Keep boot, load, scratch, freeze, commit, abort, and exit release as
   distinct state-machine owners.
5. Keep fixed-object user translation separate from command-specific
   orchestration.
6. Keep syscall adapters above portable state, frame ownership, address-space,
   TTY, IPC, PM, and bootstrap mechanisms.
7. Keep snapshot preparation and current validation distinct while retaining
   one aligned snapshot storage owner.
8. Keep one mutable context owner per split native or QEMU test family.
9. Preserve exact command, case, scenario, random, marker, timeout, and
   shutdown behavior.

## Non-goals

This baseline does not authorize:

- any new VM command, state, token, mapping rule, caller, privilege, or error;
- direct user-space mutation of frame ownership or page tables;
- page-fault delivery, fork, copy-on-write, paging, reclaim, recovery, or
  live update;
- changes to PM, VM-service, address-space, frame-ownership, TTY, grant, or
  scheduler behavior;
- copying MINIX source, identifiers, structures, or comments; or
- changing test cadence because source files are split.

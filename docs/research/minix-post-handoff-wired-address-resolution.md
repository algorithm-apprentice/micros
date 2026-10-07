# MINIX Runtime Safe-Copy and Post-Handoff Address Resolution

## Purpose

This study traces the fixed MINIX runtime safe-copy path that remains usable
after VM owns memory policy, then classifies the smallest `micros` replacement
for its bootstrap-only address resolver.

The reference is MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

MINIX source is behavioral evidence only. `micros` remains an independent C17
and RISC-V implementation.

## Runtime safe-copy entry

MINIX service code calls `sys_safecopyfrom()` or `sys_safecopyto()` with:

- the grantor endpoint;
- the grant identifier;
- a grant-relative offset;
- one local virtual address;
- the byte count.

The wrappers place those values in one kernel-call message and invoke the
kernel-call path
(`minix/lib/libsys/sys_safecopy.c:5-43`).

The kernel safe-copy handler first determines source and destination from the
requested direction, verifies the complete grant chain, exact grantee,
direction, offset, and bounds, and resolves the final grantor virtual address.
Only then does it construct one source and one destination virtual-address
descriptor
(`minix/kernel/system/do_safecopy.c:270-335`).

There is no bootstrap-only safe-copy phase. The same entry remains the runtime
service mechanism after VM owns mapping policy.

## Mapping and VM authority

Ordinary safe copy calls `virtual_copy_vmcheck()`, which resolves the source
and destination endpoints and attempts the complete virtual copy
(`minix/kernel/system/do_safecopy.c:335-371`,
`minix/kernel/arch/i386/memory.c:590-665`).

If either mapping faults, the kernel records:

- the exact requestor;
- the exact target endpoint;
- the faulting virtual address and length;
- whether write access is required;
- the suspended kernel-call kind.

It sets the caller's VM-request blocking state and queues one request for VM
(`minix/kernel/proc.c:233-258`). The kernel-call dispatcher retains the
original request message while VM resolves or rejects the mapping request, and
the call is resumed through the kernel-call continuation
(`minix/kernel/system.c:59-75,610-636`).

Thus MINIX keeps:

- grant identity and direction in the kernel;
- page mapping policy in VM;
- process virtual mappings as the runtime address authority;
- retry state in the blocked kernel call.

The `CPF_TRY` variant skips VM retry and returns a fault after one direct copy
attempt, while marking the mutable grant entry
(`minix/kernel/system/do_safecopy.c:335-370`).

## Mapping lifetime and service use

MINIX does not require a process's user pages to retain a bootstrap owner
classification after VM starts. Runtime copy authority comes from:

1. exact live endpoint resolution;
2. a verified grant;
3. the current source and destination virtual mappings;
4. VM-mediated recovery when a required mapping is absent.

The caller remains blocked while VM handles an ordinary fault. A failed VM
request returns an explicit kernel-call error; successful resolution resumes
the same saved operation.

## Existing `micros` boundary

`micros` already provides:

- exact process and endpoint generations;
- direct, non-transitive grants;
- page-bounded, all-or-nothing checked copy;
- arbitrary-address Sv39 translation;
- exact typed ownership for process page tables and bootstrap user frames;
- `VM_WIRED` ownership that retains one exact process generation;
- `VM_TRANSFERABLE` ownership that deliberately carries no process identity;
- a one-way `BOOTSTRAP -> HANDED_OFF` ownership transition.

The current address-space resolver requires:

- ownership phase `BOOTSTRAP`;
- exact `PROCESS_PAGE_TABLE` ownership for private tables;
- exact `PROCESS_USER` ownership for every user leaf.

Consequently, root validation, root activation, IPC-buffer access, and checked
grant copy all become unavailable after ownership handoff, even for the
launcher and statically embedded services whose complete working sets are
required to remain wired.

MINIX's underlying virtual-copy path rejects a zero-byte request with `EDOM`
before endpoint or mapping resolution
(`minix/kernel/arch/i386/memory.c:607-609`). ADR-0039 instead makes zero length
a capability-only no-op after grant identity, direction, offset, bounds, and
phase validation.

## `micros` classification

| Topic | MINIX behavior | `micros` classification |
| --- | --- | --- |
| Runtime safe-copy availability | Safe copy remains usable after VM owns mapping policy | Baseline parity: wired initial services must retain IPC-buffer and grant-copy access after handoff |
| Grant authority | Kernel verifies endpoint, token, direction, and bounds before mapping access | Baseline parity through ADR-0038 and ADR-0039 |
| VM fault retry | Kernel suspends the copy and asks VM to resolve an ordinary fault | Staged substitution: resident wired mappings only until VM fault delivery is dependency-ready |
| Bootstrap owner replacement | Runtime mappings no longer depend on a bootstrap-only user-frame class | Staged substitution: replace `PROCESS_USER` leaf authority with exact `VM_WIRED` authority for initial services until dynamic VM mapping exists |
| Page-table identity | Runtime copy follows the target process's current page tables | Compatible safety extension: private tables remain exact `PROCESS_PAGE_TABLE` owners for one process generation |
| Transferable frames | VM may later assign ordinary frames to mappings | Staged substitution: `VM_TRANSFERABLE` is not valid live-leaf authority until the VM mapping task adds an exact mapping transition |
| Aliasing | MINIX mapping policy may establish mappings accepted by VM | Compatible extension: no shared or multiply mapped transferable frame before a reviewed mapping protocol |
| Scalar bounds and atomicity | Scalar safe copy accepts an arbitrary verified length; vectored operations may complete earlier elements | Compatible extension: one operation is page-bounded and preflights both ranges for all-or-nothing completion |
| Zero-length copy | `virtual_copy_f()` rejects zero bytes with `EDOM` | Compatible extension: a zero-length request succeeds only after grant identity, direction, bounds, and phase checks, without claiming mapping authority |
| Handoff commit | MINIX starts with VM-aware runtime mappings already established | Staged substitution: `micros` validates every live bootstrap leaf is planned `VM_WIRED` before the irreversible owner transition |
| Service lifecycle | Runtime services continue using their existing address spaces | Baseline parity: validation, lookup, translation, and activation become phase-aware read operations |

## Required adaptation

The smallest dependency-ready replacement is a wired-only handed-off resolver:

1. before ownership commit, validate every live private root and require every
   reachable `PROCESS_USER` leaf to have a `VM_WIRED` handoff target for the
   same process generation;
2. leave private page-table frames as exact `PROCESS_PAGE_TABLE` owners;
3. after commit, validate every live user leaf as exact `VM_WIRED` ownership;
4. permit read-only root validation, page lookup, arbitrary-address
   translation, and root activation in both phases;
5. keep create, allocate, map, unmap, release, and destroy operations
   bootstrap-only;
6. reject a live leaf naming `VM_TRANSFERABLE`, a foreign wired owner, or a
   stale generation as an invariant;
7. retain resident-only `FAULT` behavior and add no VM callback.

This boundary is sufficient for the launcher and statically embedded services,
whose complete code, data, stacks, IPC buffers, grant buffers, and required
page-table working set must be wired before handoff. It does not authorize VM
to create or destroy post-handoff mappings.

## Deferred VM mapping work

Before VM maps transferable frames into spawned processes, a later reviewed
task must define:

- allocation and release from the VM-transferable pool;
- the exact owner or mapping record installed for a live user leaf;
- page-table allocation after handoff;
- map/unmap mutation while preserving inactive-root and TLB rules;
- mapping-generation updates and prepared-address-space sealing;
- fault delivery and retry, if added.

That task may broaden the handed-off resolver's accepted leaf authority. It
must not weaken grant identity, direction, bounds, failure atomicity, or exact
process-generation checks.

## Source map

- `minix/lib/libsys/sys_safecopy.c`
- `minix/kernel/system/do_safecopy.c`
- `minix/kernel/arch/i386/memory.c`
- `minix/kernel/proc.c`
- `minix/kernel/system.c`
- [MINIX direct grant and safe-copy study](minix-direct-grants-and-safecopy.md)

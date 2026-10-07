# MINIX Direct Grant and Safe-Copy Study

## Purpose

This study traces the fixed MINIX grant lifecycle and checked-copy path before
`micros` implements development-DAG Step 7. It records authority, identity,
bounds, direction, fault, and teardown behavior rather than source to copy.

The reference is MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

MINIX source is behavioral evidence only. `micros` remains an independent C17
and RISC-V implementation.

## Grant representation and identity

MINIX grant entries are user-owned table records. A direct entry names:

- one grantee endpoint;
- one virtual base in the grantor;
- one byte length;
- read and/or write authority.

The table also supports indirect and magic entries. Grant IDs contain an index
and sequence number; `-1` is invalid
(`minix/include/minix/safecopies.h:9-75`).

The initial free list is built in ascending order, and allocation pops its
head. Revocation clears the entry, advances the sequence, and pushes the slot
onto the free-list head, so later reuse is LIFO rather than globally
lowest-slot. Entry fields are published before valid flags
(`minix/lib/libsys/safecopies.c:90-171,222-273`).

The sequence protects ordinary reuse, but MINIX wraps it to zero. Deprecated
reassignment APIs retain IDs across reuse and explicitly provide weaker stale
protection (`minix/lib/libsys/safecopies.c:253-282`).

## Table authority

A privileged process registers the virtual address and entry count of its own
grant table through `SYS_SETGRANT`. The kernel stores the table location,
count, and current owner endpoint in the process privilege structure
(`minix/kernel/system/do_setgrant.c:15-29`,
`minix/include/minix/safecopies.h:103-107`).

For every copy, the kernel:

1. validates the exact grantor endpoint and grant ID;
2. verifies that the registered table still belongs to the grantor's current
   endpoint;
3. bounds-checks the table index;
4. copies the selected entry from grantor memory into kernel storage;
5. verifies used/valid flags and sequence
   (`minix/kernel/system/do_safecopy.c:41-141`).

This preserves endpoint lifetime authority, but table contents remain mutable
user memory and therefore require validation on every use.

## Direct, indirect, and magic authority

MINIX follows an indirect chain for at most five entries. Every hop verifies
the next grantee before redirecting authority to another grantor and token.
Long or cyclic chains fail (`minix/kernel/system/do_safecopy.c:143-173`).

A direct grant then verifies:

- requested direction bits;
- nonwrapping base plus grant length;
- exact grantee or wildcard authority;
- nonwrapping offset plus requested length within the grant
  (`minix/kernel/system/do_safecopy.c:175-216`).

Magic grants may name memory owned by another process, but only hard-coded
trusted services may create them
(`minix/kernel/system/do_safecopy.c:217-255`).

The shell MVP does not require forwarding grant authority. ADR-0005 already
chooses direct, non-transitive grants and resident bounce buffers.

## Copy direction

MINIX defines direction from the grantee's perspective:

- `CPF_READ`: copy from grantor memory into the grantee's local memory;
- `CPF_WRITE`: copy from the grantee's local memory into grantor memory
  (`minix/include/minix/safecopies.h:63-68`).

`sys_safecopyfrom()` requests `CPF_READ`; `sys_safecopyto()` requests
`CPF_WRITE`. Both pass the grantor endpoint, token, grant-relative offset,
local address, and byte count
(`minix/lib/libsys/sys_safecopy.c:5-43`,
`minix/kernel/system/do_safecopy.c:374-394`).

The verified grant determines the final remote address. The kernel then copies
between that address and the grantee's local address
(`minix/kernel/system/do_safecopy.c:270-335`).

## Fault and atomicity behavior

Ordinary MINIX safe copy may call VM to resolve faults. A `CPF_TRY` grant
instead fails immediately on a soft source or destination fault and records a
fault marker in the user-owned grant entry
(`minix/kernel/system/do_safecopy.c:335-371`).

Vectored safe copy verifies and executes entries one by one, stopping at the
first error. Earlier vector elements may already have copied
(`minix/kernel/system/do_safecopy.c:396-446`).

ADR-0005 deliberately chooses a smaller bootstrap contract:

- no VM callback;
- complete local and remote range validation before copying;
- scalar operations only;
- all requested bytes or no bytes.

## Exit and stale authority

MINIX binds a registered table to the grantor endpoint. Endpoint replacement
therefore invalidates table use even if stale user memory still contains an
entry (`minix/kernel/system/do_safecopy.c:75-101`).

User-space revocation invalidates an individual token before slot reuse.
Indirect and magic chains add further endpoint checks at each hop.

`micros` requires stronger lifecycle closure: every active grant stores exact
grantor and grantee generations, and endpoint teardown cancels either side's
grants before process-slot reuse.

## `micros` classification

| Topic | MINIX behavior | `micros` classification |
| --- | --- | --- |
| Direct bounded region | Grant names grantee, base, length, and direction | Baseline parity |
| Grant token | Index plus sequence identifies a reusable slot | Baseline parity with a different opaque 32-bit packing |
| Allocation order | Initial allocation is ascending, then revoked slots are reused LIFO | Compatible deterministic extension: always select the lowest canonical free slot |
| Generation advance | Revocation advances sequence before reuse | Baseline parity; revoke and endpoint cancellation advance before `FREE`, or quarantine |
| Sequence wrap | Sequence wraps and deprecated APIs may retain IDs | Compatible extension: quarantine before stale token resurrection |
| User grant table | Kernel rereads mutable user entries on every copy | Compatible extension fixed by ADR-0005: kernel-managed immutable active records remove table TOCTOU |
| Dynamic table growth | Each process grows its own table | Compatible extension: one fixed-capacity kernel registry gives deterministic memory use |
| Wildcard grantee | Direct grants may authorize `ANY` | Compatible least-authority extension: require one exact active grantee endpoint |
| Self grantee | The general direct entry can name the grantor itself | Compatible least-authority extension: reject self-grants because local memory needs no cross-address-space capability |
| Zero permissions | MINIX rejects unknown permission bits but accepts an empty known subset | Compatible least-authority extension: require at least one of read or write |
| Empty grant | Direct creation does not reject a zero byte length | Compatible extension: reject zero-length active grants and reserve zero length for no-op copy requests |
| Indirect grants | Authority may traverse a bounded chain | Staged substitution: v0.1 uses non-transitive VFS bounce buffers; reconsider only with a reviewed zero-copy requirement |
| Magic grants | Trusted services may grant another process's memory | Staged substitution: VM scratch mappings and direct VFS-to-VM grants replace magic authority in the shell MVP |
| VM fault callback | Ordinary copy may ask VM to resolve faults | Required development-DAG adaptation: resident mappings only until VM-safe fault delivery exists |
| `CPF_TRY` marker | Fail-fast copy records a mutable user-table fault bit | Required adaptation: every v0.1 copy is fail-fast and returns an explicit error; no user table exists to mark |
| Vectored copy | Entries execute sequentially and may partially succeed | Compatible extension: one bounded scalar operation preflights both ranges and is failure-atomic |
| Copy size | Scalar safe copy accepts an arbitrary verified byte count | Compatible bounded extension: one operation copies at most one page; larger baseline transfers repeat the same checked operation |
| Endpoint lifetime | Table registration and chain hops revalidate endpoints | Baseline parity plus exact generation checks on both grant participants |
| Exit cleanup | Endpoint change makes table authority stale | Compatible extension: bounded cancellation explicitly frees every related active grant |
| User syscall ABI | Drivers invoke setgrant and safe-copy kernel calls | Staged substitution: portable grant mechanisms and QEMU evidence precede the separately reviewed user-runtime syscall ABI |

## Current substrate and missing mechanism

`micros` already provides:

- exact active endpoint and process-generation resolution;
- fixed-capacity immutable privilege profiles;
- arbitrary-address user translation with exact process ownership and
  per-page permissions;
- fully mapped managed RAM in the kernel address space;
- one-hart SIE-clear serialization;
- typed user-frame ownership and generation-safe root teardown;
- bounded two-page message copy as prior art for retained physical plans.

It does not yet provide:

- grant token allocation or stale-token rejection;
- grantor/grantee lifetime tracking;
- direction and grant-relative bounds validation;
- cancellation on endpoint teardown;
- page-sized cross-address-space copy planning and commit;
- an authoritative target grant runtime or QEMU grant component.

## Derived task sequence

The current ADR-0038 design authorizes only:

1. a fixed kernel registry of exact-generation direct grants;
2. explicit create, revoke, endpoint-cancel, inspect, and validate operations;
3. exact grantor ownership and one exact active grantee;
4. nonzero read/write permission subsets;
5. arbitrary byte-aligned grant ranges within the user window; and
6. no indirect grant, magic grant, wildcard grantee, or transitive authority.

After that registry implementation merges, the checked-copy findings in this
research become inputs to a separate design review. That later design must
decide and review copy bounds, translation plans, failure atomicity, mapping
faults, target evidence, and its own implementation PR. This research does not
authorize those implementation details.

A still later user-runtime design exposes the reviewed mechanisms without
changing accepted token, direction, bounds, or lifetime rules.

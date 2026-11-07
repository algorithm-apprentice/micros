# MINIX Selected User Address-Space Resolution

## Status and fixed references

This is the fixed-source study for Accepted
[ADR-0107](../adr/0107-selected-user-address-space-validation.md) and its
[selected-operation contract](../architecture/selected-user-address-space.md),
not an implementation result. Design Acceptance does not claim runtime
execution or retirement. No MEM04 code, test, model, fixture, generator or
executable prototype has been authored.

The current source is independently reviewed EV01-C05 implementation #221:

```text
micros commit: f28377decc0d20533e05330f2f2211db3f230910
micros tree:   4b56076479ac53092c4c83d9c9b52829b66139ba
MINIX commit:  4db99f4012570a577414fe2a43697b2f239b699e
MINIX tree:    80db89de58d5ecaf31efe48b4d1d98c7e285e81f
```

Runtime source is unchanged from MEM03 #219. The separately merged
[EV01-C05 contract](../architecture/selected-ownership-native-source-authority.md)
adds granular source identities, not a source waiver. The current catalog
has 4,774 facts in nineteen families; five prior claims retire seventy
identities. Its 484 new current-source bindings remain active.

MEM04 owns B02-F005/B02-E004 and depends on merged MEM02 and MEM03.
The [correction DAG](minix-complete-baseline-findings-and-correction-dag.md)
retains separate MEM05 release/handoff, OBJ01/OBJ02 scheduler,
IPC/GRANT, BOOT and VM outcomes. This study must not treat a selected root
correction as completion of those callers' entire operations.

## Fixed MINIX behavior and structure

Read-only inspection verified the local fixed MINIX revision. In
`minix/servers/vm/vm.h:8`, `SANITYCHECKS` is zero.

| Fixed MINIX source | Behavior and logical structure |
| --- | --- |
| `minix/kernel/arch/i386/memory.c:325-371` | `vm_lookup` follows selected directory/table entries, returns early for absent entries, handles a large page and preserves the byte offset |
| `minix/kernel/arch/arm/memory.c:302-350` | Section/coarse-table translation likewise follows selected entries |
| `minix/servers/vm/region.c:616-645` | `map_lookup` searches the VM region AVL tree; region policy is distinct from a hardware-table point walk |
| `minix/servers/vm/pagetable.c:1358-1422` | `pt_bind` updates the selected process-slot page-directory mapping and requests address-space binding |
| `minix/kernel/system/do_vmctl.c:17-185` | Exact endpoint selection precedes architecture delegation |
| `minix/kernel/arch/i386/klib.S:605-626` | Selected-root switch with zero/same-root short circuits |
| `minix/kernel/arch/arm/arch_system.c:196-220` | Selected TTBR binding |
| `minix/kernel/proc.c:299-349` | Scheduler return reaches selected address-space binding |

Fixed-level translation has constant average/worst work in mapped-page and
process counts, and constant automatic/auxiliary storage. Amortization is
not needed for the individual walk. A range translation may walk additional
pages to establish contiguous physical extent. AVL region lookup has its
own logarithmic region-count cost; this is not a claim that every VM
operation is constant-time.

MINIX does not establish a whole address-space/physical-owner audit before
each selected hardware-table lookup or selected root binding.

Required `micros` adaptations include:

- Sv39's three levels rather than the referenced two-level/section shapes;
- root slot 1 and `[0x40000000, 0x80000000)` as the private user domain;
- 4 KiB private user leaves even though shared kernel entries may be large;
- distinct process/thread/endpoint objects and checked generations;
- typed physical-frame ownership in addition to the PTE;
- existing TTY device and VM scratch exceptions;
- SIE-clear one-hart serialization; and
- full ASID-zero fences and exact `satp` readback, not MINIX's same-root
  shortcut.

No MINIX source is copied. The exact selected-operation and certification
contract must be accepted separately before implementation.

## Current ordinary paths

`user_address_space_mapping.c:296-382` already contains a fixed three-level
walker. It dereferences root/intermediate frames on the assumption that its
caller has validated the whole structure. It is not independently safe to
call after simply deleting that validation.

Public lookup at lines 459-539 and translation at lines 542-635 currently:

1. reject null output arguments;
2. save/clear SIE and obtain canonical runtime/phase authority;
3. resolve the selected live process and require an attached root;
4. check the supplied page/byte address;
5. acquire scratch;
6. validate the complete root;
7. walk the selected PTE path;
8. load exception authority again and reject an exceptional leaf;
9. publish outputs only on success; and
10. release scratch and restore the original interrupt status.

Lookup returns page-base physical address and stored permissions without
`USER`. Translation adds the byte offset and returns only the remaining
bytes in that 4 KiB page. Neither API decides a caller's requested access
mode. IPC/grant consumers retain their own permission/fault classification.

`user_address_space_activation.c:68-129` similarly acquires scratch and
validates the complete root before the existing architecture activation
call/readback comparison. Kernel-root activation is a separate interface.

`scheduler_context.c:76-102` explicitly invokes the complete public
validator before storing `selected_root`. Its later activation commit at
lines 149-176 programs the already selected root and panics on bad readback.
No new fallible validation may be inserted after that commit begins.

`user_execution_validation.c` distinguishes ordinary context PC/SP lookups
from the explicit complete validation used during context preparation.
Selected point correction must not silently erase the preparation contract.

The runtime view is not a permanently selected process capability.
`user_address_space_runtime.c:170-192` overwrites its process, handle and root
on each resolution. The all-process-roots validator at
`user_address_space_validation.c:859-894` reuses that view for successive
processes. Existing VM/TTY callers retain separate exact selected identities.
A shared selected walker must bind the requested handle explicitly rather
than trust whichever process the preceding all-root pass visited last.

## Where current work grows

`user_address_space_runtime.c` owns two 32 KiB scratch bitmaps and one busy
flag. For `p = managed_frame_count`, acquiring a lease clears two active
`ceil(p/64)` word prefixes; complete validation clears reachability again;
release clears both prefixes. An ordinary complete path therefore performs
**five bitmap-clear passes** before accounting for its tree/owner work.

Complete validation in `user_address_space_validation.c`:

- loads VM scratch and TTY device authority;
- applies MEM03's active metadata check, not its superseded direct
  whole-frame/object check;
- marks the selected root's exact process/table owner;
- checks the canonical kernel root's private slot 1 is zero;
- compares all other 511 shared root entries;
- walks every present private table, scanning its 512 entries;
- classifies every reached leaf and rejects repeated frame reachability;
- checks that required exceptional leaves were found; and
- scans all `p` active frame-owner records for orphan/wrong-role ownership.

The tree term depends on reached tables/leaves; the orphan term depends on
managed memory, not merely the requested path. Each selected ownership
lookup also retains MEM03's documented active-geometry `O(g + k)` cost.
A future selected path must not be described as universally `O(1)` while
silently retaining that geometry cost.

### VM scratch authority is not currently a local read

`vm_handoff_runtime.c:236-282` calls
`micros_vm_handoff_state_validate()` before returning a current scratch
binding. For handed-off state, that validator scans mapping slots, validates
mapping records and checks loading/frozen relationships. An active scratch
transaction also searches mappings for conflicting physical addresses.

Public lookup/translation reach this getter once during full validation and
again during exceptional-leaf rejection. Removing the page-tree scan alone
would not remove this global work.

### TTY authority reintroduces complete registry work

`tty_handoff_runtime_validate.c:310-421` invokes the TTY runtime validator
when device authority is active. The path includes bootstrap binding and
transition searches plus selected binding checks.

Two actual callees matter:

- `endpoint.c:7-22` resolves the endpoint only after
  `micros_endpoint_registry_validate_objects()`.
- `endpoint_privilege.c:3-25,125-151` resolves the profile only after
  `micros_endpoint_registry_validate()`.

`endpoint_object_validation.c:203-235` additionally calls
`micros_kernel_objects_validate()`, clears a thread-capacity array and
validates process/thread relationships.

Thus complete endpoint/object work still reaches ordinary address-space
queries through active TTY authority, despite MEM03 removing the direct
frame-validation route. This is a verified call chain, not an inference
from a getter's name. Any selected exception reader must preserve the
owning controller's authority without retaining these global loops or
silently starting IPC01/TTY01.

## PTE and exceptional-mapping semantics

The common decoder, `sv39.c:155-236`, admits only exact zero as absent.
It rejects a nonzero invalid PTE, software/reserved high bits, non-leaf
`U/G/A/D`, global leaves, missing accessed bits, `W != D`, `W` without `R`,
and `W + X`. A private walk additionally requires tables above level zero
and a user leaf at level zero. These rules must not be replaced with a
different permission whitelist.

Private tables remain exact `PROCESS_PAGE_TABLE` owners for the selected
process generation. Regular leaves are exact `PROCESS_USER` before handoff
or `VM_WIRED` afterward. The owner check must precede physical table
dereference when complete prevalidation is removed.

`user_address_space_core.c:95` makes the active VM scratch virtual slot
exclusive for its exact process/root: even an otherwise valid wired leaf
there is rejected if the physical address, permissions, accessed/dirty
state or `VM_TRANSFERABLE` encoding does not match the scratch binding.
Merely rejecting transferable frames is not an equivalent replacement.

`tty_handoff_core.c:429-487` likewise rejects UART aliases and an
ordinary physical page placed at the active TTY owner's reserved virtual
slot. A different process using that virtual address for a regular owned
page is not automatically a device mapping.

The complete validator also requires the expected exceptional leaf to
exist. The proposal must distinguish a point query's selected slot from
activation's required selected-controller mappings, and must not silently
drop either class of authority.

## Error, state and storage boundaries

Accepted [ADR-0040](../adr/0040-post-handoff-wired-address-space-resolution.md)
currently places complete structure validation before `NOT_MAPPED`.
An absent queried leaf therefore cannot hide unrelated corruption elsewhere
in the root. Selected diagnosis intentionally changes that rule and needs
explicit supersession; it cannot be presented as a behavior-preserving
source refactor.

Preserve initialization/phase/process/range ordering, meaningful selected
PTE/owner errors, success-only output publication, generation checks,
SIE restoration and the unchanged hardware activation sequence. Determine
scratch-busy handling explicitly rather than accidentally losing `BUSY`
when no bitmap is needed.

A selected path needs only a fixed number of table identities, indices,
decoded descriptors and controller bindings. No new capacity-sized buffer,
persistent validation cache, generation waiver or cross-process pointer
authority is justified. Exact automatic bounds and the actual target
stack/ELF acceptance belong in the completed proposal.

## Complete current caller census

Existing function-record inspection found **25 production lexical caller
rows** for the public validator, locked validator and all-process-roots
wrapper. This counts source call sites, not dynamic invocation frequency.

| Current callers | Rows | Proposed scope or retained owner |
| --- | ---: | --- |
| Lookup, translate, public activate, scheduler selected-thread root check | 4 | Named MEM04 hot paths |
| `micros_user_address_space_allocate_page` | 1 | MEM04 selected-prefix preflight and coupled exact cursor compensation |
| Public complete validator and all-process-roots wrapper | 2 | Complete diagnostic/enumeration mechanisms |
| Create, prepared-service validation, execution-context preparation, installed VM-freeze validation | 4 | Certification candidates; retain exact lifecycle/caller constraints |
| Release, destroy, inventory, prepare/complete wired handoff | 5 | MEM05's selected release/root-set work |
| VM freeze preflight, scratch map/remove, VM teardown preflight | 4 | Existing all-process work tracked by the VM correction nodes |
| Bootstrap runtime validation, TTY mapping prepare/validate, post-handoff VM runtime and TTY runtime preflights | 5 | Existing BOOT/VM/TTY command owners and their separate complete-validation work |

The B02-F005 title identifies lookup, translation and activation, but its
smallest-correction paragraph also forbids complete audits on ordinary
mapping/mutation/scratch/freeze/teardown/handoff paths. The proposal includes
the per-page allocation preflight and explicitly retains the other twenty
caller rows under their existing owners. This reconciles the end-state with
the correction DAG's downstream outcomes without claiming that five
selected migrations remove every ordinary complete audit.

In particular, bootstrap allocation uses complete validation before its
existing selected allocation plan, while release/destruction use the
scratch release set. Treating those operations as identical would either
leave an unowned cost or pull MEM05's data-structure work into this task.
The proposal includes allocation's selected-prefix preflight and preserves
its mutation order. Its exact residual-call ledger assigns
the other paths to existing outcomes rather than calling per-page
allocation a one-time certification.

### Coupled allocation-cursor compensation

The current `user_page_allocation` plan retains no original allocator cursor.
`rollback_page_allocation()` releases unpublished frames in reverse order.
The allocator advances to `selected_index + 1` on success
(`frame_allocator.c:528`) and releases with
`min(cursor, released_index)` (lines 605-606).

ADR-0100 permits a valid cursor behind allocated words. For example, with
cursor 64 and bits 0 through 127 already allocated, a first unpublished
allocation at 128 advances the cursor to 129. A following injected failure
and release of 128 leave cursor 128, not 64.
The bitmap/count/owner state may be restored while allocator metadata is
not. This is static source evidence, not an executed regression claim.

ADR-0021:179-181 requires the complete allocator to remain unchanged on
such a failure; its acceptance at lines 403-405 requires exact rollback.
The proposal includes the directly coupled operation-local cursor snapshot
and final prevalidated restoration. Implementation must first demonstrate
this case in native Red. It does not change the primitive allocator policy
or introduce another rollback framework.

## Source authority boundary

EV01-C05 provides the missing MEM03 native-header keys, but leaves every
actual guard active. The public user-address-space header currently has
three active payload intersections: two native dependency identities and
a stack authority anchor. A payload intersection is not automatically an
identity to retire.

The proposal enumerates 33 existing active identities with a 28-path direct
closure, including exact function/definition/header/native consumers and
the two target assertion adaptations. Keep complete original records and
independently bind current helpers/observations before genuine Red; never
retire the broad aggregate or derive an expected value from the corrected
candidate.

Portable work and state changes require native actual-source evidence.
Changed MMU/activation admission also requires an actual failing QEMU
component case, not a native CSR stub or missing new interface. Existing
target corruption, integration and stack gates remain mandatory.

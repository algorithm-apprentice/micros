# Shared VM Memory Transfers

## Status and scope

This is the Accepted VM05 contract under
[ADR-0139](../adr/0139-shared-vm-memory-transfers.md). COPY01 #191,
GRANT03 #264, BOOT03 #270 and VM03 #276 are its actual predecessors.
VM04 #278 is the preceding merged outcome in the preferred order.
Independent design review is clean; implementation remains pending the
separate design merge.

Close B07-F005 only: use the existing shared memory provider for VM-owned
copy/clear helpers, preserving every caller's data extent and authority.
No new runtime dependency or parallel task is introduced.

The [fixed study](../research/minix-vm-memory-transfers.md) owns current
source/reference provenance and caller analysis. Existing Accepted
contracts still own allocation, mappings, scratch, grants, stack layout,
prepared generations, faults, publication and release.

## Exact movement owners

The fifteen helpers and current direct production-call counts are:

| Owner | Copy helpers | Clear helpers | Calls |
| --- | --- | --- | ---: |
| `servers/vm/vm_core.c` | `vm_core_copy_bytes` | `vm_core_zero_bytes` | 5 + 14 |
| `servers/vm/vm_image.c` | `vm_image_copy_bytes` | `vm_image_zero_bytes` | 2 + 1 |
| `servers/vm/vm_service_protocol.c` | None | `vm_service_clear_bytes` | 2 |
| `kernel/vm/vm_handoff_core.c` | `micros_vm_handoff_copy_bytes` | `micros_vm_handoff_clear_bytes` | 24 + 21 |
| `kernel/vm/vm_handoff_syscall_runtime.c` | `micros_vm_handoff_syscall_copy_bytes` | `micros_vm_handoff_syscall_clear_bytes` | 2 + 18 |
| `kernel/vm/vm_handoff_runtime.c` | None | `clear_bytes` | 1 |
| `kernel/vm/vm_snapshot.c` | `copy_bytes` | `clear_bytes` | 1 + 4 |
| `kernel/vm/vm_bootstrap.c` | `copy_bytes` | `clear_bytes` | 1 + 6 |
| `lib/vm/vm_boot_validate.c` | `vm_boot_copy_bytes` | None | 1 |

Keep names, signatures, visibility and each original call. Each copy helper
delegates its unchanged three arguments once to `micros_runtime_memcpy`.
Each clear helper calls `micros_runtime_memset(storage, 0, size)` once.
Discarding the provider's returned destination preserves the existing void
helper interface.

Do not retain another production byte loop, add an alternate provider,
specialize on observed fixture sizes or introduce a generic transfer
dispatcher. The shared provider and its compiler-visible forwarding
symbols are different owners; kernel/native callers link only the
prefixed implementation they need.

## Caller extents, aliases and authority

### Local candidates and state

Kernel decode/load/freeze/release candidates and service transaction,
record, action and reply candidates are separate local storage from their
publication destinations. Source arrays copied into the preparation
candidate are distinct live automatic objects. Preserve all full-object
and full-capacity clears, including padding/tails and failure cleanup.

State initialization retains its state/boot-object nonoverlap check.
No frame/cache/record validation is moved into a memory primitive.
VM04's local reservation, tag/availability, cache/origin rollback and
resource-equality rules remain unchanged.

### Executable and request snapshots

`publish_load_input` retains its exact same-buffer branch: staged input
already at the destination is not copied onto itself; only its unused
tail is cleared. External input remains disjoint under the existing
source-shape preflight. Copy and zero order, exact supplied lengths,
complete buffer clearing and error cleanup remain.

The immutable executable/request snapshot lifetime is unchanged. This
does not borrow VFS memory directly, forward a grant or begin VM06
snapshot reclamation.

### Scratch and stack contents

Image fill still validates the retained selected page/frame and file
intersection before touching the destination. It clears all 4,096 bytes
and then copies exactly the selected file bytes, preserving BSS,
fragments and zero holes.

The initial stack still validates its request/plan before the first write,
clears the complete page and copies argv/env strings from the disjoint
request snapshot. Keep exact string order, terminal NULs, virtual-pointer
encoding, alignment, auxv and the 2,048-byte occupied/runtime-reserve
boundary. Encoding eight bytes of a scalar is not an eight-byte movement
loop to replace.

The shared provider's different-residue byte path remains important for
small stack strings. Do not pad, realign or coalesce strings to manufacture
word counts.

### Fixed user objects

The existing operation-12 prepare function validates the complete fixed
user range, permissions, contiguous lengths and its at-most-two-chunk
plan before read or publication. Preserve that plan and every chunk's
physical address, kernel-buffer offset and size.

Read/write helpers continue iterating the retained chunks. Each calls the
same copy helper once for that chunk; no retranslation, merging,
rounded-up access or new persistent span is added.

The translated frames are exact user-owned wired RAM, distinct from the
kernel candidate/stack storage. Physical pointers are temporary mechanism
inputs, never new user pointer authority. Copy remains inside the existing
protected lifetime, and no fallible check is added after the first store.

### Boot database and snapshot

The boot builder keeps the existing header/range/frame-array clears and
complete failure clear while preserving collected address spaces/mappings.
The snapshot owner retains its single aligned object, build sequence,
complete cleanup and local-candidate result publication.

The boot validator copies only its local completed summary after validation.
No digest, byte predicate, mapping inventory or snapshot lifetime is changed.
The whole 364,704-byte object remains the same wire format.

## Shared primitive contract

Do not change `lib/runtime/memory.c` or `memory.h` algorithms/ABI.
All nonzero ranges remain valid RAM; every inventoried copy is nonoverlapping.
No MMIO/DMA path or overlap guarantee is added.

Zero length performs no access, including null helper arguments.
The provider preserves arbitrary caller alignment, unsigned-byte fill
semantics, exact requested ranges and no overread/overwrite.

Copy uses eight-byte words only for compatible residues, with bounded byte
prefix/suffix. Incompatible residues use the retained byte path.
Fill uses four-byte words and the existing twelve-byte short threshold.
The admitted Clang inline-copy/alignment builtins and production compiler
flags remain unchanged.

For requested length `n`, word width `w` and the provider's independently
derived complete-word count `q`:

```text
transferred bytes = n
transfer units = q + n - w*q
```

Copy has `w=8`; fill has `w=4`. The exact `q` follows the existing provider
contract, including zero, short and differing-residue cases. For aligned
4-KiB input, this gives 512 copy or 1,024 clear units. For aligned
128-KiB input it gives 16,384 copy or 32,768 clear units.

Average/worst work remains linear in requested bytes. Auxiliary storage is
constant and no pointer survives the call. This outcome changes primitive
units, not the number of page/record validations, mappings or snapshots.

## Evidence and consumer integration

### Native actual movement

Extend the existing ordinary/observed VM pair without replacing its current
semantic cases. Before editing the runtime helpers, count each old byte
assignment as one unit/one byte. An unrolled eight-byte group has eight
units/eight bytes, not one loop observation relabelled as a transfer.
The old complete outputs must agree before independent aligned-work
bounds reject their byte-only movement.

Green measures only the executing shared provider's existing RUNTIME
annotations. Do not charge the wrapper too, double-insert provider sites
or infer movement from caller invocations/timing. Keep complete source,
case, primitive, generated-input and linked-product qualification.

Cover actual page fill, 128-KiB input clearing/copy, boot-object-sized
movement, record/plan/reply sizes, zero length, matching and differing
residues, short thresholds and canaries. Reuse COPY01's exhaustive
alignment/access tests rather than multiplying every VM scenario by them.

All fifteen wrapper edges must have independent current source and
compiled/linked evidence. Existing lifecycle/boot/syscall fixtures retain
their caller coverage. Where the current native pair lacks an owner,
use its actual pure helper through the existing test-only
extraction/facade pattern or the already owning native fixture; never
handwrite a replacement algorithm or successful authority stub.

### Dependent native families

The current affected observation families are VM01 and MEM03/MEM04/MEM05.
Admit only the existing provider/private header and its RUNTIME owner,
with one provider per linked product. Other library admissions stay closed.

VM01's work channel observes the provider once. MEM03 enables provider
observations only in its aggregate execution; its four focused
frame/geometry channels keep their existing scope. MEM04 and MEM05 enable
them in aggregate and the existing context component only, preserving
exact disjoint composition without counting the provider in every
component.

Ordinary/non-owning provider compilations remain uninstrumented. Retain
every current family case, work bound, corruption control and completeness
check. When work moves from a VM owner to RUNTIME, update the owning
movement oracle rather than accepting a vacuous zero in the old column.

### Same-guest machine evidence

Use the existing accepted ELF-spawn guest/build. Its original contents,
load/rollback/exit/resource witnesses and scoped VM command counters remain.
Keep the shared runtime provider uninstrumented in that guest: its existing
collector lifetime extends beyond individual command-active intervals,
so unconditional provider annotations would count unrelated runtime work.

After the same guest, use the existing artifact proof engines to verify
all fifteen actual helper/provider edges in the kernel/VM images,
exact argument/extent preservation, one provider per image, real
eight-/four-byte provider transfers and absence of recursive or foreign
memory calls. Bind actual compiler commands, source/object/linked
ownership, bytes and closing input/tool identities.

Before correction, the original successful guest's actual old helper
objects must fail this delegated-word algorithm requirement. That is
algorithmic artifact Red, not a missing public API, failed compilation,
generic panic or timeout. Native observations independently establish the
actual old/new unit counts.

No new guest, IPC transport, observer ABI, global runtime work mode or
copying of a historical implementation is needed.

## Preserved gates and implementation sequence

After independent design review, Acceptance and separate merge:

1. establish actual native and same-guest artifact Red with complete
   byte/unit and source/primitive ownership;
2. delegate the fifteen helpers and keep every native/target consumer
   linked to one existing provider; and
3. complete focused Green, original native models/target contents,
   current qualification, the full author plan and independent review
   before any permanent implementation commit.

Keep all VM04 native cases, complete model seeds/counts, valid/malformed
boot matrices, caller/permission/generation/output guards, scratch
preemption/stale-translation checks and original resource witnesses.
VM03's three ELF plans/nine request decodes and VM04's cache/search
requirements remain; zero ordinary global audits/root enumeration and
five selected root certificates remain.

No new production BSS, buffer, plan, VLA, heap or capacity-sized automatic
object is allowed. Keep existing preparation lifetimes and every current
image/stack/deadline limit. The current observed VM is 160/160 resident
pages with a 3,744/4,096-byte stack bound; actual new artifacts must still
fit. All 44 static configurations, 113 ELFs and ten observed scopes remain,
with B13 explicitly blocked.

Current source/recipe/callback/primitive maintenance belongs here under
ADR-0123. One sufficient final author plan supplies affected-unit evidence;
no metadata prerequisite PR, historical source replay or second
recertification execution is added.

This phase authors documentation only. VM06 snapshot lifetime, LIFE01
cross-owner plans, other service movement, paging/COW, recovery,
concurrency, hosted CI and node-Q/POSIX completion remain out of scope.

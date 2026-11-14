# MINIX VM Frame-Allocation Study

## Fixed inputs

```text
micros source: 77c194b67f81bd1115ee1e5553ef1de93c91e0a6
micros tree:   6c985b0981baeb9063d753f61c9b46f7f43870e4
MINIX source: 4db99f4012570a577414fe2a43697b2f239b699e
```

MEM01 #210 and VM03 #276 are merged. VM04 owns only B07-F003.
The fixed local MINIX checkout was reconfirmed at its pin and clean.
No reference source is copied and no new executable prototype is authored.

## Reference mechanism

| Fixed source | Actual behavior and structure |
| --- | --- |
| `minix/servers/vm/alloc.c:32-44` | A current-free bitmap spans the supported 32-bit physical-page domain; a 10,000-entry integer cache stores single-page hints. |
| `alloc.c:242-278` | `alloc_mem` wraps allocation, optional placement/alignment and cache reclamation. Those broader flags/reclamation are not current `micros` requirements. |
| `alloc.c:298-335` | Initialization clears the bitmap, visits declared memory chunks in reverse order and frees/imports each range. The old hole-list comments do not describe the actual bitmap implementation. |
| `alloc.c:369-400` | `findbit` scans downward, resets a failed contiguous run and skips empty bitmap chunks. |
| `alloc.c:404-460` | Single-page unrestricted requests pop the LIFO cache and recheck actual bits, discarding stale entries. Fallback uses retained `lastscan`, then retries from the upper bound. Success remembers the selected index and clears availability. |
| `alloc.c:465-481` | Release sets every bitmap bit and appends indexes in ascending range order until the cache is full. It does not lower `lastscan`. |
| `minix/include/minix/bitmap.h:12-21` | Page indexes select a bitmap word and bit; chunk access supports empty-word skipping. |

A valid cache-top selection has constant policy work. A stale prefix costs
its popped entries, each removed once after insertion. Fallback is
worst-case linear in bitmap search span, with empty-word skipping and at
most two searches per request. These are not average hashing or resizing
assumptions.

The reference has broader contiguous/placement/reclaim/clear operations.
VM04 specializes only its one-page path: the existing `micros` loader needs
four private tables and separate destination pages, not physically
contiguous regions or a new allocation API.

MEM01's kernel bootstrap cursor retains lowest-address allocation and a
different prefix invariant. It is not the VM origin and is not changed.

## Current implementation

`servers/vm/vm_frames.c` translates compact frame coordinates with checked
binary range lookup, but `vm_core_select_lowest_frames` still starts at zero.
It constructs a temporary global membership view, inspects initial
eligibility/current byte state and selects globally lowest frames.

`vm_core_selected_frames_match` reconstructs the same selection for complete
diagnostics. A retained reference origin/cache cannot support this replay:
diagnostics must instead prove the actual selected set and current
availability/ownership invariants.

`vm_core.c` builds the existing 32-KiB immutable initial-free bitmap.
`vm_validation.c::vm_validation_digest_active_frame_states` uses it to
recover original FREE bytes while hashing the imported frame state.
That original-class information is necessary; simply turning the bitmap
mutable would break diagnostic provenance.

The existing local byte already distinguishes static classes from dynamic
transferable/reserved/wired/table classes. Tagging only those four dynamic
values preserves initial-free provenance in existing storage. Untagged
FREE remains self-identifying; static values remain unchanged. The reused
bitmap can then describe actual current availability without a second
frame plane.

Cold initialization and `vm_server_complete_handoff` were traced:
initialization validates the original object before publishing VM state,
and handoff sends the original header summary. Building the cache/bitmap
need not modify the imported frame bytes. Tags first appear when local
load reservations begin, and disappear on release.

Current preparation chooses frames but reserves only tables at bind.
The new bitmap must reserve the entire local set during preparation so
sequential cache/scan choices cannot repeat a frame. This changes only
local policy state before `LOAD_BEGIN`; actual kernel claims remain at
scratch/freeze. `INPUT_COPIED` abort consequently has a real local set to
release.

All existing local releases iterate selected tables/data or the ordered
record. Those are the exact places to return current bitmap availability
and cache hints after the unchanged kernel release boundary.

## Resource evidence

Existing artifacts were inspected read-only with the installed
`llvm-readelf --program-headers --wide`:

| Current VM artifact | RX pages | R pages | RW pages | External stack | Total |
| --- | ---: | ---: | ---: | ---: | ---: |
| Ordinary ELF-spawn VM | 10 | 1 | 144 | 1 | 156 |
| Observed ELF-spawn VM | 13 | 1 | 144 | 1 | 159 |

Paths are
`build/riscv64-elf-spawn-test/bootstrap/micros-vm-server.elf` and
`build/stack-evidence/riscv64-elf-spawn-test/observed/bootstrap/micros-vm-server.elf`.
The observed image has one page of headroom against 160, not room for a
second 32-KiB bitmap. The #276 complete certificate covers 44
configurations, 113 ELFs and ten observed scopes; its VM bound and
high-water use are 3,744 bytes against 4,096.

The proposed cache/count/origin use 88 bytes before alignment; actual
future layout and code/image fit must still be measured during
implementation. The proposal retains the existing bitmap and exclusive
key workspace, and uses the existing preparation arrays plus scalar
rollback metadata rather than copying either capacity-sized state.

## Classified adaptations

| Difference | Classification | Reason |
| --- | --- | --- |
| Current bitmap, LIFO cache, retained descending origin and empty-word skipping | Baseline parity | Reproduce the actual reference allocator instead of repeated lowest-frame search. |
| Compact indexes plus checked binary physical translation | Required adaptation | Preserve accepted sparse 64-bit physical geometry without an address-span-sized bitmap; disclose `O(log(r+1))` translation. |
| Twenty-entry cache | Required adaptation | Bound accelerator storage to one maximal existing load while the complete bitmap retains every managed frame. |
| Reuse bitmap backing and tag existing dynamic local bytes | Required adaptation | Preserve both current availability and original-class diagnostics within the measured image budget. |
| Failure-atomic multi-page local batch using scalar cache/origin rollback | Compatible extension | Preserve the existing hidden-load failure contract without another journal or bitmap copy. |
| Sort the selected set into existing table/data order | Required adaptation | Keep the Accepted pointer-free mapping representation and role ordering, not globally lowest selection. |
| Fresh actual kernel owner/PTE checks and global diagnostic equality | Required adaptation | Unprivileged VM allocation policy is not privileged mapping authority. |
| No contiguous/placement/reclaim/clear allocation flags | Staged substitution | Retain the current resident single-page loader; VM05 and the documented later VM/POSIX outcomes own their required replacements. |

The [Accepted contract](../architecture/vm-frame-allocation.md) fixes the
representation, complete local reservation, failure ordering, diagnostics
and independent acceptance. Independent design review is clean; its
separate merge remains required before implementation.

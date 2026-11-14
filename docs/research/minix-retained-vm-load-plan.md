# MINIX Retained VM Load-Plan Study

## Fixed inputs and scope

```text
micros source: 2dc124e71f4b128eafa0be995f8f2ede8a8fb632
micros tree:   a58d913f35b6a3d543b4c040f1ebe20a6a1dd709
MINIX source: 4db99f4012570a577414fe2a43697b2f239b699e
```

VM01 #272 and VM02 #274 are independently reviewed and merged, satisfying
VM03's actual predecessors. Reference inspection uses the
fixed local MINIX checkout read-only, not a newer web copy.

VM03 owns only B07-F002. VM01 already removed ordinary complete VM audits;
VM02 removed all-root and pairwise mapping-plan work. The remaining question
is not whether to perform those audits less often: they must stay absent.
It is whether each page consumes the prepared load description or rebuilds
and revisits the complete description.

The [Accepted contract](../architecture/retained-vm-load-plan.md) preserves
the current representations and assigns the remaining allocator, movement,
snapshot-lifetime and cross-owner lifecycle work to VM04-VM06 and LIFE01.

## Fixed reference

| Reference | Behavior, structure and work |
| --- | --- |
| `minix/lib/libexec/libexec.h:23-53` | One caller-owned `exec_info` retains the endpoint, header extent, file/stack/segment metadata, callbacks and opaque context through one load. It contains useful operation data, not a Boolean remembered sanity result. |
| `minix/servers/vfs/exec.c:185-240,334-401` | VFS serializes exec, captures its stack, retains one vnode/exec context, installs loader callbacks, loads the image, prepares/copies one stack and publishes PC/SP. Cleanup releases the retained resources. |
| `minix/lib/libexec/exec_elf.c:127-178` | Unpack/check the header, reject another interpreter and check file extents before clearing the process. `elf_has_interpreter` unpacks the header again: the reference has constant whole-load passes, not literally one call to every parser. |
| `minix/lib/libexec/exec_elf.c:175-318` | Walk program headers and retained segment extents, map or allocate/copy/clear each selected segment, then allocate the stack and publish the entry/load base. No whole-VM validation is interposed between page-sized pieces. |
| `minix/lib/libexec/exec_general.c:21-71` | Default callbacks implement selected anonymous mappings, process clear, copy and fill through the named process endpoint. Callback authority remains separate from the retained description. |
| `minix/servers/vfs/exec.c:413-454,688-715` | Stack augmentation consumes the captured frame; segment copying checks the selected file extent and exact I/O result. These do not rediscover every VM process or reparse every segment per copied page. |
| `minix/servers/vm/mmap.c:200-278` | Check third-party caller privilege and exact endpoint, select `vmproc[n]`, then operate on the requested region. Allocation and region/page-table work remain in their own owners. |
| `minix/servers/vm/main.c:331-407` | Boot loading likewise retains one exec context, calls the same loader and separately prepares/copies one initial stack. Its automatic header/stack buffers are not a suitable source layout for the four-KiB `micros` user stack. |

The reference library supports its fixed 32-bit ELF and dynamic/file-backed
paths. `micros` retains its Accepted ELF64/RV64 three-segment, resident
15-image-page-plus-one-stack subset. Disabled or incomplete reference
range checks, broad permissions and callback error handling are not copied.
Existing `micros` overflow, W^X, failure, isolation and provenance rules remain.

Let `h` be admitted program headers, `u` resident image/stack pages and `s`
captured argument/environment bytes. Excluding separately owned allocator,
mapping and I/O cost, the reference loading control is `O(h + u + s)`;
there is no `u` multiplier on a whole-image/stack rediscovery pass.
Its context is constant-sized and references retained input/operation
storage; copied data and page-table/region storage are accounted separately.
There is no expected hashing, resizing or amortized assumption in this path.

## Current remaining work

`micros_vm_service_load_transaction` already retains the complete ELF page
description, stack layout, destination frames, reservation, process/token
binding and progress. It need not be copied into another plan or repacked.

`servers/vm/vm_validation.c::vm_core_transaction_plans_are_canonical`
nonetheless rebuilds both descriptions and compares the complete ELF plan.
The ordinary selected validator invokes it through
`vm_core_transaction_common_is_canonical`. It also revisits data-frame order,
the complete selected record and every record/transaction entry relation.

The service page loop calls selected admission through `next_page`,
`scratch_mapped`, `fill_scratch` and `scratch_committed`. For `u` pages,
the normal LOAD request therefore invokes:

```text
selected service admission: 5 + 4*u
ELF planner:                4 + 4*u
stack decode/plan:          6 + 4*u
```

The first count includes the two IDLE admissions, which do not parse a
transaction. The stack count includes the retained pre-write stack-plan
comparison and the public stack builder's independent decode/plan.
Each later frozen commit or abort adds two more ELF/stack reconstructions.
These are source-level call counts, not an elapsed-time inference.

The current kernel selected validator also validates the complete loading
record. `micros_vm_handoff_loading_record_can_append` repeats its layout/key
proof before selecting the next entry. VM02's ordered keys remove a
quadratic proof within one record, but repeatedly proving every completed
prefix still introduces a `u` multiplier across one load.

The exact current native checker remains available from the repository root:

```bash
"${MICROS_PYTHON}" -E -S -B tools/check_selected_vm.py \
  --executable "${PWD}/build/host-debug/selected_vm_ordinary" \
  --executable "${PWD}/build/host-debug/selected_vm_test"
```

An existing-product read-only measurement accepted all 66 cases in both
products and reported:

| Current captured operation | Complete audits | Record visits | Index probes |
| --- | ---: | ---: | ---: |
| Service next image page | 0 | 244 | 126 |
| Service scratch mapped | 0 | 244 | 127 |
| Service image fill | 0 | 244 | 124 |
| Service scratch committed | 0 | 244 | 126 |
| Kernel scratch map plus claim | 0 | 70 | 107 |
| Kernel scratch commit | 0 | 34 | 39 |
| Service next page, maximal page/range fixture | 0 | 375 | 475 |

These existing cases satisfy VM02, not a new VM03 Red or closure. Their
different geometries/page counts must not be treated as an isolated timing
comparison. VM03 needs equivalent selected-page cases with varied unconsumed
pages/input sizes, plus one complete load measurement.

## Lifetime and resource evidence

The #274 certificate covers 44 static configurations, 113 ELFs and ten
observed scopes. Its maximal observed VM stack is 3,744 of 4,096 bytes.
All existing image limits remain, including VM's 160 resident pages.

The proposed mechanism adds no persistent object or capacity-sized automatic
buffer. Existing transaction fields are the retained data; existing phase,
token and generation relations identify their lifetime. Preparation-only
aggregates keep ADR-0087's separate helper lifetime.

The public stack builder currently decodes and validates before its first
store. Keeping the existing VM pre-write comparison plus that builder costs
three stack decode/plan passes per whole load, including preparation.
That constant linear cost preserves failure atomicity without another
resident stack page, decoded-request cache or new unchecked builder API.
It is not necessary to eliminate every constant pass to remove the audited
per-page multiplier.

Actual compiler/linked/observed implementation evidence still decides fit.
The current margin is not permission to enlarge a budget or exclude code
from observation.

## Adaptation ledger

| Difference | Classification | Reason |
| --- | --- | --- |
| Retain a load description and consume selected page/progress data | Baseline parity | Follow the reference's context/segment organization rather than rediscovering the operation at each step. |
| Existing transaction and kernel append-only mapping record | Required adaptation | Preserve unprivileged VM, exact generations/tokens and kernel-owned PTE mechanism without another plan object. |
| Three segment descriptors, resident pages and one scratch window | Staged substitution | Preserve the current ELF/spawn subset; SOM-04/SOM-06/SOM-17 retain their replacement points. |
| Actual owner/PTE/caller checks and protected publication at each kernel command | Required adaptation | A retained description never replaces fresh privileged authority. |
| Full selected-set checks at bind/freeze/release and constant pre-write stack validation | Compatible extension | Linear once-per-boundary work preserves failure atomicity without multiplying whole-load parsing by page count. |
| Complete diagnostics of source/plan/global-state disagreement | Compatible extension | Retain original corruption coverage outside ordinary page admission. |
| Retained allocator scans, byte movement and duplicate bootstrap snapshot | Staged substitution | VM04, VM05 and VM06 remain separately owned, measured work. |

This study authors no runtime, test, model, harness or executable prototype.
Independent design review is clean and the decision is Accepted. Its separate
merge remains required before implementation starts.

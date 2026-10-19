# B07 VM Baseline Audit

## Status

```text
audit unit:       B07 VM
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; six blocking findings; not certified
implementation:   forbidden during complete-audit phase
```

B07 audits the fixed boot database, one-way handoff, VM-local frame policy,
mapping records, executable and stack planning, scratch/freeze/commit/abort,
exit release, operation-12 authority, the PM-facing VM service protocol, and
the fatal fault boundary.

## Exact primary path manifest

B07 has 50 primary paths in the pinned tree.

### Public contracts

| Path | Responsibility |
| --- | --- |
| `include/micros/vm.h` | Load-begin/freeze fixed objects and scratch address |
| `include/micros/vm_bootstrap.h` | Fixed VM boot database, summary, digest, build, and authority validation |
| `include/micros/vm_service.h` | PM-to-VM service protocol commands and stable results |

### Kernel VM owners

| Path | Responsibility |
| --- | --- |
| `kernel/vm/vm_bootstrap.c` | Authoritative boot-object build/finalize/authority validation |
| `kernel/vm/vm_handoff_abi.c` | Operation-12 command and register decoding |
| `kernel/vm/vm_handoff_boot.c` | Static prepare/summary/TTY identity/handoff commit/self-fault action |
| `kernel/vm/vm_handoff_commit.c` | Prepared/unprepared/activated/final commit transitions |
| `kernel/vm/vm_handoff_core.c` | Common predicates, record validation, uniqueness, complete state validation, page digest |
| `kernel/vm/vm_handoff_core.h` | Kernel VM phases, requests, records, actions, state, and transition API |
| `kernel/vm/vm_handoff_freeze.c` | Freeze request/action/result and state commit |
| `kernel/vm/vm_handoff_internal.h` | Private common validation and candidate contracts |
| `kernel/vm/vm_handoff_load.c` | Load-begin preflight/commit |
| `kernel/vm/vm_handoff_release.c` | Abort, exit-detach, exit-release preflight/commit |
| `kernel/vm/vm_handoff_runtime.c` | Static authoritative handoff state and role/scratch authority |
| `kernel/vm/vm_handoff_runtime.h` | Target handoff runtime interface |
| `kernel/vm/vm_handoff_scratch.c` | Scratch-map/commit portable transitions |
| `kernel/vm/vm_handoff_syscall.c` | Captured operation-12 authority, phase gate, and dispatch |
| `kernel/vm/vm_handoff_syscall.h` | Captured operation-12 adapter |
| `kernel/vm/vm_handoff_syscall_boot.c` | VM-ready and TTY mapping adapters |
| `kernel/vm/vm_handoff_syscall_freeze.c` | Freeze fixed objects, PM/frame/address-space orchestration |
| `kernel/vm/vm_handoff_syscall_internal.h` | Private authorities, plans, errors, and command adapters |
| `kernel/vm/vm_handoff_syscall_load.c` | Load-begin and final load-commit adapters |
| `kernel/vm/vm_handoff_syscall_memory.c` | Retained fixed-user-object translation and copy |
| `kernel/vm/vm_handoff_syscall_release.c` | Load-abort and exit-release adapters |
| `kernel/vm/vm_handoff_syscall_runtime.c` | VM authorization, phase, PM reservation, result/fatal helpers |
| `kernel/vm/vm_handoff_syscall_scratch.c` | Scratch-map/commit privileged adapters |
| `kernel/vm/vm_snapshot.c` | Sole aligned kernel snapshot, mapping collection, build, and publication |
| `kernel/vm/vm_snapshot.h` | Snapshot result and prepare/current-validation API |
| `kernel/vm/vm_snapshot_internal.h` | Private binding/mapping callbacks |
| `kernel/vm/vm_snapshot_validation.c` | Current mapping/allocator/owner re-inventory |

### VM service and portable policy owners

| Path | Responsibility |
| --- | --- |
| `servers/vm/vm_core.c` | Byte/range/identity primitives, initial-free bitmap, digest, state initialization |
| `servers/vm/vm_core.h` | VM-local state, records, transactions, frame states, and public lifecycle |
| `servers/vm/vm_elf.c` | Fixed RISC-V ELF64 validation and page plan |
| `servers/vm/vm_exit.c` | Active-record exit-release begin/complete |
| `servers/vm/vm_frames.c` | Managed-range translation, record membership, frame selection/set comparison |
| `servers/vm/vm_image.c` | Little-endian, copy/zero, checked-add, and range helpers |
| `servers/vm/vm_image.h` | Stack/ELF plans and public image algorithms |
| `servers/vm/vm_image_internal.h` | Private image primitives |
| `servers/vm/vm_internal.h` | Private state validation/frame/image contract |
| `servers/vm/vm_load.c` | Input copy, frame reservation, bind, page/scratch loop, freeze/abort/commit |
| `servers/vm/vm_service_backend.c` | Production runtime/grant/operation-12 backend |
| `servers/vm/vm_service_core.c` | Service initialization, complete-instance validation, dispatch/reply |
| `servers/vm/vm_service_core.h` | Service bindings, backend, and public adapter |
| `servers/vm/vm_service_internal.h` | Private request/load/release/protocol contract |
| `servers/vm/vm_service_load.c` | PM load request, grant copies, page loop, freeze result |
| `servers/vm/vm_service_protocol.c` | Message decode/shape and result encoding |
| `servers/vm/vm_service_release.c` | Abort, load commit, and exit release protocol |
| `servers/vm/vm_stack.c` | Canonical application stack planning/building |
| `servers/vm/vm_validation.c` | Complete boot immutability, frame/record/transaction/phase validation |

### VM boot validator

| Path | Responsibility |
| --- | --- |
| `lib/vm/vm_boot_validate.c` | Independent fixed boot-object ranges/mappings/counts/digest validation |

## Cross-owner source dependencies

| Path family | B07 operation | Primary/later owner |
| --- | --- | --- |
| allocator, ownership, user roots, scratch/freeze/teardown | B07-O01/O05-O08 privileged mechanisms | B02 |
| objects and scheduler | B07-O05-O09 process/thread/current/return state | B03 |
| endpoint and IPC | B07-O01/O08/O09 VM authority and PM/launcher protocol | B04 |
| grants, trap/syscall, user runtime | B07-O05/O08/O09 copied inputs and operation 12 | B05 |
| static bootstrap/controller gates | B07-O01/O08 VM release/readiness/TTY phase | B06 |
| PM reservation/exit coordination | B07-O05-O09 transaction authority | B08 |
| TTY handoff/device mapping | B07-O01/O08 exact UART exception | B09 |
| VFS executable/request snapshot and complete spawn/exit | B07-O04-O09 input authority and integration | B11/B12 |
| B07 native/QEMU/model/generator evidence | All B07 operations | B14 |

## Fixed MINIX source evidence

The B07 comparison uses only local source:

- `minix/kernel/main.c`
- `minix/kernel/system/do_vmctl.c`
- `minix/kernel/arch/i386/arch_do_vmctl.c`
- `minix/kernel/arch/i386/exception.c`
- `minix/kernel/arch/earm/exception.c`
- `minix/servers/vm/main.c`
- `minix/servers/vm/utility.c`
- `minix/servers/vm/alloc.c`
- `minix/servers/vm/vmproc.h`
- `minix/servers/vm/region.h`
- `minix/servers/vm/region.c`
- `minix/servers/vm/regionavl.h`
- `minix/servers/vm/regionavl.c`
- `minix/servers/vm/pagetable.c`
- `minix/servers/vm/exit.c`
- `minix/servers/vm/pagefaults.c`
- `minix/lib/libexec/exec_elf.c`
- `minix/lib/libexec/exec_general.c`
- `minix/servers/vfs/exec.c`
- `minix/include/minix/bitmap.h`
- `common/lib/libc/arch/arm/string/memcpy.S`
- `common/lib/libc/arch/arm/string/memset.S`

## Logical variables

| Variable | Meaning |
| --- | --- |
| `m` | active managed frames |
| `mcap` | fixed managed-frame capacity, 262,144 |
| `mbitmap` | MINIX supported physical-page bitmap search span |
| `r` | active managed ranges, at most 96 |
| `p` | dynamic process/mapping-record capacity, 64 |
| `e` | entries in one dynamic mapping record, at most 20 |
| `g` | static boot mappings, at most 4,096 |
| `a` | static boot address spaces, at most 7 |
| `nroot` | live process roots visited by complete root validation |
| `u` | data pages in one application load, at most 16 |
| `xcap` | resident executable-buffer capacity, 131,072 bytes |
| `qcap` | resident request-buffer capacity, 4,096 bytes |
| `bboot` | fixed boot-object capacity, 364,704 bytes |
| `Vsvc` | complete VM-service state validation work |
| `Vkh` | complete kernel VM-handoff state validation work |
| `Vboot` | B06 complete bootstrap runtime validation work |
| `Vep` | B04 complete endpoint/object/IPC validation work |
| `Vreg` | B04 complete privilege-profile validation work |
| `Vvmrt` | complete target VM runtime wrapper validation work |
| `Rmem` | B02 complete address-space validation/translation work |
| `Fown` | B02 complete frame-ownership validation work |
| `Dpm` | B08 PM reservation/runtime validation work |

## Complete validation cost definitions

One complete VM-service validation:

1. validates the immutable boot database and digest, including all fixed
   tails;
2. scans executable/request buffer tails;
3. reparses current ELF and stack inputs in active load phases;
4. scans all active frame states;
5. validates every mapping record and performs pairwise entry/identity
   distinctness checks; and
6. in input/loading/frozen phases, recomputes deterministic frame selection.

```text
Vsvc = O(
    bboot
    + xcap
    + qcap
    + m
    + (p*e)^2
    + p^2
    + selection * m * (r + p*e)
)
```

One complete kernel handoff-state validation scans all `p` records, performs
pairwise token and physical-overlap checks, and validates each bounded entry
set:

```text
Vkh = O(p^2 * e^2 + p * record_bytes)
```

The target VM runtime wrapper adds:

```text
Vvmrt = O(Vkh + Fown + Vep + Vreg + local binding work)
```

Initial current-VM authorization separately performs another
`Vep + Vreg`. One ordinary post-handoff operation-12 command therefore
executes:

```text
(Vep + Vreg) + Vboot + Vvmrt + Dpm + Rmem
```

before its command-specific preflight, which invokes `Vkh` again.

## Operation ledger

### B07-O01 Boot database construction, import, and one-way handoff

MINIX supplies kernel boot memory/process data, initializes VM's private free
bitmap and `vmproc` table, reconstructs VM's page-table state from wired
spares, and makes VM the ordinary policy owner before dependents proceed.

`micros` builds one 364,704-byte pointer-free object containing ranges,
address spaces, mappings, frame states, counts, and an independent digest. VM
validates it, builds its local state/initial-free bitmap, sends one summary,
and the kernel commits the existing wired handoff.

```text
boot validation:
    O(bboot + g^2 + g*(r + a) + mcap)
```

The pairwise `g^2` mapping-physical uniqueness check is included in B07-F004.

| Difference | Classification |
| --- | --- |
| VM imports authoritative boot memory/process/mapping state | Baseline parity |
| Pointer-free fixed transport object | Required adaptation |
| Exact generations, typed states, zero tails, and independent digest | Compatible extension |
| All static leaves staged exact `VM_WIRED` | Required adaptation |
| One scalar summary operation | Required adaptation |
| Irreversible preflighted typed commit | Compatible extension |
| VM self-fault is fatal | Baseline parity |

### B07-O02 VM-local state and complete validation

MINIX copies boot input into durable VM allocator, `vmproc`, region, and
page-table state. Ordinary operations validate their selected process,
region, bitmap, and page-table path. Optional sanity checks inspect broader
state; the imported transport object is not rehashed before every transition.

`micros` retains the complete boot transport, a 201,552-byte VM state, fixed
mapping records, an immutable initial-free bitmap, copied executable/request
buffers, and one globally serialized transaction. Every public transition
calls `Vsvc` before mutation and most call it again after mutation.

Accepted ADR-0077 currently requires this complete cadence and explicitly
preserves every call site. B07 does not silently override that decision. The
audit finds a conflict between that retained cadence and ADR-0080's MINIX
path-local baseline; any correction must explicitly supersede ADR-0077 and
ADR-0078, preserving their corruption semantics and evidence at the new
explicit diagnostic/acceptance operations and selected transaction-subplan
checks.

ADR-0077 measured and corrected the fixed-capacity inactive-tail
implementation needed to meet readiness. It did not measure or justify the
need to rerun the complete validator at every transition relative to the
MINIX path-local mechanism. It therefore remains current authority until
superseded, but is not sufficient evidence to close the ADR-0080 cadence
finding.

| Difference | Classification |
| --- | --- |
| One VM-owned policy state below service dispatch | Baseline parity |
| Fixed records and exact token/generation/frame-set validation | Compatible extension |
| Complete immutable transport/state audit as an explicit diagnostic | Compatible extension |
| Complete audit on every ordinary transition | Divergence requiring correction |
| Dynamic AVL regions, faults, sharing, paging, and file mappings absent | Staged substitution; SOM-04, SOM-05, SOM-06, SOM-14 |

See B07-F001.

### B07-O03 Physical-frame availability and selection

MINIX owns a current free-page bitmap, a single-page free cache, a remembered
scan position, and chunk-skipping first-fit search. Common one-page allocation
can pop the cache in `O(1)`; worst-case search is bitmap-capacity linear with
word/chunk skipping. Release updates the same bitmap and cache.

`micros` preserves only the immutable initial-free bitmap. Current eligibility
is inferred from mutable frame-state bytes and all mapping records.
`vm_core_select_lowest_frames()` restarts at frame zero and, for each candidate
frame, scans managed ranges and every mapping record/entry.

```text
MINIX one-page allocate:
    O(1) cache common; O(mbitmap) worst scan with empty-word skipping
MINIX 4+u pages:
    O(u) common; O(u*mbitmap) conservative worst
micros selected set:
    O(m * (r + p*e + record_zero_bytes))
```

The deterministic lowest-address choice is a pre-baseline algorithm change,
not a compatible extension. See B07-F003.

### B07-O04 ELF and canonical stack planning

MINIX validates ELF header/program ranges before mutation, allocates/copies
segments through reusable loader callbacks, zero-fills anonymous bytes, and
builds a separate initial stack from captured arguments.

`micros` validates a fixed three-segment RISC-V ELF64 subset, builds an
ascending page plan, decodes the pointer-free spawn object, and builds one
canonical stack page from immutable local input snapshots.

```text
ELF planning:   O(segment_count + image pages)
stack planning/building:
                O(argument count + environment count + string bytes)
```

| Difference | Classification |
| --- | --- |
| Validate complete file ranges before allocation/publication | Baseline parity |
| Separate portable ELF and stack algorithms | Baseline parity |
| RISC-V ELF64 layout and register/stack ABI | Required adaptation |
| Fixed three-segment static ELF, bounded vectors, and one resident stack page | Staged substitution; SOM-04, SOM-06, SOM-17 |
| VFS-to-VM grants plus scratch fill instead of direct target copy | Required adaptation |

### B07-O05 Load preparation, binding, scratch fill, and freeze

The PM-facing VM service copies immutable executable/request grants, plans the
image/stack, selects four table frames plus `u` data frames, obtains a kernel
load token, and processes each page through:

```text
next page
-> kernel scratch map
-> local scratch mapped
-> fill page
-> kernel scratch commit
-> local scratch committed
```

After all pages, VM publishes one freeze plan and mirrors the kernel mapping
generation.

MINIX similarly separates executable planning, allocator policy, page-table
mechanism, and publication, but it does not completely revalidate all VM state
between each page substep.

```text
VM-service complete validations for one load:
    8 + 7*u calls to Vsvc
kernel scratch map+commit per page:
    2*((Vep + Vreg) + Vboot + Vvmrt + Dpm + Rmem + Vkh)
    plus B02 mechanisms
```

The expression above records four direct top-level `Vkh` calls per map/commit
pair. `Rmem` and the B02 all-root paths also reload VM scratch authority:
each command adds one handoff validation through ordinary root validation, one
through `require_vm_scratch_state()`, and one for every rooted process.
Expanded, one map/commit pair performs at least
`(8 + 2*nroot) * Vkh`.

See B07-F001 and B07-F002.

### B07-O06 Kernel operation-12 authority and privileged mapping mechanism

MINIX VMCTL validates the exact VM caller/target and dispatches one privileged
mapping, inhibition, flush, or release operation. VM policy remains in the
service; architecture mutation remains below the adapter.

`micros` validates exact VM process/thread/endpoint/profile generations,
bootstrap/handoff phase, PM reservation, fixed user objects, frame ownership,
scratch/root plans, and portable handoff state before committing one command.

| Difference | Classification |
| --- | --- |
| User-space VM policy over privileged kernel mapping mechanism | Baseline parity |
| Unified RV64 operation 12, exact generations, fixed objects, tokens | Required adaptation |
| Complete preflight and non-failing commit | Compatible extension |
| Complete bootstrap/PM/root/handoff validation on every command | Divergence requiring correction |

The address-space/frame plan quadratic work confirms B02-F008/F009. Kernel
handoff-state cadence contributes B07-F001/F002.

### B07-O07 Freeze, commit, abort, and exit release

MINIX finalizes page tables before publication and releases process regions
and page tables before clearing the VM process slot.

`micros` constructs four page-table frames and user entries at freeze, mirrors
frozen/prepared/activated/active phases, reverses loading/frozen state on
abort, and uses exact exit/load/generation/PID identities before kernel
teardown and local frame/record release.

```text
local selected record:
    direct by process slot for load/freeze/abort/commit
exit begin:
    O(p) token/PID record search
release entries:
    O(e*r) physical-to-frame translation
plus pre/post Vsvc and kernel Vkh/B02 work
```

The fixed process-slot record is a compatible specialization for the current
one-record-per-process model, but the token/PID exit scans and per-entry range
translation are not baseline lookup algorithms. They are included in
B07-F003/F004. General region trees and fault-backed mappings remain
SOM-04/SOM-14.

### B07-O08 Service protocol and caller authority

The VM service accepts only exact PM messages, checks version/shape and one
reply token, validates the complete service instance, dispatches load/abort/
commit/exit, and returns a canonical result.

MINIX VM has a broader request namespace and per-call ACL dispatch, but the
authority direction is the same.

```text
one message before selected command:
    Vsvc through service_instance_is_valid
selected command:
    command-specific additional Vsvc/kernel validation
```

The exact PM-only bounded protocol is a required current-DAG adaptation.
Broader mapping/fault/fork calls remain staged.

### B07-O09 Fault handling and future VM breadth

MINIX inhibits an ordinary faulting process, sends VM a kernel-origin fault
request, resolves region/backing policy, and resumes or signals the process.
VM's own fault remains fatal.

`micros` implements only the fatal VM self-fault rule. Dynamic regions,
ordinary fault delivery/retry, file-backed mappings, reclaim, sharing,
copy-on-write, paging, and recovery are owned by SOM-01, SOM-04, SOM-05,
SOM-06, SOM-09, and SOM-14.

### B07-O10 VM copy, clear, and fixed-object transfer algorithms

MINIX uses architecture/generic `memcpy`/`memset` word/block paths for
allocator state, executable pages, stack state, and kernel copy mechanisms.

`micros` has multiple source-level byte algorithms:

- VM core copy/zero performs eight explicit byte assignments per loop;
- VM image helpers copy/zero byte by byte;
- kernel handoff/snapshot/fixed-object helpers copy/clear byte by byte.

The FNV digest itself is byte-defined and is not a copy-algorithm divergence.
General storage/image movement is. See B07-F005.

## Difference summary

| Area | Result |
| --- | --- |
| Boot import and one-way ownership handoff | Baseline authority with required pointer-free/typed adaptations |
| VM-local allocator/process/mapping authority | Baseline owner, divergent allocation algorithm |
| Complete state validator | Compatible diagnostic, divergent ordinary cadence and pairwise algorithms |
| ELF/stack planning | Baseline algorithm boundary with bounded staged subset |
| Scratch/freeze/commit/abort/exit | Required unprivileged-VM adaptation |
| Dynamic regions, faults, sharing, paging, COW | Staged substitutions |
| Copy/fill algorithms | Blocking divergence |
| Kernel snapshot storage | Blocking auxiliary-space divergence |

## Time, stack, and auxiliary-space envelope

Persistent B07 mechanism storage includes:

```text
VM boot object in VM address space:       364,704 bytes
kernel snapshot object:                   364,704 bytes
VM portable state:                        201,552 bytes
kernel handoff state:                      30,920 bytes
combined fixed storage:                   961,880 bytes
```

The 201,552-byte VM state includes:

```text
64 * 512-byte mapping records:             32,768 bytes
initial-free bitmap:                       32,768 bytes
executable snapshot:                      131,072 bytes
spawn-request snapshot:                     4,096 bytes
transaction and scalar state:              remaining bytes
```

Representative automatic storage is:

```text
VM load-prepare locals:   about 1.5 KiB before nested validation/planning
kernel freeze plans:      1,224 bytes
freeze state plan:          512 bytes
abort action:               456 bytes
exit action:                440 bytes
fixed-object plan:           48 bytes
```

The VM service has one 4 KiB user stack. Load preparation can nest its local
transaction/ELF/frame arrays with complete validation, ELF planning, and stack
decoding. Kernel freeze/scratch/release paths nest their plans with B02-B06
validator and scheduler/IPC frames on a 16 KiB supervisor stack. Neither
complete envelope has direct target stack-usage evidence.

The kernel snapshot is needed only through prepared-state validation and VM
handoff; no post-handoff path uses it as the authoritative database.

## Staged omissions

| ID | B07 relation |
| --- | --- |
| SOM-01 | VM/RS restart, reconstruction, live update, and recovery |
| SOM-04 | Dynamic regions, ordinary page faults, map/unmap/protection, file-backed mappings, reclaim |
| SOM-05 | Persistent file-backed VM integration |
| SOM-06 | Fork, copy-on-write, complete exec/process replacement |
| SOM-08 | Multithreaded address-space and stack policy |
| SOM-09 | Fault-to-signal and asynchronous user-fault semantics |
| SOM-14 | POSIX mapping and shared-memory interfaces |
| SOM-17 | Public POSIX memory/libc interfaces above VM mechanisms |
| SOM-23 | Asynchronous/suspended fault and backing transport where required |

## Evidence gaps

| ID | Missing evidence | Owner |
| --- | --- | --- |
| B07-E001 | No transition counter records `Vsvc`/`Vkh` calls across one load page and complete load/abort/commit/exit protocols | B14 |
| B07-E002 | No allocator probe compares bitmap/cache/chunk visits with managed-frame, range, and record-entry scans | B14 |
| B07-E003 | No comparison counter records boot mapping, VM record-entry, record-identity, and kernel record-overlap pairwise work | B14 |
| B07-E004 | No copy/fill work counter distinguishes byte, word, and block operations for 4 KiB pages and 128 KiB snapshots | B14 |
| B07-E005 | No direct VM-user-stack or kernel-supervisor-stack evidence covers worst nested load/freeze/validation paths | B14 |
| B07-E006 | No evidence proves the kernel's duplicate 364,704-byte snapshot is reclaimed or reused after handoff | B14 |
| B07-E007 | No ordinary page-fault, dynamic region, COW, file-backed mapping, reclaim, or recovery evidence exists for the staged SOM rows | B14 after owning designs |

## Findings register

### B07-F001: Complete kernel and service VM validation precedes ordinary transitions

- Severity: High
- `micros` evidence:
  - `vm_core_validate_state`
  - every `micros_vm_load_*` and exit entry
  - `service_instance_is_valid`
  - `micros_vm_handoff_state_validate`
  - `require_post_handoff_runtime`
- MINIX evidence: path-local VM process, region, bitmap, page-table, and
  VMCTL checks; separate optional sanity paths
- Difference: one local load/page/commit/release step first scans fixed boot
  storage, buffer tails, all managed frames, all mapping records, pairwise
  uniqueness, deterministic frame selection, and cross-owner kernel state.
- Impact: service operations pay `Vsvc`; operation-12 commands pay
  `(Vep+Vreg) + Vboot + Vvmrt + Dpm + Rmem` before command-specific work.
- Smallest correction boundary: retain complete VM validators as explicit
  diagnostic/acceptance operations; ordinary transitions validate only their
  phase, selected process/record/token, requested frames, and consumed
  cross-owner plans.
- Dependencies:
  - an explicit superseding ADR for ADR-0077/ADR-0078;
  - B02-F002/F005/F008, B04-F001, B05-F001, and B06-F002.

### B07-F002: One application load multiplies complete validation per page

- Severity: High
- `micros` evidence:
  - `vm_service_load`
  - `service_fill_pages`
  - load/scratch/fill/freeze public transitions
  - operation-12 scratch adapters
- MINIX evidence: one allocator/page-table/segment path per page without a
  complete VM database audit between each substep
- Difference: one `u`-page load executes `8 + 7*u` complete service
  validations. Each page's scratch map and commit execute four direct complete
  kernel handoff validations plus transitive root/scratch-authority checks,
  for at least `8 + 2*nroot` `Vkh` calls per page pair.
- Impact: a 16-page application can invoke 120 complete VM-service
  validations before abort/commit, compounding fixed-capacity and pairwise
  work.
- Smallest correction boundary: one retained, generation-bound load plan whose
  per-page transition validates the next page and current scratch tuple in
  path-local work. Whole-state validation remains only an explicit
  diagnostic/acceptance operation, not an ordinary load-transaction creation,
  freeze, commit, or abort boundary. B12's cross-owner audit makes this
  narrowing explicit so the correction does not reintroduce B07-F001 at a
  coarser valid-path boundary.
- Dependencies: B07-F001, including explicit ADR-0077/ADR-0078 supersession,
  and B02-F008/F009.

### B07-F003: Frame allocation does not reproduce the MINIX bitmap/cache allocator

- Severity: High
- `micros` evidence:
  - immutable `initial_free_bitmap`
  - mutable `frame_states`
  - `vm_core_select_lowest_frames`
  - `vm_core_physical_is_in_record_other_than`
- MINIX evidence:
  - current `free_pages_bitmap`
  - `free_page_cache`
  - remembered `lastscan`
  - chunk-skipping `findbit`
  - common `O(1)` single-page cache allocation
- Difference: `micros` has no authoritative current-free bitmap/cache. It
  restarts at frame zero and proves each candidate unused by scanning ranges
  and all mapping records.
- Impact: selecting at most twenty frames is
  `O(m*(r+p*e))` rather than common `O(u)` whole-set cache work or bounded
  chunk-skipping bitmap searches. Releasing one record also pays `O(e*r)`
  rather than direct frame-index bitmap updates, and validation recomputes
  selection repeatedly.
- Smallest correction boundary: reproduce a VM-owned mutable free bitmap,
  single-page cache/scan cursor, allocation/release owner, and bitmap-based
  first-fit selection; retain frame indexes in selected records so release
  updates the allocator directly while preserving kernel owner/PTE mirrors.
- Dependencies: B07-F001/F002 and B14 allocator operation counts.

### B07-F004: VM mapping uniqueness is established by pairwise searches

- Severity: High
- `micros` evidence:
  - boot validator's prior-mapping physical scan
  - `record_entry_is_unique`
  - `record_identity_is_unique`
  - service and kernel exit-token/PID record scans
  - kernel `record_overlaps_prior_records`
  - kernel `record_tokens_are_unique`
- MINIX evidence: allocator bitmap, direct `vmproc` slots, region AVL
  ownership, physical-region references, and page-table state
- Difference: boot import is `O(g^2)` for physical uniqueness, while runtime
  service/kernel validators compare record entries and identities against all
  prior records, reaching `O((p*e)^2)`/`O(p^2*e^2)`. Exit begins independently
  scan all `p` records despite exact token/process ownership.
- Impact: complete validation and every transition inherit quadratic
  process/mapping work independent of the selected record.
- Smallest correction boundary: use one bounded frame-index distinctness
  bitmap plus direct process-slot and token/generation indexes for validation
  and exit lookup; pairwise scans remain only in independent diagnostic
  evidence if desired.
- Dependencies: B02-F009 and B07-F001.

### B07-F005: VM image/state movement lacks the MINIX word/block copy family

- Severity: High
- `micros` evidence:
  - `vm_core_copy_bytes` / `vm_core_zero_bytes`
  - VM image copy/zero helpers
  - kernel handoff/snapshot/fixed-object copy/clear helpers
- MINIX evidence: architecture/generic `memcpy`/`memset` word/block paths
- Difference: large executable/request snapshots, 4 KiB page fills, the
  364,704-byte kernel snapshot, and transaction/state objects use explicit
  byte assignments (sometimes eight unrolled bytes) without a word/block
  transfer algorithm.
- Impact: VM load and boot paths move hundreds of KiB with a potentially
  large constant-factor regression.
- Smallest correction boundary: use independently written standard-defined
  RV64 block/word copy and fill primitives with alignment handling and byte
  tails; keep FNV digest byte semantics separate.
- Dependencies: B05-F005/F006, B06-F004, and B14 transfer-work evidence.

### B07-F006: The kernel permanently reserves a duplicate complete VM snapshot

- Severity: Medium
- `micros` evidence:
  - static aligned `snapshot` in `kernel/vm/vm_snapshot.c`
  - separate VM-resident `micros_vm_boot_info`
  - existing allocator/ownership/PTE mirrors
- MINIX evidence: bounded boot input is imported into VM-owned allocator/
  process/page-table state; the kernel does not retain a second full
  per-frame transport database
- Difference: `micros` reserves two 364,704-byte boot objects. The kernel
  snapshot remains in permanent BSS even though post-handoff authority comes
  from the allocator/ledger/PTE mirrors and no post-handoff path consumes it.
- Impact: 364,704 bytes of privileged wired memory remain unavailable to VM
  policy for the boot lifetime.
- Smallest correction boundary: preserve failure-atomic snapshot construction
  and readback while using reclaimable preparation storage or the VM transport
  object itself, retaining only the compact summary/identity after handoff.
- Dependencies: B06 bootstrap preparation and B14 storage/lifetime evidence.

## Unit conclusion

B07 primary-path and operation coverage is complete for the pinned tree. The
audit adds B07-F001 through B07-F006 as blocking findings,
confirms B02-F002/F005/F008/F009, B04-F001, B05-F001/F005/F006, and
B06-F002/F004, and records SOM-01, SOM-04, SOM-05, SOM-06, SOM-08, SOM-09,
SOM-14, SOM-17, and SOM-23.

B07 is **audited but not certified**. Independent review corrected
ADR-0077/ADR-0078 disposition, exit/release lookup ownership, operation-12
validation terms, MINIX allocator bounds, fixed source evidence, transitive
scratch-validation multiplicity, and atomic difference classifications. Final
re-review reported B07 complete. No B07 correction may start during the
read-only audit.

## References

- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)
- [B02 memory audit](minix-complete-baseline-b02-memory.md)
- [B04 endpoint/IPC audit](minix-complete-baseline-b04-endpoint-ipc.md)
- [B05 grant/user-execution audit](minix-complete-baseline-b05-grants-user-execution.md)
- [B06 static-bootstrap audit](minix-complete-baseline-b06-static-bootstrap.md)
- [MINIX VM bootstrap/handoff study](minix-vm-bootstrap-and-handoff.md)
- [MINIX VM service source study](minix-vm-service-source-organization.md)
- [MINIX kernel VM-handoff source study](minix-kernel-vm-handoff-source-organization.md)
- [VM validation timeout root cause](elf-spawn-vm-validation-timeout.md)

# MINIX Selected Grant Validation Study

## Fixed inputs and scope

```text
micros source: 7f7c6fea0898ed95d3c36b8a0a08c549d4bf8b9e
micros tree:   b2aafe85cf11cdabd496b94fe795bca7fadf709f
MINIX source:  4db99f4012570a577414fe2a43697b2f239b699e
```

Both local inputs were checked clean at these revisions. IPC04 #257 is
merged. GRANT01 is the next preferred correction, with merged IPC01 #251
as its actual prerequisite. It owns B05-F001 and B05-F002, not the later
GRANT02 allocation/cancellation indexes or GRANT03 copy algorithm.

The [canonical selected-grant contract](../architecture/selected-grant-validation.md)
owns the proposed replacement. This study is source evidence, not executed
implementation Red/Green.

## Fixed MINIX mechanism

Paths are relative to the local MINIX checkout.

| Source | Established mechanism |
| --- | --- |
| `minix/lib/libsys/safecopies.c:119-171` | Allocate from the grantor's free list, fill a direct entry, then publish validity; return its index/sequence identity. |
| `minix/lib/libsys/safecopies.c:229-272` | Resolve one used ID, invalidate its flags, advance sequence and return the slot to the free list. |
| `minix/kernel/system/do_safecopy.c:42-267`, `verify_grant` | Admit the exact grantor/table extent, decode the index/sequence, copy one selected entry, check its state, sequence, grantee, direction and complete offset/length bounds. Direct verification does not audit all grants or processes. |
| `minix/kernel/system/do_safecopy.c:270-376`, `safecopy` | Verify authority before directional transfer; ordinary faults may involve VM and `CPF_TRY` has separate soft-fault semantics. |

Direct verification and revoke use constant selected work and fixed record
storage. Indirection has a separate bounded-depth mechanism; the direct
`micros` baseline does not gain that authority. MINIX free-list allocation
and endpoint-table invalidation belong to GRANT02's comparison, while
translation/copy chunking belongs to GRANT03.

Kernel-owned records, exact participants, terminal quarantine, non-transitive
scalar grants and all-or-error resident copy remain Accepted `micros`
adaptations. This outcome changes no token packing, sequence, permission,
mapping, wire result or VM-fault policy.

## Actual current cost and call census

The fixed audit's repeated per-record `Vep` cost is historical. IPC01 now
makes public endpoint resolution selected, so current complete grant
validation has two grant-table passes, one complete endpoint/object audit,
and constant selected participant checks per active record:

```text
Vgrant(current) = O(G + Vep(current))
captured ordinary grant request = 2*Vgrant(current) + Vep(current)
                                 + selected operation work
```

An active TTY device lookup adds its complete runtime check, including
another `Vep`. This is still redundant global work even though IPC01 has
removed the older multiplicative per-record audit cost.

The eight direct expensive call sites owned by this correction are:

| Current owner | Replaced boundary |
| --- | --- |
| `kernel/grant/grant.c:534`, `micros_grant_prepare_create` | Complete grant relation before selected participants and lowest-free search. |
| `kernel/grant/grant.c:681`, `resolve_owned_record` | Complete grant relation before inspect/revoke's caller and indexed record. |
| `kernel/grant/grant.c:984`, `micros_grant_prepare_copy_authority` | Complete grant relation before exact grantee, token, grantor, direction and bounds. |
| `kernel/grant/grant_syscall.c:189-190` | Complete grant-runtime and IPC-runtime audits before request decoding. |
| `kernel/grant/grant_syscall.c:64`, `grant_syscall_create_request` | Complete TTY authority after successful create preflight, before MMIO denial and token publication. |
| `kernel/grant/grant_copy.c:181`, `grant_copy_execute` | Complete TTY authority before nonzero copy range planning. |
| `kernel/grant/grant_copy.c:243`, `grant_copy_prepare_remote_plan` | Complete TTY authority before nonzero operation-15 remote planning. |

The following are explicitly retained:

- full `micros_grant_registry_validate` and `micros_grant_runtime_validate`;
- runtime initialization and its unpublished scratch;
- cancellation prepare's complete relation check and cancellation commit's
  complete storage, selection/generation and one-use-plan checks;
- bootstrap and PM's existing runtime-audit callers;
- every existing explicit native/target diagnostic boundary.

The coherent registry remains in `grant.c`, the Accepted source-audit
exception. No new decomposition or generalized authority framework is needed.

## Selected state and precedence implications

An initialized grant registry has one scalar active count and 64 records.
Tokens already index one slot. Existing `validate_record_storage` covers
state, generation/packability, free/quarantined zero authority, permissions
and virtual-range shape. Active records additionally require exact live
grantor and grantee relations; current full validation permits their
`ACTIVE` or `SOURCE_ONLY` lifecycle states.

The complete validator currently proves exact active-count equality.
Constant selected work cannot recompute that equality. Preserve bounded
metadata, selected active-count underflow/overflow guards and the full
diagnostic count, but explicitly stop claiming that a selected success
certifies all unvisited rows.

For example, `grant_test_precedence_record_output` currently increments a
valid count from one to two and expects inspect to fail globally. Its
output-preservation purpose needs an actually invalid selected/header
condition after this correction; the original modest count mismatch must
remain a separate selected-versus-full-diagnostic case, not disappear.

Create still uses the lowest-free scan. It must admit every consumed prefix
record's storage before choosing a free slot, preserve participant-before-
allocation order, and avoid resolving skipped records' participants.
This keeps allocation work visible for GRANT02 rather than hiding a complete
grant audit inside a renamed selector.

Inspect/revoke/copy admit the decoded record's complete storage and stored
participant relation before the supplied caller checks, retaining selected
corruption precedence even for a stale supplied generation. Stored
`SOURCE_ONLY` validity is not permission to use an inactive participant.
The operation's later active-caller/grantor checks still own that result.

## Existing committed-device inspection

`tty_handoff_runtime_validate.c:312-360` implements the old full device getter.
Its full runtime check searches bootstrap binding/transition state, compares
the current TTY lifecycle and scheduler tuple, checks the kernel-operation
profile, and invokes complete endpoint/object validation.

MEM04 already provides
`micros_tty_handoff_runtime_inspect_device_binding` in the same owner.
It checks fixed identity, pure console/route/deadline-field shape and the
selected process/thread/root/profile/scheduler-assignment tuple. It does not
reauthorize bootstrap publication, TTY commands or IRQ policy.

Its current Accepted consumer set is memory-only. GRANT01 must explicitly
extend that set before using it at the three grant exclusion sites.
This is a denial-only use after independently checked grant participants.
Actual device-span PTE validation remains in `grant_copy_plan_device_span`;
normal translation still rejects MMIO/transferable aliases. An ordinary
process's RAM at the same virtual address remains valid.

Consequently, no full TTY command or bootstrap validator needs modification.
The original complete providers remain available. Unconsumed bootstrap
transition/profile/queue corruption no longer dominates an ordinary grant;
malformed consumed binding or actual mapping still fails closed.

## Classification and resource ledger

| Mechanism | Classification | Bound or obligation |
| --- | --- | --- |
| Indexed record plus exact participant/direction/bounds checks | Baseline parity | Constant selected work and scratch. |
| Kernel-owned direct records and scalar RV64 requests | Required adaptation | No mutable user grant table or second kernel-call protocol. |
| Exact generations, quarantine and failure-atomic plans | Compatible extension | Existing publication/retirement order, constant per-record work. |
| Lowest-free search and all-record endpoint cancellation | Divergence requiring correction | Remain explicitly owned by GRANT02; this outcome does not close them. |
| Selected committed-device description used only for exclusion | Required adaptation | Constant binding checks; ordinary mapping authority remains separate. |
| Explicit complete relation/count diagnostics | Compatible extension | Retain complete original scope outside the eight ordinary call sites. |

No persistent type, token, registry, cancellation-plan or user ABI changes.
One private selected-record view needs only the record and two existing
endpoint/process selections; bound that view by 64 bytes.
No heap, cache epoch, global validated flag, capacity witness or new physical
authority is introduced. Existing create/copy plans and stack budgets remain.

Native evidence must execute the real portable authority and captured C
adapter, not substitute a fake validation success. Hardware evidence remains
the existing real grant, syscall, handoff, TTY and composed-service workflows.
Their independent expected results, model count/seed, complete observations
and current stack admission cannot be replaced by candidate output.

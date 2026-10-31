# MINIX Stack-Bound Evidence Study

## Status and provenance

This is EV03 design research under ADR-0080. The local MINIX checkout was
inspected read-only at:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

The current `micros` research base is the separately reviewed EV02 merge:

```text
commit: fc98edd26505ac522d3752ad85f09366e8085f32
tree:   2bf434271aae027b29a0385ddd8febdb84c7a00c
```

No MINIX source, test, identifier inventory, or architecture assembly is
copied. No reference test runner was executed. Compiler help was inspected;
no new executable prototype or frame certificate was authored in design.

## Fixed-reference behavior and authority

| MINIX source and location | Relevant evidence |
| --- | --- |
| `minix/kernel/arch/i386/include/arch_proto.h:7,228-232` | Architecture page-sized kernel-stack unit and CPU-indexed stack-top calculation |
| `minix/kernel/arch/earm/include/arch_proto.h:7,49-53` | The corresponding ARM page-sized unit and CPU-indexed top |
| `minix/kernel/arch/i386/mpx.S:632-649` | Distinct initial and boot stacks; aligned two-page-per-CPU reservation |
| `minix/kernel/arch/earm/mpx.S:269-287` | Corresponding initial, boot, and per-CPU reservations |
| `minix/kernel/arch/i386/arch_system.c:247-256` | Stack-base alignment check and boot-CPU privileged-stack installation |
| `minix/kernel/arch/earm/arch_system.c:101-113` | Corresponding ARM installation |
| `minix/kernel/priv.h:49,68-69` | Kernel-task stack-guard ownership and guard value |
| `minix/kernel/proc.c:1716-1737` | Queue-removal preconditions include the kernel-task guard check |
| `minix/kernel/arch/i386/protect.c:409` | Initial service exec uses a preallocated 64 KiB stack policy |
| `minix/kernel/arch/earm/protect.c:136` | Corresponding initial ARM service policy |

The two-page kernel reservation is an architecture allocation arrangement.
It is not evidence that two pages are freely available to every C chain or
that every possible chain was measured. The guard check detects damage at
one transition; it does not calculate a maximum call-chain depth.

Kernel architecture code owns privileged stack installation. Process
creation owns user stack allocation. Queue and task privilege state do not
turn a user address into privileged stack authority.

## `micros` ownership and algorithms

Accepted ADR-0019 routes traps through a hart anchor. ADR-0022 supplies a
separate static supervisor stack for each thread slot; ADR-0026 retains that
ownership through scheduling and idle pivots. Boot, idle-primary, thread,
and emergency stacks are distinct, even on one hart.

`arch/riscv64/boot.S` reserves the 16 KiB boot stack.
`arch/riscv64/trap.S` reserves the 16 KiB primary and 4 KiB emergency stacks
and allocates the exact 288-byte regular trap frame.
`arch/riscv64/panic.S` allocates the exact 48-byte panic context.
`arch/riscv64/user_entry.S` reserves 128 separate 16 KiB thread slots.
`arch/riscv64/scheduler.S` explicitly pivots to the idle stack.
`lib/runtime/start.S` retains the external user stack;
`lib/runtime/raw_syscall.S` uses no stack.

ADR-0050 bounds an application's initial frame to 2,048 bytes inside its
single 4 KiB stack page. Runtime nesting must fit below its actual initial
`sp`; a 4 KiB allowance for runtime alone would double-count that page.
Static service stacks have no argv/env startup occupancy.

EV03 analyzes the existing compiled mechanisms without replacing them:

1. bind every compiled translation unit and assembly source to one image;
2. join compiler frame artifacts to actual object/linked function identities;
3. enumerate machine calls, then close direct, indirect, and assembly edges;
4. reject non-fixed frames and cycles within one stack epoch;
5. compute the maximum simultaneous fixed-frame sum;
6. add only the entry occupancy belonging to that stack;
7. compare with the external Accepted budget; and
8. independently observe actual RV64 stack use in the named scenarios.

For a closed graph with `V` functions and `E` edges, traversal and longest
path calculation require `O(V + E)` work and auxiliary storage after artifact
decoding. Indirect edges contribute every admitted target, not an average.
Mutually exclusive sequential calls contribute their maximum, not their sum;
simultaneously live caller and callee frames are summed. Unknown recursion
has no finite admitted bound.

The target observer does constant work per compiler observation. Stack
classification uses fixed intervals and direct thread-slot arithmetic, not
a scan of the object table. Per-stack maxima live in test BSS, not automatic
storage. The observer does not allocate, block, call a service, or change IRQ
policy. Its machine frame is zero and its own code is not instrumented.

## ADR-0080 classification ledger

| Difference | Classification | Required mapping and bound |
| --- | --- | --- |
| RV64 trap/context entry instead of x86/ARM save machinery | Required adaptation | Exact Accepted frame offsets, CSR order, and stack ownership |
| Separate process/thread/hart identities and thread supervisor stacks | Required adaptation | Retain ADR-0011/0019/0022; never equate a process with a stack owner |
| Fixed 4 KiB service/application and 16 KiB supervisor budgets | Required adaptation | Retain current MVP budgets rather than import MINIX's service allocation size |
| Compiler/machine-call graph and longest-path evidence | Compatible extension | Development-only `O(V + E)` composition; no additional runtime work |
| Explicit bounded callback-target contracts | Compatible extension | Exact current targets, source binding, and maximum of all targets; unknown edge fails |
| Test-only minimum-SP observation | Compatible extension | Constant primitive work and bounded BSS; no production observer or changed authority |
| Missing current proof and mandatory future B13 admission | Divergence requiring correction | EV03 closes current gaps; STACK13 closes B13 only after node-Q code exists |

The evidence additions are not claimed as MINIX-provided tooling or a
runtime optimization. Artifact size and a measured high-water mark remain
different evidence dimensions.

## Hot paths and failure boundaries

The relevant hot chains include scheduler selection and checked return,
IPC pair/reply/close and completion, grant-copy validation, bootstrap
control, VM load/freeze/rollback, PM spawn/exit/wait, IRQ/TTY processing,
RAMFS initialization and requests, VFS pathname/console/cleanup, and the
complete lifecycle transaction.

IPC suspends a caller's user chain but executes service and supervisor
chains on other stacks. Summing every PM/VFS/VM frame into one stack would
be wrong; omitting the preserved user caller frame would also be wrong.
Each producer and stack epoch is proved separately.

No measurement may convert absent output, unknown target, dynamic frame,
budget excess, stale generation, malformed report, timeout, or panic into
success. A discovered existing excess blocks the evidence gate and requires
a separately reviewed correction; the design does not authorize padding the
budget or silently changing production storage.

## References

- [Accepted ADR-0084](../adr/0084-checked-stack-bound-evidence.md)
- [Stack-bound contract](../architecture/stack-bound-evidence.md)
- [B14-F003](minix-complete-baseline-b14-evidence.md#b14-f003-resident-page-checks-do-not-prove-runtime-stack-bounds)
- [Correction DAG](minix-complete-baseline-findings-and-correction-dag.md)

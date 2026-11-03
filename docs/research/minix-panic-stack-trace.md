# MINIX Panic Stack-Trace Baseline

## Scope and fixed inputs

This is the documentation-only PLAT03 study for B01-F004/B01-E004.
PLAT02 implementation #201 is separately reviewed and merged:

```text
micros runtime reference: e12b2d6c163ac92282d380eff5be419fad7f7a30
micros runtime tree:      53d900f539d0e766b2998ff57aeea990ac25e30a
MINIX reference:         4db99f4012570a577414fe2a43697b2f239b699e
RISC-V psABI reference:  9b2767e2a8851709608c4e1452998ad232d6278b
implementation:         pending separate Accepted design merge
```

The PLAT02 author ran all 49 current commands fresh, the actual fixed-source
terminal Red/current Green, 43 static configurations/112 ELF outputs and
ten observed stack scopes. Independent GPT-6.1 Sol review found no
substantive issue. Reviewed, published and squash trees are identical.
A fresh committed checkout preserves COPY01 and PLAT01 retirement without
author journals. This establishes the prerequisite, not an existing trace.

The MINIX checkout was verified at the fixed revision. The reference code
is evidence, not source to copy. The RISC-V reference is the
[pinned calling-convention document](https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/9b2767e2a8851709608c4e1452998ad232d6278b/riscv-cc.adoc).
Its frame-pointer convention, rather than the x86/ARM offsets, governs the
RV64 adaptation.

## Reference source ledger

All MINIX locations below refer to the fixed revision.

| Source | Evidence |
| --- | --- |
| `minix/kernel/utility.c:22-49`, `panic` | Claim the panic sentinel before reporting; repeated entry resets; print the reason and CPU, trace the current kernel stack, then shut down |
| `minix/lib/libsys/stacktrace.c:13-36`, `util_stacktrace` | Three scalar cursor values, two word loads per visited frame, immediate PC output, null/non-increasing-link termination |
| `minix/lib/libc/arch/i386/get_bp.S:9-11` | Return the current `ebp` without creating another frame |
| `minix/lib/libc/arch/arm/get_bp.S:8-10` | Return the current `fp` without creating another frame |
| `minix/kernel/arch/i386/Makefile.inc:7-16` | Bind the kernel's unpaged frame-register helper and stacktrace object |
| `minix/kernel/arch/earm/Makefile.inc:7-14` | Bind the corresponding ARM objects |
| `minix/kernel/Makefile:8-13,59-63` | ARM/LLVM's explicit `-O0` case and conditional `USE_SYSDEBUG=1` |
| `share/mk/bsd.own.mk:1496-1505` | MINIX debug support defaults on unless its corresponding build option is disabled |
| `share/mk/sys.mk:55-74` | Generic optimization defaults; the displayed AArch64 omit-frame-pointer branch is not the x86/ARM kernel target |

Availability of the debug routine does not prove that every optimized
logical function invocation creates a frame. The source ledger claims the
actual frame-chain algorithm, not a full DWARF unwinder or a guarantee that
inlined, tail-called or assembly-only activations appear as separate rows.

The kernel's process-stack trace inside `panic` is under `#if 0`.
The active baseline traces its own kernel continuation, not an arbitrary
process, a user stack, or every interrupted stack epoch.

## Reference behavior and ordering

The active kernel panic path:

1. checks the already-panicking sentinel and resets on repeated entry;
2. publishes that sentinel;
3. prints an optional reason and the CPU identification;
4. invokes the current frame-chain trace;
5. follows the existing shutdown path.

The trace obtains the current frame register once. While that pointer is
nonzero, it reads the saved PC and previous-frame link, prints the PC, and
then checks progress. A nonzero next pointer no greater than the current
pointer causes the numeric failure sentinel and terminates the loop.
Otherwise it advances to the next pointer. A final newline ends the trace.

The order matters: the current PC is emitted before the non-increasing
link is reported. The function retains no vector of earlier frames,
performs no recursion, and does not sort or symbolize addresses.

There is no explicit depth maximum, owned-memory range, alignment check,
PC-range check, safe-copy boundary or page-fault recovery in that walker.
It dereferences the current pointer before evaluating the next link.
Those omissions are not authority for reproducing an unchecked kernel
memory read in `micros`.

## Logical structures and cost

Let `s` be the number of existing frame records visited before the
reference stops, `d` the accepted explicit depth maximum, and `p` the
maximum UART status observations for one physical character.

| Property | MINIX mechanism | Required `micros` mapping |
| --- | --- | --- |
| Frame topology | Singly linked caller records in the current stack | Same logical chain in one authenticated kernel stack epoch |
| Cursor | Current frame, saved PC, next frame | Fixed scalar cursor and one current record; no retained frame array |
| Frame reads | Saved PC, then previous pointer | Two checked RV64 words, preserving that read order |
| Output | Stream one raw saved PC per record | Stream one separately named fixed-format record |
| Progress | Null ends; nonzero non-increasing link fails | Preserve that order, with earlier safe-read admission and an explicit depth limit |
| Work | `O(s)` time; two word loads per visited record | `O(min(s,d))` traversal with constant-time selected-owner admission |
| Auxiliary space | `O(1)` scalars | `O(1)` runtime state and no allocation |
| Stack | `O(1)` trace helper storage, independent of `s` | Fixed compiler/assembly frames, included in current stack certificates |
| Hot path | Fatal diagnostics only | First admitted fatal report only; no ordinary syscall/IRQ bookkeeping |

There is no distinct average or amortized traversal improvement: every
visited record incurs fixed work. A depth bound caps an otherwise
linear operation; it does not turn traversal into a constant-time
algorithm for an arbitrarily long logical chain.

Formatting adds `O(min(s,d))` fixed-width output. With the retained PLAT02
per-character bound, its worst polling work is
`O(p * min(s,d))`; any exhausted wait terminates through the existing
no-console path rather than continuing a partial trace indefinitely.

## RV64 frame representation

The pinned RISC-V psABI's `riscv-cc.adoc:31-75` makes the frame pointer
optional, but requires `s0/x8` when one exists. It defines the frame
pointer after a prologue as that procedure's entry stack pointer, the
canonical frame address.

For RV64 LP64:

| Address | Eight-byte value |
| --- | --- |
| `fp - 16` | Previous frame pointer |
| `fp - 8` | Saved return address |
| `fp` | Current procedure's entry SP, not the address of the first record word |

MINIX's two positive-offset 32-bit fields therefore cannot be reused as
an RV64 layout. RV64 values are 64-bit, stack alignment remains 16 bytes,
and compressed instructions permit two-byte-aligned return addresses.

Read-only inspection of the already built PLAT02 panic object confirms
64-byte and 80-byte C frames for `micros_panic_report` and
`panic_report_claimed`: each saves `ra` at entry-SP minus eight, saves
`s0` at entry-SP minus sixteen, and sets `s0` to the entry SP.
This is existing-artifact evidence, not a design-phase compiler prototype
or proof for a future changed image.

The current target recipe does not explicitly mandate frame-pointer
retention. The implementation-ready design must make the selected kernel
C frame-chain convention explicit and bind it to actual compiler/object/
linked evidence. It must not assume that a passing source-level test
forces the compiler to retain every required link.

## Existing `micros` failure boundary

The relevant current owners are:

| Owner | Current responsibility |
| --- | --- |
| `arch/riscv64/panic.S`, `panic_context.h` | Original 48-byte direct/trap panic captures and six-argument report transfer |
| `kernel/diagnostic/panic.c` | First-entry wrapper, console seizure, original core/trap records, history, flush/reset/fallback |
| `kernel/diagnostic/panic_control.c` | Independent panic ownership and no-console termination |
| `arch/riscv64/sbi.c` | Sole admission of a failure-shutdown request |
| `arch/riscv64/boot.S` | Fixed boot stack and initial kernel entry |
| `arch/riscv64/trap.S`, `trap_context.h` | Primary/thread stack entry, 288-byte captured context and emergency pivot |
| `arch/riscv64/scheduler.S` | Primary-idle pivot and the separate idle-selection C continuation |
| `arch/riscv64/user_entry.S`, `kernel.ld` | Slot-derived kernel-stack bank and immutable linked section bounds |
| `kernel/objects/kernel_object_runtime.c` | Exact runtime registry and registered boot-hart handle |
| `kernel/objects/kernel_object_hart.c`, `kernel_object_thread.c`, `kernel_object_process.c` | Selected slot/generation resolution |
| `kernel/objects/kernel_object_identity.c` | Constant-time initialization and handle-shape checks |

PLAT02's entry claim precedes the report body. A second entry never reaches
formatting, history or a future trace. The trace must remain inside that
first-body boundary and leave the original five contiguous records,
optional trap record, history and returning-SRST fallback intact.

The existing panic assembly preserves `s0` but does not create a psABI
frame record for its 48-byte capture. That storage remains a real live
frame for stack-budget accounting, even though a frame-pointer walk does
not enumerate it as another linked record. Keeping the capture ABI is
compatible with obtaining the reporter's current frame register separately.

## Stack ownership is not just a RAM range

The system has distinct stack owners and entry epochs:

| Owner/epoch | Immutable allocation | Last possible C-frame CFA |
| --- | --- | --- |
| Boot | `__boot_stack_bottom` through `__boot_stack_top`, 16 KiB | Boot stack top |
| Hart primary, trap dispatch | `__trap_stack_bottom` through `__trap_stack_top`, 16 KiB | Selected primary top minus the 288-byte trap capture |
| Hart primary, idle selection | Same hart-owned primary allocation | Idle-primary top |
| Current thread, trap dispatch | Exact slot in `micros_thread_kernel_stacks`, 16 KiB | Selected thread top minus the 288-byte trap capture |
| Hart emergency | Exact installed 4 KiB emergency allocation | Emergency top |

The existing nested-trap fixture installs its separately named 4 KiB test
stack. Any design admission for that image must name that exact linked
allocation under its existing test flag; arbitrary mutable bounds are not
a second production stack authority.

The thread bank contains 128 independent slots under ADR-0022.
Membership in its two-megabyte address interval does not prove which live
thread generation owns the current continuation. An authenticated
thread trace must retain the selected hart, current-thread generation,
attached-context bounds and slot-derived allocation relation. Process,
thread and hart identities must not be treated as equivalent.

Existing registry, boot-hart-handle and selected process/thread/hart
resolvers provide a constant-work path. They do not require the complete
registry validator or a scan of every represented object. The trace need
not add a second registry, mutable ownership cache or a new bootstrap
lifecycle operation.

### Stop before a non-ABI entry boundary

A trapped user's `s0` is untrusted even after assembly saves it in the
kernel's 288-byte trap frame. It can numerically point inside the selected
kernel stack. Whole-stack bounds alone would therefore permit a forged
continuation into saved trap-register bytes.

The walk must stop at the last C-frame CFA for the authenticated epoch.
It must not follow the predecessor stored by that root frame, interpret
the 288-byte trap capture as frame records, or continue into the
interrupted/user stack. Current `sscratch`, the registered hart anchor and
the actual stack identify trap dispatch versus idle selection; a panic
argument or a supplied trap-frame pointer is not that authority.

The same terminal rule applies to boot, idle and emergency entry: a
register inherited across a raw assembly stack pivot is not proof of a
caller frame in the new epoch. No change to the existing assembly
capture or context-switch ABIs is implied.

## Retained evidence and coupling

Current authority has 3,929 facts in thirteen families, plus the already
committed COPY01 and PLAT01 retirement claims. The panic whole-source
identity is already among PLAT01's twenty retired keys; no new retirement
may be inferred for a different source, recipe, layout or helper.

The PLAT02 literal declarations and existing test outcomes remain
authorities for their original properties. Its target oracle checks the
five core records, exact retained history, actual physical output length,
one failure-reset request, and bounded status observations. The complete
output-length check already includes additional emitted bytes; no
normalization that discards a trace from the physical-write count is
appropriate.

`tools/qemu_smoke_faults.py:_has_complete_panic_report` counts exactly
five lines starting with `MICROS_PANIC`. The trace therefore uses
`MICROS_TRACE_BEGIN`, `MICROS_TRACE_FRAME` and `MICROS_TRACE_END`,
not another `MICROS_PANIC_*` spelling. No old classifier or expected
core-record count needs to change.

Its EV02 snapshot requires exactly the observed UART comparisons plus
declared history movement. New trace-specific counters must not silently
enter that old snapshot or broaden its accepted count vector. Their
enablement, actual word-load sites, producer and capture belong to the
new trace evidence.

The current finite fatal proof has four UART/latch qualifiers and four
infeasible sites. Trace helpers need explicit source/object/linked
preservation and caller/object bindings; adding a function name to a
preserver set is not proof. The new walk must not delete a panic edge,
release an assembly frame, waive a cycle or increase a stack budget.

The following require independently declared, bounded extensions rather
than regenerated old expectations:

- new private source membership and kernel-only compilation inputs;
- the one new call inside the already claimed report body;
- a frame-register/word-load assembly boundary;
- native and target fixture ownership;
- source/recipe inverses before the existing terminal/history projections;
- actual frame-read and fixed-space evidence; and
- complete static-image and unchanged current observed-scope coverage.

## Difference classifications

| Difference | Classification | Reason |
| --- | --- | --- |
| Linked caller-frame traversal and streamed raw PCs | Baseline parity | Same logical structure, order and linear work |
| RV64 words, negative CFA offsets and `s0` | Required adaptation | Pinned LP64 procedure-call convention |
| Current-stack and entry-epoch ownership | Required adaptation | Distinct wired kernel stacks and untrusted user register values |
| Bounds/alignment admission before memory access | Compatible extension | Reject malformed links without speculative reads |
| Explicit finite maximum and terminal stop reason | Compatible extension | Bound diagnostic work without allocation or recursion |
| Separately named fixed-width trace records | Compatible extension | Preserve original diagnostics while making trace evidence deterministic |
| No symbol table, DWARF, process-stack walk or cross-epoch unwind | Baseline parity | The active reference streams its own frame-chain PCs |
| Kernel frame-chain compiler guarantee | Required adaptation | The optional psABI mechanism must be selected and independently checked |
| Reentry or UART failure bypasses further trace work | Compatible extension | Retain Accepted PLAT02 terminal ownership and failure precedence |

This study closes no finding by itself. The
[canonical contract](../architecture/panic-stack-trace.md) specifies
the exact interfaces, frame/error ordering, read boundary, output
grammar, eighteen-case fixture matrix, source authority and acceptance
gates. Those contracts passed independent design review and are Accepted;
implementation remains pending their separate design merge.

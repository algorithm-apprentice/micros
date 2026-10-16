# MINIX Kernel Directory Layout Baseline

## Reference and scope

This is the directory/build responsibility trace for
[ADR-0073](../adr/0073-kernel-directory-layout.md), not a new kernel
mechanism or a source import.

```text
MINIX: 4db99f4012570a577414fe2a43697b2f239b699e
micros: f524a234f7bef07c69f15eba6f388ca5d017625c
micros kernel tree: b9258fb38f34e1b92fab8e64d64ee1d71d980091
```

The local reference HEAD and selected kernel/VM paths were checked read-only.
No reference script was run, no source was copied and no reference change
was made.

## Responsibility trace

`minix/kernel/Makefile:14-41` selects one architecture make fragment, lists
generic clock/CPU/interrupt/entry/process/system owners explicitly, retains
one kernel program/linker boundary and uses repository-qualified
`kernel/*.h` includes. It includes the separate system-call source list
rather than discovering source files through a recursive glob.

`minix/kernel/system/Makefile.inc:1-42` names one explicit system-call
directory and its handler sources. Directory placement does not bypass
dispatch authority: `minix/kernel/system.c:97-160` validates the call number,
checks the caller's kernel-call mask, dispatches its existing handler and
returns through the existing result path.

`minix/kernel/system/do_copy.c:14-16,23-90` uses qualified private kernel
headers and retains caller/endpoint/range validation and the appropriate
copy/error path inside its existing mechanism. Those are behavioral
owners, not permission conferred by the directory name.

`minix/kernel/arch/earm/Makefile.inc:5-7,88-113` names architecture and board
paths, explicit C/assembly inputs and generated context-offset dependencies.
This is not an instruction to import its ARM machinery, object namespace,
library composition or generated-offset protocol into RISC-V `micros`.

## Classification of differences

| Difference | Classification | Required `micros` disposition |
| --- | --- | --- |
| Explicit cohesive source owners and static build membership | Baseline parity | Preserve the already Accepted mechanism/adapter/evidence owners and their exact ordered CMake consumers. |
| RISC-V code remains in `arch/riscv64` outside the generic kernel hierarchy | Required adaptation | Retain the existing RISC-V/OpenSBI/Sv39 separation and assembly contracts; move no architecture implementation. |
| C17/CMake/Ninja instead of the MINIX BSD make fragments | Required adaptation | Keep the existing toolchain, target identities and source sets; no build-system replacement. |
| Fourteen production and sixteen target-evidence directories instead of MINIX's precise tree | Compatible extension | Use the closed current 373-file `micros` partition, not deferred MINIX services or one file per call. |
| Narrow declarations and distinct process/thread/hart objects rather than a broad kernel umbrella | Compatible extension | Preserve all existing `micros` contracts and private dependency directions. |
| Native models, real component fixtures and immutable logical source oracles remain separate | Compatible extension | Move paths, not evidence authority, cases, algorithms, scripts or executions. |
| Stable old runtime source/line diagnostics despite new physical locations | Compatible extension | Use fixed C-only macro prefix maps and unchanged line counts; no source-level `#line` or panic rewrite. |
| Bootstrap allocation/publication exceptions remain in their existing owners | Staged substitution | Preserve the already documented handoff/revocation points; directory changes neither transfer authority nor add policy. |

No behavioral divergence requiring correction is introduced by the directory
task. Any such discovery is a separate reviewed design, not an opportunity
to change behavior while moving source.

## Current feasibility observations

The tracked `micros` inventory is exactly 274 C files and 99 headers, all
flat. The closed candidate partition classifies every member with no
unclassified file. Existing Ninja compilation metadata identifies 21 native
kernel-source consumers; `micros` is the target consumer. Existing private
repository-root includes already exist where most cross-family references
are compiled; four additional native consumers need that same private
operand. No kernel-path test stub exists; the existing architecture
interrupt stub remains at its unchanged path and priority.

Read-only preprocessing of the existing `boot_component_tests.c`, with no
code generation or new source fixture, verified both selected compiler
families' macro-location behavior. LLVM target preprocessing with the
existing file-prefix map followed by a more specific macro-prefix map
reported `canonical-kernel/boot_component_tests.c:107`; Apple host
preprocessing reported `/canonical-native-kernel/boot_component_tests.c:107`.
This establishes the required fixed mapping shape, not runtime acceptance.

The exact ordinary source, include, generated-byte, compiler, metadata and
acceptance requirements are in the
[directory source contract](../architecture/kernel-directory-layout.md).

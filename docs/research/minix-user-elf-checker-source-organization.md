# MINIX User ELF Checker Source-Organization Baseline

## Fixed reference and scope

The reference is MINIX commit
`4db99f4012570a577414fe2a43697b2f239b699e`, verified in the local checkout.
This study read `minix/lib/libexec/exec_elf.c:29-169` directly and uses the
already reviewed
[ELF execution and spawn study](minix-elf-exec-and-spawn.md) and
[user-service runtime study](minix-user-service-runtime.md) for the surrounding
authority and startup traces.

This correction reorganizes the host service-image checker. It does not move
executable policy into the kernel, implement an application loader, or copy
MINIX code. Accepted ADR-0042 and ADR-0050 retain their different service and
application contracts.

## Primary trace

MINIX's `elf_sane`, `elf_ph_sane`, and `elf_unpack` separate header and table
recognition from loading (`minix/lib/libexec/exec_elf.c:29-72`).
`check_header` recognizes the target ELF encoding and program-header shape
(`:80-91`). `elf_has_interpreter` separately examines the interpreter record
and its bounded file data (`:99-125`). `libexec_load_elf` checks the declared
segment file limits before its destructive `clearproc` boundary
(`:127-169`).

The reusable organizational behavior is that decoding representation,
validating executable shape, and performing lifecycle mutation are distinct
tasks. MINIX's loader callbacks subsequently own allocation, copying, and
zero fill; a host image checker owns none of those resources.

The current `micros` tool already decodes bytes into frozen records, reports
structural and service-policy diagnostics, and returns before any build
consumer publishes a generated fixture. Its five-step validation sequence is
header, program headers, sections, symbols, and instructions. Source movement
must retain that sequence, including each helper's early guards and ordered
diagnostic accumulation.

MINIX's broader executable formats, dynamic interpreter support, target
layouts, and destructive replacement do not define the accepted `micros`
service image. Nor is an unchecked arithmetic expression or disabled bounds
check in the reference a behavior to reproduce.

## Difference ledger

Each row uses exactly one ADR-0025 classification.

| Responsibility | Classification | Current authoritative disposition |
| --- | --- | --- |
| Recognize executable headers and validate declared file ranges before consuming executable bytes | Baseline parity | Existing decoder and structural checks remain ordered before fixture publication. |
| ELF64 little-endian RISC-V, fixed entry, exact RX/R/RW service closure, and no dynamic loading | Required adaptation | Preserve the existing ADR-0042 target/toolchain contract rather than importing MINIX's broader loader. |
| Inspect sections, exported linker symbols, undefined names, and full link-image closure on the host | Compatible extension | Preserve the existing development-time safety checks; the host tool acquires no runtime mapping authority. |
| Validate exact RISC-V service entry, `ecall; ret`, fatal service return, and `gp`/`tp` writes | Required adaptation | Preserve the existing bounded decoder and instruction policy; do not replace it with MINIX architecture code or a general disassembler. |
| Frozen Python records, deterministic error lists, byte-level fixtures, and native generator regression hashes | Compatible extension | Preserve current representations, fixture mutability, diagnostics, and build evidence. |
| Separate decoder, structure, service policy, CLI, and private evidence owners | Compatible extension | Apply ADR-0051 without changing the accepted service or application behavior. |

No new staged substitution or divergence requiring correction is introduced
by this source-only task. The later application checker remains a separate
ADR-0050 implementation surface, not a mode added to the service checker.

## Current source evidence

The inspected `micros` baseline is `dfa9d00c2a12923449e9ba06338d79b5b6b1bafb`.

| Source | Physical lines | Functions | SHA-256 |
| --- | ---: | ---: | --- |
| `tools/check_user_elf.py` | 1,001 | 27 | `8fe90e35134f46fbbcb79d3d4ba3444c9d466e845770b4cce4f9a4775a0f6400` |
| `tests/host/test_check_user_elf.py` | 595 | 17, including twelve methods and one nested helper | `1851895d5b2878e8d5433421bea6a1809f26f508e51078d2a4f2a0cc76333437` |

The checker also defines one exception, five frozen dataclasses, and 38
binary-layout/constant/policy objects. `_validate_symbols` is 140 lines;
`build_elf` is 293. Neither receives an unchanged-long exception.

The test entry's `build_elf` is consumed by the runtime and bootstrap fixture
test families, which feed the other service fixtures. Existing byte and
rendered-output identities therefore matter beyond the twelve checker cases.
The canonical contract records the independently measured fixture bytes and
the exact source, import, build, and classification boundaries.

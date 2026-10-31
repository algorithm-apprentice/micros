# Stack Metadata Artifact Boundary Evidence

## Status and scope

This is a design-only prerequisite repair for EV03 under ADR-0033/0084.
The initial implementation is blocked, uncommitted, and abandoned.
Its exact patch is retained outside the repository as reference only.

```text
merged design:       #180
main:                893eed3e35840fd4d2564d4bbc3160a2eb738c53
abandoned tree:      dd93a89e914cf6c46c015975ce79202a0a81288e
abandoned source:    4173b89fe0321a6b1df3545dcaeb70d37c8d02fe051584c3d6b6edcccf1a2c5f
ordinary/metadata:   42 configurations, 84 actual successful builds
required passivity:  FAIL, not waived
```

The prototype's native results are not current corrective-design or restarted
implementation acceptance. No MINIX runtime code, source identity, stack
budget or seed algorithm was changed.

## Measured symptom

The ELF-spawn leaf application images have equal loaded bytes, geometry,
entry and retained runtime symbols, but different complete-file sizes:

| Application | Ordinary bytes | Metadata bytes |
| --- | ---: | ---: |
| Child | 18,760 | 19,640 |
| Init | 19,320 | 20,304 |

LLVM identifies the additional `.stack_sizes` as non-allocatable metadata.
The ordinary leaf has no such section.

The complete ELF-spawn seeds nevertheless differ:

```text
ordinary SHA-256: 2ec53d63dcdcd51e888af3665380b9238dfea058e7f6d546485e9668bac3e974
metadata SHA-256: b8935f7e8e458fb15fe14a344c3e617e9763057df978bda8df1c3c2941ee60f1
```

In `riscv64-elf-spawn-test`, metadata consequently changes RAMFS text and
read-only loaded bytes and the kernel's read-only loaded bytes. Equal
segment sizes do not make unequal bytes passive.

## Existing authoritative path

| Source | Exact dependency |
| --- | --- |
| `tools/generate_elf_spawn_seed.py:_checked_snapshot` | Validates and snapshots `bytes(image.data)`, not only `PT_LOAD` payloads |
| `tools/generate_elf_spawn_seed.py:build_elf_spawn_seed` | Stores the complete init/child snapshots as seeded application files |
| `tools/generate_application_seed.py:build_application_seed` | Validates and stores the complete supplied runtime snapshot |
| `CMakeLists.txt:3123-3139` | Both full ELF target outputs are prerequisites and inputs of the ELF-spawn seed |
| `CMakeLists.txt:3165-3198` | Runtime-probe ELF copy/check precedes application seed generation |

The retained seed is compiled into RAMFS. Different file extents and seed
bytes alter generated constants/data, explaining the RAMFS text/read-only
changes. The kernel then embeds that RAMFS image through the existing catalog.

This is not a timing, QEMU, runtime algorithm, stack-capacity, or MINIX parity
failure. It is a missing artifact representation boundary in the evidence
build graph.

## Required correction classification

The paired metadata-only canonicalization in
[Accepted ADR-0085](../adr/0085-stack-metadata-artifact-boundary.md) is a
**compatible extension** under ADR-0080:

- compile the real target objects with both required frame flags;
- prove the ordinary/metadata leaf's allocatable/runtime equality;
- canonicalize only its unloaded complete-file representation before the
  existing complete-file consumer;
- retain compiler/object evidence and every original seed/runtime contract.

The ordinary file is a same-candidate artifact-transport counterpart, not a
MINIX baseline oracle. It cannot erase a loaded-byte edit or supply evidence
from another source, configuration or toolchain.

## References

- [Accepted ADR-0085](../adr/0085-stack-metadata-artifact-boundary.md)
- [Stack-bound contract](../architecture/stack-bound-evidence.md)
- [Reviewed-design phase barrier](../adr/0033-reviewed-design-before-implementation.md)

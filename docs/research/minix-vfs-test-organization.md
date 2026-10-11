# MINIX VFS Test-Organization Baseline

This note records the MINIX 3 test-organization baseline used to design the
first responsibility-based split of the `micros` VFS test sources. It is an
organizational baseline only. VFS behavior remains governed by
[ADR-0049](../adr/0049-vfs-service-and-application-io-protocol.md) and
[ADR-0050](../adr/0050-post-handoff-elf-spawn.md).

## Reference

The reference tree is the local MINIX checkout at:

```text
/Users/alephling/Repo/minix
```

The inspected revision is:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

The representative sources are under `minix/tests/`. The inventory commands
used for this note were:

```sh
git -C /Users/alephling/Repo/minix rev-parse HEAD
sed -n '1,260p' /Users/alephling/Repo/minix/minix/tests/common.c
sed -n '1,120p' /Users/alephling/Repo/minix/minix/tests/common.h
sed -n '1,180p' /Users/alephling/Repo/minix/minix/tests/Makefile
sed -n '1,220p' /Users/alephling/Repo/minix/minix/tests/test23.c
sed -n '1,220p' /Users/alephling/Repo/minix/minix/tests/test24.c
sed -n '1,220p' /Users/alephling/Repo/minix/minix/tests/test25.c
sed -n '1,220p' /Users/alephling/Repo/minix/minix/tests/test29.c
sed -n '1,540p' /Users/alephling/Repo/minix/minix/tests/test38.c
sed -n '1,220p' /Users/alephling/Repo/minix/minix/tests/test76.c
sed -n '1,220p' /Users/alephling/Repo/minix/minix/tests/test78.c
sed -n '1,360p' /Users/alephling/Repo/minix/minix/tests/test84.c
```

No GitHub or web copy was used.

## Shared harness ownership

MINIX links `common.o` into its ordinary test programs. `common.c` and
`common.h` own cross-test setup and reporting rather than filesystem
semantics:

- `start()` selects a test number, removes stale output, creates one temporary
  directory, enters it, and closes inherited descriptors above standard
  error;
- `quit()` reports the aggregate result and removes the temporary directory;
- `e()` records a numbered failure with the active subtest and `errno`;
- `rm_rf_dir()` and `rm_rf_ppdir()` own bounded temporary-tree cleanup; and
- `system_p()` owns the harness-specific command execution wrapper.

The harness exposes process-global test bookkeeping because that is the
convention of the selected MINIX suite. `micros` does not copy that mutable
global shape. The reusable baseline is narrower: common setup, cleanup, and
failure reporting have one explicit owner, while behavior-specific state
stays with the test that exercises it.

## Behavior-oriented test programs

Representative MINIX programs group filesystem behavior by user-visible
operation rather than by one production implementation file:

| Behavior | Representative MINIX program |
| --- | --- |
| `chdir` and `getcwd` behavior | `test23.c` |
| Directory creation, removal, and traversal behavior | `test24.c` |
| Open and close behavior | `test25.c` |
| Descriptor duplication behavior | `test29.c` |
| Read and write behavior | `test38.c` |
| Interrupted VFS calls | `test76.c` |
| `getdents` behavior | `test78.c` |
| `posix_spawn`, attributes, and file actions | `test84.c` |

This organization is useful even though the programs share the common
harness. A change to descriptor duplication does not require adding cases to
the open/close or directory source, and spawn owns separate helper
executables where another process is part of the evidence.

The top-level `minix/tests/Makefile` reflects the same ownership:

- ordinary programs link the shared `common.o`;
- `test84` remains the spawn scenario owner; and
- `t84_h_spawn` and `t84_h_spawnattr` are separate helper executables rather
  than being embedded into unrelated tests.

## Function-shape limit of the baseline

MINIX's file separation is useful evidence, but its function shape is not a
style authority for `micros`. Some scenarios remain long scripts. For
example, `test38a` spans `test38.c:61-497` and combines many read/write cases
inside one function.

Copying those function bodies would conflict with
[ADR-0051](../adr/0051-responsibility-based-source-modules.md). `micros`
therefore adopts a compatible structural extension:

- keep behavior-oriented source ownership;
- keep one small shared fixture contract;
- retain one public scenario when one ordered story is useful; and
- expose independently understandable setup, action, failure injection,
  verification, and cleanup phases through named helpers.

This extension does not change the MINIX behavior being exercised. It makes
the accepted `micros` task hierarchy visible.

## Current `micros` evidence layers

The current VFS test evidence is broader than the representative MINIX
programs:

- portable white-box state tests inspect fixed process, descriptor,
  open-file, vnode, pending-console, and spawn state;
- byte-exact service tests validate application, RAMFS, TTY, and PM-control
  codecs plus portable dispatch;
- a deterministic 8,192-transition model compares complete production state
  with an independent reference;
- a freestanding application probe validates real IPC, grants, filesystem
  behavior, directory translation, console I/O, and the drain barrier;
- a kernel hook authenticates the reporting thread and validates final
  bootstrap, VM, TTY, grant, PLIC, and readiness state; and
- a host generator validates seven ELF images and renders one exact static
  fixture.

These layers are required by the accepted `micros` architecture. They must
not be collapsed into only MINIX-style black-box programs.

## Difference classification

The classification follows
[ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md).

| Topic | MINIX baseline | `micros` adaptation | Classification |
| --- | --- | --- | --- |
| Behavior grouping | Separate programs own open/close, directory, duplication, read/write, interrupted-call, getdents, and spawn behavior | Native VFS sources are split by process, namespace, regular I/O, directory, console, and spawn behavior while retaining the current executables | Baseline parity |
| Shared support | `common.c` and `common.h` own generic setup, cleanup, and reporting | One VFS fixture family owns grants, RAMFS, TTY, and deterministic seeding behind explicit test-only contracts | Required adaptation |
| Spawn helpers | Spawn evidence uses separate child executables | The current VFS transaction tests stop before child execution; dynamic-child evidence remains in the later ADR-0050 integration fixture rather than being invented by this refactor | Required adaptation |
| White-box state | Ordinary MINIX tests primarily exercise public system-call behavior | `micros` retains complete fixed-table, token, reference, pending-operation, and rollback assertions | Required adaptation |
| Protocol codecs | The selected programs do not separately model every VFS backend wire codec | `micros` retains byte-exact application, RAMFS, TTY, and PM-control service tests | Required adaptation |
| Replayable model | The selected programs do not provide the current complete-state model | `micros` retains the fixed seed, 8,192 transitions, operation trace, and complete-state comparison | Compatible extension |
| Target integration | Programs run inside the full MINIX environment | `micros` retains an isolated launcher/VM/PM/TTY/RAMFS/VFS/application QEMU fixture with exact markers | Required adaptation |
| Function hierarchy | Some MINIX scenario functions remain several hundred lines | `micros` extracts named semantic phases without changing scenario behavior | Compatible extension |

## Derived constraints

The baseline and accepted `micros` contracts imply:

1. Organize tests by behavior, state machine, fixture authority, or evidence
   layer rather than by the former production monolith.
2. Keep one shared fixture family, but separate application/backend grants,
   RAMFS behavior, TTY behavior, and facade initialization.
3. Preserve the existing native executable, CTest, image, and QEMU workflow
   identities. Source decomposition must not multiply test jobs.
4. Keep white-box state, byte-exact codec, model, and target evidence because
   they are required adaptations, not accidental duplication.
5. Give spawn, model, target-probe, kernel-hook, and fixture-generation work
   explicit owners rather than adding all new cases to one source.
6. Do not copy MINIX's mutable global harness or its long scenario-function
   shape.
7. Do not add MINIX compatibility, worker, recovery, or broader POSIX test
   machinery that the current milestone does not require.

## Non-goals

This baseline does not authorize:

- VFS exit-cleanup behavior or new Step 14 assertions;
- changes to any VFS, RAMFS, TTY, PM, grant, or bootstrap protocol;
- new test executables, CTest identities, workflow names, seeds, markers, or
  transition counts;
- replacement of white-box or model evidence with only black-box tests;
- a repository-wide test-framework abstraction; or
- copying MINIX source code into `micros`.

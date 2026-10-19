# Complete MINIX Baseline Audit Contract

## Purpose

This contract defines the complete read-only audit required before any further
runtime implementation or correction work. It applies ADR-0080 to the whole
accepted `micros` runtime baseline instead of discovering divergences while
new features are being implemented.

The audit produces documentation and findings only. It does not edit
production code, executable tests, models, harnesses, build scaffolding, or
generated fixtures.

## Pinned source identities

The `micros` production and evidence source under review is fixed at:

```text
commit: da0bd95148f2704d5347c2af4c64085b90132746
tree:   3b90c7716d69efd68930b3f8ab86a32813bdd6c3
```

The MINIX reference remains:

```text
commit: 4db99f4012570a577414fe2a43697b2f239b699e
```

Later audit-document commits do not change the reviewed production source.
Any production or executable-evidence change invalidates this audit identity
and requires an explicit new baseline decision.

## Hard implementation gate

Until the complete audit is accepted:

- node-Q implementation remains blocked;
- no finding may start a correction design or implementation;
- no Step 16 or later feature design or implementation may start;
- no optimization or refactor may start; and
- the abandoned pre-merge node-Q branch is evidence of discovered defects,
  not an implementation base.

Audit units may merge as sequential documentation-only pull requests. No
correction work begins until the final consolidated findings register and
correction DAG are independently reviewed and accepted.

After the audit, corrections proceed one at a time in dependency order. Each
uses the normal design/implementation pair and is followed by re-certification
of every affected audit unit. Feature work resumes only after every blocking
finding is corrected and the complete baseline is re-certified.

## Closed path inventory

The final audit manifest starts from:

```bash
git ls-tree -r --name-only da0bd95148f2704d5347c2af4c64085b90132746
```

Every path is assigned exactly once to a primary audit unit or to the explicit
non-MINIX exclusion register. Shared files list downstream consumers but retain
one primary owner. Missing and duplicate assignments block audit completion.

### Production path owners

| Unit | Primary production paths |
| --- | --- |
| B01 platform | `arch/**`, `kernel/platform/**`, `kernel/diagnostic/**` |
| B02 memory | `kernel/memory/**` |
| B03 objects and scheduler | `kernel/objects/**`, `kernel/scheduler/**` |
| B04 endpoints and IPC | `kernel/endpoint/**`, `kernel/ipc/**` |
| B05 grants and user execution | `kernel/grant/**`, `kernel/trap/**`, `kernel/execution/**`, `lib/runtime/**` |
| B06 static bootstrap | `kernel/boot/**` |
| B07 VM | `kernel/vm/**`, `servers/vm/**`, `lib/vm/**` |
| B08 PM and spawn | `kernel/pm/**`, `servers/pm/**`, `lib/spawn/**` |
| B09 TTY | `kernel/tty/**`, `servers/tty/**` |
| B10 RAMFS | `servers/ramfs/**` |
| B11 VFS | `servers/vfs/**` |

### Public-header owners

| Unit | `include/micros/` headers |
| --- | --- |
| B01 | `fdt.h`, `panic.h`, `timer.h`, `trap.h` |
| B02 | `bootstrap_memory.h`, `frame_allocator.h`, `frame_ownership.h`, `frame_ownership_runtime.h`, `kernel_address_space.h`, `sv39.h`, `user_address_space.h`, `user_address_space_core.h` |
| B03 | `kernel_object_runtime.h`, `kernel_objects.h`, `scheduler.h`, `scheduler_core.h`, `user_context.h` |
| B04 | `endpoint.h`, `ipc.h`, `ipc_abi.h`, `ipc_buffer.h`, `ipc_core.h`, `ipc_runtime.h` |
| B05 | `application_runtime.h`, `grant.h`, `grant_copy.h`, `grant_runtime.h`, `grant_syscall_core.h`, `runtime.h`, `syscall_abi.h`, `user_execution.h` |
| B06 | `bootstrap.h`, `bootstrap_control.h` |
| B07 | `vm.h`, `vm_bootstrap.h`, `vm_service.h` |
| B08 | `pm.h`, `spawn.h` |
| B09 | `tty.h` |
| B10 | `ramfs.h` |
| B11 | `vfs.h` |

The final manifest must prove that this table names every header present at the
pinned tree exactly once.

### Integration-operation units

These units own cross-owner operations, not additional primary production
paths:

| Unit | Complete operation families |
| --- | --- |
| B12 ELF spawn and application lifecycle | hidden reservation; executable lookup; VM load/freeze; descriptor preparation; execution preparation; activation; normal/fault exit; wait; reverse rollback |
| B13 node-Q accepted design | production init creation; shell session; command lookup/launch; `echo`; `cat`; `ls`; `ps`; shell resource baseline |

B13 audits accepted ADR-0079 and the abandoned candidate findings. There is no
node-Q production source in the pinned `micros` tree.

### Executable-evidence owner

B14 owns:

- `kernel/tests/**`;
- `tests/host/**`;
- `tests/qemu/**`;
- `tools/application_elf_policy.py`;
- `tools/check_application_elf.py`;
- `tools/check_elf_sections.py`;
- `tools/check_user_elf.py`;
- `tools/embed_ramfs_seed.py`;
- every `tools/generate_*.py`;
- every `tools/qemu_smoke_*.py`;
- `tools/run_qemu_smoke.py`; and
- every `tools/user_elf_*.py`.

Each B14 row maps the evidence to one or more B01-B13 operations and states
what structural, algorithmic, complexity, semantic, or target property it can
and cannot prove.

B14 also maintains a secondary evidence-role manifest independent of primary
path ownership. Its authority is the pinned `CMakeLists.txt`,
`CMakePresets.json`, source-set expansion, compile definitions, and generated
input declarations. It contains every path in the union of:

1. every source, header, linker input, fixture input, and generated-input owner
   transitively selected by a registered CTest, a `test-*` workflow, or a
   standalone image evidence workflow;
2. every production path containing a `MICROS_*TEST` or
   `MICROS_BUILD_*TEST` conditional, even when its primary owner is B01-B13;
3. every `arch/riscv64/*_test.S` target entry;
4. explicit in-production bridges such as
   `kernel/boot/boot_component_tests.c`,
   `kernel/trap/trap_test_hooks.c`,
   `servers/pm/pm_service_fixture.c`, and
   `servers/pm/pm_service_self_test.c`; and
5. every generator, checker, runner, protocol, report, probe, and host oracle
   used by those recipes.

The materialized manifest records path, primary unit, evidence roles, compile
definitions, generated outputs, and every consuming CTest/workflow. A path may
have one primary owner and many evidence roles. Audit completion requires zero
recipe-selected or test-conditional paths without a B14 row and zero B14 rows
without pinned recipe, conditional, or explicit-bridge evidence.

### Explicit non-MINIX exclusions

The following paths remain ordinary project/build/process tooling and are not
MINIX runtime baseline candidates:

- `.github/**`;
- `docs/**`, including the audit itself;
- `.gitignore`, `AGENTS.md`, `CONTRIBUTING.md`, and `README.md`;
- `CMakeLists.txt`, `CMakePresets.json`, and `cmake/**`;
- `tools/check_docs.py`;
- `tools/python_host_runtime.py`; and
- every `tools/validation_*.py`.

Exclusion means "not a MINIX runtime mechanism," not "unreviewed software."
These paths retain their ordinary documentation, correctness, and performance
requirements.

## Closed operation inventory

Each production unit must account for every operation family below. A unit may
split a family into finer rows but may not omit it.

Under Accepted ADR-0081, every omitted MINIX/POSIX mechanism also references
exactly one row in
[the staged-omission manifest](posix-staged-omission-manifest.md). That
manifest owns the missing mechanism/interface, current substitute, replacement
point, predecessors, and removal/conformance evidence. "Outside v0.1" is an
ordering statement, not a permanent exclusion.

B01 and B02 were accepted before ADR-0081. This design updates their staged
timer, signal, region, mapping, and fault rows to reference the manifest;
later units apply the same rule during their initial audit.

| Unit | Required operation families |
| --- | --- |
| B01 | firmware entry; FDT parse/canonicalization; UART ownership/output; panic; PLIC; timer program/delivery |
| B02 | physical-frame allocation/free; typed ownership transition; Sv39 map/walk/validate/activate; user-root create/map/freeze/teardown |
| B03 | process/thread/hart allocate/resolve/release; ready-queue enqueue/dequeue/select; accounting; preemption; idle/return |
| B04 | endpoint encode/resolve/publish/close; privilege lookup; sender/receiver queues; send/receive/call/reply/reply-receive; notification; token lookup; deadlock; cancellation |
| B05 | grant create/resolve/revoke/cancel; range planning; checked copy; syscall decode/return; user context entry/trap/return; runtime wrapper |
| B06 | manifest decode/validation/topology; image preparation/rollback; service release/readiness/deadline; launcher sealing |
| B07 | boot-database import/validation; frame selection; mapping/load/scratch/freeze/commit/abort/exit; one-way handoff; fault path |
| B08 | PID/parent/reaper table operations; spawn/wait/exit/rollback; coordinator dispatch; kernel PM control; spawn-request encode/decode |
| B09 | console handoff; UART ring operations; canonical input/echo; output queue; read/write submit/complete/cancel; IRQ claim/complete |
| B10 | seed decode; node lookup/create/mkdir; reference/link lifecycle; sparse read/write; getdents; putnode |
| B11 | process/descriptor/open-file/vnode lifecycle; pathname traversal; mount; regular I/O; directory translation; console forwarding; exit cleanup |
| B12 | every cross-owner successful and rollback transition named above |
| B13 | every accepted init/shell/basic-command operation named above |
| B14 | evidence ownership, oracle independence, operation-count coverage, target authentication, and failure visibility |

For each family, the audit records every meaningful hot path and lookup,
mutation, ordering, rollback, and validation algorithm. "Covered elsewhere"
must name an exact row; it cannot remove the operation from the inventory.

## Audit-unit output

Every B01-B14 unit produces:

1. the exact assigned path list from the pinned tree;
2. the exact MINIX files/functions used as evidence;
3. one row per meaningful operation with logical structures and owners;
4. algorithm steps and mutation order;
5. average, worst-case, and amortized time where they differ;
6. stack and auxiliary-space bounds;
7. valid-path safety/validation overhead;
8. one ADR-0080 classification for every difference;
9. current evidence and evidence gaps; and
10. findings with stable IDs, severity, dependencies, and the smallest
    correction boundary;
11. the exact staged-omission manifest ID for every deferred MINIX/POSIX
    mechanism.

Audit documents do not propose implementation details beyond the correction
boundary needed to make a later design task well scoped.

## Sequential audit order

The only allowed order is:

```text
B01 -> B02 -> B03 -> B04 -> B05 -> B06 -> B07
    -> B08 -> B09 -> B10 -> B11 -> B12 -> B13 -> B14
    -> consolidated findings and correction DAG
```

One audit unit is active at a time. Later units may reference accepted earlier
ledgers but do not revise them silently.

## Audit progress

| Unit | Status | Ledger |
| --- | --- | --- |
| B01 platform | Audited; six blocking findings | [B01 platform audit](minix-complete-baseline-b01-platform.md) |
| B02 memory | Audited; confirms B01-F006 and adds nine blocking findings | [B02 memory audit](minix-complete-baseline-b02-memory.md) |
| B03 objects and scheduler | Audited; six blocking findings | [B03 object/scheduler audit](minix-complete-baseline-b03-objects-scheduler.md) |
| B04 endpoints and IPC | Audited; six blocking findings | [B04 endpoint/IPC audit](minix-complete-baseline-b04-endpoint-ipc.md) |
| B05 grants and user execution | Audited; six blocking findings | [B05 grant/user-execution audit](minix-complete-baseline-b05-grants-user-execution.md) |
| B06 static bootstrap | Audited; four blocking findings | [B06 static-bootstrap audit](minix-complete-baseline-b06-static-bootstrap.md) |
| B07 VM | Audited; six blocking findings | [B07 VM audit](minix-complete-baseline-b07-vm.md) |
| B08 PM and spawn | Audited; six blocking findings | [B08 PM/spawn audit](minix-complete-baseline-b08-pm-spawn.md) |
| B09 TTY | Audited; seven blocking findings | [B09 TTY audit](minix-complete-baseline-b09-tty.md) |
| B10 RAMFS | Audited; six blocking findings | [B10 RAMFS audit](minix-complete-baseline-b10-ramfs.md) |
| B11 VFS | Audited; six blocking findings | [B11 VFS audit](minix-complete-baseline-b11-vfs.md) |
| B12 ELF spawn and application lifecycle | Audited; two blocking findings | [B12 ELF/application lifecycle audit](minix-complete-baseline-b12-elf-lifecycle.md) |
| B13 node-Q accepted design | Audited; seven blocking candidate findings | [B13 node-Q audit](minix-complete-baseline-b13-node-q.md) |
| B14 executable evidence | Pending | N/A |
| Consolidation | Pending | N/A |

## Complete-audit acceptance

The read-only audit is complete only when:

- all B01-B14 unit documents are merged;
- the exact path manifest has zero missing and zero duplicate assignments;
- the independent B14 secondary evidence-role manifest has zero missing and
  zero unsupported assignments;
- the operation inventory has zero untraced rows;
- every difference has exactly one ADR-0080 classification;
- the staged-omission manifest has zero missing, zero duplicate, and zero
  incomplete rows across README, roadmap, DAG, ADRs, and B01-B14;
- every finding appears once in the consolidated register;
- the correction DAG includes every blocking finding and contains no cycle;
- no production or executable-evidence source changed from the pinned tree;
- documentation and structure checks pass; and
- an independent reviewer reports no substantive audit gap.

The correction DAG is a plan, not completion. The baseline is restored only
after every blocking correction is separately designed, implemented, merged,
and re-certified.

## References

- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [Structural transition](minix-structural-algorithm-transition.md)
- [Development DAG](../architecture/development-dag.md)
- [AI-native workflow](../development/ai-native-workflow.md)

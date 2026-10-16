# Kernel Directory Layout

## Status, base and outcome

This is the Proposed exact contract for
[ADR-0073](../adr/0073-kernel-directory-layout.md).
Only `kernel-directory-layout-design` is active. Its paired implementation
stays pending until independent design review, acceptance and separate
merge. No implementation artifact belongs to this phase.

```text
reviewed predecessor: f524a234f7bef07c69f15eba6f388ca5d017625c
predecessor tree: ff3551cba733ab6bcc0f7a8f73722bb70dafbc9f
kernel subtree: b9258fb38f34e1b92fab8e64d64ee1d71d980091
fixed MINIX: 4db99f4012570a577414fe2a43697b2f239b699e
design: kernel-directory-layout-design
implementation: kernel-directory-layout-implementation
```

QEMU implementation #146 is independently reviewed and merged with its
identical reviewed squash tree and obsolete branches removed. Its
pre-squash receipts are historical, never reusable across the new HEAD.
The [fixed responsibility trace](../research/minix-kernel-directory-layout.md)
introduces no runtime-DAG node or new kernel authority.

The measurable outcome is one navigable physical hierarchy for all 373
current tracked kernel files: 274 C sources and 99 headers, with unchanged
basenames, accepted logical owners, bodies, interfaces and evidence.
The 231 production files occupy fourteen directories; 142 target-evidence
files occupy sixteen. No C/header file or compatibility alias remains in
the flat `kernel/` root.

## Closed exact partition

The domain is only the complete tracked `kernel/` file inventory at the
fixed predecessor above. There are no additional suffixes or nested files
in that inventory. Implementation freezes the full literal bijection and
raw identities before source rewriting. The following fixed partition
defines every destination without a new ownership decision.

Keep each basename and append it beneath the destination. In this table,
`stem X` means only `X.c`, `X.h` and `X_*` basenames already present in
that closed inventory; `prefix X` means only current basenames beginning
with that prefix. These are design notation, not runtime globs or permission
to classify a future file.

Apply exact exceptions first: `bootstrap_memory.c` goes to memory,
`trap_test_hooks.c` to production trap, and `trap_test.c` to target trap.
Then apply target rows before production prefixes. Exact root files are
listed explicitly. Every current file matches once after these priorities;
the resulting counts must equal this independent table.

| Destination beneath `kernel/` | Exact current selection | Files |
| --- | --- | ---: |
| `boot/` | Prefixes `boot_`, `bootstrap_`, plus `main.c`; exclude target rows and the memory exception | 31 |
| `diagnostic/` | `panic.c` | 1 |
| `endpoint/` | Prefix `endpoint`, excluding target rows | 12 |
| `execution/` | Prefix `user_execution`, excluding target rows | 4 |
| `grant/` | Prefix `grant`, excluding target rows | 12 |
| `ipc/` | Prefix `ipc`, excluding target rows | 21 |
| `memory/` | Prefixes `address_space`, `user_address_space`, `frame_`; `sv39.c`, `bootstrap_memory.c`; exclude target rows | 29 |
| `objects/` | Prefix `kernel_object` | 12 |
| `platform/` | Prefix `plic`; `fdt.c`, `timer.c` | 6 |
| `pm/` | Prefix `pm_control` | 23 |
| `scheduler/` | Prefix `scheduler`, excluding target rows | 18 |
| `trap/` | Prefixes `trap`, `syscall`, excluding exact target trap; includes `trap_test_hooks.c` | 15 |
| `tty/` | Prefixes `tty_`, `uart_console`, excluding target rows | 20 |
| `vm/` | Prefix `vm_`, excluding target rows | 27 |
| `tests/boot/` | Stem `bootstrap_test` | 3 |
| `tests/endpoint/` | Stem `endpoint_test` | 4 |
| `tests/execution/` | Stem `user_execution_test` | 6 |
| `tests/grant/` | Stems `grant_test`, `grant_syscall_test` | 12 |
| `tests/ipc/` | Stems `ipc_test`, `ipc_ecall_test`, `ipc_syscall_test`, `ipc_syscall_panic_test` | 23 |
| `tests/memory/` | Stems `address_space_handoff_test`, `user_address_space_test`, `frame_ownership_test`, `mmu_test` | 23 |
| `tests/objects/` | Stem `object_model_test` | 3 |
| `tests/pm/` | Stem `pm_service_test` | 6 |
| `tests/ramfs/` | Stem `ramfs_service_test` | 6 |
| `tests/runtime/` | Stems `user_runtime_test`, `user_runtime_image` | 10 |
| `tests/scheduler/` | Stems `scheduler_test`, `scheduler_invalid_test` | 14 |
| `tests/spawn/` | Stem `elf_spawn_test` | 12 |
| `tests/trap/` | Only `trap_test.c` | 1 |
| `tests/tty/` | Stems `tty_service_test`, `uart_console_test` | 5 |
| `tests/vfs/` | Stem `vfs_service_test` | 3 |
| `tests/vm/` | Stems `vm_handoff_test`, `vm_scratch_preemption_test` | 11 |

Directory names do not merge authority. Objects, scheduler, endpoints, IPC,
grants, mappings, VM/PM control, bootstrap and TTY retain their Accepted
private graphs, context owners, automatic lifetimes and commit boundaries.
For example, the existing scheduler/base-object validation direction and
the one address-space scratch owner remain unchanged.

## Preserved source and private includes

Move each ordinary file once, preserving its Git mode. No body, signature,
type, object, initializer, guard, declaration, inline implementation,
statement, comment, storage duration or line count changes. Existing
cohesive exceptions and all completed subsystem migrations remain intact.

The only source-text transport is an exact quoted kernel-private include
operand. Replace its old flat or same-directory private header name with
the new repository-qualified `kernel/<directory>/<basename>` operand.
Every public `micros/...`, architecture, service, host fixture and standard
include keeps its original spelling and order. The include line itself
remains one line, with all other bytes retained.

All 99 private headers move with their existing logical owner. Their
include guards, qualifiers, constants, declarations and existing Accepted
inline bodies remain unchanged. No declaration moves into
`include/micros`, and all supported public headers remain byte-exact.
No new production/test dependency edge is allowed merely because two files
now share a directory. Production continues to reject test-private includes
except existing explicitly guarded hooks.

The target and most native consumers already have the private repository
root include operand. Add that same `PRIVATE` operand only to:

```text
kernel_objects_test frame_ownership_test endpoint_test ipc_model_test
```

Keep existing include order, especially `tests/host/stubs` ahead of the
repository root. Do not add each subsystem directory as a basename search
path, broaden public include propagation or add an old-path include tree.
The three remaining native leaf consumers without root includes
(`fdt_test`, `frame_allocator_test`, `sv39_test`) need no new private operand.

## Canonical runtime locations and build ownership

Old `kernel/<basename>` names remain logical diagnostic/evidence identities;
the table above is their sole physical placement. Preserve target-relative
and native-absolute `__FILE__` values and every `__LINE__` value.

Use exactly thirty fixed C-only macro-prefix maps, one per destination
directory, after the existing compile options:

- target: the absolute new directory prefix maps to `kernel/`;
- native: the absolute new directory prefix maps to the absolute original
  repository `kernel/` prefix.

Keep the existing target file/debug prefix map and all other options.
Do not map architecture, service, runtime, user or host-test source names.
No source-level `#line`, `__FILE__` definition, panic wrapper, response file,
configuration flag or environment normalization is introduced.
Actual preprocessing must independently prove location strings and line
numbers under both selected compiler families; required QEMU panic/trap
records prove the real target behavior.

Apply the native maps only to these 21 existing kernel-source consumers:

```text
bootstrap_control_test bootstrap_test elf_spawn_fixture_test elf_spawn_model_test
endpoint_test fdt_test frame_allocator_test frame_ownership_test ipc_model_test
kernel_objects_test pm_test scheduler_test sv39_test trap_route_test
tty_control_syscall_test tty_fault_test tty_interrupt_test tty_test
vm_bootstrap_test vm_core_test vm_handoff_syscall_test
```

`micros` is the target consumer. Names, source-set variables, source and
link order, conditions, guards, defines, symbols, linker scripts, ELF
policies, page/stack quotas, generated-output paths, CTests, labels,
presets, commands, marker/regex operands and budgets remain unchanged.
Every source/header dependency operand changes only through the fixed map.
There is no source glob, new target, new build directory or duplicate source.

Nine generators currently emit one kernel-fixture include each:

```text
generate_bootstrap_fixture.py generate_user_runtime_fixture.py
generate_vm_handoff_fixture.py generate_pm_service_fixture.py
generate_vm_scratch_preemption_fixture.py generate_tty_service_fixture.py
generate_ramfs_service_fixture.py generate_vfs_service_fixture.py
generate_elf_spawn_fixture.py
```

Only that exact emitted include operand follows its moved header.
Descriptor validation, AST algorithms, configuration/VM objects, image
data, topology, entry symbols and every other emitted byte remain exact.
Before old complete-render vectors or AST hashes run, reverse only the
actual emitted include transport at its frozen location; no regenerated
expected value substitutes for the original independent oracle.

## Actual-source and retained-oracle transport

Implementation freezes the reviewed 373-file raw inventory, every literal
include operand/position, all external consumers, generated-include sites,
complete CMake recipe, all previous source-gate module ASTs and independent
four-field metadata before any rewrite.

Add one bounded shared host leaf
`tests/host/kernel_layout_source.py` and one structural gate
`tests/host/test_kernel_directory_layout.py`. Both are host-unit-only.
The leaf has only standard-library dependencies, the fixed closed manifest,
immutable original hashes/include metadata and the exact physical-owner
migration. It must not import another source gate or create an import cycle.

Existing source gates may keep their frozen logical owner labels. Their
source reads, existence checks and finite family inventories resolve the
one actual current owner through the fixed manifest. While a member is
pending it must exist only at the recorded old path; after migration it
must exist only at the final path. There is no missing-new-file fallback,
duplicate/linked owner, directory scan used to infer ownership, `Path`
subclass, monkeypatch, generated alias tree or general filesystem resolver.

Before an older byte/source parser sees a moved kernel member, reconstruct
its actual old include operands at their frozen positions and verify the
complete original bytes. The body comes from the current physical file,
never an archive, Git, compiler, unchecked old-body literal or expected hash.
Unrelated text, extra directives, changed positions, wrong paths and changed
member bytes fail, not normalize away.

Old reader calls and bounded inventory operations may receive only that
exact identity adapter. A finite AST inverse removes the verified new
import/read binding before comparing each complete old gate module to
its frozen AST. Existing C lexical/CPP/type/body/storage/exit algorithms,
including `tests/host/c_source_records.py`, remain byte-exact. Independent
fixture/model algorithms and previously expected source, recipe, trace,
data, output and AST values are not rebaselined.

Compose directory transport before all earlier source, generated-byte,
recipe and metadata projections. The structural gate verifies complete
old modules and all actual current owners, not just individual expressions.
It does not introduce a C/CMake interpreter or replace behavioral evidence.

## Data-only validation inheritance

Preserve classifier/discovery/execution algorithms. The fixed predecessor
has 88 `PATH_CLASSIFICATION_EQUIVALENTS` rows, 40 with kernel baselines.
For each original row, preserve its baseline and all original aliases in
their exact order, then append, without duplication, the relocated
baseline and relocated versions of its kernel aliases in that order.
For each of the other 333 kernel logical members, append exactly one row
`old logical member -> (new physical member,)`, in original path order.
There are therefore exactly 421 final rows, with the complete original
88-row declaration recoverable by removing only this verified delta.

Flattening new members into existing rows is required because the existing
alias expansion is intentionally one-pass. Do not change it into a
transitive resolver or rely on a new singleton row being discovered after
an old alias was expanded.

Freeze every old member's full-tier Boolean, ordered QEMU set, ordered
slow-model set and ordered image set independently before metadata edits.
Require equality for every new physical path and complete fast/PR/full
plans, not merely that a moved file selects some test. Preserve unknown/
mixed/deleted/rename/untracked fallback and all discovered Git layers.
Do not translate real Git/candidate paths into nonexistent logical files.

Add only the two host-only paths above. Before earlier TTY/Grant/QEMU/Trap
and other planner oracles run, reverse exactly the alias/member additions
and these two host-only inputs. The complete old planner AST and expected
values remain unchanged; no classifier algorithm is removed or rewritten.

The actual new CMake must invert completely to
`730a6dd6e29ff12cf191d3040ae607026da790be7945c94a6966c24f03c42fab`
before updating its one admitted recipe digest. The only extra recipe
changes are the fixed physical operands, four private root-include
operands and the exact native/target diagnostic-map blocks.
Toolchain files, QEMU source family, presets, other recipe declarations,
shared input/manifest/build-impact/collector/execution/evidence algorithms
and all default/configuration rules remain unchanged.

## Failure boundaries and acceptance

There is no runtime state transition, rollback or authority mutation in this
task. A rejected source/include/recipe/metadata identity is a failed author
gate, not a successful compatibility fallback. Unknown or unstable validation
context remains explicitly fresh-only without a reusable receipt.

The implementation begins with structural Red against the absent physical
layout. Independent fixtures must reject wrong/duplicate/linked/missing
members, old-path aliases after completion, altered include scope/bytes,
changed statements/storage/guards, misplaced compiler maps, wrong native/
target prefix values or line results, extra/missing source operands, changed
consumer order, generator payload changes and any lost metadata dimension.
A genuine read-only preprocessor fixture supplements, not replaces, real
panic/trap and existing subsystem gates.

The exact physical-owner migration has 373 entries and may only shrink.
It must finish as a literal empty tuple, with all actual owners real and
all previous completed migrations still empty. No new function-outline
migration is introduced: function/task/storage boundaries do not change.

Move one coherent production/evidence family in each green slice, updating
all incoming includes, matching generator includes, CMake and selection data
in that same slice. Establish the bounded reader/metadata/diagnostic
foundation first, then low-level platform/memory/object/execution owners,
scheduler/endpoint/IPC/grant, VM/PM, bootstrap/trap, and the remaining
TTY/service/spawn evidence. This is one task, not parallel subsystem work.

Use only the narrow existing native/model or real target evidence affected
by the active slice; do not weaken or duplicate its executions.
Before independent review, stage the complete current change, prove no
unstaged/untracked task files, record HEAD/tree and exact commands/results,
close ordinary native/debug/TTY-image outputs through canonical builds,
and execute the complete current fresh author plan. Shared CMake currently
selects 47 commands: native, 38 QEMU, four images, docs and three diffs.

Additionally execute actual bounded native/smoke/TTY-image reuse from that
same original current full producer. Verify every complete log's original
producer/attempt/candidate/path/size/mode/SHA identity. No permanent
ineligibility, old-HEAD receipt or narrower plan waives acceptance.
Then obtain clean complete independent read-only review, squash only the
reviewed head, verify identical tree/main fast-forward, and delete exact
obsolete local/remote branches.

## Non-goals

No behavior or policy redesign, semantic splitting, public API/header move,
basename rename, new state/context, compatibility framework, directory
discovery, recovery, new platform/tool family, model/test/workflow identity,
configuration exception, receipt redesign, init, shell or command work.
Compiler/debug metadata and ordinary object/dependency paths may reflect
real physical locations; runtime diagnostic source/line identities may not.

# Checked Stack-Bound Evidence

## Status, outcome, and non-goals

EV03 is the next sequential evidence foundation after merged EV02 #179.
This Accepted design supplies the implementation contract for B14-F003
under [ADR-0084](../adr/0084-checked-stack-bound-evidence.md).
The fixed-reference mapping is in
[the MINIX study](../research/minix-stack-bound-evidence.md).

The outcome is a fail-closed static stack certificate plus authenticated
target high-water observations for the ten current B01-B12 path families.
It installs mandatory future B13 admission without claiming production
init/shell already exists.

Designs #180, #181, and #182 are merged. The later uncommitted implementation
closed the fatal-call cycle through the finite qualification below, then
rejected an observed VM service stack bound of 4,240 bytes against 4,096.
It is preserved and abandoned pending the separate
[Accepted ADR-0087](../adr/0087-phase-bounded-vm-load-temporaries.md).
The [measured evidence](../research/vm-load-temporary-stack-evidence.md)
does not certify the abandoned implementation.

Non-goals are runtime algorithm changes, larger stacks, new allocation or
guard-page policy, storage promotion, general recursion proofs, LTO,
SMP, fault recovery, profiling in production, telemetry IPC, node-Q code,
and later POSIX features.

The sole production exception is the phase-bounded temporary lifetime
described below. It is not active until ADR-0087 is separately merged;
no other runtime correction is admitted within EV03.

## Current path and scenario register

Each row requires a static certificate and nonempty target evidence. The
workflow names below already exist; their observation variants and the
aggregate stack gate do not yet exist.

| Gap | Current path family | Observation scenario | Stack owners |
| --- | --- | --- | --- |
| B03-E006 | Object, queue, checked scheduler return, completion, idle | `test-qemu-scheduler` | Thread supervisor and hart primary/idle |
| B04-E006 | Immediate/blocked IPC, reply/receive, notification, close, syscall | `test-qemu-ipc-syscall` | Thread supervisor and user callers |
| B05-E006 | Grant syscall, checked copy, context and nested validation | `test-qemu-grant-syscall` | Thread supervisor and user callers |
| B06-E005 | Bootstrap operation 11, readiness, IPC and scheduler | `test-qemu-bootstrap-launcher` | Boot, thread supervisor, launcher/probe user |
| B07-E005 | VM import, load/freeze, scratch and kernel validation | `test-qemu-vm-scratch-preemption` | VM user, thread supervisor and idle |
| B08-E006 | PM coordinator, prepare/abort, activation, exit/wait | `test-qemu-pm-service` | PM/VM/application user and thread supervisor |
| B09-E009 | TTY IRQ, control, UART, grant and request nesting | `test-qemu-tty` | TTY/peer user, thread supervisor and primary |
| B10-E008 | RAMFS seed initialization, namespace/I/O/grant requests | `test-qemu-ramfs` | RAMFS/peer user and thread supervisor |
| B11-E006 | VFS namespace, console, spawn control and cleanup | `test-qemu-vfs` | VFS/peer/backend user and thread supervisor |
| B12-E007 | Composed spawn, rollback, startup, exit and wait | `test-qemu-elf-spawn` | Each PM/VM/VFS/RAMFS/conditional-TTY/application user producer and thread supervisor |

Static closure also includes boot, panic, emergency, and all directly or
indirectly reachable platform/memory functions. It checks every currently
declared QEMU build variant, not only the ten success variants, and every
linked user image of those variants. It also checks every current standalone
image configuration and its checked full-link/seeded-runtime outputs.
Expected-panic images need static
closure even when no high-water success scenario is selected for them.

The additional static-only configurations are
`riscv64-tty-service-image`, `riscv64-ramfs-service-image`,
`riscv64-vfs-service-image`, and `riscv64-application-images`, selected by
the four existing image workflows. In particular, production VFS without
`MICROS_BUILD_VFS_SERVICE_TEST` or `MICROS_BUILD_ELF_SPAWN_TEST` is a distinct
required variant; neither fixture certificate substitutes for it. Static
membership must equal the union of the current QEMU and image workflow
inventories, including the option-free kernel and all their declared ELF
outputs. Identical bytes may share decoding work, not omit an image identity.
The ten dynamic scenarios remain unchanged.

Each current row binds its exact root functions, images, machine-call sites,
and expected live producer roles in a finite declaration. The initial
root inventory contains `_start`, `kernel_main`, `micros_trap_dispatch`,
`micros_trap_nested_panic`, `micros_panic_report`, scheduler idle/select
entries, `micros_service_main`, and application startup/entry/exit entries
where present. The declaration must equal the observed active root set;
a name absent from a variant is not an invented zero-cost root.

## Budgets and stack epochs

| Stack | Capacity | Initial occupancy included in proof |
| --- | ---: | --- |
| Boot | 16,384 | Actual boot-entry frame, then the C boot chain |
| Hart primary/idle | 16,384 | Regular S-origin trap frame when applicable |
| Each thread supervisor | 16,384 | 288-byte regular trap frame plus the complete active C/assembly chain |
| Hart emergency | 4,096 | Emergency-entry and nested-fatal chain; no abandoned primary frames |
| Each static-service/probe user | 4,096 | Startup assembly and the full live user chain |
| Each application user | 4,096 | At most 2,048 startup bytes plus the runtime chain below initial `sp` |

All comparisons are unsigned checked byte arithmetic. An exactly full
budget is not overflow, but a bound exceeding it is a failure. Reports show
capacity, initial occupancy, static use, observed use, and remaining bytes;
they do not invent an additional stack allowance or subtract observer cost.

Calls retain their caller frame. Sequential callees share space and
contribute the largest child bound. Verified tail transfers may release a
caller frame only after machine evidence proves restoration; otherwise
retaining it is the conservative rule.

A new epoch requires the exact stack-switch binding below. A user `ecall`
and kernel entry have different stack owners; IPC does not merge their
chains. A returning S-origin handler preserves its interrupted continuation.
Direct panic adds its 48-byte context and reporter to the live caller chain.
The emergency pivot starts from the emergency top. Idle and first-user
pivots abandon their old C chain and enter the named new owner.

## Artifact and function authority

The implementation uses a finite compiler/build mode:

| Mode | C additions | Observer | Purpose |
| --- | --- | --- | --- |
| Ordinary | None | Absent | Existing production and acceptance behavior |
| Metadata | `-fstack-usage -fstack-size-section` | Absent | Exact fixed frames without allocatable-byte changes |
| Observed | Metadata flags plus `-finstrument-functions-after-inlining` | Test-only leaf | Independent actual-SP observations with its own static proof |

Only the selected RV64 Clang family and existing non-LTO configuration are
admitted initially. Unsupported metadata or a different compiler fails
explicitly. Tool spelling, resolved executable/version, source identity,
complete compile/link argv, generated inputs, objects, and linked image
identities are part of the artifact manifest.

Each function key is `(variant, image, translation-unit, machine-symbol)`.
Local names never resolve across translation units. Global linkage resolves
through the linked image's actual symbol table, not a filename heuristic.
Inlining is represented by the final emitted caller frame; omitted or
garbage-collected functions do not acquire imaginary frames.

The compiler emits `.su` and non-allocatable `.stack_sizes` records. The
parser requires fixed unsigned sizes and `static` classification, joins
every retained C function to both artifacts, and checks agreement. The
identity prefix in `.su` is parsed against that exact translation unit and
its machine symbols; unrestricted suffix matching is forbidden.
Assembly functions have explicit external contracts instead of fabricated
`.su` rows.

Object call relocations and machine instructions identify the call sites.
Linked disassembly/symbols establish their final targets. Every retained
call-like instruction must be either a resolved direct call, a completely
bound indirect call, a verified return, or an explicit assembly/epoch edge.
Unknown `jalr`, cross-function jump, unresolved symbol, hidden helper,
unsupported instruction, or unexplained stack-pointer write fails.

Metadata-only images must have identical allocatable segment bytes, entry,
permissions, and retained runtime symbol values to their ordinary
counterparts from the same source/recipe. Non-allocatable metadata is
additional evidence only. This equality is checked, not assumed from flag
names.

### Complete-ELF artifact boundary

The first uncommitted implementation exposed metadata entering complete-ELF
seed payloads and consequently changing RAMFS and kernel loaded bytes.
Implementation is blocked pending the separate corrective design in
[Accepted ADR-0085](../adr/0085-stack-metadata-artifact-boundary.md).
[The measured artifact path](../research/stack-metadata-artifact-boundary.md)
records the failure; no passivity check is waived.

The correction defines exact same-candidate ordinary/metadata leaf
canonicalization after existing ELF validation and before complete-file
copies/seeds. Both object frame sources remain required. Observed code is
never replaced by an ordinary artifact. Restarted implementation and its
current acceptance remain separate from the abandoned reference.

## Indirect-call closure

A binding names image/variant, caller function, exact machine-call
occurrence, source call expression, callback type/member, and the complete
target set. Source anchors include the whole enclosing compound chain and
the exact initializer/argument providers; moving a call or changing a
provider invalidates the binding. Machine and source call occurrences must
agree after inlining/optimization. Ambiguous joins fail rather than choose
the first matching call.

The current callback groups are:

| Group | Source authority to close |
| --- | --- |
| Kernel user-mapping visitor | `include/micros/user_address_space.h` and all compiled visitor providers in `kernel/memory/user_address_space_mapping.c` consumers |
| PM output translation | `kernel/pm/pm_control_syscall_core.h` and exact runtime/native-target providers |
| PM coordinator backend | All thirteen members in `servers/pm/pm_coordinator.h`; production table in `servers/pm/pm_service_backend.c` |
| VM service backend | All nine members in `servers/vm/vm_service_core.h`; production table in `servers/vm/vm_service_backend.c` |
| VFS I/O backend | All six members in `servers/vfs/vfs_core.h`; production table in `servers/vfs/vfs_service.c` |
| TTY UART bus | Both members in `servers/tty/tty_uart.h`; production bus and exact deferred-write test bus in `servers/tty/tty_service.c` |
| TTY service copy | `servers/tty/tty_service_core.h` and actual service/test provider |
| RAMFS copy | `servers/ramfs/ramfs_core.h` and actual service/test provider |

This table is not permission to accept every function with a matching type.
The implementation declaration enumerates the actual finite providers in
each compiled variant and checks complete observed/declaration equality.
New callback fields, assignments, escaping callback values, casts,
provider mutations, and unbound sites fail. Conditional target-test
providers remain separate from production providers.

The production provider declaration is fixed at the EV02 research base:

| Source | Member or argument to exact provider mapping |
| --- | --- |
| `kernel/vm/vm_snapshot.c` | Mapping visitor: `collect_mapping` |
| `kernel/vm/vm_snapshot_validation.c` | Mapping visitor: `validate_mapping` |
| `kernel/tests/memory/address_space_handoff_test_tty.c` | Target-only mapping visitor: `handoff_test_record_managed_mapping` |
| `kernel/pm/pm_control_syscall_runtime.c` | Output translator: `translate_output` |
| `servers/pm/pm_service_backend.c` | `reserve -> service_backend_reserve`; `abort_reserved -> service_backend_abort_reserved`; `prepare_execution -> service_backend_prepare`; `abort_prepared -> service_backend_abort_prepared`; `activate -> service_backend_activate`; `exit_begin -> service_backend_exit_begin`; `exit_detach -> service_backend_exit_detach`; `release_exited_process -> service_backend_release`; `grant_create -> service_backend_grant_create`; `grant_revoke -> service_backend_grant_revoke`; `grant_copy_from -> service_backend_grant_copy`; `call -> service_backend_call`; `reply -> service_backend_reply` |
| `servers/vm/vm_service_backend.c` | `copy_from -> vm_backend_copy`; `load_begin -> vm_backend_begin`; `scratch_map -> vm_backend_map`; `scratch_page -> vm_backend_scratch`; `scratch_commit -> vm_backend_scratch_commit`; `freeze -> vm_backend_freeze`; `abort_load -> vm_backend_abort`; `commit_load -> vm_backend_commit`; `exit_release -> vm_backend_exit` |
| `servers/vfs/vfs_service.c` | `client_validate`, `client_copy`, `backend_grant_create`, `backend_grant_revoke`, `ramfs_call`, and `tty_call` each bind the same-named provider |
| `servers/tty/tty_service.c` | UART `read -> uart_read`, `write -> uart_write`; copy `service_copy`; target-only deferred UART `read -> read_deferred_uart`, `write -> defer_uart_write` |
| `servers/ramfs/ramfs_service.c` | Copy `service_copy` in its own translation unit |

The finite target-test provider additions are validated only in the variant
that actually compiles them. Native-only backend doubles are not silently
added to an RV64 target set. Observation support's additional call sites
bind only the two exact compiler hooks and their declared zero-frame
assembly; they cannot authorize another callback target.

All possible admitted targets contribute their maximum bound. A
null-check does not supply a target or waive a missing edge.

## Assembly closure

An assembly declaration binds exact source and expanded include identities,
compile definitions, symbol/range, local stack maximum, calls/branches,
return behavior, and stack-owner transitions. Constants derive from
Accepted layout/ownership contracts, not candidate disassembly estimates.
Machine bytes must satisfy the declared stack and control-flow pattern.

The initial production bindings are:

- `arch/riscv64/boot.S`: establish boot top, call `kernel_main`, terminal loop;
- `arch/riscv64/trap.S`: regular 288-byte entry on selected primary,
  dispatch, exact return, and separate emergency pivot;
- `arch/riscv64/panic.S`: both public entries share the same 48-byte common
  allocation; the first entry's jump across symbol ranges must not hide it;
- `arch/riscv64/user_entry.S`: zero local allocation and the exact user pivot;
- `arch/riscv64/scheduler.S`: zero local allocation, exact idle pivot,
  interrupt window, selector/panic calls, and repeat within the idle epoch;
- `arch/riscv64/mmu.S`: declared zero-local-stack MMU operations;
- `lib/runtime/start.S` and `raw_syscall.S`: external user-stack startup and
  zero-stack syscall boundary;
- `lib/runtime/application_start.S`, where compiled: the actual application
  startup pivot and C entry under ADR-0050.

All compiled target-test `.S` sources receive separate finite declarations
for their saved-state/stack roles. The compiled assembly-source set must
equal the declaration; the list above is not an open exclusion for tests.
An added or changed assembly member is blocked until independently bound.

The complete existing target-test assembly-source inventory is:

```text
arch/riscv64/address_space_handoff_test.S
arch/riscv64/grant_syscall_test.S
arch/riscv64/ipc_ecall_test.S
arch/riscv64/ipc_syscall_panic_test.S
arch/riscv64/ipc_syscall_test.S
arch/riscv64/mmu_test.S
arch/riscv64/nested_trap_test.S
arch/riscv64/scheduler_test.S
arch/riscv64/trap_test.S
arch/riscv64/user_address_space_test.S
arch/riscv64/user_execution_test.S
arch/riscv64/user_runtime_test.S
tests/qemu/bootstrap_probe.S
tests/qemu/elf_spawn_report.S
tests/qemu/pm_service_report.S
tests/qemu/ramfs_service_report.S
tests/qemu/tty_service_claim_wait.S
tests/qemu/tty_service_report.S
tests/qemu/user_runtime_probe.S
tests/qemu/vfs_service_report.S
tests/qemu/vm_handoff_probe.S
tests/qemu/vm_scratch_preemption_peer.S
tests/qemu/vm_scratch_preemption_vm.S
tests/qemu/vm_self_fault.S
```

There are nine production/startup and 24 target-test assembly sources at
this base. Per-variant compiled membership is a subset of this exact
33-source set, not a wildcard for future sources. The target raw-register
probe in `tests/qemu/user_runtime_probe.S` retains its 128-byte local frame;
`tty_service_claim_wait.S` retains its 16-byte frame. Register-pattern
tests that deliberately select a test stack require the corresponding
owner/epoch binding, not an ordinary zero-stack leaf classification.

Inline assembly is likewise inventoried. CSR/barrier/MMIO operations with
no stack effect are distinct from saved supervisor `sp`/continuation
operations. Unsupported stack writes or control transfers fail.

## Static composition and publication

For a function with fixed local frame `F`, its conservative same-epoch
bound is `F + max(child bounds)`, or `F` without children. Calls across a
verified epoch produce a separately checked root, not another same-stack
sum. Add trap/panic/startup occupancy only once on its actual owner.

Use a checked iterative graph traversal and longest-path calculation;
host recursion is unnecessary. Work and storage are `O(V + E)` after
bounded artifact decoding. Any cycle within a stack epoch, arithmetic
overflow, missing root, missing frame, incomplete target set, or exceeded
budget fails before executing an observation image.

A certificate has:

- format version and exact candidate/tool/build identities;
- exact current scope and gap ID;
- image, stack owner, root and witness chain;
- local frames, edge kinds, initial occupancy and checked total;
- Accepted budget and remaining bytes; and
- explicit `PASS`, `BLOCKED`, or `FAIL` outcome.

Only a complete successful certificate is publishable. Failed attempts
retain diagnostics and never replace a prior record with success-shaped
partial data. A changed source, index, tool, recipe, object, callback,
assembly contract, or image invalidates the certificate.

### Finite fatal context

Accepted ADR-0086 refines only the current kernel UART panic cycle. It does
not add a general predicate solver, remove ordinary panic calls, change a
stack epoch or implement `PLAT02`.

Actual function keys and frame artifacts remain unchanged. The host analysis
node is `(function-key, qualifier)`, with exactly `UNQUALIFIED` and
`UART_PANIC_READY`. The latter means the exact `uart_console_state` in
`arch/riscv64/uart.c` has valid initialization magic, owner `PANIC` and
`quiesced=true`, with SIE and SEIE clear. It is derived only from the
normal-return and preservation proof below.

#### Closed source and effect register

| Site or function | Permitted proof role |
| --- | --- |
| `kernel/tty/uart_console_core.c:micros_uart_console_panic` | Exact magic check, success stores and stable result; failure has no ready return effect |
| `arch/riscv64/uart.c:uart_panic_seize` | Clear SIE/SEIE, exact console-state argument, failure halt, normal-return UART initialization and state guarantee |
| `kernel/diagnostic/panic.c:micros_panic_seize` | Only its normal continuation after `uart_panic_seize` may qualify subsequent seizure-diagnostic calls |
| `kernel/diagnostic/panic.c:micros_panic_report` | Only its normal continuation after `micros_panic_seize` may qualify subsequent formatting calls |
| `arch/riscv64/uart.c:uart_write` | Preserve ready state through the predicate and MMIO writer; prove only its ownership-failure panic edge infeasible |
| `arch/riscv64/uart.c:uart_write_hex64` | Preserve ready state through its exact `uart_write` prefix and MMIO character calls |
| `arch/riscv64/uart.c:uart_write_character` | Fixed UART MMIO only; no console-state or interrupt-enable mutation |
| `arch/riscv64/uart.c:uart_flush` | MMIO status reads only; preserve console metadata and SIE/SEIE on every normal return, without claiming bounded polling |
| `arch/riscv64/sbi.c:sbi_system_reset` at the reporter's exact reset site | Bind SRST EID/FID/scalar arguments and the pinned SEE normal-return effects below; retain its actual wrapper/caller frames and required returning-call failure continuation |
| `kernel/tty/uart_console_core.c:micros_uart_console_output_allowed` and `micros_uart_console_validate` | Exact read-only predicate over the bound console state |
| `arch/riscv64/interrupt.h` operations used by these functions | Actual CSR/barrier semantics and no hidden UART-state write |
| The two declared compiler observer hooks, in observed mode only | Existing zero-stack ABI, observer-only writes and interrupt/state preservation |

The implementation declaration names every actual source-stage function,
source primitive, whole compound chain, occurrence and machine-call/branch
site. The source stage is the independently reviewed reference, not a
candidate-derived body or measured output. Ordinary and observed site
inventories are separately complete after compiler instrumentation.

PLIC/TTY mirror calls before UART seizure remain unqualified ordinary edges
with all their frames and failure paths. Their success is not UART-state
authority. Failure diagnostics after UART seizure may use the guarantee
because the UART transition is independently proved, not because another
mirror succeeded.

The current console-state fields, owner values and validation branch come
from the exact `kernel/tty/uart_console_core.h`/`.c` contract. The implementation
must bind the actual linked object address and pointer argument; another
same-shaped state cannot provide the fact.

#### Establishment, dominance and preservation

Source ordering is insufficient. Actual object and linked control flow must
prove that each qualified call is dominated by the transition's normal-return
continuation. No incoming branch, switch target, tail transfer or other
entry may bypass it. The transition failure path cannot reach that
continuation. Its unchanged complete source/body/control declaration and
machine register/argument ownership remain required.

Only the named post-transition calls and their exact formatting descendants
may carry ready context. Every intervening call/operation is inventoried.
The proof rejects a console-state write, alias escape, SIE/SEIE enable,
unbound effect or new callee; it never assumes an unknown operation preserves
the fact. Standard entry/exit compiler hooks receive no exception unless their
already required observer-only leaf proof succeeds.

The seizure wrapper's optional diagnostic flush, the reporter's pre-reset
flush, System Reset return continuation, fallback `uart_write`, final flush
and fatal `wfi` loop are part of that complete inventory. A nonreturning
success contract cannot erase the required returning-call failure path.

#### Pinned System Reset return contract

The initial SEE proof is limited to native S-mode on the already selected
one-hart QEMU `virt` platform and its exact default OpenSBI image. The
independent upstream artifact, source pin and inspected effects are recorded
in [the research evidence](../research/fatal-stack-context-evidence.md#system-reset-return-preservation).
The firmware is 275928 bytes with SHA-256
`894e2aef99590fc07ec6c60ab00282b8bc5d5d5bb2a1d0c6ada0c52df24274c0`.
This is an upstream artifact expectation, not a digest learned from the
candidate or a generic compatibility promise.

The proof requires exact existing `sbi_system_reset` register binding:
EID `0x53525354`, FID zero, shutdown type zero, system-failure reason one,
and no supervisor buffer or console-state pointer argument. Its normal
return is to the wrapper's exact `ecall + 4` continuation. The scoped SEE
contract preserves the bound supervisor console metadata, SIE and SEIE;
only the ABI result registers may change. Firmware private M-mode frames
are distinct from the preserved supervisor stack. The actual C wrapper
and every live supervisor caller frame still count.

The fixed OpenSBI return path saves/restores `mstatus` (including SIE),
preserves the relevant supervisor interrupt-enable bits, and updates the
saved return PC/result registers rather than supervisor console storage.
This additionally requires the current closed SBI inventory and no enabled
Supervisor Software Events: OpenSBI's post-ecall event-injection path is not
generic preservation authority. The current kernel uses TIME/SRST, not SSE
registration/enable; a changed inventory, SEE, boot context or injected/
redirected return is unsupported until independently bound.

Resolve and recheck the actual configured default firmware through the
existing closed QEMU data/configuration boundary. No user firmware override,
unsealed file, same-version filename, different bytes or missing proof can
substitute for the pinned image. Unknown/changed firmware or memory/CSR
effects fail before qualifying the fallback; they do not skip a required
stack row or authorize rebaselining the expected artifact.

Model both nonreturning success and every normal-return failure continuation.
Do not condition preservation on the ignored error/result value or treat an
unexpected success-shaped return as a pass. The unchanged
`MICROS_TEST_FAILURE sbi-system-reset-returned` output, final flush and fatal
wait remain mandatory and cannot become successful QEMU completion.

Native state evidence covers valid magic with each ordinary owner and
quiescence state, invalid owner values, and invalid magic. Only the exact
normal-return success relation establishes ready fields. The existing
failure spin is recorded as nonreturning, not as a successful reset or proof
of bounded polling.

#### Exact edge disposition and composition

In `UART_PANIC_READY`, the complete predicate must prove that the exact
`console-owner-violation` branch in `uart_write` cannot reach its call to
`micros_panic_entry`. Record that single source/machine site as infeasible
with its proof references. In `UNQUALIFIED`, retain that edge unchanged.
No other direct/indirect/assembly edge is removed by this rule.

Use the existing fixed frame for each qualified node and retain callers across
both ordinary and qualified invocations. A qualifier transition is not
`RESET`. In particular, an ownership failure may retain an ordinary
`uart_write` frame through panic and a second ready-context `uart_write`.
The 48-byte panic context remains live exactly once per actual invocation.

Compute the same conservative longest path over the finite qualified graph.
With at most two nodes per actual function, work/storage remain `O(V + E)`.
Every remaining same-epoch cycle, unknown frame/edge, overflow, budget excess
or undeclared qualifier still fails.

Native source/machine/ABI controls cover a returning reset with preserved
state, missing flush/reset effects, changed console writes or SIE/SEIE,
different EID/FID/arguments, changed firmware bytes or origin, SSE/injected
return and bypassed transition dominance. Retain both live UART frames and
the fallback chain; a rejected firmware contract must never silently switch
to an unqualified cyclic success certificate.

Each certificate additionally records qualifiers, exact establishing sites,
preservation/guard evidence and the complete infeasible-edge disposition.
Observed/declaration equality rejects omitted, extra, duplicated or borrowed
proofs. No partial certificate publishes successfully.

This closes neither recursive-panic runtime ownership nor corrupt-console,
stuck-UART, trace or shutdown findings. Those remain at their existing
`PLAT02`/`PLAT03` outcomes. All ten current stack rows, both metadata/observed
graphs, future B13 admission and the complete ordinary author gate remain
mandatory after restart.

## Phase-bounded VM preparation

Accepted ADR-0087 addresses the observed 4,240-byte VM chain without changing
the 4 KiB budget or either compiler mode. It admits exactly one private
helper in `servers/vm/vm_load.c` after separate design merge.

`micros_vm_load_prepare` keeps its existing public signature. Its preflight
remains null checks, `vm_core_validate_state` with the existing staged-input
expression, the IDLE check, and `prepare_source_shape_is_valid`, in that
order. It retains only input parameters and the scalar error needed to
propagate results.

`vm_load_prepare_transaction` takes the same eight arguments and owns the
existing transaction candidate, ELF plan, application-stack plan,
`table_frames` and `data_frames`, including their initial zeroing and
temporary scalar work. It executes the unchanged original interval from
`prepare_binary_plans` through publication of `MICROS_VM_LOADER_INPUT_COPIED`.
It propagates every original preparation error. Success returns `OK` to the
facade, which then returns the result of the unchanged final
`vm_core_validate_state(state, false)`.

The helper cannot call complete state validation, recurse, publish pointers
to its workspace, introduce static/heap/TLS storage, or change cleanup,
frame selection, copying, field assignment or phase order. The facade cannot
retain the aggregate workspace. Local initializations may move only with
the declarations into the helper, before their first read.

Source evidence reconstructs the exact original function from these two
actual bodies through one finite declared expansion. It retains the original
independent source expectation; expansion is not stack evidence and cannot
erase an arbitrary runtime change. Compiler `.su` and `.stack_sizes`,
actual linked call/return instructions, and the full graph independently
prove that the helper frame has ended before final validation. The helper
and facade frames are both counted during preparation.

Native load/staged-input/capacity/rollback cases preserve the original result
and complete state. Both compiler-mode graphs, all current variants,
ordinary image passivity against its same-candidate metadata counterpart,
and all ten observed scenarios remain required. No observation cost is
subtracted. An excess on any other chain remains a blocker.

This narrow storage correction does not implement or retire VM01-VM06,
change the correction graph, or open EV04 early. It is the only exception
to EV03's production-edit non-goal and remains inactive until the separate
ADR-0087 design merge.

## Target high-water observations

The RV64 observer supplies only the compiler's entry/exit hook ABI in
explicit observed images. Entry uses caller-saved registers only, does no
stack allocation, and performs bounded unsigned maximum/count updates in
test BSS. Exit is a zero-effect leaf. Both hooks are uninstrumented, have
no C/runtime dependency, and cannot alter interrupt or continuation custody.

Supervisor classification uses the fixed boot/primary/emergency intervals
and direct thread-slot arithmetic. User classification uses that
executable's exact external stack interval. A range/alignment violation or
counter overflow is sticky failure. Recording cannot repair the stack.
No live stack bytes are overwritten with a paint or canary pattern.

Every observer-producing image has its own data object. Executing shared
runtime code in PM and VM does not share one observation. The kernel's
record distinguishes each actual supervisor stack slot. Before reuse,
target evidence closes the old generation and starts a new producer label;
it may not silently combine two generations into one live owner.

Each dynamic record binds scenario, executable identity, exact process or
thread generation where applicable, stack interval, sample count, observed
use, and the separately computed static bound. At least one sample is
required for each declared live producer. Observed use must fit both its
physical interval and the observed image's static upper bound. The host
does not infer an expected maximum from current observation output.

Assembly-only peaks are accounted statically. A sample proves only the
actual `sp` at its compiler hook; it does not claim every instruction or
every branch was sampled. Current scenarios exercise nested C paths whose
ancestor assembly allocations are included in the samples.

Target reports are captured only at declared quiescent fixture boundaries.
Service user records are inspected through the kernel fixture's existing
privileged, generation-checked address-resolution mechanism, using the
checked ELF's observation-object offset. The offset is data, not user
authority. Children that exit must be captured before execution/mapping
teardown; their records are then immutable retained evidence. Reused slots,
partial reads, wrong ELF identity, missing producers, and post-teardown
lookup fail.

The existing semantic scenario, grants, reply ownership, cleanup,
readiness deadlines, markers, serial ordering, UART drain, and shutdown
remain required. Observation records appear outside exact legacy record
groups. Timeout, panic, unexpected exit, malformed or absent reports, and
nonzero exit cannot become a high-water pass. There are no timing sleeps,
automatic retries, deadline increases, or host-kill success.

## Build, transport, and command boundary

Implementation adds a single explicit aggregate workflow intended to be
named `test-stack-evidence`. Its fixed dispatcher:

1. captures and rechecks the exact clean staged candidate;
2. builds ordinary and metadata-only variants for every current declared
   QEMU and standalone-image configure preset and all their declared
   kernel/user/full-link/seeded-runtime ELF outputs in separate directories;
3. validates artifact/edge closure, byte equality, and all current budgets;
4. builds separately declared observed variants for the ten scenario rows;
5. checks their static closure before launching each exact QEMU command;
6. authenticates complete target records and legacy scenario acceptance; and
7. publishes the ten current outcomes and blocked future B13 record.

The dispatcher accepts scope selection only from its closed declaration,
not arbitrary commands, extra compiler flags, hand-authored success files,
or a path subset that hides an active variant.
It invokes existing target recipes with explicit evidence mode only; it
does not borrow stale output from another build directory or share a
mutable cache with ordinary workflows.

The existing planner remains the coverage authority. Full/milestone and
affected stack/assembly/callback changes select the aggregate gate.
Unknown/shared changes remain full-tier. Host parser/graph/observer tests
join existing native discovery. Ordinary workflows, the old recipe
checksums, and their current reusable profiles remain required.

Any new CMake module or compile-time fixture/report hooks have an exact
independent artifact/finite transport declaration. Projection removes
only the declared evidence additions; it cannot erase a runtime edit or
regenerate an old expected hash. Observer absence is checked in ordinary
kernel, service and application outputs.

The future command and mode names in this section are design interfaces,
not claims of implemented targets. They become discoverable commands only
in the separate implementation PR.

## B13 admission and downstream use

Current scope emits exactly one `BLOCKED` B13-E007 record with reason
`production-node-q-absent` and completion barrier `STACK13`. It cannot be
treated as a B13 certificate or as a missing current path.

After NODE03, the same gate's separately selected node-Q scope requires
actual init/shell/command roots, compiler/indirect/sort-recursion contracts,
PM snapshot/client paths, application startup occupancy, and complete
target command evidence. Unknown or unbounded sort recursion still fails;
its later Accepted algorithm design must supply a finite depth contract.
`STACK13` and `RECERT01` cannot pass with the blocked record.

Every subsequent correction changing frame size, call depth, callbacks,
assembly, entry occupancy, or stack ownership reruns the affected stack
rows and their downstream composed consumers. A certificate does not
survive a changed graph merely because an older scenario passed.

## Implementation slices and acceptance

The implementation is blocked until the separate ADR-0087 corrective design
is Accepted and merged. It then restarts test-first from current main.

1. Native Red/Green: artifact identity and fixed-frame graph composition,
   including sequential-versus-nested work, recursion, missing edges,
   dynamic frames, overflow, duplicate local names and epoch negatives.
2. Native/target Red/Green: exact indirect and assembly closure, real RV64
   metadata/byte-equality checks, the exact phase-bounded VM preparation
   correction, leaf observer ABI and default absence.
3. Target Red/Green: all current rows, pre-teardown producer capture,
   authenticated complete reports, blocked future admission, aggregate
   build/planner integration, and complete current author gates.

Focused negative tests must fail without the implemented guard. Existing
linters, compiler warnings, native suites, image checks, QEMU gates,
documentation and staged/worktree/branch-range checks remain required.
Independent implementation review inspects the exact complete validated
tree and does not routinely rerun those gates.

If current code exceeds a budget, record the concrete witness and block
EV04. Do not waive the proof or enlarge EV03 with an unreviewed runtime
correction. No current gap is closed before its static and dynamic
evidence passes on the final implementation.

## References

- [Accepted ADR-0084](../adr/0084-checked-stack-bound-evidence.md)
- [Accepted VM temporary-lifetime correction](../adr/0087-phase-bounded-vm-load-temporaries.md)
- [VM stack failure evidence](../research/vm-load-temporary-stack-evidence.md)
- [Fixed MINIX study](../research/minix-stack-bound-evidence.md)
- [B14 evidence](../research/minix-complete-baseline-b14-evidence.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)
- [Testing strategy](../testing-strategy.md)
- [Authoring workflow](../development/ai-native-workflow.md)

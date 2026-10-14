# Trap and Syscall Source Organization

This is the exact design contract for the six trap/syscall correction sources
in [the source-module audit](source-module-audit.md).
[ADR-0061](../adr/0061-trap-and-syscall-source-decomposition.md) is Accepted
after a clean independent design review. Implementation remains pending until
the separate design merge.

## Base, outcome, and non-goals

```text
reviewed micros base: 09271b80de386870b7ecadcf0368576e46da56ae
fixed MINIX reference: 4db99f4012570a577414fe2a43697b2f239b699e
design task: trap-syscall-decomposition-design
implementation task: trap-syscall-decomposition-implementation
```

The predecessor is the completed source audit; the selected current base also
contains the reviewed bootstrap correction, PR #120. This is the next selected
high-risk dependency-ready maintenance outcome, not a new runtime DAG node.

Expose the existing trap/call algorithm and user-runtime evidence through
cohesive owners and short semantic phases. The
[fixed MINIX trace](../research/minix-trap-syscall-source-organization.md)
defines responsibility direction; existing Accepted behavioral ADRs remain
authoritative.

No ABI, authority, privilege, identity, frame, syscall number/result, scheduler,
timer, interrupt, fault, bootstrap, console, image, runtime, allocation, retry,
recovery, directory, validation-reuse, or concurrency behavior changes.
Application startup/exit, dynamic PID-1 integration, grant/endpoint/object/
scheduler decomposition, and QEMU/ELF-checker tooling remain separate tasks.

At the reviewed base:

| Source | Physical lines | Definitions | Longest function |
| --- | ---: | ---: | --- |
| `kernel/trap.c` | 1,710 | 22 | `micros_trap_dispatch`, 924 |
| `kernel/syscall.c` | 302 | 4 | `micros_syscall_handle_user_ecall`, 180 |
| `kernel/user_runtime_test.c` | 1,232 | 29 | `micros_user_runtime_runtime_run_self_test`, 157 |
| `tests/qemu/user_runtime_service.c` | 799 | 15 | `run_server`, 242 |
| `tests/host/runtime_test.c` | 553 | 9 | `test_grant_wrappers`, 169 |
| `tools/generate_user_runtime_fixture.py` | 201 | 5 including one nested helper | `render_fixture`, 136 |
| **Total** | **4,797** | **84** | **Nine functions above 120 lines** |

These are review signals, not a reason to fragment cohesive leaf algorithms.
Every one of the nine long functions receives the semantic split below.

## Stable surfaces

Keep these eleven headers byte-for-byte unchanged:

```text
include/micros/trap.h
include/micros/runtime.h
include/micros/syscall_abi.h
kernel/syscall.h
kernel/trap_route_core.h
kernel/user_runtime_test.h
kernel/user_runtime_image.h
tests/qemu/user_runtime_protocol.h
lib/runtime/raw_syscall.h
lib/runtime/memory.h
arch/riscv64/trap_context.h
```

Keep these cohesive implementations and architecture artifacts byte-exact:

```text
kernel/trap_route_core.c
lib/runtime/runtime.c
lib/runtime/memory.c
lib/runtime/memory_symbols.c
arch/riscv64/trap.S
arch/riscv64/trap_test.S
arch/riscv64/nested_trap_test.S
arch/riscv64/user_runtime_test.S
tests/qemu/user_runtime_probe.S
lib/runtime/start.S
lib/runtime/raw_syscall.S
lib/runtime/user.ld
```

The first structural gate records their reviewed-base hashes, never expected
hashes derived from the edited worktree. Preserve existing public signatures,
assembly symbols, frame/status layouts, ELF options/checker, and all delegated
IPC/grant/bootstrap/VM/PM/TTY interfaces. Those subsystems are not decomposed
again by this task.

## Trap responsibility owners

| Final owner | Responsibility |
| --- | --- |
| `kernel/trap.c` | Retained install, dispatch, and nested-panic entries; short origin/phase orchestration and generic unexpected-trap fatal boundary |
| `kernel/trap_context.c` | Existing CSR reads, primary-stack/frame validation, exact routed-hart resolution |
| `kernel/trap_user.c` | Ordered user accounting/capture, user route phases, ordinary final return and post-return phases |
| `kernel/trap_interrupt.c` | Existing scheduler-timer result mapping, user external/timer phases, direct supervisor interrupt handling |
| `kernel/trap_syscall.c` | Ordinary user-ecall bridge, normal/captured selector choice, existing return-failure classification |
| `kernel/trap_fault.c` | Bootstrap service and TTY fault records; exact VM self-fault identity, state, record and fatal phases |
| `kernel/trap_test.c` | Existing armed supervisor trap/SUM tests, their sole context, self-test and panic-test entry points |
| `kernel/trap_test_hooks.c` | Stateless compile-time bridges to existing component hooks at their original phases |
| `kernel/trap_internal.h` | Declarations-only private constants, context types and narrow phase/helper contracts |

The three retained public entries stay in `trap.c`:
`micros_trap_install`, `micros_trap_dispatch`, and
`micros_trap_nested_panic`. Generic unexpected-supervisor-exception reporting
stays at the real retained dispatch call site: the current trap-panic gate
requires `kernel/trap.c`, the complete trap-context record, and its unchanged
eight-second timeout. Do not spoof source locations. Any other source-bound
diagnostic expectation must follow its actual owner without weakening its
reason, record, instruction label, or timeout.

Move `read_sscratch`, `read_stvec`, `read_tp`, `read_sstatus`,
`frame_is_on_primary_stack`, and `resolve_trap_hart` to context/common.
Move `handle_scheduler_timer_error` to interrupt handling.
Move `fail_active_bootstrap_service_trap`, `record_tty_owner_failure`, and
`handle_vm_self_fault` to fault handling.
The seven `trap_test_*`/`trap_panic_test_*` validation/return helpers and the
two existing public trap-test entries belong to `trap_test.c`.

### Required routing order

The facade resolves the exact hart, primary frame, zero `sscratch` sentinel,
and live kernel `tp` before classifying cause/origin. Invalid routing preserves
the existing boot-hart fallback and trap-aware panic.

User routing preserves this sequence:

1. scheduler user-to-kernel accounting;
2. the user-execution fixture's existing pre-capture hook, then complete
   user-frame capture;
3. early scratch, RAMFS, and VFS fault hooks in that order and under their
   current non-timer/non-external/non-ecall conditions;
4. VM self-fault classification, then active-bootstrap service failure for
   the same remaining unexpected user traps;
5. user-external TTY dispatch and ordinary selected return, with its existing
   owner-fault-before-panic boundary;
6. user-timer pre-hooks, scheduler timer handling, then post-handler hooks;
7. bootstrap, VM, PM, TTY, and user-runtime fixture fault handling;
8. IPC-core finish, grant control, and address-space-handoff fixture routing;
9. ordinary production ecall routing only under the existing exclusions;
10. the current mutually exclusive invalid-scheduler, scheduler,
    user-execution, or unexpected-user fallback;
11. ordinary selected-user return; and
12. the scratch and IPC post-timer-return hooks.

Do not simplify the existing predicates, nested preprocessor branches, or
fixture fallthrough while moving them. In particular, RAMFS may fall through,
VFS has its current ignored return, scratch mismatch is fatal, and
user-runtime may return either an already selected user frame or the prepared
supervisor continuation.

The ordinary ecall bridge preserves before-hook order (runtime, IPC, grant),
the single production dispatcher call, after-dispatch order (runtime, grant,
IPC buffer-loss panic hook), exact normal/captured selector choice, and
after-return order (scratch, IPC, grant). Its failed selector retains the
current bootstrap running/sealed diagnostic choice, then TTY owner failure,
then `ipc-syscall-return`. It must not read a newly cached selector or
operation where the current code observes the returned frame.

The address-space-handoff fixture retains its separate control-ecall and
ordinary-ecall branches, before/after-dispatch/after-return checks, selector
choice, and distinct panic strings. It does not call the ordinary fixture
handler merely to deduplicate similar code.

Supervisor routing performs existing address-space/MMU and armed trap-test
hooks before interrupt/fatal classification. An S-origin external interrupt
accounts supervisor entry and dispatches TTY without replacing its interrupted
frame. An S-origin timer uses the scheduler path when initialized, otherwise
the current timer-result switch. Unknown/inactive/overflow/rearm outcomes keep
their exact fatal boundaries. No S-origin path becomes a user scheduling
point.

Hook bridges return only whether their current path already handled the
frame. They introduce no registry, callback table, runtime test selection,
new kernel state, retry, or recovery. Before/after hook helpers do not call
each other or move component implementation out of its existing owner.

### Trap-test storage

Consolidate only the existing three trap-test statics into one file-local
context in `trap_test.c`: volatile `trap_test_state`, volatile
`trap_test_count`, and nonvolatile `trap_test_entry_sstatus`. Preserve their
types, zero/idle initialization, two armed phases, exact count, register
patterns, SUM checks, and build guards. No ordinary kernel mutable trap
singleton or global current process/thread is added.

The nested-panic entry retains its existing emergency-stack-local probe and
outer-frame validation. The architecture entry/restore and injected-fault
assembly remain unchanged.

## Top-level syscall ownership

| Final owner | Responsibility |
| --- | --- |
| `kernel/syscall.c` | Retained public entry, scalar capture/PC advancement, ordered orchestration and operation dispatch |
| `kernel/syscall_decode.c` | Existing bootstrap/VM/PM/TTY early shape checks, VM command/union decode |
| `kernel/syscall_context.c` | Existing fatal helper and exact object/hart/current-thread/process preflight |
| `kernel/syscall_internal.h` | Declarations-only private phase contract |

Preserve the actual current precedence:

```text
null argument invariant
-> sepc overflow invariant
-> capture a0..a7
-> advance sepc exactly four once
-> bootstrap, VM, PM, TTY early decode in that order
-> authoritative object/hart/current-thread/process resolution
-> operation-specific existing handler or unknown-operation ARGUMENT
```

Malformed known control shapes return `ARGUMENT` before current-context
resolution. Unknown operations still reach current-context resolution before
their result. Context corruption for the four control operations retains the
running-bootstrap authority failure before `syscall-current-invariant`.

`capture_arguments` stays with the facade, `decode_vm_handoff_request` belongs
to decode, and `panic_syscall` belongs to context/fatal handling. Decoded
requests/union and the syscall context remain automatic; no retained request,
second registry, authorization cache, new profile check, or duplicate
operation policy is added. Delegated handlers retain argument/result ownership.

## Kernel user-runtime evidence

| Final owner | Responsibility |
| --- | --- |
| `kernel/user_runtime_test.c` | Sole non-returning entry, immutable profiles, exact supervisor register capture and entry/failure outline |
| `kernel/user_runtime_test_state.c` | Sole existing-state context, byte primitives, SATP read and exact current-actor lookup |
| `kernel/user_runtime_test_image.c` | Page/segment/stack access, copy/verify, configuration and writable snapshots |
| `kernel/user_runtime_test_setup.c` | Baseline/mechanism/process preparation and exact scheduler admission phases |
| `kernel/user_runtime_test_trap.c` | Before-ecall/after-dispatch hooks, held peers, rodata recovery and service-return trap |
| `kernel/user_runtime_test_cleanup.c` | Exact endpoint/grant cancellation, process teardown, complete baseline and finish orchestration |
| `kernel/user_runtime_test_report.c` | Existing success/failure state records, reset/fallback reporting |
| `kernel/user_runtime_test_internal.h` | Declarations-only record/context/phase contract |

One private context consolidates exactly these seventeen current statics:

```text
processes, threads, endpoints, images,
writable_snapshots, writable_snapshot_valid,
endpoint_registry, grant_registry,
baseline_owned, baseline_free, baseline_processes, baseline_threads,
baseline_harts, rodata_fault_seen, loader_validated, final_client_send,
returned_actor
```

Preserve all original types/bounds and zero initialization except
`returned_actor`, whose initial value remains `USER_RUNTIME_PROCESS_COUNT`.
The `mapped_page` and `process_image` records keep their fields/layouts.
The fixture remains three processes, at most eight image pages each, and one
external stack page. Do not increase bounds to accommodate outlining.

The three assembly ABI globals `micros_user_runtime_test_saved_sp`,
`micros_user_runtime_test_saved_gp`, and
`micros_user_runtime_test_saved_tp` remain distinct `uintptr_t` definitions
in the original entry source. They are explicit ABI exceptions, not context
fields. Their inline `sp`/`gp`/`tp` capture stays in the original
`micros_user_runtime_runtime_run_self_test` invocation, immediately before
scheduler start and after the same validation/admission. A returning helper
must never supply the saved supervisor stack.

State/common owns `clear_bytes`, `fill_bytes`, `copy_bytes`, `bytes_equal`,
`bytes_are_zero`, `read_satp`, and `current_actor`.
Image owns `segment_permissions`, `find_page`, `allocate_image_pages`,
`copy_image_segments`, `verify_loaded_pages`, `translate_range`,
`patch_configuration`, `snapshot_writable_pages`,
`writable_pages_match_snapshot`, and `read_configuration`.
Setup owns `prepare_process`. Trap owns `hold_other_threads` and the three
existing before/after/trap entry points. Cleanup owns
`configurations_complete`, `close_endpoint`, `release_process`,
`endpoint_registry_is_empty`, `scheduler_is_idle`, and the public finish entry.

Preserve process/endpoint setup for all actors before image preparation;
poison, complete zeroing, copy, permission verification, patch/snapshot,
context preparation and `fence.i`; server/peer/client scheduler admission
order; the final-client-send priority adjustment; exact rodata fault/resume;
held-peer supervisor return; and all cleanup stages, actor details, and
resource comparisons. Success-reset return still falls through to the
existing failure report; do not manufacture a success or alter reset fallback.

## Standalone user-side evidence

| Final owner | Responsibility |
| --- | --- |
| `tests/qemu/user_runtime_service.c` | Sole service entry, original startup register observation and role dispatch |
| `tests/qemu/user_runtime_service_common.c` | Seven exported image objects, byte/message helpers, BSS/compiler-support checks |
| `tests/qemu/user_runtime_client.c` | Caller/grantor script with automatic canaries and grant/message state |
| `tests/qemu/user_runtime_responders.c` | Coupled server reply/grantee and peer-notification scripts |
| `tests/qemu/user_runtime_service_internal.h` | Declarations-only image/automatic role/helper contract |

Retain `startup_checks` and its local `sp`, `gp`, and `tp` reads in the entry
source; preserve its position before the rodata probe and role script.
Move `run_client` to client and `run_server`/`run_peer` to responders. These
are protocol phases, not a general service framework.

Move these seven definitions together to common, preserving exact symbols,
types, qualifiers, initial bytes and sizes:

```text
micros_user_runtime_test_config
micros_user_runtime_test_rodata
micros_user_runtime_test_data_sentinel
micros_user_runtime_test_read_region
micros_user_runtime_test_write_region
micros_user_runtime_test_server_region
micros_user_runtime_test_bss_probe
```

They are existing ELF/probe ABI objects and must not become context fields.
The `test_region` layout and 32-byte transfer/64-byte BSS bounds remain exact.
Common owns the original ten byte/message/BSS/memory helpers.

Client/server message, reply, token, grant, index, and volatile canary locals
remain automatic for the original role invocation. They may form minimal
automatic role contexts containing only those existing values. Preserve the
two existing immutable expected-byte tables without adding a mutable role
registry or hoisting automatic messages/canaries.

Preserve raw-register probe before the two calls, exact replies and token
use, peer wake/notification before combined reply/receive, grant directions
and validation before copies, all canaries, revoke/stale retry, status/failure
codes, final-client-send ordering, server return, and the client's/peer's
terminal receives. Do not reorder short-circuit calls or strengthen/weaken
the current acceptance subset.

## Native wrapper evidence

| Final owner | Responsibility |
| --- | --- |
| `tests/host/runtime_test.c` | Retained `main` and exact four-case short-circuit order |
| `tests/host/runtime_test_fixture.c` | Sole existing raw capture, fake raw symbol and fixture primitives |
| `tests/host/runtime_wrapper_test.c` | Existing IPC/grant/console/compiler-memory cases and semantic assertion phases |
| `tests/host/runtime_test_internal.h` | Declarations-only capture/case/helper contract and existing assertion macro |

The existing `raw_capture` remains one private static instance with exactly
`arguments`, `result`, `calls`, `write_argument`, `expected_input`,
`input_snapshot`, `output`, and `input_observed`. A narrow private borrow
may expose that same fixture to cases; there is no second capture or new
mutable test registry. `reset_capture`, `arguments_match`, `message_pattern`,
and `micros_runtime_raw_syscall` belong to fixture.

Retain this order:

```text
test_ipc_wrappers
test_grant_wrappers
test_bootstrap_launcher_console_begin
test_memory_support
```

All four cases move to wrapper evidence. Preserve every eight-register
argument assertion, errno sentinel, input/output snapshot and canary, grant
success-only output, null-output zero-call check, full-width address/length/
offset values, descending negative-result loop, and exact memory lengths and
return pointers. This remains `runtime_test` / CTest `user-runtime` in its
existing native tiers, with no new executable, model or CTest.

## Nine required long-function splits

| Original function | Lines | Required named phases |
| --- | ---: | --- |
| `handle_vm_self_fault` | 143 | Exact fault/current identity; runtime/service/ownership classification; record; running versus sealed fatal outcome |
| `micros_trap_dispatch` | 924 | Routed context; U/S accounting/capture; ordered fault/interrupt/fixture/ecall phases; exact selected return or direct supervisor return; generic fatal fallback |
| `micros_syscall_handle_user_ecall` | 180 | Arguments/PC/capture; ordered early control decode; authoritative current context; exact operation dispatch |
| `micros_user_runtime_runtime_run_self_test` | 157 | Image/runtime/baseline preflight; mechanisms; all identities; images/contexts/fence; exact admission; original-frame register capture/start; unchanged failure |
| `run_client` | 203 | Raw-register send; two call/reply checks with retained canaries; grants and copy result; revoke/stale retry; complete status and terminal receive |
| `run_server` | 242 | Initial receive/first reply/second call; peer wake and combined reply/receive; grant preflight/copies; stale retry/final completion |
| `test_ipc_wrappers` | 162 | Ordered send/receive/call; reply/combined exchange; notification and failing-call preservation |
| `test_grant_wrappers` | 169 | Create success/error/null output; revoke/copy/validate marshalling; full negative-result/errno loop |
| generator `render_fixture` | 136 | Validated ELF/symbols/loads and permission subset; segment-byte rendering; image record/symbol fields; unchanged concatenation |

No unchanged long-function exception is granted. A move, rename, or formatting
compression does not discharge an entry. Both active migrations must be empty
before final validation.

The generator remains one module/CLI. Preserve `_format_bytes`, `render_fixture`,
`parse_arguments`, `main`, options, error prefix, output behavior, all ten
fixture symbols/fields, segment order, includes, and generated image symbol.
For the existing checked synthetic fixture, complete output is 53,812 UTF-8
bytes with SHA-256
`7a9432c01e562e89d270452df361e0610796c4cbb107a2bce9f1384b7b6a60ea`.
Identical checked inputs must render identical complete bytes.

## Fourteen symbol migrations

The first gate records exactly fourteen moving function symbols:

```text
trap.c -> trap_test.c:
  micros_trap_run_self_test
  micros_trap_run_panic_test
user_runtime_test.c -> user_runtime_test_trap.c:
  micros_user_runtime_test_before_ecall
  micros_user_runtime_test_after_dispatch
  micros_user_runtime_test_handle_trap
user_runtime_test.c -> user_runtime_test_cleanup.c:
  micros_user_runtime_test_finish
user_runtime_service.c -> user_runtime_client.c:
  run_client
user_runtime_service.c -> user_runtime_responders.c:
  run_server
  run_peer
runtime_test.c -> runtime_test_fixture.c:
  micros_runtime_raw_syscall
runtime_test.c -> runtime_wrapper_test.c:
  test_ipc_wrappers
  test_grant_wrappers
  test_bootstrap_launcher_console_begin
  test_memory_support
```

The paths above retain their original directory prefixes. Private cross-file
role helpers may gain their family prefix; the gate tracks semantic owners
rather than permitting duplicated old/new definitions. All other existing
helpers have the exact owners listed above.

## Build ownership and dependency direction

Define these five complete ordered source sets, with each listed C source once:

```text
MICROS_TRAP_SOURCES
  kernel/trap.c
  kernel/trap_context.c
  kernel/trap_user.c
  kernel/trap_interrupt.c
  kernel/trap_syscall.c
  kernel/trap_fault.c
  kernel/trap_test.c
  kernel/trap_test_hooks.c

MICROS_SYSCALL_SOURCES
  kernel/syscall.c
  kernel/syscall_decode.c
  kernel/syscall_context.c

MICROS_USER_RUNTIME_KERNEL_TEST_SOURCES
  kernel/user_runtime_test.c
  kernel/user_runtime_test_state.c
  kernel/user_runtime_test_image.c
  kernel/user_runtime_test_setup.c
  kernel/user_runtime_test_trap.c
  kernel/user_runtime_test_cleanup.c
  kernel/user_runtime_test_report.c

MICROS_USER_RUNTIME_SERVICE_TEST_SOURCES
  tests/qemu/user_runtime_service.c
  tests/qemu/user_runtime_service_common.c
  tests/qemu/user_runtime_client.c
  tests/qemu/user_runtime_responders.c

MICROS_RUNTIME_HOST_TEST_SOURCES
  tests/host/runtime_test.c
  tests/host/runtime_test_fixture.c
  tests/host/runtime_wrapper_test.c
```

`micros` expands trap and syscall sets once, retaining the unchanged route-core
and architecture sources. Only its current `MICROS_BUILD_USER_RUNTIME_TEST`
branch adds the kernel test set, existing generated fixture and unchanged
supervisor-resume assembly. The existing standalone runtime test ELF uses the
complete service set plus the same runtime/probe sources. `runtime_test` uses
the complete native set plus unchanged runtime/memory and launcher-console
wrapper sources. No raw split-source listing remains beside a set.

Context/common is below route/interrupt/fault/test owners; user/facade code
orchestrates them. The syscall bridge uses the existing dispatcher and
selectors, not a sibling interrupt/fault handler. Hook bridges call existing
component APIs and lower context/fault primitives, never the new `trap_user`
or `trap_syscall` orchestration. The address-space-handoff fixture's existing
ordinary-ecall branch still calls the supported production syscall dispatcher
and scheduler selectors directly. Armed supervisor-test state stays separate
from stateless cross-component hooks.

Kernel runtime-test state is below image, setup, trap, cleanup and report.
Setup may use image; trap/cleanup/report may inspect existing image/config
state; cleanup may invoke report. Report never invokes cleanup or setup.
Service common is below client/responders/entry; client and responders never
invoke each other. Native fixture is below cases and main. Production routing
does not include the new kernel runtime-test, user-side, or native evidence
private headers. Test bridges retain existing component-test APIs behind their
current build guards.

Cross-file helpers use `trap_`, `syscall_`, `user_runtime_test_`,
`user_runtime_service_`, or `runtime_test_` prefixes and one private declaration.
Local helpers remain static. New private headers contain no functions,
inline algorithms, object definitions, textual C includes, or new executable
macros; retain only the existing native assertion macro. Check recursive
local includes, not only the entry's direct includes.

## Validation inheritance and acceptance

Preserve exact current per-path metadata:

| Original owner | Full tier | Workflows | Separate slow/image metadata |
| --- | --- | --- | --- |
| `kernel/trap.c` | yes | All current 37 QEMU workflows | empty / empty |
| `kernel/syscall.c` | no | Its exact current 20-workflow union | `test-ipc-model` / empty |
| `kernel/user_runtime_test.c` | no | `test-qemu-user-runtime` | empty / empty |
| `tests/qemu/user_runtime_service.c` | no | `test-qemu-user-runtime` | empty / empty |
| `tests/host/runtime_test.c` | no | `test-qemu-user-runtime` | empty / empty |
| `tools/generate_user_runtime_fixture.py` | no | `test-qemu-user-runtime` | empty / empty |

The syscall owner's twenty-workflow union is exactly:

```text
test-qemu-uart-console
test-qemu-grant-syscall
test-qemu-user-runtime
test-qemu-bootstrap-launcher
test-qemu-vm-handoff
test-qemu-pm-service
test-qemu-vm-scratch-preemption
test-qemu-tty
test-qemu-ramfs
test-qemu-vfs
test-qemu-vm-ready-early
test-qemu-vm-self-fault
test-qemu-vm-self-fault-sealed
test-qemu-bootstrap-ready-timeout
test-qemu-bootstrap-manifest-panic
test-qemu-ipc-ecall-core
test-qemu-ipc-syscall
test-qemu-ipc-syscall-panic
test-qemu-user-address-space
test-qemu-address-space-handoff
```

Every new family source/header inherits its owner's exact classification.
Do not narrow native evidence to host-only or narrow test-guarded trap owners.
The new structural gate itself is explicitly host-unit-only. Aggregate CMake/
shared trap ownership still requires the complete final matrix; changing
validation execution/reuse policy is not part of this correction.

The implementation begins with the planned structural Red gate
`tests/host/test_trap_syscall_source_layout.py`. It fails closed over the exact
25-source/five-header/five-set inventory and all consumers, reviewed-base
stable hashes, fourteen function moves, seven image-object owners, retained
entry/assembly symbols, exact helper/context fields and lifetimes, recursive
dependencies, nine long-function semantic migrations, expanded route/call/
hook/selector/error ordering, source-bound panic evidence, case/protocol
scripts, generator identity/bytes, and validation inheritance.

Narrow extraction evidence uses the existing native `user-runtime`, trap,
timer, nested-trap, MMU, user-execution, captured IPC/grant, VM self-fault,
console, and real runtime workflows as appropriate to the changed phase.
Preserve the user-runtime marker exactly, its 12-second timeout, three actor
identities, instruction symbols, all forced register/stack/memory checks and
the post-cleanup resource baseline. Other workflow timeouts and markers do not
change.

Before independent implementation review, stage the complete change, prove
clean tracked/untracked task state, record HEAD/tree/exact commands/results,
and run one authoritative PR plan. Current shared trap/CMake ownership requires
complete native validation, all 37 QEMU workflows, the three existing service
image workflows, documentation, and three diff checks. Do not separately repeat
selected plan workflows. The independent reviewer inspects exact author
evidence without routinely rerunning suites, builds, models, QEMU or docs.

## Implementation sequence

After the separate design merge, start a fresh implementation branch:

1. structural Red, exact source/private skeletons, owner inheritance and
   initial fourteen/nine migrations;
2. syscall decode/context/facade phases;
3. routed-context, fault, interrupt, user/syscall, armed test and bounded
   compile-time hook extraction with exact entry/return custody;
4. kernel runtime-test state/image/setup/trap/cleanup/report extraction while
   retaining supervisor register capture in the original entry frame;
5. user-side common/client/responder phases with automatic role state;
6. native fixture/case phases and byte-preserving generator phases;
7. empty migrations and the authoritative final author plan; and
8. complete independent implementation review and justified bounded corrections.

Design phase contains documentation only: no executable tests, models,
harnesses, scaffolding, reusable prototypes, or source movement.

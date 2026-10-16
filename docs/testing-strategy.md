# Testing Strategy

## Objective

No test suite can prove that a general-purpose kernel is free of defects. The
`micros` strategy builds confidence through independent layers:

- compiler and static checks;
- fast native unit and property tests;
- QEMU kernel component tests;
- multi-service integration tests;
- end-to-end shell scenarios;
- assertions and ownership invariants;
- deterministic stress and fuzz testing.

Formal models may later be used for small critical protocols. Full-system
formal verification is outside the MVP.

## Test-first development protocol

Behavior changes use Red-Green-Refactor at the layer that can measure the real
requirement:

1. write a failing native, QEMU, integration, or end-to-end test;
2. confirm that it fails for the expected missing behavior rather than a broken
   harness;
3. implement the smallest production change that makes it pass;
4. refactor while the relevant suite remains green;
5. retain the test as regression coverage.

Privileged and hardware behavior is not forced into a host unit test. Its Red
step is a failing QEMU component or acceptance test. Documentation-only changes
do not require an invented runtime test. Exploratory spikes are not merged.

The full task and review loop is defined in
[the AI-native development workflow](development/ai-native-workflow.md).

## Behavior-preserving source decomposition

A source-organization refactor starts with a structural Red test for the
accepted module set and build integration. It then uses the existing behavior
tests as the semantic oracle; a refactor must not weaken, replace, or silently
skip those tests.

For the VFS decomposition defined by ADR-0051:

- each coherent extraction commit runs `vfs_test`, `vfs_service_test`, and
  `vfs_model_test`;
- the final candidate runs the complete native unit workflow;
- the final candidate builds the standalone VFS service image;
- the final candidate passes the isolated QEMU VFS gate;
- the final candidate passes the complete pull-request validation tier.

Review must also inspect module ownership, private declarations, dependency
direction, validation order, rollback order, and build source lists. Runtime
test success alone does not prove that a responsibility split is correct.

The VFS test-source decomposition defined by ADR-0052 applies the same rule
to the evidence implementation itself. It retains the existing `vfs-core`,
`vfs-service-core`, `vfs-model`, standalone-image, and QEMU VFS identities;
splitting a source file does not create another CTest or workflow. During
extraction, only the narrow existing gate for the active slice runs. Before
review, the author executes the authoritative pull-request plan once and
verifies that it includes complete native validation, the standalone VFS
image, and the QEMU VFS gate. A workflow already selected by that plan is not
run separately. The reviewer does not routinely rerun those suites.

The kernel address-space decomposition proposed by ADR-0053 preserves
`endpoint_test`, `endpoint-profile`, `test-qemu-user-address-space`,
`test-qemu-address-space-handoff`, and `test-qemu-user-execution` while
splitting kernel-root, user-root, VM-transaction, execution, fixture, and
scenario owners. It begins with one source-layout test and an exact
23-function migration allowlist. Each extraction runs only its narrow
existing evidence; before review, the author runs the authoritative
pull-request plan once, and the reviewer inspects rather than routinely
reruns that evidence.

The frame-ownership decomposition defined by ADR-0054 preserves
`frame_allocator_test`, `frame-allocator`, `frame_ownership_test`,
`frame-ownership`, and `test-qemu-frame-ownership`. It begins with one
source-layout test, an exact 17-symbol owner migration map, and an exact
15-function migration allowlist. Native extraction retains the allocator and
ledger model seeds, step counts, random-draw order, and test-case order.
Target extraction retains the exact runtime scenario and pass record. Before
review, the author runs the authoritative pull-request plan once; the
reviewer inspects that evidence without routinely rerunning it.

The kernel VM-handoff decomposition defined by ADR-0055 preserves
`vm_bootstrap_test`, `vm-bootstrap`, `vm_handoff_syscall_test`,
`vm-handoff-syscall`, `test-qemu-vm-handoff`,
`test-qemu-address-space-handoff`, `test-qemu-vm-scratch-preemption`, and the
existing VM role/fault panic workflows. It begins with one source-layout test,
an exact 92-symbol owner migration map, and an exact 13-function migration
allowlist. Extraction preserves command decode, fixed-object input-copy and
output-publication timing, preflight/commit ordering, native case order,
retained target scenario order, assembly hooks, markers, and timeouts. Before
review, the author runs the authoritative pull-request plan once; the reviewer
inspects rather than routinely reruns that evidence.

The kernel PM-control decomposition defined by ADR-0056 preserves `pm_test`,
`pm-lifecycle`, `test-qemu-pm-service`,
`test-qemu-vm-scratch-preemption`, and the downstream TTY, RAMFS, and VFS
workflows that compile the same production PM-control sources. It begins with
one fail-closed source-layout test, an exact 32-function owner migration map,
and an exact 15-function migration allowlist. Extraction preserves
operation-13 decode and authority precedence, retained output translation,
prepare/`fence.i`/publication order, activation, exit detach and release,
native case order, the sole target state owner, one mutable context per native
family, the PM-service marker, and its timeout. Before review, the author runs
one authoritative pull-request plan, and reviewers inspect rather than
routinely rerun that evidence.

The VM service decomposition defined by ADR-0057 preserves `vm_bootstrap_test`,
`vm-bootstrap`, `spawn_abi_test`, `spawn-abi`, `vm_core_test`, `vm-core`, the
real `micros_vm_server` ELF, `test-qemu-vm-handoff`,
`test-qemu-pm-service`, `test-qemu-vm-scratch-preemption`, the retained TTY,
RAMFS, and VFS integrations, and both VM self-fault workflows. It begins with
one fail-closed source-layout test, an exact 23-function owner migration map,
and an exact 11-function C/Python migration allowlist. Extraction preserves
boot-object import, deterministic frame selection, ELF/stack planning,
scratch/freeze/abort/commit/exit ordering, native case order, service phase
order, generated fixture bytes and symbols, scratch trap/report stages,
markers, and timeouts. Before review, the author runs one authoritative
pull-request plan; reviewers inspect rather than routinely rerun that
evidence.

The PM service decomposition defined by ADR-0058 preserves `pm_test` /
`pm-lifecycle`, `spawn_abi_test` / `spawn-abi`, the real PM service and its
32-page budget, PM-service and scratch-preemption QEMU scenarios, and the
downstream TTY/RAMFS/VFS consumers. Its implementation begins with a structural
Red gate, an exact 44-function owner migration, and an exact 16-function
long migration. PM case/model tables stay local; existing service globals and
all seven spawn-ABI mutable static buffers gain explicit ownership. Existing
case order, 4,096-step model seed/draws/trace/comparison, request and stack
results, kernel report, generator bytes for identical inputs, markers, and
timeouts are preserved. Narrow extraction evidence precedes one authoritative
final PR plan; reviewers do not routinely rerun it. Every PM service variant
must receive the existing common post-link checker with the Accepted
32-resident-page/one-stack-page options, without a duplicate checker run.
The retained kernel PM-control source-layout gate follows the moved report
owner without weakening marker or timeout evidence.

The kernel IPC decomposition defined by ADR-0059 preserves
`endpoint-profile`, `ipc-model`, all existing fast/slow entry selection, and
the trusted IPC, ecall-core, full syscall, and unchanged buffer-loss panic
workflows with their exact eight-second timeouts. Its implementation
began with one fail-closed structural Red gate, an exact 70-symbol owner
migration, and an exact 42-function long migration; both are now empty.
Nine contexts consolidate
only existing mutable evidence storage; automatic snapshots/reference state
and six assembly saved-register ABI objects retain their roles. The
8,192-transition acceptance model keeps trace `65c2014aad08f70b`, while
notification and close retain 4,096 steps and 512 cases with their original
draw schedules. Target register capture stays in the original non-returning
entry frame, and the full syscall script retains all 21 commands and deferred
timer/negative-state evidence. Narrow extraction gates precede one
authoritative final PR plan; independent reviewers inspect rather than
routinely rerun those commands.

The kernel bootstrap correction defined by Accepted ADR-0060 preserves
`bootstrap_test` / `bootstrap-manifest`, `bootstrap_control_test` /
`bootstrap-control`, `fdt_test` / `fdt-parser`, the existing launcher ELF,
all component/prefix/fatal workflows, and the shared fixture generator.
Its implementation began with a structural gate, a 60-symbol owner migration
and a 27-function long migration; both active migrations are now empty.
Native case order, the 4,096-step reference
model seed/draw/dispatch/comparison and last-16 failure trace, automatic
fixture lifetimes, three static catalog pages, target branch/commit order,
generator bytes and imported helper interfaces remain unchanged.
Narrow extraction evidence precedes one complete author PR plan; independent
reviewers inspect rather than routinely rerun that evidence.

The Accepted trap/syscall correction in ADR-0061 preserves `runtime_test` /
`user-runtime`, the existing standalone runtime test ELF and twelve-second
QEMU runtime gate, and every current trap/interrupt/IPC/grant/VM/console
workflow. Its implementation began with a structural Red gate recording
exactly fourteen moving functions and nine long-function semantic splits;
both active migrations are now empty. Exact route/hook/error/
selector order, original-frame supervisor register capture, three existing
evidence contexts, seven image ABI objects, native case order, all budgets,
generator bytes, markers and timeouts remain unchanged. Narrow extraction
evidence precedes one complete current author PR plan; independent reviewers
inspect rather than routinely rerun the evidence. Implementation starts only
after the separate design is reviewed, Accepted and merged.

The scheduler correction defined by Accepted ADR-0062 preserves `scheduler_test` /
`scheduler-core`, the exact eleven native cases, its 4,096-step two-hart
model, three scheduler QEMU workflows, and all downstream production
consumers. Its separately reviewed design merged as #123. Implementation
began with a structural Red recording 38 C sources, six private headers,
six source sets, 138 original-definition moves, and twelve long-function
semantic migrations; both active migrations are now empty. The five
existing-state consolidations,
sole planning workspace, automatic plans/snapshots/reference state, assembly
ABI objects, original entry continuation, exact LCG/prelude/draws/eighteen
coverage checks, deferred completion, timer-before-commit, markers, and
eight-second timeouts remain unchanged. Bounded source/exit checks complement
existing semantic evidence, not a general C interpreter. Each split path
inherits all four original metadata dimensions; the native sources retain
full-tier classification despite empty separate workflow metadata. Narrow
green extraction precedes one authoritative final author PR plan; independent
reviewers inspect rather than routinely rerun the author's evidence.

The RAMFS correction defined by Accepted ADR-0066 preserves the exact `ramfs-seed`,
`ramfs-core`, and `ramfs-model` CTests, replayable 8,192-step model,
standalone RAMFS image, authenticated six-service QEMU scenario, and
downstream VFS/ELF-spawn consumers. The
[canonical source contract](architecture/ramfs-source-organization.md)
defines 32 C sources, six private headers/sets, 180 original-definition
moves, sixteen long C splits, and one Python renderer split. Its separately
reviewed design merged as #133; implementation #134 is also merged with
empty owner/function migrations. Structural Red froze immutable
owner/body/storage/case/trace/output inventories and exact CMake
reconstruction; full-tier, QEMU, slow-model, and
image classifications remain independently unchanged. Native/model,
image/QEMU, and current-candidate validation-profile evidence supplement
structural checks. Narrow green slices preceded the complete fresh author
plan, bounded real reuse acceptance and clean independent review.

The endpoint correction defined by Accepted ADR-0067 preserves `endpoint_test` /
`endpoint-profile`, `ipc_model_test` / `ipc-model`, both existing model link
consumers, all 25 deterministic runner entries, the four-slot 4,096-step model,
and `test-qemu-endpoint` with its existing grant lifecycle proof. The
[exact source contract](architecture/kernel-endpoint-source-organization.md)
defines 25 C sources, four private headers/sets, 90 original-definition moves,
ten long-function dispositions, typed storage/exit proof, and complete
recipe maintenance. Implementation starts only after the separately reviewed
design merges. Narrow green extraction precedes one complete current author
plan and bounded real native/smoke/TTY-image reuse acceptance; reviewers
inspect rather than routinely rerun it. This Accepted design adds no test,
model, build target, execution, or endpoint behavior.

The kernel-object correction defined by Accepted ADR-0068 preserves
`kernel_objects_test` / `kernel-objects`, its fifteen cases and existing
4,096-step xorshift model, all real core consumers, and
`test-qemu-object-model` with its exact marker and eight-second budget. The
[object source contract](architecture/kernel-object-source-organization.md)
defines 23 C owners, three private headers/sets, 72 moves, eight long and four
shorter semantic dispositions, original storage/exit/IRQ scopes and complete
byte-source/recipe inverses. Shared inspection moves unchanged into one
host-only owner with reviewed ASTs frozen; old scheduler/endpoint object
hashes are not rebaselined. Implementation follows a separate reviewed design
merge. Narrow native/source-layout or real target checks precede one current
complete PR plan and additional actual same-producer native/smoke/TTY-image
reuse acceptance. Its design authors no executable test or model and
changes no existing execution, timeout or policy.

The Accepted grant data-plane correction follows
[ADR-0069](adr/0069-grant-data-plane-source-decomposition.md) and its exact
[source contract](architecture/grant-data-plane-source-organization.md).
It preserves the cohesive registry/core/runtime, the existing endpoint and
IPC-model native entries, original model seed/LCG/4,096 transitions/thirteen
operations/32-entry trace, and real grant/grant-syscall target scripts.
The fourteen long and five shorter dispositions retain typed fixture/work,
array, initializer, snapshot, script-data and IRQ scopes under complete
original-body reconstruction. Exact source-set/consumer and three-alias
inverses preserve previous recipe/planner oracles and all four validation
dimensions. Only after separate clean design merge may implementation
start with structural Red, narrow existing native or real target gates, one
complete current PR plan and actual original-producer three-profile reuse.
This design adds no executable test/model/harness, new invocation, marker,
timeout, policy or profile-admission change.
Implementation's structural gate is
`tests/host/test_grant_data_plane_source_layout.py`: all 95 original complete
bodies/signatures and original data remain immutable under finite typed
scope/row/helper expansion. Independent positive and negative fixtures
precede C outlines; literal-empty migrations and all 22 real bounded owners
close source work, not final author/reuse or independent-review acceptance.

The next TTY source correction is Accepted
[ADR-0070](adr/0070-tty-function-and-evidence-decomposition.md) and its exact
[source contract](architecture/tty-source-organization.md). Design authors
documentation only and complete independent design review is clean.
After separate merge, implementation must
start with immutable 156-body/type, seven-long-C and Python-renderer facts,
structural Red and independent typed state/scope/AST/data fixtures.
Existing native and real UART/TTY/RAMFS/VFS/ELF evidence remains required:
preserve jump-target roots, model draw/trace identity, generated bytes and
all four selection dimensions. Full current author acceptance and bounded
actual original-producer reuse follow narrow green slices; reviewers inspect
that evidence without routinely rerunning it.

Implementation #142 exposed the model validator's existing final dependency
on `reference_output_pending`, omitted by the private graph. Its structural
failure blocks implementation despite unchanged native behavior. Accepted
[ADR-0071](adr/0071-tty-model-validation-reference-dependency.md) corrects
only that necessary acyclic edge; independent design review is clean and
its separate #143 merge is complete. Restart from current `main` and prove the exact final
comparison, all other owner directions and the original model identity.
Historical abandoned-candidate results do not satisfy restarted acceptance.

The restarted #144 source outcome has all 26 real C owners and literal-empty
move/task migrations. Actual original-body/type/CPP-scope and Python module/
data/CLI expansion, independent context/initializer/jump/ordering fixtures,
complete native render bytes and descriptor/error precedence are checked.
Current narrow native and all four renderer-consumer QEMU gates pass.
Complete current author acceptance passed all 47 commands fresh, including
35 CTests/908 Python cases/all 38 QEMU workflows/four images. Actual bounded
three-reused/seven-fresh original-full-producer evidence and complete
independent read-only review are clean. #144 is merged with its identical
reviewed tree and obsolete branches removed.

The next correction is Accepted
[ADR-0072](adr/0072-qemu-runner-source-decomposition.md) and its exact
[runner source contract](architecture/qemu-runner-source-organization.md).
Design remains documentation only and independent design/correction review
is clean. After separate design merge,
implementation must freeze the original runner/file/AST/data, all 85 test
IDs and fixture bodies, and prove finite actual-source restoration before
the reviewed code-family source declaration changes. Retain the cohesive
serial lifecycle, all first errors/outcomes, independent case expectations
and every real guest/budget. New source-member, bytecode, same-stem package,
native-extension and foreign-namespace mutations must reject smoke reuse
before QEMU metadata while all three supported workflows still
demonstrate actual eligible original-producer reuse. Full current author
and complete read-only implementation review remain mandatory.

## Test layers

| Layer | Environment | Primary purpose | Expected cadence |
| --- | --- | --- | --- |
| Compile checks | Host | Warnings, ABI assertions, freestanding violations | Every build |
| Unit tests | Native host process | Algorithms, parsers, state machines | Every local change |
| Kernel component tests | QEMU test image | Traps, MMU, contexts, IRQ, IPC, grants | Every applicable pull request |
| Integration tests | Full QEMU image | Server protocols and transactions | Every applicable pull request once all participating services exist |
| End-to-end tests | Full QEMU image | Boot and shell-visible behavior | Every applicable pull request after shell exists |
| Stress and fuzz tests | Host and QEMU | Long sequences, malformed data, rare transitions | Scheduled or explicit |
| Hardware tests | Physical target | Behavior not represented by QEMU | Post-MVP |

### PM Step 10 evidence

PM precedes the VM mapping and VFS executable paths in the development DAG.
Its first implementation therefore splits evidence by authority boundary:

- native unit and replayable model tests cover PM record generations,
  monotonic PIDs, parent/child relationships, init reparenting, spawn stages,
  reverse rollback including parent loss before activation, post-activation
  orphan reparenting, exit, zombie retention, exact-child and any-child wait,
  `NOHANG`, one-shot reply ownership, malformed transitions, and capacity
  exhaustion;
- native kernel-transition tests cover exact PM role and production tuple,
  exact inert `APPLICATION` profile identity and policy, bootstrap-sealed
  gating, retained output translation, hidden empty-process reservation,
  stale abort rejection, byte-exact preservation for rejected operations,
  zero live resources after successful abort, unchanged
  endpoint/scheduler/ownership state, and exact one-step advancement or
  terminal quarantine of the consumed process generation plus one-step
  PM-control transaction advancement; and
- one QEMU component test uses the real launcher, VM, PM, and a probe to prove
  PM readiness after handoff, malformed and unmanaged-caller results, the
  kernel-origin bootstrap-sealed event, a real PM-only reserve/abort sequence,
  and deterministic success shutdown.

The Step 10 target test must prove that the reserved process has no root,
thread, endpoint, profile, grant, scheduler state, or process-owned frame. It
must not create a fake runnable child or report a successful spawn.

The later executable-path integration adds the first successful spawn, VFS and
VM tokens, application-profile installation, instruction synchronization,
activation, running exit teardown, and an end-to-end wait result. Those later
tests extend rather than replace the Step 10 native lifecycle model.

### TTY Step 11 evidence

TTY combines portable state machines with hardware and privilege behavior, so
its evidence is split at the real authority boundaries:

- native TTY tests cover byte-exact protocol parsing, strict request IDs,
  grant direction and bounds, one pending read and write, cancellation,
  completion notification and collection, 256-entry canonical input, CR/LF,
  erase, overflow, echo, LF-to-CRLF output, physical drain, and replayable
  mixed transitions;
- native kernel-transition tests cover the exact TTY tuple and profiles,
  console begin, one-shot operation-12 mapping, the non-managed device-leaf
  exception, managed-memory rejection, operation-14 authority, PLIC source-10
  claim/in-service/complete state, production syscall commit ordering and
  failure preservation, cause-9 origin classification, `SEIE` independence,
  begin-time deadline coverage, DLAB-safe panic seizure, and sealed owner-fault
  diagnostics that retain the pre-seizure route/source snapshot; and
- one QEMU component test uses the real launcher, VM, PM, and TTY plus an exact
  test VFS peer. It proves begin before mapping, mapping before release, commit
  before ready, one unextended guest deadline, marker-triggered host input,
  canonical grant-backed read, empty-to-nonempty transmit start, grant-backed
  interrupt-driven output, and deferred PLIC completion.

The QEMU harness sends the configured bytes only after observing an exact TTY
input-ready line. It uses no sleep, preserves the guest-owned readiness
deadlines and host absolute timeout, and leaves every other QEMU workflow's
stdin disconnected.

The implementation exposes one stable `test-qemu-tty` workflow. The final
marker is emitted by TTY after the probe's grant-backed write, and isolated
test shutdown occurs only after the UART has physically drained and the
kernel has validated matching source-10 claim/completion evidence.

### RAMFS Step 12 evidence

RAMFS is a portable state owner behind real IPC and grant boundaries, so its
implementation must split evidence as follows:

- native seed tests cover the versioned JSON declaration, deterministic
  parent-before-child generation, pointer-free header and entry ABI, digest,
  reserved bytes, parent/name/mode rules, exact payload coverage, aggregate
  rounded block demand, retained seed-plus-arena cost, and every malformed or
  capacity rejection;
- host post-link and manifest tests cover the complete 192-page RAMFS service
  limit and aggregate 4,096-static-mapping limit;
- native RAMFS tests cover generation-safe nodes, root and parent invariants,
  link/reference ownership, mount gating, absolute and relative traversal,
  repeated separators, `.`, `..`, trailing separators, create, mkdir, short
  EOF reads, sparse zeroes, cross-block writes, all-or-error grant failures,
  complete directory records, cursor continuation, putnode batching, stale
  handles, nondirectory empty/`.`/`..` traversal, and every stable result;
- one slow replayable model performs at least 8,192 mixed mount, lookup,
  create, mkdir, read, write, getdents, putnode, malformed, stale, capacity,
  and injected grant-failure transitions while comparing complete production
  state with an independent reference after each operation; and
- one QEMU component scenario uses the real launcher, VM, PM, TTY, and RAMFS
  plus an exact test VFS peer. It proves seed validation, one mount,
  `/etc/motd` lookup and EOF, runtime directory/file creation, sparse
  grant-backed I/O, one-record cursor continuation, stable malformed and
  stale results, balanced non-root references, no live grants, VFS readiness,
  launcher sealing, TTY-routed output, and clean shutdown.

The slow native workflow is `test-ramfs-model`. The six-service component
workflow is `test-qemu-ramfs`; it uses no host input or sleep and accepts only
the exact RAMFS pass marker after VFS readiness, launcher sealing, physical
UART drain, and zero live grants.

### VFS Step 13 evidence

VFS owns application-visible identity and coordinates two backend protocols,
so its implementation must split evidence as follows:

- native grant tests cover non-copying validation of exact grantor/grantee
  generations, zero length, dual-invalid precedence, direction, bounds,
  complete resident mappings, zero mutation, runtime-wrapper registers, and
  existing checked-copy compatibility;
- native manifest/VM tests cover the exact 128-byte capacity-seven service
  configuration, 364,704-byte VM boot object and dependent offsets/digest,
  an unchanged exact six-entry production manifest, and one isolated
  seven-entry VFS fixture;
- native VFS tests cover trusted process attach/detach, descriptor and
  open-file ownership, shared positions, root/cwd routing, vnode and backend
  reference aggregation, open/create/close, sequential read/write, translated
  getdents, mkdir, chdir, every stable result, and complete rollback;
- native console tests cover grant preflight before consuming reads, pending
  completion, exact `CANCEL/REQUEST -> COLLECT/OK` races, accepted writes,
  physical-drain backpressure, writable retry, coalesced event bits,
  request-ID exhaustion, full global grant capacity, and cleanup;
- one slow replayable model performs at least 8,192 mixed process,
  descriptor, path, regular-I/O, directory, console, malformed, capacity,
  grant-failure, and backend-failure transitions while comparing complete
  production state with an independent reference after each operation;
- host post-link and manifest tests cover the complete 80-page VFS service
  limit and retained aggregate 4,096-static-mapping limit; and
- one QEMU component scenario uses the six real production services plus one
  isolated application probe. It proves one mount, trusted console descriptors,
  relative directory creation, absolute and relative paths, regular-file data
  through both grant hops, fixed application directory-record translation,
  marker-triggered canonical input, TTY-routed output, the no-authority
  test-only drain barrier, zero live grants, launcher sealing, physical UART
  drain, and clean shutdown.

The implementation must expose `test-vfs-model`, `build-vfs-service-image`,
and `test-qemu-vfs`. The QEMU harness must send its configured input only after
the exact VFS input-ready line, use no sleep, and accept only the exact final
VFS pass marker.

### ELF spawn Step 14 evidence

The [application runtime implementation map](architecture/application-runtime.md)
separates the native `application-runtime` decoder/frontend checks from
the existing PM component's checked application entry and return-exit proof.
The separate [full PID-1 integration](architecture/elf-spawn-integration.md)
now composes all six production services. Neither native startup decoding
nor the PM component alone claims that proof.

Before the host application checker reuses the existing image tooling,
the separate [user ELF checker source correction](architecture/user-elf-checker-source-organization.md)
must pass its design/implementation barriers. Accepted ADR-0063 preserves
the twelve original checker cases and fixture-generator output identities,
adds focused facade/CLI/AST regressions, and carries exact dependency and
validation ownership to every new private source. It introduces no new QEMU
execution or application acceptance claim.

The ELF spawn path crosses every production authority owner, so evidence must
remain split at the actual boundaries:

- byte-level native protocol tests cover the canonical at-most-one-page path,
  argv, envp, and duplicate-only descriptor request; PM, VFS, and VM payload
  fields; every reserved byte and overflow; stable public result precedence;
  exact application-stack pointer rebasing and auxv values; and the
  2,048-byte initial-frame/2,048-byte runtime-stack boundary. Only malformed
  caller-to-PM grants produce public `GRANT`; trusted internal grant-capacity
  exhaustion produces `NO_SPACE`, and every other trusted grant failure is
  fatal;
- native ELF tests cover the fixed static RISC-V ELF64 header and three
  load-segment shape, complete file-range validation, exact RX/R/RW
  permissions, no W+X, deterministic zero fill, entry, alignment,
  contiguity, stack separation, 15-image-page limit, and every truncated or
  overflowed input;
- native VM and kernel-transition tests cover lowest-frame selection, the
  sole VM-root scratch alias and its active- and inactive-root
  complete-validation exception, transferable-to-exact-process-generation
  `VM_WIRED` ownership, exact four-frame private Sv39 construction,
  byte-identical shared supervisor root entries outside VPN2 index 1, freeze,
  mapping generation, instruction synchronization, held execution
  preparation, fixed lower-priority application scheduling, reservation
  consumption, final activation, rollback before and after freeze, exit
  detach, frame release, caller authority, stale tokens, and byte-exact
  failure preservation;
- native PM and VFS tests cover PID consumption, parentless init, executable
  lookup and buffering, VFS-to-VM direct grants, root/cwd inheritance,
  descriptor preparation, hidden commit, every reverse rollback row, normal
  exit, reaper-exit rejection, child reparenting, same-parent combined
  lowest-PID selection after zombie publication, distinct init/original-parent
  two-waiter staging and independent reply restoration, zombie retention,
  one-shot wait, and complete reference cleanup. The nested regression uses
  `init -> C -> A -> B`, with `B` already zombie and both init and `C`
  blocked: publishing `A` must stage `B` for init and `A` for `C`.
  Executable lookup tests require `regular-file/child -> ACCESS` and a
  component longer than `MICROS_VFS_NAME_MAX -> TOO_BIG`, with consumed PID,
  complete rollback, and precedence over executable, grant, and descriptor
  work;
- one replayable native model compares complete production and independent
  reference state across mixed valid and invalid load, spawn, rollback, exit,
  and wait transitions and prints its seed and operation trace on failure;
- host checks cover full-link and seeded-runtime application ELF parity, the
  16-page application limit, 128-KiB executable limit, PM 32-page limit, VM
  160-page limit, VFS 80-page limit, and retained aggregate static-mapping
  limit; and
- one isolated QEMU MMU/context-switch component uses distinct VM and peer
  Sv39 roots. A labeled compile-time hook after the real first `SCRATCH_MAP`
  makes the peer runnable and shortens VM's quantum to one counter tick. The
  ordinary timer/scheduler path must preempt VM, activate the peer root,
  validate VM's inactive root with the scratch leaf present, block the peer,
  and reactivate VM through the normal ASID-zero full-fence path. VM rereads a
  scratch sentinel, performs the real `SCRATCH_COMMIT`, and an exact expected
  load-page-fault at the scratch VA proves the PTE and stale translation are
  gone; and
- one QEMU integration scenario uses the six real production static services
  plus a dynamically spawned test-only PID 1 and child. It forces one
  descriptor failure after VM freeze, then proves valid argv/env/auxv,
  inherited descriptors 0/1/2, VFS I/O, normal return through PM exit, exact
  wait status, zero live image/request grants, no scratch or transferable
  frame, and final child-resource equality with the post-PID-1 baseline.

The implementation must expose `test-elf-spawn-model`,
`build-application-images`, and `test-qemu-elf-spawn`. The QEMU harness uses
no sleep. The target runs both QEMU scenarios and accepts integration success
only after physical UART drain and the complete resource-baseline report.
The test PID 1 is not production init, and this gate does not claim shell
behavior.

## Planned test commands

The build system should expose stable intent-based targets:

```text
ninja test-unit
ninja test-qemu-smoke
ninja test-qemu-integration
ninja test-stress
```

Performance budgets are:

- incremental `test-unit-fast`: normally below 6 seconds;
- persistent native models: isolated from the fast loop and run in the PR or
  milestone tier;
- QEMU smoke suite: normally below 10 seconds;
- QEMU integration suite: kept short enough for every applicable pull request;
- stress suite: unconstrained by the fast feedback budget and run separately.

Budgets are review signals, not reasons to hide necessary coverage.

The native validation tiers are `test-unit-fast`, `test-ipc-model`,
`test-tty-model`, `test-ramfs-model`, `test-vfs-model`,
`test-elf-spawn-model`, and the complete
`test-unit` gate. The standalone image workflows are
`build-tty-service-image`, `build-ramfs-service-image`, and
`build-vfs-service-image`, plus `build-application-images`. The implemented
QEMU targets are `test-qemu-smoke`,
`test-qemu-panic`, `test-qemu-trap`, `test-qemu-timer`,
`test-qemu-uart-console`,
`test-qemu-frame-allocator`, `test-qemu-trap-panic`, `test-qemu-mmu`,
`test-qemu-object-model`, `test-qemu-endpoint`,
`test-qemu-grant`, `test-qemu-grant-syscall`, `test-qemu-user-runtime`,
`test-qemu-bootstrap-launcher`,
`test-qemu-vm-handoff`,
`test-qemu-pm-service`,
`test-qemu-vm-scratch-preemption`,
`test-qemu-tty`,
`test-qemu-ramfs`,
`test-qemu-vfs`,
`test-qemu-elf-spawn`,
`test-qemu-vm-ready-early`,
`test-qemu-vm-self-fault`,
`test-qemu-vm-self-fault-sealed`,
`test-qemu-bootstrap-ready-timeout`,
`test-qemu-bootstrap-manifest-panic`,
`test-qemu-ipc`, `test-qemu-ipc-ecall-core`,
`test-qemu-ipc-syscall`, `test-qemu-ipc-syscall-panic`,
`test-qemu-nested-trap`, and
`test-qemu-frame-ownership`, `test-qemu-user-address-space`,
`test-qemu-address-space-handoff`, and
`test-qemu-user-execution`, `test-qemu-scheduler`,
`test-qemu-scheduler-invalid-outgoing`, and
`test-qemu-scheduler-invalid-next`. From a clean checkout,
the implemented configure, build, and execution gates are:

```bash
cmake --workflow --preset test-unit-fast
cmake --workflow --preset test-ipc-model
cmake --workflow --preset test-tty-model
cmake --workflow --preset test-ramfs-model
cmake --workflow --preset test-vfs-model
cmake --workflow --preset test-elf-spawn-model
cmake --workflow --preset test-unit
cmake --workflow --preset build-tty-service-image
cmake --workflow --preset build-ramfs-service-image
cmake --workflow --preset build-vfs-service-image
cmake --workflow --preset build-application-images
cmake --workflow --preset test-qemu-smoke
cmake --workflow --preset test-qemu-panic
cmake --workflow --preset test-qemu-trap
cmake --workflow --preset test-qemu-timer
cmake --workflow --preset test-qemu-uart-console
cmake --workflow --preset test-qemu-frame-allocator
cmake --workflow --preset test-qemu-trap-panic
cmake --workflow --preset test-qemu-mmu
cmake --workflow --preset test-qemu-object-model
cmake --workflow --preset test-qemu-endpoint
cmake --workflow --preset test-qemu-grant
cmake --workflow --preset test-qemu-grant-syscall
cmake --workflow --preset test-qemu-user-runtime
cmake --workflow --preset test-qemu-bootstrap-launcher
cmake --workflow --preset test-qemu-vm-handoff
cmake --workflow --preset test-qemu-pm-service
cmake --workflow --preset test-qemu-vm-scratch-preemption
cmake --workflow --preset test-qemu-tty
cmake --workflow --preset test-qemu-ramfs
cmake --workflow --preset test-qemu-vfs
cmake --workflow --preset test-qemu-elf-spawn
cmake --workflow --preset test-qemu-vm-ready-early
cmake --workflow --preset test-qemu-vm-self-fault
cmake --workflow --preset test-qemu-vm-self-fault-sealed
cmake --workflow --preset test-qemu-bootstrap-ready-timeout
cmake --workflow --preset test-qemu-bootstrap-manifest-panic
cmake --workflow --preset test-qemu-ipc
cmake --workflow --preset test-qemu-ipc-ecall-core
cmake --workflow --preset test-qemu-ipc-syscall
cmake --workflow --preset test-qemu-ipc-syscall-panic
cmake --workflow --preset test-qemu-nested-trap
cmake --workflow --preset test-qemu-frame-ownership
cmake --workflow --preset test-qemu-user-address-space
cmake --workflow --preset test-qemu-address-space-handoff
cmake --workflow --preset test-qemu-user-execution
cmake --workflow --preset test-qemu-scheduler
cmake --workflow --preset test-qemu-scheduler-invalid-outgoing
cmake --workflow --preset test-qemu-scheduler-invalid-next
```

`test-unit-fast` runs the FDT parser corpus, portable frame allocator,
typed frame-ownership ledger, Sv39 encoding, kernel-object lifecycle/model
tests, and deterministic scheduler admission, RTS, priority-queue, policy,
current-selection, preemption-repair, return-plan, and separate
thread/kernel/idle accounting tests plus a replayable 4,096-step two-hart
reference model. They also cover reversible current-thread IPC detach,
idle/thread trap-stack anchor transitions, exact rollback, same-priority head
preservation, and higher-priority wakeup selection. Endpoint tests cover
endpoint encoding, immutable privilege-profile tables,
process-bound lifecycle, stale-generation rejection, relationship validation,
authorization, and deterministic endpoint/IPC regressions under ASan and
UBSan. It also verifies the exact 64-byte IPC message ABI and zero-state
invariants for dormant thread and endpoint IPC storage. The native host tests
also cover generation-safe sender/receiver FIFO topology, exact queue
membership, stale links, cycles, duplicate membership, and close rejection
while queues or exact-source/reply waiters remain attached. Canonical queued
call and queue-less reply-wait shapes, unique token values, reserved message
types, and impossible already-matchable queue pairs are validated too.
Failure-atomic enqueue tests prove canonical source/token overwrite, FIFO tail
append, duplicate-token rejection, and detection of a matching peer before
blocking. Staged-delivery tests require an exact active source generation,
user message type, aligned retained receive buffer, runnable scheduler state,
and no residual queue or reply authority. Matching-delivery regressions cover
head, middle, and tail dequeue, specific and `ANY` selection, unmatched FIFO
preservation, ordinary versus call sender wake behavior, exact staged message
bytes, canonical no-message `OK` completion for consumed ordinary senders, no
premature call completion, ready-queue placement, stale and corrupt queues,
no-match preservation, and byte-exact state and output preservation on every
failure. Direct completion tests require zero message/buffer state and
duplicate-staging atomicity. Authorized
send/receive operation tests add exact active source and destination
resolution, receive/send operation and target-profile enforcement, one
canonical message snapshot, immediate matching before blocked enqueue,
specific and `ANY` FIFO selection, explicit scheduler-held non-current caller
requirements, and complete state preservation on every rejected transition.
Authorized call tests additionally prove fixed-state monotonic nonzero token
allocation, low-level mint/reuse rejection, no wrap at exhaustion, exact
live-caller/source/callee binding for staged nonzero tokens, rejection of
queued token bindings, preservation of independent receiver run-time flags,
canonical token delivery through immediate and queued paths, reply-only wait,
retained reply buffers, and byte-exact failure preservation including
scheduler-transition rejection.
Authorized reply tests prove bounded opaque-token lookup, exact caller-thread
and callee-generation matching, rejection before queued calls enter reply
wait or before the token-bearing request completion returns, active endpoint
and reply-operation enforcement, token-authorized bypass of an absent ordinary
send-target permission, canonical replying source and zero delivered token,
one-shot consumption, preservation of independent caller run-time flags,
wakeup only at the zero boundary, validator rejection of noncanonical or
duplicate staged call tokens, and complete state/token preservation on every
failure including a late scheduler preflight rejection. The seeded endpoint
model also exercises allowed and denied reply-operation authorization.
Authorized reply/receive tests prove that the combined operation has distinct
authority, reuses the complete reply token and message preflight, accepts a
specific active source or `ANY`, preserves unmatched FIFO senders, retains a
consumed queued call in reply wait, and atomically chooses immediate delivery
or receiver blocking. They also exercise canonical replies and requests,
validator rejection of corrupted final states, later delivery to the blocked
receiver, and byte-exact token/state preservation for invalid receive
arguments and late caller or sender scheduler failures. The seeded endpoint
model exercises allowed and denied reply/receive-operation authorization.
Authorized notification tests prove nonzero-mask validation, exact active
source and destination generations, operation and target-profile authority,
immediate first-compatible receiver wakeup, call-reply exclusion, nonblocking
notifier return, per-source OR coalescing, and bitmap/event agreement. Receive
and reply/receive tests require lowest-slot pending selection before blocked
senders, exact source-generation revalidation, and a canonical reserved kernel
message containing the little-endian 64-bit event mask with a zero token and
zero payload tail. Corruption, pending-source close, stale-source, and late
scheduler failures preserve all state. A replayable 4,096-step notification
model compares coalescing and specific/`ANY` selection with a compact
reference.
Deterministic deadlock tests exhaust every realizable chain length across the
64 endpoint slots and independently exercise the
`MICROS_THREAD_CAPACITY`-step reference bound. They cover `SEND` before
`REPLY` before specific `RECEIVE`, `ANY` termination, exact generation
resolution, zero/multiple-thread projection rejection, candidate-return
cycles, repeated intermediate corruption, and the post-reply graph used by
`reply_receive`. Every deadlock or corruption result preserves the complete
registry, object state, tokens, messages, and outputs.
Endpoint-close cancellation tests combine closing-owned send, receive, call,
notification, and staged-delivery state with foreign queued senders, specific
receivers, reply waits, pending notifications, and already staged messages.
They require one atomic transition to detach the closing process, wake exact
foreign dependents with `DEAD_ENDPOINT`, preserve unrelated `ANY` receivers,
cancel caller/callee tokens, and clear notification state in both directions;
an owned staged `DEAD_ENDPOINT` result is discarded by a later cascading
close, while successfully staged messages, notifications, and no-message
completions reject close before mutation until drained. Separate stale
queue-generation, token-generation, notification-source, staged-source,
held-thread, and late scheduler failures prove byte-exact preservation. A
deterministic seeded 512-case scenario sweep creates a fresh fixture for each
selected cancellation class and checks staged-delivery preflight plus
unrelated-state preservation.
The final portable acceptance model retains one fixture for 8,192 mixed
transitions. It uses seven process generations, including three processes with
two model threads each, and independently compares complete run-time flags,
sender/receiver queues, reply tokens, notification masks, staged messages and
results, plus ready-queue/current scheduler state after every transition.
Its trace covers all IPC operations, close/reuse, profile denial, malformed
input, ordinary-send completion drain, reply-before-request-return rejection,
and deadlock, and prints the replayable seed plus a trace hash on success and
the recent complete operation trace on failure.
The Python host tests include ELF allocatable-section closure, legacy-global
rejection, and machine-readable QEMU record regressions.
`test-ipc-model` contains the replayable endpoint lifecycle, notification,
close-cancellation, and persistent 8,192-transition IPC models.
`test-ramfs-model` compares the complete portable RAMFS state with an
independent reference across 8,192 replayable mixed filesystem transitions.
`test-vfs-model` compares the complete portable VFS state with an independent
reference across 8,192 replayable mixed descriptor, pathname, file-I/O, and
asynchronous console transitions.
`test-unit` combines the fast tests and every persistent model and remains the
complete native gate.

`tools/validation_plan.py` maps committed, staged, unstaged, and untracked
paths to `fast`, `pr`, or `full` execution plans. Documentation-only changes
avoid target builds. Code PR plans always include complete `test-unit`.
Unknown non-documentation paths, mixed mapped/unmapped changes, and shared
toolchain, linker, target-entry, conditional multi-image, post-link, public
header, or QEMU-harness changes run the full tier. Explicit paths are unioned
with Git discovery; rename discovery classifies both paths. Execution rejects
remaining untracked files. Every plan ends with separate index, worktree, and
branch-range diff checks.
Accepted [ADR-0064](adr/0064-fail-closed-validation-evidence-reuse.md) and its
[exact contract](architecture/validation-evidence.md) define an opt-in local
execution ledger and a bounded existing-workflow CMake exception. They are
separately implemented in merged #132. Default and full execution remain
fresh; reuse does not remove a required command or authorize cross-candidate
evidence.
The initial-profile implementation follows Accepted
[ADR-0065](adr/0065-closed-tool-default-configuration-for-validation.md);
its fresh implementation followed merged corrective design #131.
The correction does not reduce any validation gate.
The explicit fresh/reuse/gate-only interfaces and bounded supported
environment are documented in [the build guide](development/building.md).
RAMFS protocol, seed-parser, namespace, file-state, and replayable-model
changes select both `test-ramfs-model` and `test-qemu-ramfs`. RAMFS startup,
generated seed source and catalog, seed embedding/generation, and the
six-service fixture generator retain the user-runtime, launcher, VM, PM, TTY,
and RAMFS service gates. RAMFS service sources, generated seed data, and seed
embedding/generation inputs also select `build-ramfs-service-image`. Inventory
regressions require every documented QEMU workflow to have exact preset,
ownership-map, and representative-input parity.
The VFS implementation extends that fail-closed map: VFS core/model changes
select `test-vfs-model`; VFS protocol, service, fixture, or test-application
changes select `test-qemu-vfs`; operation-15 changes retain the native grant,
grant-syscall, handed-off grant, user-runtime, and VFS gates; bootstrap
capacity or VM static-address-space changes retain every affected launcher,
VM, PM, TTY, RAMFS, and VFS fixture; and VFS linker/table changes select
`build-vfs-service-image`. The VFS QEMU workflow has the same preset,
ownership-map, and representative-input parity as every other authoritative
gate.

`test-qemu-smoke` verifies the real OpenSBI handoff, exact object/trap
readiness, FDT memory discovery, agreement between decoded range counts and
emitted range events, a nonempty firmware reservation result, allocator and
MMU readiness, exact agreement between reachable kernel page tables and typed
ownership, and clean SBI shutdown. The remaining stable targets are added when
their dependency-DAG layers become implementation-ready.

`test-qemu-panic` builds an isolated test image and verifies ordered source,
hart, and machine-state diagnostics plus clean fatal shutdown. Panic output
does not turn a timeout, explicit failure, missing FDT evidence, or malformed
diagnostic stream into a pass.

`test-qemu-trap` verifies the real direct-mode entry and `sret` paths with two
distinct x1-x31 register patterns, two stacks, an observable status change, and
an exact expected illegal-instruction label. `test-qemu-timer` verifies
expired-only return preparation, failure-atomic timer programming, three
accepted supervisor timer expirations, two successful interrupt rearms, final
disarm, and the SIE-clear wait handshake.
`test-qemu-frame-allocator` verifies production FDT/linker reservation
integration, deterministic allocation and release from the live post-MMU
baseline, preservation of retained table bits, exact free-count restoration,
and runtime memory sizing by booting one ELF at 128 MiB and 256 MiB.
`test-qemu-trap-panic` verifies that an unarmed exception retains a separately
captured trap frame and that its `sepc` equals the fault symbol in the built
ELF. `test-qemu-mmu` verifies exact recovery from a store page fault against RX
text and an instruction page fault from RW/NX kernel data.
`test-qemu-object-model` exercises the production process/thread/hart registry,
including generation advance, stale rejection, the one-thread policy, and
hart-local current-thread state. `test-qemu-endpoint` exercises reserved versus
active visibility, immutable profile installation, asymmetric
call/send/notify authorization, bound-process release rejection, stale
endpoint rejection after generation advance, failed runtime-initialization
preservation, and complete baseline restoration through the authoritative
target registry. `test-qemu-grant` creates three exact address spaces and
proves bootstrap-phase checked copy in both directions across local and
cross-page ranges, byte/canary preservation on every failure, stale authority
rejection, revoke/cancel behavior, generation reuse, and complete
grant/endpoint/root/frame/object cleanup. `test-qemu-ipc` uses that same target
registry with three
exact production process generations,
three distinct generation-bound Sv39 roots, saved integer contexts, complete
thread-owned kernel-stack patterns, scheduler state, and trusted kernel-owned
messages before the syscall ABI exists. It proves immediate and blocked
send/receive, canonical no-message completion for consumed queued senders,
exact call/reply token routing and one-shot use, atomic `reply_receive`,
notification coalescing and call-reply exclusion, deadlock rejection, close
cancellation, stale generation rejection after reuse, authorization denial,
and final endpoint/object/hart/frame baseline restoration while roots,
contexts, stacks, scheduler state, and messages remain preserved.
`test-qemu-ipc-ecall-core` executes the production U-mode ecall route. It
proves stable operation/result numbering, exact `sepc + 4`, preserved
non-result registers, target-buffer snapshot before guard begin, immediate
notify completion through captured return, unauthorized-send rollback through
ordinary return, upper endpoint-bit rejection before mutation, and complete
endpoint/object/address-space/frame baseline restoration.
`test-qemu-ipc-syscall` runs three real address spaces through all six
operations. It proves blocked and immediate send/receive, two tokenized call
round trips, atomic `reply_receive` plus later sender wake, notification
coalescing while the destination is in reply wait, pending notification
delivery, page-crossing request/reply buffers, higher-priority wake selection,
stable `-3`, `-1`, and `-6` results, deferred completion across a timer-selected
peer, exact `sepc + 4`, preserved non-result registers, and complete
endpoint/thread/root/frame cleanup.
`test-qemu-ipc-syscall-panic` releases a previously accepted receive-buffer
page after sender commit but before selected-thread return preflight. It
requires the exact `invalid-bootstrap-ipc-buffer` panic and trap context.
The ADR-0037 kernel-origin notification slice extends the native notification
and persistent IPC models plus `test-qemu-ipc`. They prove source-`NONE`
immediate delivery,
pending-mask coalescing, `ANY`-only consumption, reply-wait exclusion,
state-preserving failures, selected-thread completion return, and complete
baseline restoration. That evidence does not claim PLIC routing,
`irq_complete`, console handoff, or TTY behavior.
ADR-0038 grant evidence now includes deterministic native lifecycle tests, a
replayable 4,096-transition registry model, and target-runtime lifecycle
coverage in `test-qemu-endpoint`. It proves token generation, revoke-time
advance, quarantine, exact participant authority, failure-atomic endpoint
cancellation, stale reuse rejection, and complete
registry/endpoint/object restoration. Checked-copy ranges, directions,
mappings, and byte evidence remain a separate design.
ADR-0039 defines that checked-copy evidence: native authority/bounds
and chunk-pairing tests, grant-model copy transitions, and a dedicated
`test-qemu-grant` image proving page-local and cross-page copy in both
directions, byte-exact failure atomicity, stale authority rejection, and
complete cleanup. The marker explicitly identifies bootstrap phase; no
post-handoff copy claim is valid before a separate mapping-authority design.
ADR-0040 evidence includes native phase/owner classification and the isolated
`test-qemu-address-space-handoff` component. It first rejects a reachable leaf
planned `VM_TRANSFERABLE` without
mutation, then commits every live service leaf to exact `VM_WIRED` ownership.
After the irreversible transition it proves root validation and activation,
IPC-buffer snapshot/write, and page-local/cross-page checked copy, while
bootstrap mapping mutation remains phase-rejected. Foreign wired ownership,
transferable live leaves, malformed PTEs, absent mappings, and permission
failures preserve all affected bytes and prevalidated state. An absent
target combined with unrelated structural corruption reports the
structural failure first. Generic execution-context preparation returns
`PHASE` without changing its thread, saved context, or kernel stack, while an
already prepared thread still returns through the common scheduler path. The
exact marker is:

```text
MICROS_ADDRESS_SPACE_HANDOFF_TEST_PASS phase=handed-off wired=validated ipc=resident grants=atomic mutation=revoked
```

ADR-0041 evidence now includes native unified-ABI tests and the dedicated
`test-qemu-grant-syscall` workflow. Native tests cover the unified
operation/result values, exact
grant register shapes, upper-bit and unused-register rejection, 64-bit scalar
preservation, token/result disjointness, recoverable grant-error mapping, and
fatal phase/invariant classification. The isolated bootstrap QEMU component
executes real U-mode `ecall` instructions through the production top-level
dispatcher. It preserves representative IPC behavior while
proving grant create, revoke, both copy directions,
page-local/cross-page/zero-length behavior, exact stable failures,
non-result-register preservation, stale authority after reuse, and complete
cleanup. It emits:

```text
MICROS_GRANT_SYSCALL_TEST_PASS namespace=unified phase=bootstrap lifecycle=checked directions=checked errors=stable registers=preserved cleanup=complete
```

The retained-state `test-qemu-address-space-handoff` image also executes real
operations 7 through 10 after its `VM_WIRED` commit, proving the same syscall
path in `HANDED_OFF` without claiming teardown. In addition to the unchanged
ADR-0040 marker it emits:

```text
MICROS_GRANT_SYSCALL_HANDOFF_PASS phase=handed-off operations=create,revoke,copy-from,copy-to errors=stable registers=preserved
```

ADR-0042 evidence now includes native production-wrapper tests linked against
a host raw-syscall capture stub for all operations 1 through 10. They prove
exact register marshalling, full-width scalar preservation, direct stable
results without `errno`, success-only grant-token publication, unchanged
outputs on failure, and stack-local blocking-buffer semantics. The same native
target tests the production-prefixed `memcpy` and `memset` bodies under ASan
and UBSan.

Step 13 extends the same host and real-runtime evidence with operation 15,
including zero length, dual-invalid precedence, and unchanged non-result
registers.

`tools/check_user_elf.py` parses ELF64 headers, program headers, sections,
symbols, and RISC-V instructions. Its malformed-fixture regressions reject
wrong identity or flags, permission or range violations, orphan allocatable
sections, BSS errors, dynamic/TLS/constructor/small-data/unwind state,
relocations, undefined symbols, noncanonical startup, extra `gp`/`tp` writes,
and any raw stub other than exact uncompressed `ecall; ret`.

The isolated `test-qemu-user-runtime` workflow consumes only that checked ELF
through a bounded generated fixture. Three isolated process generations run
the same fixed virtual image with patched test-only role data and separate
external stacks. User code proves initialized data, zero BSS, readable and
protected rodata, zero `gp`/`tp`, stack-local canaries across blocking IPC,
raw-stub and kernel register preservation, all production operations 1 through
10, checked grant copies and stale-token rejection, and deterministic return
through the labeled breakpoint. Loader and teardown checks prove complete
page zero fill, final RX/R/RW-NX permissions, instruction synchronization,
endpoint/grant/root/frame cleanup, and restoration of the pre-test baseline.
Only the complete sequence emits:

```text
MICROS_USER_RUNTIME_TEST_PASS elf=freestanding startup=validated syscalls=1-10 registers=preserved stack=external data=initialized bss=zero rodata=protected return=trapped cleanup=complete
```

ADR-0043 native evidence now includes the `bootstrap-manifest` and
`bootstrap-control` CTests in both native tiers. They cover exact version-1
layout and offsets, immutable image/profile resolution, page limits,
explicit prerequisites, deterministic lowest-ID topology, cycles, generated
RX/R/RW image-catalog bounds, operation-11 decoding, held versus published
endpoint state, exact profile and scheduler publication, one starting
service, readiness acknowledgment construction, deadline boundaries,
VM/console role gates, failure-atomic output/state preservation, source-only
endpoint sealing, and a replayable 4,096-operation launcher transition model.
The common RISC-V build also compiles the fixed preparation, reverse-order
rollback, manifest-view, syscall, timeout, and final-seal paths.

ADR-0044 decoder coverage keeps operation-11 width and unused-register checks
before current-thread resolution, then validates the `FAIL` reason and its
`a4` detail after controller authority and runtime validation. Native cases
cover all nine malformed-field codes and unrelated-reason detail rejection;
detailed manifest validation separately covers authoritative cycle masks.

The successful launcher workflow is now implemented:

```text
test-qemu-bootstrap-launcher
```

It uses checked launcher and probe ELFs, a generated immutable image catalog
and deliberately permuted manifest, the production preparation and
operation-11 paths, and one post-seal probe trap. Only the exact pass marker
below is accepted:

```text
MICROS_BOOTSTRAP_TEST_PASS manifest=immutable order=topological profiles=exact endpoints=staged readiness=acknowledged authority=revoked
```

The two fatal-path workflows are also implemented:

```text
test-qemu-bootstrap-ready-timeout
test-qemu-bootstrap-manifest-panic
```

The timeout image runs a released probe that deliberately omits readiness and
requires the guest counter deadline to emit `reason=ready-timeout`; host
timeout is rejected. The malformed-manifest image supplies a dependency cycle
and requires `reason=manifest-cycle` before endpoint, user-frame, or scheduler
publication. Both require the complete bootstrap panic report and explicitly
forbid the success marker.

The success image uses one real launcher ELF and test-only probe ELFs
through the production object, address-space, runtime, IPC, scheduler, and
syscall paths. It proves that all images and contexts are prepared while
inactive and scheduler-unassigned, unreleased endpoints remain hidden, release
follows explicit prerequisites, and exact profiles plus scheduler policies are
installed atomically with endpoint publication. Readiness calls carry the
exact source generation and reply token, and acknowledgments commit atomically.
The native transition/classification tests retain malformed, duplicate,
early, and foreign precedence coverage.
Final completion holds the launcher, clears the exact controller binding, and
seals authority while the retained source-only endpoint rejects new
destinations but still validates already staged acknowledgment source state.
Only the complete sequence may emit:

```text
MICROS_BOOTSTRAP_TEST_PASS manifest=immutable order=topological profiles=exact endpoints=staged readiness=acknowledged authority=revoked
```

The missing-readiness image must fail from the guest's own `time`-counter
deadline with:

```text
MICROS_BOOTSTRAP_FAILURE reason=ready-timeout
```

The malformed-manifest image must reject a two-entry cycle before any
manifest-directed process, user-frame, endpoint, or scheduler mutation with:

```text
MICROS_BOOTSTRAP_FAILURE reason=manifest-cycle
```

Both expected-failure images then require
`MICROS_PANIC reason=bootstrap-failure`, clean SBI system-failure shutdown, no
success marker, and no host timeout. The launcher gate does not claim a VM
ownership handoff, TTY console transition, or any real service protocol.

`test-qemu-nested-trap`
injects a second fault
after the per-hart `sscratch` sentinel is armed and proves that the registered
emergency stack is selected without trusting interrupted `tp`.
`test-qemu-frame-ownership` exercises the production ledger and object
registry together: stale and cross-process owners are rejected, process
release remains blocked while exact owners exist, forbidden handoff plans are
failure-atomic, and one complete plan seals bootstrap mutation in a single
transition.
`test-qemu-user-address-space` constructs two generation-bound roots, maps the
same virtual page to distinct typed frames, switches ASID-zero roots with exact
fences, checks SUM-gated hardware access, proves full-page zeroing on reuse,
rejects active/stale/corrupt roots without mutation, atomically tears roots
down, then verifies mapping mutation is revoked after ownership handoff while
read-only operations reject a rootless process with `STATE`. It also
translates arbitrary aligned user addresses and proves failure-atomic 64-byte
IPC snapshot/write across a page boundary, including range, alignment,
mapping, and read/write permission rejection.
`test-qemu-user-execution` enters a relocation-free payload in U-mode, proves
user-stack access and kernel-page isolation, captures every integer register
on a thread-owned supervisor stack, uses scheduler-owned admission/current
selection and accounting, resumes a modified user frame through the common
return plan, and returns through an interrupt-disabled supervisor continuation
while preserving caller state.
`test-qemu-scheduler` runs two private address spaces under real
supervisor timer delivery. It proves failure-atomic timer start,
return-boundary timer preparation, queue-reachable current ownership, separate
thread/kernel/idle accounting, repeated equal-priority alternation, complete
integer-register preservation, a forced spurious idle iteration, and a later
real timer wake. The same gate now proves shared IPC completion preflight and
commit across scheduler start, ordinary U-return, captured IPC return, and idle
wake; deferred message copy through a validated user buffer; stable success and
dead-endpoint `a0` results; retained prevalidated physical chunks with no
post-timer address-space revalidation; rejection of malformed residual
non-pending state; timer-before-completion ordering; and exact completion
clearing. It additionally raises real source-10 UART interrupts while U-mode
and S-mode are active, proving that user-origin cause 9 captures and returns
through ordinary scheduler selection while supervisor-origin cause 9 returns
directly. A third source-10 interrupt from the idle enable window proves
`IDLE`-to-`KERNEL` accounting and immediate idle-wake selection. The exact
additional marker is:

```text
MICROS_TTY_TRAP_TEST_PASS user=cause9-scheduled supervisor=cause9-direct idle=cause9-selected
```
The two isolated invalid-context gates require a U-origin timer panic with
exact diagnostics proving the outgoing or selected context failed before any
return-plan mutation.

## Native unit tests

Code should run natively when its correctness does not depend on actual
privilege levels or device behavior. Target and host builds use the same
implementation source where practical.

Initial native test subjects include:

- intrusive lists, queues, bitmaps, and ring buffers;
- endpoint packing, generation changes, and special endpoint rejection;
- process/thread ownership and one-thread-per-process enforcement;
- reply-token allocation, matching, one-shot use, and cancellation;
- scheduler queue selection;
- physical range insertion, allocation, release, and coalescing;
- FDT memory-node iteration, multiple `reg` tuples, reservations, and malformed
  bounds;
- IPC transition and deadlock-detection models;
- immutable bootstrap-manifest parsing, profile/image resolution, explicit
  prerequisite topology, and launcher lifecycle transitions;
- permission masks;
- grant bounds, direction, lifetime, and overflow checks;
- user-runtime wrapper register marshalling, stable result propagation, and
  grant-token output preservation through a host raw-syscall stub;
- freestanding `memcpy` and `memset` semantics without a hosted libc;
- page-table index and flag calculations;
- allocatable ELF section closure against linker permission ranges;
- kernel and standalone user ELF header, program-header, relocation, symbol,
  and section-permission validation;
- RAMFS directories, inode lifetime, and path traversal;
- protocol message validation.

Unit tests do not replace target tests for trap assembly, page-table activation,
TLB behavior, context switching, or interrupt delivery.

## Property and model-based tests

Examples of required properties:

- allocated physical ranges never overlap;
- free plus allocated frames equals the managed frame total;
- each runnable context occurs in exactly one run queue;
- every thread has exactly one owning process;
- every current-thread pointer belongs to one hart-local object;
- a stale generation never resolves to a reused process slot;
- a stale generation never validates, activates, mutates, or destroys a reused
  process address space;
- a stale thread generation never prepares, inspects, detaches, or enters a
  reused execution-context slot;
- every running thread is current on exactly one hart, while every inactive
  thread is current on none;
- failed object-table operations preserve both registry state and output
  arguments;
- an allocator bit is set exactly when the corresponding typed owner is not
  free;
- stale process generations cannot release, adopt, or reclassify frames;
- a failed ownership or handoff operation preserves allocator, ledger, plan,
  counts, and output arguments;
- the ownership handoff changes every planned class and the phase atomically;
- a reply token wakes exactly one blocked caller thread and cannot be reused;
- an IPC transition preserves exactly one blocked or runnable state;
- a bootstrap service is hidden before release, has exactly one starting and
  ready transition, and cannot release a dependent before every prerequisite
  is ready;
- a failed prepublication bootstrap transition preserves complete manifest,
  profile, endpoint, scheduler, deadline, and launcher state;
- sealed bootstrap authority cannot be reintroduced;
- a grant cannot authorize bytes outside its declared range;
- RAMFS link and open-reference counts never become negative;
- failed spawn steps restore every resource acquired by earlier steps.

Randomized tests use explicit seeds. A failure prints the seed and operation
sequence so the exact case can be replayed.

## QEMU kernel component tests

These tests execute the real RISC-V entry, privilege, and MMU paths. They cover:

- trap entry and return with known register patterns;
- expected and unexpected exception handling;
- user attempts to access supervisor pages;
- timer interrupt and preemption;
- repeated context switching;
- page-table activation and invalidation;
- exact agreement between the reachable page-table tree, allocator bits, and
  typed frame owners;
- same virtual addresses in distinct process roots resolve distinct exact
  user-frame owners;
- inactive-root mutation plus complete ASID-zero activation flushes prevent
  stale cross-root translations;
- every attached thread stack is pairwise disjoint, bound to its exact slot,
  and selected only for that hart's exact current thread;
- user return status, PC, and stack mappings are revalidated before every
  `sret`;
- store rejection on kernel text and instruction-fetch rejection on writable
  kernel data;
- executable-frame reuse with different code after `fence.i`;
- synchronous IPC blocking and wakeup;
- specific-source and any-source receives;
- concurrent-call reply matching in a model with several threads per process;
- notification coalescing;
- stale and unauthorized endpoint use;
- deadlock-chain rejection;
- safe-copy across distinct address spaces;
- invalid grant and unmapped-buffer rejection.
- standalone user `_start`, external stack, BSS/data/rodata behavior, raw
  `ecall`, typed wrappers, and accidental-return trapping.
- static launcher preparation, reserved endpoint visibility, exact-profile
  release, versioned readiness acknowledgment, internal timeout failure, and
  final authority sealing.
- UART transmitter drain, interrupt-disable quiescence, ordinary ownership
  transfer away from the kernel, state-before-delivery PLIC enable, retained
  source-10 claim ownership, explicit completion, and terminal panic seizure.

Most component tests may run in one test kernel to avoid repeated QEMU startup.
Tests expected to panic or corrupt their own address space use isolated images.

The isolated UART ownership workflow is:

```text
cmake --workflow --preset test-qemu-uart-console
```

It enables the NS16550A interrupt-enable register, executes the production
console-begin quiesce and ownership transition, proves `IER = 0`, and then
simulates TTY-side interrupt programming while ownership remains with TTY. It
publishes the owned/idle route before enabling PLIC source 10 and `sie.SEIE`,
checks timer and global interrupt state are preserved, triggers a real UART
THRE interrupt, retains the PLIC claim while the route is in service, and
requires an explicit completion write before publishing the idle route. It
finally seizes the terminal through the production panic path. The panic check
proves supervisor-external delivery and PLIC source 10 are disabled, the
runtime console and route are both `PANIC`, and the fixed 8N1 divisor is
restored before reporting success. It accepts only the exact markers:

```text
MICROS_UART_CONSOLE_TEST_PASS quiesce=drained ier=disabled ownership=tty panic=seized
MICROS_TTY_IRQ_TEST_PASS route=state-before-enable claim=source10 retained=in-service completion=explicit
```

## Integration tests

Integration tests exercise stable protocols rather than internal functions.
Before every participating service is dependency-ready, the earlier component
uses a native protocol/transition model and does not pull its successor into
the current task. The first pull request or milestone gate where all peers
exist must add the corresponding QEMU integration scenario.

Integration scenarios include:

- launcher exact-generation readiness, timeout, and fatal malformed-message
  paths;
- manifest service-profile installation without inferred privileges;
- VM `VM_READY` ownership commit before its generic launcher readiness;
- TTY console-begin and exact mapping before release, then console commit
  before its generic launcher readiness;
- final launcher authority sealing before PM spawns init;
- application privilege-profile installation through PM's separate authority;
- VM ownership handoff;
- fatal VM-originated fault handling;
- PM exit/wait behavior;
- PM/VFS/VM spawn success and rollback;
- load-complete token binding, stale-token rejection, and PM-only executable
  preparation/activation authority;
- rejection of mapping mutation while prepared, generation revalidation at
  activation, and abort/reprepare recovery;
- UART interrupt delivery to TTY;
- two-phase early-console ownership transfer and deferred PLIC completion;
- init console descriptors and child descriptor-duplication actions;
- VFS/RAMFS open, read, write, close, and directory operations;
- non-transitive application/VFS/backend bounce-buffer paths;
- client termination during an outstanding request;
- malformed message type, endpoint, grant, and request identifier;
- later, service restart and endpoint replacement.

The currently implemented ADR-0045 VM-handoff evidence validates the exact
364672-byte six-address-space boot-information ABI, canonical ranges, address
spaces, mappings, frame states, independent counts and digest,
failure-atomic all-user-frame wired staging, operation-12 shape and summary
authority, irreversible commit, and the retained replayable 4096-transition
ownership model. Step 13 must atomically update that same evidence to the
364704-byte, seven-address-space ADR-0049 layout before any capacity-seven
fixture can pass.

The successful workflow is implemented:

```text
test-qemu-vm-handoff
```

It uses the production launcher, one real VM ELF, and one test-only probe. The
kernel constructs and reads back the fixed VM database, while the VM
independently validates every range, mapping, state, count, and digest before
issuing the real VM-only operation-12 ecall. The gate then proves post-handoff
return, VM readiness only after ownership publication, dependent probe
release, launcher sealing, and a real bidirectional checked-copy grant exchange
between the probe and VM. Only the exact marker is accepted:

```text
MICROS_VM_HANDOFF_TEST_PASS snapshot=validated ownership=handed-off vm=wired readiness=acknowledged authority=vm
```

The expected-failure workflow:

```text
test-qemu-vm-ready-early
```

has the real VM validate its boot database but deliberately send ordinary
readiness before operation 12. It requires the exact `ready-role-gate`
bootstrap failure for the bound VM service and endpoint, forbids the handoff
success marker, and rejects host timeout.

Both fatal workflows are implemented:

```text
test-qemu-vm-self-fault
test-qemu-vm-self-fault-sealed
```

The running image commits the same handoff and faults before generic VM
readiness. The fixture first quiesces the UART and transfers ordinary output
ownership to TTY, so the exact VM self-fault record also proves that fatal
diagnostics seize panic ownership before their first byte. The workflow
requires the exact VM identity, fault registers, `ownership=handed-off`, the
authoritative `RUNNING+STARTING` service-fault record, ordered trap-context
panic, and no success marker. The sealed variant first completes VM readiness,
the real grant exchange, and launcher sealing; it then requires
`MICROS_PANIC reason=vm-self-fault`, exact trap context, no success marker, and
no `MICROS_BOOTSTRAP_FAILURE` record of any kind.

The PM Step 10 workflow is implemented:

```text
test-qemu-pm-service
```

It starts the production launcher and VM, the real PM service, and one
test-only probe. PM validates its exact service identity and portable
lifecycle-table initialization before sending readiness. Before sealing, the
probe receives stable bad-version, malformed, and unmanaged-caller results,
and the test compares the complete PM table byte for byte to prove rejection
did not mutate lifecycle state. Launcher completion then commits the exact
kernel-origin bootstrap-sealed notification. PM consumes that event, reserves
a process, and asks VM to freeze a four-page application containing a
position-independent child. PM proves stale prepare output preservation,
prepare/abort/re-prepare, activation, and load commit; the child then calls PM
so PM can prove stale exit output preservation, exit begin/detach,
premature-release rejection, VM exit release, and final process release. The
kernel requires no remaining child root, mapping, thread, endpoint, profile,
grant, scheduler state, or process-owned frame, while process/thread
generations and reservation/prepared/exit tokens remain advanced. Only the
exact marker is accepted:

```text
MICROS_PM_SERVICE_TEST_PASS handoff=complete readiness=acknowledged protocol=stable sealed=received prepare=aborted-reprepared activate=committed exit=detached-released resources=clean process-generation=advanced thread-generation=advanced tokens=advanced
```

The first ELF-spawn Step 14 QEMU component is implemented:

```text
test-qemu-vm-scratch-preemption
```

It starts the production launcher, VM, and PM service plus one test-only
orchestration driver. PM performs a real operation-13 reservation and replies
to the driver, preserving PM's exact launcher/VM/VFS call authority; the driver then
calls VM with the opaque transaction. VM performs real `LOAD_BEGIN` and
`SCRATCH_MAP` operation-12 calls, writes the scratch sentinel, and reaches the
labeled test hook. The hook releases PM as the measured peer, reduces VM's
remaining quantum to one tick, and resumes VM in a labeled user-mode spin so
only the ordinary timer can cause the measured preemption. The scheduler must
switch to PM's distinct root, where the production complete-root validator
accepts VM's inactive scratch exception. PM then blocks through ordinary
`RECEIVE`; the scheduler reactivates VM through the normal ASID-zero full-fence
path. VM rereads the sentinel, performs the real `SCRATCH_COMMIT`, and executes
the labeled stale load. Only an exact load-page fault at the removed scratch
address passes, together with an absent scratch PTE, `SCRATCH_CLEAR`, and the
frame retained as the transaction's unmapped `VM_TRANSFERABLE` entry. The
final `test-qemu-elf-spawn` workflow remains reserved for this component plus
the later complete spawn integration scenario.

The TTY Step 11 workflow is implemented:

```text
test-qemu-tty
```

It links the production launcher, VM, PM, and TTY service with one exact VFS
test peer. VM installs only the manifest-bound UART leaf before release, TTY
commits ownership before readiness, and a deliberately pending transmit
interrupt proves that readiness retains the claimed source until TTY's exact
completion. The host sends `micros-ttyx<DEL>-input<CR>` only after the complete
input-ready line. The VFS peer receives `micros-tty-input<LF>` through a
write-direction grant and submits the final marker through a read-direction
grant. The kernel requires matching nonzero source-10 claim/completion counts,
one retained pre-ready claim, no live grants, a physically drained UART, and
clean SBI shutdown. Only the exact marker is accepted:

```text
MICROS_TTY_TEST_PASS handoff=two-phase mapping=exact irq=deferred input=canonical output=interrupt-driven grants=checked
```

The RAMFS Step 12 component workflow is implemented:

```text
test-qemu-ramfs
```

It links the production launcher, VM, PM, TTY, and RAMFS services with one
exact VFS test peer. VFS mounts once, reads the seeded `/etc/motd`, observes
short EOF, creates `/tmp/note`, verifies sparse zeroes and written bytes,
enumerates one directory record at a time, and checks malformed-version,
wrong-direction-grant, stale-node, and excessive-putnode results. VFS revokes
every temporary grant and releases every non-root reference before readiness.
An exact resumable pre-readiness report proves that RAMFS test diagnostics
preempt generic active-service fault classification for the current VFS
thread; stage failures use the same path and retain their specific panic
reason.
After launcher sealing, it submits the sole marker through the real TTY path,
waits for TTY completion and writability, revokes the marker grant, and reports
through the isolated breakpoint hook. The kernel requires all six services
ready, the VFS thread current, valid VM and TTY handoffs, and zero live grants,
then waits for physical UART drain before clean shutdown. Only the exact marker
is accepted:

```text
MICROS_RAMFS_TEST_PASS seed=validated mount=single lookup=bounded files=writable directories=cursor grants=checked refs=balanced
```

The VFS Step 13 component workflow is implemented:

```text
test-qemu-vfs
```

It links the six production services with one profile-8 application probe.
Production VFS still accepts exactly six configured services; only the
compile-time fixture path accepts service 7, mounts RAMFS, and attaches that
endpoint with root, cwd, and console descriptors. The probe reads seeded
`/etc/motd` through EOF, creates `tmp` relative to root, writes `tmp/note`,
reopens it by absolute and cwd-relative paths, returns cwd to root, and
enumerates complete translated 80-byte root records without exposing a RAMFS
handle. Every transient descriptor is closed and every application and backend
grant is revoked.

The probe writes the exact input-ready line through VFS and TTY before the host
sends `micros-vfsx<DEL>-input<CR>`. Descriptor 0 receives
`micros-vfs-input<LF>`. After writing the pass marker, the probe invokes the
private no-authority drain call; VFS waits until TTY resident output is empty.
The kernel then requires the exact probe thread, all seven ready states, sealed
launcher authority, valid VM and TTY handoffs, zero live grants, and physical
UART drain before clean SBI shutdown. Only the exact marker is accepted:

```text
MICROS_VFS_TEST_PASS mount=single descriptors=owned paths=absolute,relative files=two-hop directories=translated console=two-hop grants=balanced
```

Every blocking scenario has a host-side timeout. A timeout is a test failure
with the latest structured serial events attached.

## End-to-end tests

The shell MVP test boots a release-like image and drives the serial console. It
verifies externally visible behavior rather than internal implementation
order. At minimum it:

1. waits for versioned init and shell readiness markers;
2. runs `echo`;
3. reads a seeded RAMFS file;
4. runs `ls`;
5. runs `ps`;
6. starts a child, waits for it, and checks the reported status;
7. requests a clean shutdown.

Later POSIX work may use differential tests against MINIX or another reference
system. Differential tests compare documented results, return values, and
`errno`; they do not require identical scheduling or IPC traces.

## Machine-readable output

Test images emit TAP-compatible serial output:

```text
TAP version 13
ok 1 - user cannot read a supervisor page
ok 2 - stale endpoint is rejected
not ok 3 - write-only grant accepted a read
```

Boot and service diagnostics use stable event names plus human-readable data.
The host harness must distinguish:

- successful completion;
- explicit test failure;
- kernel panic;
- unexpected QEMU exit;
- timeout.

QEMU exits through a documented test-only mechanism or SBI system reset after
the final result is flushed.

## Assertions and observability

Debug builds continuously verify:

- run-queue and IPC-queue membership;
- process/thread ownership and hart-local current-thread state;
- process state transitions;
- endpoint slot/generation consistency;
- physical frame ownership and reference counts;
- disjoint kernel and VM frame pools;
- grant owner, grantee, permission, offset, and lifetime;
- request/reply identifier matching;
- one-shot reply-token lifetime;
- VFS descriptor and RAMFS inode reference counts.

An invariant failure stops immediately. Panic output includes at least:

- build identifier;
- hart and privilege state;
- exception cause and fault address;
- program counter and stack pointer;
- current process slot and endpoint;
- current IPC peer and message type where applicable;
- source file and line for explicit assertions.

Broad catches, silent retries, and success-shaped fallback results are not
acceptable.

## Sanitizers, fuzzing, and static checks

Native tests should use compiler warnings as errors and support ASan and UBSan.
Freestanding target builds use only sanitizer checks that are explicitly
supported without a hosted runtime.

Host fuzz targets should cover:

- ELF parsing;
- protocol decoders;
- path parsing;
- RAMFS image parsing;
- IPC and grant transition sequences.

Static analysis is introduced only when its configuration is reproducible and
its findings are actionable. A large unaudited warning baseline is not an
acceptable substitute for enforcement.

## Pull request gate

Each implementation pull request must provide:

- native tests for portable logic;
- QEMU coverage for changed privilege or hardware behavior;
- native protocol/transition coverage for changed cross-service behavior when
  a peer is not yet dependency-ready;
- QEMU integration coverage in the first applicable pull request where all
  participating services exist;
- explicit negative tests for permissions and invalid input;
- updated documentation when an interface or invariant changes.

Documentation-only changes do not require a target build unless documentation
tests are later introduced.

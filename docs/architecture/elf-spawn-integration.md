# ELF Spawn Integration Implementation Map

## Authority and dependency readiness

Task `elf-spawn-implementation` completes development-DAG node P under
[Accepted ADR-0050](../adr/0050-post-handoff-elf-spawn.md), design #87,
commit `b6b291d9f7acf922759b732362be38a1ad67ecd1`.
The production coordinator #118, scratch-preemption component, and checked
application runtime #127 are merged predecessors. This is an implementation
map of that design, not a new lifecycle, authority, wire, or failure contract.

The outcome is one `test-qemu-elf-spawn` scenario using exactly the six
production static services and two RAMFS-seeded dynamic applications.
Its PID-1 reaper is a test program, not production init.
The workflow and new sources below are planned until their green
implementation lands.

Production init, shell policy, commands, fault-to-exit, signals, fork,
recovery, and generalized test or snapshot frameworks are non-goals.
The existing PM-only application component remains a separate regression;
its test VFS peer cannot substitute for production VFS here.

## Existing behavioral baseline

The fixed local MINIX reference remains
`4db99f4012570a577414fe2a43697b2f239b699e`.
[The execution and spawn study](../research/minix-elf-exec-and-spawn.md)
records the complete trace. This integration rechecked PM stop/VFS/VM exit
ordering and VFS descriptor/root/cwd release before process retirement.
No MINIX source is copied.

| Integration behavior | ADR-0025 classification | Existing authority |
| --- | --- | --- |
| PM coordinates lifecycle and one-shot parent wait | Baseline parity | ADR-0046 and ADR-0050 |
| VFS owns descriptor references and releases them before VM teardown | Baseline parity | ADR-0049 and ADR-0050 |
| A hidden new child rolls back rather than destroying the caller's image | Required adaptation | Accepted spawn-before-fork and non-transitive grant contracts |
| Real RV64 startup, Sv39 loading, and normal-return PM exit | Required adaptation | Accepted platform and application-runtime contracts |
| Deterministic post-init resource equality and compile-time fixture checkpoints | Compatible extension | ADR-0050's production integration evidence |

## Scenario and publication boundaries

The manifest contains only launcher, VM, PM, TTY, RAMFS, and VFS, with their
existing exact identities, profiles, dependency order, and service budgets.
There is no static application entry, test VFS, extra application privilege,
generic config page, or alternate loader.

After the exact kernel-origin bootstrap-sealed event, one fixture-only PM
trigger invokes the existing trusted-init coordinator with an absolute
fixture path and zero descriptor actions. VFS installs PID 1's console
descriptors through the existing trusted path. Ordinary activation and the
lower application priority ensure mandatory PM/VM/VFS commits precede its
first instruction.

The dynamic parent then:

1. validates its startup values and reaches a fixture-only baseline
   breakpoint after every production service is ready and idle;
2. sends a canonical child request with one absent source descriptor,
   requiring `DESCRIPTOR` after VM freeze and complete reverse cleanup;
3. proves the hidden PID was consumed, the parent remains usable, and no
   failed-child resource survives;
4. spawns the real child with bounded argv/env and explicit duplication of
   parent descriptors 0, 1, and 2;
5. verifies the next monotonic PID, waits for that exact PID and fixed normal
   exit status, then verifies `NO_CHILD` on the second wait;
6. writes the scenario marker through VFS and waits for the existing
   no-authority TTY drain barrier; and
7. reaches the final fixture-only breakpoint.

The child validates its argument/environment values and runtime globals,
uses inherited stdout, opens and reads one seeded regular file through VFS,
closes it, and returns the fixed nonzero status from
`micros_application_main`. It never calls VM, TTY, RAMFS, or a lifecycle
syscall directly.

Checkpoint instrumentation authenticates the exact current application
generation and checked report PC. Additional rollback and child-entry
checkpoints establish freeze-token advancement, cleanup before another
spawn, and actual inherited descriptor/open-file identity. These are
compile-time evidence, not new production calls or lifecycle transitions.
Unexpected stage, caller, PC, protocol reply, grant failure, or resource
disagreement is an explicit fatal result.

## Source ownership

| Owner | Responsibility |
| --- | --- |
| `tests/qemu/elf_spawn_protocol.h` | Fixed fixture paths, expected values, report stages, and markers |
| `tests/qemu/elf_spawn_request.c` and its private header | Canonical fixture requests shared by the trusted trigger and parent |
| `tests/qemu/elf_spawn_pm.c` | One post-seal call to the existing trusted-init coordinator |
| `tests/qemu/elf_spawn_init.c` | Parent rollback, spawn, wait, and drain scenario |
| `tests/qemu/elf_spawn_child.c` | Actual startup, inherited I/O, seeded-file read, and normal return |
| `tests/qemu/elf_spawn_client.c` and its private header | Checked PM/VFS reply decoding, direct grants, and bounded application I/O |
| `tests/qemu/elf_spawn_report.S` | Unrelaxed breakpoint/report continuation for fixture applications |
| `kernel/elf_spawn_test.c` and its private evidence owners | Launch, checkpoint authentication, staged assertions, and clean shutdown |
| `kernel/elf_spawn_test_fixture.c` | Immutable six-service configuration and the sole mutable scenario context |
| Kernel resource and service evidence owners | Capture and compare complete live-resource projections through checked mappings |
| `tools/generate_elf_spawn_seed.py` | Validate immutable application snapshots and reuse the RAMFS encoder |
| `tools/generate_elf_spawn_fixture.py` | Reuse checked service descriptors and render six-service catalog/report metadata |
| Focused native and host fixture tests | Canonical request, resource-oracle, generator, malformed-input, and ownership regressions |

Applications reuse the separate checked application SDK. Static services
retain the no-argument service runtime and existing production adapters.
The ordinary VM adapter gains an explicit six-service fixture selection,
not a fallback to a shorter historical prefix. The existing drain barrier
is enabled for this test image without enabling VFS's unrelated static-probe
configuration or granting any new authority.

No behavior is added to the pending RAMFS, TTY, endpoint, object, or grant
correction families. Their existing mechanisms are consumers, not rewrite
targets. If a real defect requires changing a signaled subsystem, its
before-next-touch correction and design barriers still apply.

## Complete resource evidence

The baseline is captured only after PID 1 has activated and all six
production services have returned to their quiescent protocol states.
The final checkpoint requires the same live resources, not merely equal
process or page totals:

- kernel process/thread/endpoint/profile identities, scheduler memberships,
  IPC queues, pending completions, reply rights, and grants;
- roots, page-table and user mappings, allocator availability, and typed
  frame owners;
- VM frame states, persistent mapping records, idle load state, and absent
  scratch/transferable ownership;
- PM's sole running reaper and absence of spawning, exiting, zombie, or wait
  residue;
- VFS process/descriptor/open-file/vnode references, free bounce-page
  ownership, empty pending state, and idle image transaction;
- unchanged RAMFS live node/link/reference/block ownership; and
- TTY's empty pending/completion/resident-output state, idle source-10 route,
  and physically empty UART.

Persistent PID-1 resources remain present. Consumed semantic PIDs and opaque
tokens advance as required; free-slot generations are not live resources and
must never be reset to manufacture equality. Accounting, exhausted buffer
contents, and other inactive bytes are not resource identities.
Every ignored field has this explicit lifetime rationale; no broad masked
comparison or count-only success is acceptable.
Native oracle mutations must demonstrate detection of representative leaks
and reference/identity changes in each captured resource family.

The fixture may inspect already mapped service state through the exact
checked static image metadata and generation-bound kernel copy primitives.
It adds no user-visible inspection operation, service query, writable alias,
grant forwarding, or production state mutation.

## Green sequence and acceptance

1. Publish this map and the merged predecessor/baseline references.
2. Add failing native/host cases for canonical fixture requests, immutable
   two-application seed generation, catalog metadata, and resource comparison;
   implement the smallest complete helpers and commit a green host slice.
3. Add the full six-service target oracle and record its expected failure
   before completing the dynamic parent/child scenario and privileged
   checkpoint assertions.
4. Run `test-qemu-elf-spawn`, preserve every existing QEMU identity, add exact
   validation ownership, and keep every permanent commit green.
5. Stage the complete outcome, run the authoritative current PR plan once,
   record exact HEAD/tree/commands/results and clean/untracked evidence, then
   obtain independent read-only implementation review.

The final gate includes complete native validation, existing relevant
service/image/component workflows, the real new integration, documentation,
and all three diff checks. Machine-readable success requires both the
TTY-routed scenario marker and clean SBI termination after the complete
resource comparison and physical drain. No sleep, host timing assumption,
unchecked success fallback, or fixture-created dynamic child is permitted.

Only after this outcome merges may production init and shell begin at
development-DAG node Q.

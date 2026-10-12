# MINIX Kernel PM-Control Source-Organization Baseline

This note records the fixed MINIX baseline used to decompose the `micros`
kernel PM-control state, operation-13 adapter, and native evidence. Runtime
semantics remain governed by the Accepted `micros` ADRs, especially ADR-0046
and ADR-0050.

## Reference

The reference tree is the local MINIX checkout at:

```text
/Users/alephling/Repo/minix
```

The inspected revision is the fixed ADR-0025 baseline:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

Representative sources are:

```text
minix/kernel/system.c
minix/kernel/system/do_fork.c
minix/kernel/system/do_exec.c
minix/kernel/system/do_clear.c
minix/kernel/system/do_runctl.c
minix/kernel/system/do_privctl.c
minix/kernel/system/do_schedctl.c
minix/kernel/system/do_schedule.c
minix/servers/pm/forkexit.c
minix/servers/pm/exec.c
```

The inspection used the local tree only. Representative commands were:

```sh
git -C /Users/alephling/Repo/minix rev-parse HEAD
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/kernel/system.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/kernel/system/do_fork.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/kernel/system/do_exec.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/kernel/system/do_clear.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/kernel/system/do_runctl.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/kernel/system/do_privctl.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/kernel/system/do_schedctl.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/kernel/system/do_schedule.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/servers/pm/forkexit.c
git -C /Users/alephling/Repo/minix show 4db99f4012570a577414fe2a43697b2f239b699e:minix/servers/pm/exec.c
```

No GitHub or web copy was used.

## Common kernel-call dispatch

MINIX `system.c` owns:

- copying one trusted kernel-call message from the caller;
- checking the call number;
- checking the caller's kernel-call mask;
- dispatching through one call vector;
- preserving a suspended VM-assisted call when required; and
- returning the result unless the handler deliberately suppresses the reply.

The process-lifecycle handlers are registered separately:

```text
SYS_FORK     -> do_fork
SYS_EXEC     -> do_exec
SYS_CLEAR    -> do_clear
SYS_PRIVCTL  -> do_privctl
SYS_RUNCTL   -> do_runctl
SYS_SCHEDULE -> do_schedule
SYS_SCHEDCTL -> do_schedctl
```

The reusable source rule is that common decode, caller permission, and result
publication belong in one facade, while responsibility-specific lifecycle
mechanisms live below that facade. The dispatcher does not duplicate each
operation's process, context, privilege, scheduler, or teardown logic.

`micros` operation 13 is a register ABI rather than a message-vector kernel
task, but the same direction applies: common decode/authority/runtime checks
remain in the public syscall facade, and reservation, preparation,
activation, and exit adaptation remain separate operation owners.

## Child reservation and execution preparation

MINIX `do_fork.c` creates one inhibited child process slot. It:

- validates the parent endpoint and empty child slot;
- copies the parent's process mechanism state;
- advances the child endpoint generation;
- clears child-only accounting and pending state;
- removes inherited system privileges when required;
- leaves the child unscheduled;
- optionally leaves it VM-inhibited until a new page table exists; and
- returns the new endpoint to the caller.

Privilege assignment and runnable admission are not folded into `do_fork.c`.
`do_privctl.c` owns privilege allocation and allow/disallow transitions.
`do_schedctl.c` and `do_schedule.c` own scheduler selection and policy
installation.

The reusable responsibility direction is:

1. create or identify a hidden process mechanism;
2. install execution, privilege, and scheduling mechanisms only through their
   authoritative owners;
3. keep the child inhibited until every required owner is ready; and
4. make visibility/runnability a later publication boundary.

`micros` does not copy a running parent. ADR-0050 requires a new empty
generation, a VM-frozen address space, one held thread, one reserved endpoint,
the exact immutable `APPLICATION` profile, and fixed scheduler policy. These
objects are prepared together because the bounded direct-spawn transaction has
one exact hidden shape and one exact application policy. This is a compatible
safety extension over the MINIX multi-call mechanism, not permission to mix
reservation, preparation, and activation source ownership.

## Execution-state installation

MINIX `do_exec.c` owns only final kernel execution-state installation after PM
and VFS have completed executable policy:

- clear stale message-delivery state;
- copy a diagnostic process name;
- install the new instruction and stack state through the architecture
  context initializer;
- remove receive blocking without replying to the old exec call; and
- invalidate stale FPU state.

PM's `exec.c` separately owns lifecycle coordination, metadata changes, the
partial-exec boundary, and the call to `sys_exec`.

The reusable source rule is that context installation is a privileged
mechanism below PM's lifecycle policy. Path lookup, ELF policy, argument
capture, semantic PID state, and parent-visible completion do not belong in
the kernel execution installer.

`micros` adapts destructive exec to hidden-child spawn. Its kernel
`PREPARE_EXECUTION` transition derives PC and SP from the exact VM load token,
prepares a held context, executes local instruction synchronization, and does
not publish the child. A separate `ACTIVATE` transition revalidates the sealed
mapping generation before publishing the endpoint and runnable thread.

## Stop, detach, and process release

MINIX PM's `forkexit.c` first stops an exiting process so it cannot execute
during cleanup. VFS cleanup occurs while the endpoint still exists. Only
after VFS replies does PM stop scheduling, invoke `SYS_CLEAR`, and ask VM to
release the address space.

The kernel mechanisms remain separate:

- `do_runctl.c` sets or clears the process-stop flag;
- `do_clear.c` releases the address-space binding, IRQ hooks, IPC references,
  timers, FPU state, privilege ownership, and process slot; and
- VM owns the separate dynamic memory release.

The reusable ordering is:

1. prevent further execution;
2. retain enough identity for service-owned cleanup;
3. detach IPC, privilege, scheduling, and execution mechanisms;
4. release VM mappings and page tables; and
5. release the empty kernel process slot.

MINIX `do_clear.c` performs broad final kernel cleanup in one handler because
its VM and process model differs. `micros` preserves the authority order with
three token-bound operation-13 stages:

- `EXIT_BEGIN` makes the child permanently non-runnable while preserving the
  endpoint for VFS;
- `EXIT_DETACH` closes endpoint/IPC/grants and removes thread, context,
  profile, and scheduler state after VFS cleanup; and
- `RELEASE_EXITED_PROCESS` releases only the rootless, frameless, empty
  process after VM release.

The additional stages are a compatible safety extension. They make the
irreversible boundary and cross-owner agreement explicit rather than turning
a post-stop mismatch into a recoverable rollback.

## Source-ownership lesson

The useful MINIX source shape is responsibility direction:

1. one common kernel-call dispatcher owns generic caller permission and
   result publication;
2. process creation, execution installation, privilege control, scheduler
   control, stop/resume, and final clear live in separate mechanism owners;
3. PM orchestrates those mechanisms but does not implement them in its main
   lifecycle source; and
4. VFS and VM cleanup retain their own source and authority boundaries.

This supports separate `micros` owners for:

- operation-13 register decoding;
- complete PM-control state validation;
- empty-process reservation and abort;
- execution preparation and prepared rollback;
- final activation;
- exit begin, detach, and empty-process release;
- shared target syscall runtime;
- responsibility-specific syscall adapters; and
- fixture, ABI, authority, reservation, preparation, token, and exit evidence.

It does not justify one source file per command. Closely related preflight and
commit halves remain together in one lifecycle owner. It also does not justify
copying MINIX process structures, endpoint wrapping, FPU state, signals,
tracing, SMP paths, fork semantics, destructive exec, broad privilege
machinery, or scheduler-server protocol.

## Difference classification

The classification follows
[ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md).

| MINIX behavior or difference | Classification | `micros` treatment |
| --- | --- | --- |
| Common kernel-call permission and dispatch are separate from operation handlers | Baseline parity | The public operation-13 facade retains common decode, authority, runtime validation, phase checks, and result publication |
| Process creation, execution installation, privilege control, scheduling, stop, and clear have distinct mechanism owners | Baseline parity | Reservation, execution preparation, activation, and exit receive separate portable and adapter owners |
| PM coordinates lifecycle while the kernel owns privileged mechanism | Baseline parity | PM alone invokes operation 13; kernel sources do not own PID, parent, VFS, or VM policy |
| A child remains inhibited until execution and scheduling state are ready | Baseline parity | Prepared endpoint and thread remain hidden and held until `ACTIVATE` |
| Exit first prevents further execution and preserves identity for VFS cleanup | Baseline parity | `EXIT_BEGIN` commits source-only endpoint state and permanent non-runnability before VFS cleanup |
| Kernel execution installation updates PC/SP together and suppresses the old call path | Baseline parity | Token-derived context is committed as one held initial context; activation does not resume an older child call |
| MINIX copies a parent process during fork | Required adaptation | Direct spawn reserves a new empty generation and binds a separately loaded address space |
| MINIX installs an architecture-specific context for destructive exec | Required adaptation | RISC-V U-mode context is derived from the VM load record for a new hidden child |
| MINIX privilege and scheduler installation use separate general calls | Compatible extension | One fixed application profile and scheduler policy are committed in the exact hidden preparation transition |
| MINIX may wrap endpoint generations | Compatible extension | Process/thread generation exhaustion quarantines the slot |
| MINIX final clear is broad and largely one-shot | Compatible extension | Exit begin, detach, VM release, and empty-process release use exact nonzero tokens and complete shape validation |
| MINIX handlers do not retain caller output translation plans | Required adaptation | Pointer-free fixed output objects are fully translated before mutation and published only after non-failing commit |
| MINIX supports signals, tracing, fork, destructive exec, broader privileges, and scheduler policy | Staged substitution | Those behaviors remain outside the current DAG node and source correction |

No dependency-ready behavior is classified as divergence requiring
correction.

## Required preservation

The source decomposition must preserve:

- the exact operation-13 register ABI and eight commands;
- exact PM caller, profile, bootstrap, VM, ownership, and grant authority;
- exact process/thread generation and token exhaustion behavior;
- exact output translation and success-only publication;
- exact prepare, `fence.i`, activation, exit, grant, IPC, and VM ordering;
- exact recoverable result versus fatal-invariant classification;
- one authoritative PM-control target state;
- current native case order and target marker/timeout identities; and
- no new behavior, test execution, recovery path, or policy owner.

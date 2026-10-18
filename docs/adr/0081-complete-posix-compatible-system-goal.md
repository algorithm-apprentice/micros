# ADR-0081: Complete POSIX-Compatible System Goal

- Status: Accepted
- Date: 2026-10-18
- Refines: ADR-0009, ADR-0033, ADR-0079, and ADR-0080

## Context

The repository currently defines `micros` v0.1 as the first shell-capable
increment. Its explicit omissions include multithreaded processes, `fork`,
signals, complete shell grammar, persistent storage, networking, and broad
userland compatibility.

Those omissions were intended to make the dependency DAG implementable. They
were not intended to define the final product. Without a durable end-state
decision, a milestone deferral can be mistaken for a permanent non-goal, and
the complete MINIX baseline audit can certify only the current reduced surface
instead of recording the mechanisms that must return.

The long-term project goal is a complete POSIX-compatible operating system
with the microkernel architecture and independently implemented MINIX
mechanisms selected by the existing ADRs.

## Decision

### Product hierarchy

`micros` has two distinct scopes:

1. **Current milestone:** v0.1 boots through OpenSBI on RISC-V64 QEMU `virt`,
   reaches the bounded interactive shell, and passes its native, component,
   integration, and end-to-end gates.
2. **Final product:** a complete POSIX-compatible operating system comparable
   in system breadth to MINIX, while retaining the reviewed `micros`
   microkernel boundaries and independent implementation.

The v0.1 milestone is an integration checkpoint, not a permanent feature
ceiling.

### External and internal authorities

The final compatibility authority is:

```text
IEEE Std 1003.1-2024 / The Open Group Base Specifications, Issue 8
```

for mandatory externally observable operating-system, libc, shell, and
utility behavior.

The fixed MINIX commit remains the first internal mechanism baseline under
ADR-0080:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

The authorities have different roles:

- POSIX defines the final portable interface and observable semantics.
- MINIX defines the initial logical structures, algorithms, ownership
  boundaries, and integration mechanisms.
- Accepted `micros` ADRs define required RISC-V, isolation, safety, and
  development adaptations.

When MINIX behavior is narrower than mandatory POSIX behavior, POSIX controls
the final interface. The project first reproduces the applicable MINIX
mechanism, then extends it through a reviewed POSIX design. A POSIX extension
must not be disguised as baseline parity.

### Compatibility meaning

The final goal includes all unconditional mandatory interfaces and every
conditionally mandatory interface whenever its POSIX-defined condition holds,
including:

- processes, `fork`, `exec`, wait, process groups, sessions, and job control;
- POSIX threads, synchronization, thread-local state, and cancellation;
- signals, timers, clocks, and asynchronous interruption;
- users, groups, credentials, permissions, and access checks;
- files, directories, links, metadata, locking, pipes, FIFOs, and persistent
  storage;
- terminals, controlling terminals, termios, pseudo-terminals, and canonical
  shell interaction;
- memory mapping, shared memory, file-backed memory, and protection;
- local and network IPC required by the selected POSIX option set;
- sockets and the networking interfaces required by the selected conformance
  profile;
- libc interfaces required by the system-interface volume;
- the POSIX shell language and mandatory utilities; and
- source-level application portability demonstrated by conformance evidence.

POSIX option groups remain optional only where the standard permits them. A
later conformance-profile ADR records every claimed optional or XSI group.
Mandatory full-POSIX requirements cannot be waived by an internal profile.
"Inapplicable" is valid only when POSIX itself conditions the requirement, and
the capability row cites that exact normative condition. A POSIX subprofile
may be reported as subprofile conformance but does not satisfy this ADR's
complete-POSIX outcome.

### Permanent non-goals

The final product does not require:

- MINIX source compatibility;
- MINIX binary or syscall-number compatibility;
- copying MINIX or NetBSD source text;
- preserving x86/ARM implementation accidents on RISC-V;
- a monolithic kernel;
- SMP or broad hardware support as prerequisites for POSIX source
  compatibility; or
- claiming formal verification or production hardening without their own
  completed evidence.

Permissively licensed third-party userland may be evaluated only through a
separate provenance, licensing, architecture, and maintenance decision.
Independent implementation remains the default.

### Staged substitutions, not permanent omissions

Every currently omitted MINIX/POSIX mechanism is a staged substitution or a
future dependency-DAG outcome. Each audit/design ledger must name:

1. the missing final mechanism or interface;
2. the current bounded substitute;
3. the exact future DAG node or the event that requires a new reviewed node;
4. its predecessor interfaces; and
5. conformance evidence needed to remove the substitution.

Terms such as "MVP non-goal," "later work," or "outside v0.1" do not authorize
permanent omission. They describe ordering only.

The closed exactly-once owner for current deferrals is
[the staged-omission manifest](../research/posix-staged-omission-manifest.md).
A newly discovered family updates that manifest through review before the
discovering audit unit can merge.

The complete B01-B14 audit therefore records both current structural
divergences and every deferred POSIX/MINIX mechanism. The final correction DAG
must preserve those staged replacement obligations.

### Long-term DAG

After the current baseline is audited, corrected, and re-certified, the
long-term implementation remains sequential:

1. complete the shell-capable v0.1 system;
2. complete the dependency-ready DS, user-scheduling, RS, block, and
   persistent-filesystem outcomes sequentially; control-plane and storage work
   retain their actual DAG dependencies rather than an invented dependency
   between each other;
3. add full process, thread, signal, credential, terminal, time, memory, IPC,
   and networking semantics;
4. implement the POSIX shell and mandatory utility surface;
5. evaluate reusable userland only under an explicit provenance decision; and
6. close a versioned POSIX conformance matrix with native, target,
   integration, and application-compatibility evidence.

No broad "POSIX expansion" pull request is permitted. Each interface family
uses the existing reviewed design/implementation pair and one dependency-ready
outcome.

### Conformance evidence

The repository must eventually contain a versioned POSIX capability matrix.
Each mandatory requirement is:

- implemented and linked to exact tests;
- marked not yet implemented with its dependency-ready task; or
- identified as conditionally inapplicable by the POSIX standard itself, with
  the exact normative condition recorded.

The project does not claim complete POSIX compatibility until:

- every unconditional mandatory row is implemented;
- every conditionally mandatory row is implemented whenever its POSIX-defined
  condition holds;
- every claimed option group passes;
- the shell and mandatory utilities pass their conformance suites;
- representative third-party POSIX applications build and run without
  project-private source changes;
- no required behavior exists only in documentation; and
- independent review finds no unsupported compatibility claim.

## Consequences

- The shell MVP remains small and dependency-ready.
- Current omissions gain explicit replacement obligations.
- Baseline audit findings cannot be closed by declaring the missing behavior
  out of scope permanently.
- The roadmap grows beyond v0.1 without pulling later mechanisms into current
  implementation tasks.
- MINIX fidelity and POSIX conformance become complementary rather than
  competing goals.
- Full compatibility will require many sequential milestones and cannot be
  represented by one generalized framework or one large pull request.

## Alternatives considered

### Stop at the shell MVP

Rejected. The shell milestone proves integration but does not provide the
process, filesystem, terminal, thread, signal, networking, libc, shell, or
utility breadth expected of the final system.

### Treat MINIX parity alone as final compatibility

Rejected. MINIX is the implementation baseline, while POSIX is the external
portable interface authority. Neither substitutes for the other.

### Port a complete userland immediately

Rejected. Userland cannot compensate for missing kernel/service semantics, and
an early port would bypass the dependency DAG and provenance review.

### Implement POSIX breadth in parallel

Rejected. The runtime dependency graph is cyclic, but development remains
sequential so that ownership, failures, tests, and regressions stay
attributable.

## References

- [Roadmap](../roadmap.md)
- [Development DAG](../architecture/development-dag.md)
- [System overview](../architecture/system-overview.md)
- [Complete MINIX baseline audit](../research/minix-complete-baseline-audit.md)
- [Staged-omission manifest](../research/posix-staged-omission-manifest.md)
- [ADR-0080](0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0009](0009-spawn-before-fork.md)

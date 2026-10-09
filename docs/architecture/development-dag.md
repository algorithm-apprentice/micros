# Development Dependency DAG

## Why this graph exists

The stable runtime architecture of a microkernel system is cyclic. For
example, process creation may involve PM, VFS, and VM; service recovery may
involve nearly every core server. A cyclic runtime graph cannot be used as an
implementation order.

This document defines a separate development graph. Each edge means that the
predecessor must have a tested, stable interface before work starts on the
successor. Bootstrap substitutions intentionally break the runtime cycles.

## Graph

```mermaid
flowchart TD
    A[Documentation and accepted ADRs]
    B[Toolchain and image layout]
    C[OpenSBI entry FDT and serial]
    D[Traps and timer]
    E[Bootstrap physical memory and typed ownership]
    F[Page tables and user mode]
    G[Process thread and hart objects plus kernel scheduler]
    H[IPC endpoints and privileges]
    I[Direct grants grant syscalls wired handoff reads then user runtime]
    J[Static bootstrap launcher]
    K[VM server and one-way handoff]
    L[PM spawn exit and wait]
    M[TTY server]
    N[RAMFS server]
    O[VFS server]
    P[ELF spawn path]
    Q[Init shell and basic commands]
    R[DS service registry]
    S[User-space scheduler policy]
    T[RS lifecycle and recovery]
    U[Block layer and persistent filesystem]
    V[POSIX expansion]
    W[NetBSD userland evaluation]

    A --> B
    B --> C
    C --> D
    C --> E
    D --> F
    E --> F
    F --> G
    G --> H
    H --> I
    I --> J
    J --> K
    K --> L
    L --> M
    M --> N
    N --> O
    O --> P
    K --> P
    L --> P
    P --> Q
    M --> Q
    Q --> R
    R --> S
    S --> T
    K --> T
    L --> T
    O --> T
    O --> U
    Q --> V
    U --> V
    V --> W
```

The graph deliberately serializes the first implementation even where some
engineering work could be performed independently. This keeps reviews and
failures attributable to one coherent change.

## Cycle-breaking substitutions

| MINIX runtime cycle | Initial `micros` substitution | Later transition |
| --- | --- | --- |
| Kernel and VM depend on each other | Kernel bootstrap allocator, typed ownership ledger, and static mappings | One-way VM ownership handoff |
| RS coordinates VM, PM, scheduler, and services | Static manifest plus bootstrap launcher | RS adopts the stable service protocol |
| PM, VFS, and VM coordinate fork/exec | PM-directed `spawn` transaction | Add `exec`, then `fork` and copy-on-write |
| VFS, filesystem servers, drivers, and DS discover each other | Fixed RAMFS endpoint and static mount | DS publication and dynamic drivers |
| Kernel scheduler mechanism calls user scheduling policy | Kernel round-robin policy | User scheduler receives policy events |
| File-backed VM calls VFS while VFS calls VM | Resident anonymous memory and RAMFS executable reads | Explicit asynchronous file-backed mapping protocol |

## Topological implementation sequence

| Step | Deliverable | Required proof before the next step |
| --- | --- | --- |
| 1 | Documentation and ADR package | Decisions reviewed; DAG and scope internally consistent |
| 2 | Toolchain, linker layout, QEMU launch | Reproducible ELF build and deterministic QEMU exit |
| 3 | OpenSBI entry, FDT, UART, panic | Memory and reservations parsed; boot marker and panic diagnostics visible |
| 4 | Traps, timer, bootstrap allocator, typed frame ownership | Expected exception recovery, timer ticks, allocator/owner invariants, and atomic handoff classification |
| 5 | Page tables, process/thread/hart objects, user mode | U-mode isolation and repeated thread context switches |
| 6 | Scheduler, endpoints, IPC | Blocking, wakeup, reply-token, stale endpoint, privilege, and deadlock tests |
| 7 | Grants, grant syscalls, wired handoff reads, then user runtime | First prove real U-mode grant lifecycle and copies before and after handoff for wired service pages; then prove one standalone freestanding ELF, startup path, raw `ecall`, and C wrappers for operations 1 through 10 |
| 8 | Bootstrap launcher | Immutable manifest validation, deterministic topology, held-image preparation, exact profile/publication release, token-bound readiness, internal timeout failure, and irreversible authority revocation verified |
| 9 | VM handoff | Frame ownership is disjoint and the VM working set remains wired |
| 10 | PM | PM identity, hidden process reservation, spawn metadata, exit, wait, and failure rollback verified |
| 11 | TTY | Two-phase console handoff, deferred PLIC completion, input, and output verified |
| 12 | RAMFS | Directory, file, and direct VFS-facing grant protocol tests pass |
| 13 | VFS | Descriptor, console binding, pathname, mount, routing, and complete two-hop I/O verified |
| 14 | Executable path, init, shell | Instruction synchronization passes and the shell runs the required commands |
| 15 | DS, scheduler server, RS | Discovery and injected service recovery verified |
| 16+ | Persistent storage and POSIX work | Added only behind explicit ADRs and conformance tests |

Step 7 is internally serialized even though it is one graph node:

1. endpoint and IPC mechanisms;
2. direct-grant identity and lifetime;
3. checked copies;
4. wired post-handoff address-space reads;
5. the unified operations 1 through 10;
6. the freestanding user-service runtime.

The runtime is not complete merely because the kernel ABI is callable from a
test payload. It requires an independent user ELF, loader/BSS/stack contract,
ABI-preserving raw stub, typed C wrappers, and target acceptance. The static
launcher remains blocked until that complete runtime outcome is reviewed and
implemented.

Step 8 is also internally serialized:

1. validate the pointer-free manifest, immutable profile identities, generated
   image catalog, resource bounds, explicit prerequisites, and cycle-free
   lowest-ID topological order;
2. prepare every static process, root, image, stack, first thread, context,
   reserved endpoint, and held scheduling policy before launcher entry;
3. run only the launcher with its exact profile and active endpoint;
4. atomically install one service's exact profile, publish its endpoint, arm
   one guest-owned readiness deadline, and release its prepared thread;
5. accept one exact-generation readiness call and acknowledge it through the
   request's one-shot reply token;
6. repeat only after the previous service is ready; and
7. hold the launcher, clear the exact controller binding, and irreversibly
   seal bootstrap authority; its retained endpoint and profile remain inert
   until later teardown.

The initial static service chain is launcher, VM, PM, TTY, RAMFS, and VFS.
The Step 8 implementation uses test-only probe ELFs and does not implement
those service protocols. VM readiness is extended at Step 9 so the generic
ready call follows the ownership commit. TTY readiness is extended at Step 11
so release follows `console_handoff_begin` and readiness follows
`console_handoff_commit`.

Step 9 is internally serialized:

1. define and validate the fixed pointer-free VM boot-information/frame-state
   object;
2. atomically stage every static `PROCESS_USER` frame as `VM_WIRED`;
3. patch and read back the exact VM object before launcher publication;
4. start the real VM service and independently validate its frame database;
5. accept one exact VM-only scalar summary operation;
6. commit the irreversible ownership phase;
7. return to VM through post-handoff wired authority;
8. accept VM's ordinary launcher readiness only after that commit; and
9. prove the launcher can seal while VM remains wired.

Step 9 does not add dynamic mapping or non-VM page-fault delivery. Those
interfaces require a later reviewed mapping authority before PM or executable
loading may consume them.

Step 10 is internally serialized:

1. define the PM application table, semantic PID, parent/child, init-reaper,
   exit, zombie, and wait contracts;
2. add the exact PM bootstrap role and profile, the stable inert
   `APPLICATION` profile identity, and PM readiness;
3. inject one kernel-origin event when launcher authority is irreversibly
   sealed;
4. reserve and abort one hidden empty kernel process through exact PM-only
   authority;
5. prove the complete future spawn transaction and reverse rollback order in a
   native transition model;
6. prove versioned exit/wait decoding and malformed-client rejection; and
7. run one real PM service after VM handoff without creating a dynamic child.

The Step 10 reservation has no root, thread, endpoint, profile, mapping,
grant, scheduler state, or process-owned frame. It preserves the process slot
needed by a later transaction but cannot be published or run.

Step 10 does not add a successful spawn path, dynamic mapping, executable
loading, descriptor state, or running-process target teardown. Step 14 adds
those integrations only after VM, PM, TTY, RAMFS, and VFS are all
dependency-ready. At that point, VM creates and freezes mappings for the
reserved process; PM's prepare transition creates the first thread and
reserved endpoint, installs `APPLICATION`, attaches the executable context,
and seals the mapping generation before final activation.

`init` is not a static manifest service. Step 14 creates it through the
ADR-0009 PM/VFS/VM spawn transaction after launcher authority is sealed.

## Phase gates

### Mechanism gate

Before the first user server, the kernel must demonstrate:

- user/supervisor memory isolation;
- preemption;
- distinct process, thread, endpoint, and hart objects;
- repeatable context switching;
- generation-aware endpoint rejection;
- exact reply-token matching to a blocked thread;
- IPC permission enforcement;
- finite deadlock-chain detection;
- bounded direct grants.

### Runtime gate

Before the static launcher, the user runtime must demonstrate:

- one standalone fixed-address ELF with exact RX, R, and RW/NX closure;
- loader-owned BSS zeroing and an external aligned writable stack;
- a C-ABI-preserving raw `ecall` boundary;
- typed wrappers for every stable operation 1 through 10;
- exact stable results without `errno`;
- no relocation, hosted libc, heap, TLS, constructors, arguments, or service
  readiness policy.

### Service gate

Before executable loading, VM, PM, TTY, RAMFS, and VFS must each:

- start from the static launcher;
- perform one exact-generation, versioned readiness call and receive its
  token-bound acknowledgment;
- reject malformed requests;
- survive ordinary client termination without leaking owned state;
- expose enough diagnostics to identify the current request and peer endpoint.

### Shell MVP gate

The first MVP is complete when a clean build boots under QEMU and an automated
serial session can:

1. observe the init and shell readiness markers;
2. run `echo`;
3. read a seeded RAMFS file with `cat`;
4. list a directory with `ls`;
5. list processes with `ps`;
6. terminate a child and collect it through `wait`;
7. shut down QEMU with a successful test result.

## Rules for changing the graph

- A new predecessor requires an ADR or a documented correction to an existing
  dependency.
- A runtime cycle must not be introduced into the development graph.
- Work on a successor does not begin while its predecessor pull request is
  awaiting review.
- Deferred components cannot be pulled into the shell MVP merely to emulate
  MINIX more closely.

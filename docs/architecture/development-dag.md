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
| 8 | Bootstrap launcher | Manifest order, exact privilege profiles, and readiness gates verified |
| 9 | VM handoff | Frame ownership is disjoint and the VM working set remains wired |
| 10 | PM | Spawn metadata, exit, wait, and failure rollback verified |
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
- publish a ready state;
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

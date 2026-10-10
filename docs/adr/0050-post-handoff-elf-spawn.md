# ADR-0050: Post-Handoff ELF Spawn and Normal Exit

- Status: Accepted
- Date: 2026-10-11
- Refines: ADR-0004, ADR-0005, ADR-0006, ADR-0007, ADR-0008, ADR-0009,
  ADR-0011, ADR-0012, ADR-0021, ADR-0025, ADR-0029, ADR-0030, ADR-0031,
  ADR-0032, ADR-0033, ADR-0034, ADR-0038, ADR-0039, ADR-0040,
  ADR-0041, ADR-0042, ADR-0043, ADR-0045, ADR-0046, ADR-0047,
  ADR-0048, and ADR-0049
- Supersedes in part:
  - ADR-0006's statement that VM itself installs the final child mappings and
    PM supplies the initial execution context. VM still chooses frames and
    mapping policy, but the kernel constructs the exact page tables at freeze;
    the initial context is derived from the kernel load token.
  - ADR-0009's statement that PM presents an initial context during spawn.
    PM presents only reservation, load, mapping-generation, and prepared
    tokens; PC, SP, U-mode status, and the fixed scheduler policy are
    kernel-held load metadata.
  - ADR-0021's prohibition on mutating an active process root. One exact
    operation-12 scratch transition may edit the current VM root with SIE
    clear, an exact load phase, and local per-page fences. Every other
    active-root mutation remains forbidden.
  - ADR-0040's prohibition on mapping or validating `VM_TRANSFERABLE` frames.
    One exact post-handoff VM scratch alias may map one transaction-bound
    transferable frame into VM while it is being initialized. Complete-root
    validation accepts only that exact attached-VM-root/load-token exception,
    whether VM's root is currently active or inactive. No child mapping may
    contain a transferable frame.
  - ADR-0045's original operation-12 single-command boundary and prohibition
    on post-handoff mapping mutation. Operation 12 retains that handoff
    command and ADR-0047's exact TTY UART command, then gains only the bounded
    VM load, rollback, commit, and exit-release commands below.
  - ADR-0047's statement that every non-device handed-off leaf is
    `VM_WIRED`. The exact VM-root scratch leaf below may instead be
    `VM_TRANSFERABLE`; the TTY device exception and every other leaf rule
    remain unchanged.
  - ADR-0046's deferred spawn wire formats, mapping token, execution
    preparation, activation, rollback, and normal-exit details. Its lifecycle
    states, authority split, PID policy, and publication ordering remain
    authoritative.
  - ADR-0046's immediate staging of a reparented zombie for init's outstanding
    wait during parent exit-begin. Selection is deferred until the exiting
    process becomes a zombie. PM then independently reevaluates init's
    adopted-child set and the exiting process's original parent's child set,
    deduplicating the two scans when both parent handles are init.
  - ADR-0049's 64-page VFS resident-image limit. The limit becomes 80 pages
    solely for one bounded executable buffer and one canonical request copy.

## Context

Development-DAG Step 13 is complete:

- the static launcher, VM, PM, TTY, RAMFS, and VFS services start in
  dependency order and reach their sealed runtime states;
- VM owns the handed-off physical-frame database but cannot yet create or
  release a dynamic address space;
- PM owns semantic PIDs, parentage, hidden spawn stages, exit, zombie, and
  wait metadata but intentionally rejects every successful public spawn;
- VFS owns path routing, executable authorization policy, process filesystem
  state, descriptors, open-file descriptions, and application I/O;
- the kernel can reserve and abort one empty process for PM but cannot yet
  bind post-handoff mappings or prepare a dynamic execution context; and
- the immutable `APPLICATION` profile may call only PM and VFS.

The next dependency-ready outcome is the ELF spawn path, graph node P. It must
prove that the static services can create, run, normally exit, reap, and fully
release one dynamic application without granting generic mapping authority or
publishing a partially initialized child.

The fixed MINIX behavioral trace is documented in
[the MINIX ELF execution and spawn baseline](../research/minix-elf-exec-and-spawn.md).
The reusable baseline is:

- PM owns lifecycle completion;
- VFS copies arguments before destructive work, resolves the path, checks the
  executable object, and owns descriptor state;
- VM owns address-space memory policy;
- the loader validates file ranges, copies loadable segments, zero-fills
  anonymous bytes, and builds a separate stack;
- the kernel installs final execution state; and
- exit first stops execution, then releases VFS, kernel, and VM state before
  retaining a PM-only zombie.

MINIX's destructive `exec`, partial-exec kill behavior, transitive segment
copy, broader ELF formats, demand mappings, and fork-based `posix_spawn` are
not adopted. ADR-0009 requires direct spawn before fork, and the accepted
`micros` authority model requires direct grants and a hidden, rollbackable
child.

## Decision

Implement one globally serialized, PM-directed spawn and normal-exit
transaction for statically linked RISC-V ELF64 applications.

A successful spawn:

1. copies and validates one canonical pointer-free request;
2. consumes one semantic PID and reserves one empty kernel process;
3. asks VFS to resolve and buffer the executable and request;
4. asks VM to validate the complete ELF, build a private Sv39 address space,
   initialize the image and stack through one VM-only scratch alias, remove
   that alias, and freeze one exact mapping generation;
5. asks VFS to reserve root, working-directory, and explicit descriptor
   duplication state;
6. asks the kernel to create a held thread, reserved endpoint, immutable
   `APPLICATION` profile, scheduler assignment, and token-bound initial
   context;
7. commits PM and VFS hidden metadata;
8. invokes one final kernel activation that publishes the endpoint and makes
   the sole thread runnable; and
9. commits the activated mapping into VM's persistent address-space table and
   releases the VFS image transaction before replying to the parent with the
   child PID.

Every expected failure before activation returns one stable spawn error after
complete reverse-order rollback. Activation is the sole publication point.
No operation may return a success-shaped fallback after partial mutation.

An accepted application `EXIT` never replies. PM permanently stops the
reply-blocked thread, obtains one exit transaction, releases VFS process
state, detaches kernel execution resources, asks VM to release every child
mapping and frame, releases the empty kernel process, and retains one PM-only
zombie until a successful wait.

## Scope

This decision includes:

- one public PM `SPAWN` request;
- one trusted parentless init-spawn entry point used by the node-P QEMU
  fixture;
- a canonical path, argv, envp, and descriptor-action object;
- a bounded static RISC-V ELF64 subset;
- a one-page initial application stack and application-specific startup path;
- VFS executable buffering and hidden descriptor preparation;
- a production post-handoff VM loader and frame-release core;
- bounded extensions to kernel operations 12 and 13;
- local instruction-fetch synchronization;
- normal application return through PM `EXIT`;
- complete expected prepublication rollback;
- complete normal exit, zombie, and wait cleanup; and
- native, host, QEMU component, and resource-baseline evidence.

## Non-goals

This decision does not add:

- destructive `exec`;
- `fork`, copy-on-write, or `posix_spawn` compatibility;
- position-independent executables, shared objects, a dynamic linker, or
  relocations;
- interpreter or `#!` script handling;
- TLS, constructors, destructors, auxiliary platform services, or libc;
- credentials, setuid, setgid, executable capabilities, or mount flags;
- demand paging, file-backed mappings, page replacement, or overcommit;
- multiple application threads or multiple harts;
- signals or conversion of application faults into PM exit notifications;
- arbitrary descriptor close, move, flag, or close-on-exec actions;
- a generic application-config mapping;
- production init, shell, `echo`, `cat`, `ls`, or `ps`; or
- concurrent spawn or exit transactions.

The node-P PID-1 program is an isolated integration fixture. Production init
and shell remain graph node Q.

## Fixed bounds

The first implementation uses:

```text
page size                              = 4,096 bytes
executable file maximum                = 131,072 bytes
spawn request size maximum             = 4,096 bytes
path size maximum including NUL        = 256 bytes
argument count maximum                 = 8
environment count maximum              = 8
descriptor action count maximum        = 8
one string size maximum including NUL  = 256 bytes
application descriptor capacity        = 16
application image pages                = 3 through 15
application stack pages                = 1
initial stack occupied maximum         = 2,048 bytes
runtime stack reserve below initial sp = 2,048 bytes
application resident-page maximum      = 16
page-table frames per application      = 4
active spawn transactions              = 1
active exit transactions               = 1
application threads per process        = 1
harts                                  = 1
application scheduler hart             = 0
application scheduler priority         = 9
application scheduler quantum          = 1,000,000 counter ticks
application scheduler preemptible      = true
```

The user virtual-address interval remains:

```text
[0x0000000040000000, 0x0000000080000000)
```

The external stack page remains:

```text
[0x000000007ffff000, 0x0000000080000000)
```

The application image end is at or below:

```text
0x000000004000f000
```

Fifteen image pages plus one stack page are the complete resident application
shape. No guard, configuration, request, executable-buffer, or scratch page is
mapped into the child.

The exact service-image bounds become:

```text
PM resident pages   <= 32
VM resident pages   <= 160
VFS resident pages  <= 80
```

The bound includes every load page plus the service's external stack page.
The VFS increase covers a 32-page executable buffer and one request page.
The currently checked VM image occupies 94 resident pages including its
external stack. The fixed additions are:

```text
executable buffer                 32 pages
request buffer                     1 page
64 dynamic mapping records         8 pages
measured baseline plus additions 135 pages
remaining VM budget               25 pages
```

The remaining 25 pages cover loader code and smaller transaction/control
state. Standalone image checks and the complete manifest check enforce the
authoritative final bound.

## Authority

PM is the sole authority for:

- parent validation;
- semantic PID consumption;
- PM record and parent/reaper metadata;
- directed stage ordering;
- public spawn result selection;
- kernel reservation, preparation, activation, abort, and exit commands;
- normal exit status;
- zombie publication; and
- wait completion.

VFS is the sole authority for:

- executable path resolution;
- regular-file and execute-bit policy;
- complete executable and request buffering;
- VFS-to-VM direct grants;
- root and working-directory inheritance;
- descriptor-action semantics;
- open-file-description references;
- hidden filesystem-process commit;
- cancellation and release of exiting-process VFS state; and
- executable transaction release.

VM is the sole authority for:

- runtime ELF validation;
- application virtual-memory policy;
- physical-frame selection;
- page-content construction;
- initial-stack construction;
- selection of four page-table frames;
- synchronization of its local frame database with kernel ownership;
- mapping freeze and rollback requests; and
- exit-time mapping and frame release.

The kernel is the sole authority for:

- frame-owner transitions;
- VM scratch mapping and removal;
- child PTE construction;
- root installation;
- mapping generations and load tokens;
- process, thread, endpoint, profile, context, and scheduler objects;
- instruction-fetch synchronization;
- activation and endpoint visibility;
- permanently stopping an exiting thread;
- IPC, reply-token, and grant teardown; and
- final empty-process release.

Applications may call PM and VFS. They cannot call VM, TTY, RAMFS, launcher,
or a kernel lifecycle/mapping operation.

The PM immutable profile keeps its existing launcher target and adds only the
exact manifest VM and VFS endpoint generations. VM keeps only its operation-12
kernel authority. VFS keeps its exact TTY and RAMFS call targets and existing
grant authority. No wildcard target or operation mask is introduced.

## Transaction identities

PM, VFS, VM, and the kernel each own a separate unsigned 64-bit monotonic
counter for the tokens they issue. Zero is invalid. A counter advances before
publication, never reuses a value during the boot, and treats wraparound as
fatal exhaustion.

The tokens are:

```text
PM spawn token
kernel process-reservation token
VFS image token
VFS descriptor token
kernel load token
kernel mapping generation
kernel prepared token
kernel exit token
```

A token is opaque outside its owner. It grants no authority by itself:
validation always combines exact caller, token, expected phase, and the
complete bound object generation. Successful abort, commit, activation,
release, or exit consumes the tokens whose phase ends. Successful activation
specifically consumes both the process-reservation and prepared tokens while
retaining the live load token and mapping generation. A duplicate or stale
operation never becomes idempotent success.

## Canonical spawn request

The public request is one pointer-free object copied through a direct grant.
All multibyte fields use little-endian encoding. PM, VFS, VM, and host tests
decode bytes explicitly rather than casting untrusted storage to a C
structure.

Version 1 has a fixed 256-byte header:

```text
bytes   0..3    version = 1
bytes   4..7    header size = 256
bytes   8..11   total size in [256, 4096]
bytes  12..15   flags = 0
bytes  16..23   path string reference
bytes  24..25   argument count in [1, 8]
bytes  26..27   environment count in [0, 8]
bytes  28..29   descriptor action count in [0, 8]
bytes  30..31   zero
bytes  32..95   eight argument string references
bytes  96..159  eight environment string references
bytes 160..223  eight descriptor actions
bytes 224..255  zero
bytes 256..end  densely packed path, argument, and environment strings
```

A string reference is exactly eight bytes:

```text
bytes 0..1  unsigned offset from object start
bytes 2..3  unsigned size including terminal NUL
bytes 4..7  zero
```

A descriptor action is exactly eight bytes:

```text
bytes 0..1  parent source descriptor
bytes 2..3  child destination descriptor
bytes 4..7  operation
```

The only version-1 operation is:

```text
MICROS_SPAWN_DESCRIPTOR_DUPLICATE = 1
```

Canonical validation requires:

- the complete fixed header is present;
- every reserved field and unused table entry is zero;
- the path reference begins at offset 256;
- live string references appear in exact path, argv, then envp order;
- each live string begins exactly after the preceding string;
- the final string ends exactly at `total_size`;
- every string size is in `[1, 256]`;
- the final byte of each string is NUL and no earlier byte is NUL;
- path and `argv[0]` contain at least one non-NUL byte;
- every environment string contains `=` after at least one name byte;
- every source and destination descriptor is below 16;
- every destination descriptor is unique;
- every live action has operation `DUPLICATE`;
- no overflow occurs while computing any bound or initial-stack size; and
- the derived aligned initial-stack occupancy is at most 2,048 bytes, leaving
  at least 2,048 bytes below initial `sp`.

A source descriptor may appear in multiple actions. This intentionally permits
duplication of one shared open-file description into multiple child
descriptors. Actions are applied in listed order to an initially empty child
table, but unique destinations make the result independent of overwrites.

PM performs complete canonical validation before consuming a PID. VFS and VM
independently revalidate the copied object before using it. Structural
malformation is not deferred to descriptor or ELF policy. A structurally
canonical request whose derived stack exceeds the occupancy bound returns
`TOO_BIG` before PID consumption.

## Public PM protocol

The existing PM protocol gains:

```text
MICROS_PM_MESSAGE_SPAWN = 0x00010004
```

The 48-byte spawn payload is:

```text
bytes  0..3   protocol version = 1
bytes  4..7   flags = 0
bytes  8..11  caller-created read-grant token naming PM as grantee
bytes 12..15  spawn request size in [256, 4096]
bytes 16..47  zero
```

The kernel-written source endpoint identifies the parent. Payload data never
selects a parent or process authority.

The parent runtime:

1. constructs a canonical resident request;
2. creates one read grant targeted to the exact PM endpoint;
3. calls PM;
4. retains the request unchanged until the call completes; and
5. revokes the grant after every returning result.

PM copies exactly the declared bytes into a zeroed resident page before
consuming a PID. A grant copy failure returns `GRANT` without lifecycle
mutation.

The existing `MICROS_PM_MESSAGE_RESULT` layout remains unchanged. For a
successful spawn:

```text
original request type = SPAWN
result                = OK
child PID             = new semantic PID
exit kind             = NONE
exit code             = 0
```

For every failed spawn, child PID, exit kind, exit code, flags, and reserved
bytes are zero.

Stable PM results become:

```text
  0  OK
 -1  BAD_TYPE
 -2  BAD_VERSION
 -3  MALFORMED
 -4  CALLER
 -5  STATE
 -6  BUSY
 -7  NO_CHILD
 -8  NO_SPACE
 -9  NOT_FOUND
-10  ACCESS
-11  EXECUTABLE
-12  TOO_BIG
-13  GRANT
-14  IO
-15  DESCRIPTOR
```

Validation and result precedence for `SPAWN` is:

1. known PM message type;
2. fixed PM payload version, flags, grant field, size, and zero tail;
3. exact source endpoint resolving to one `RUNNING` application;
4. PM runtime-enabled phase and complete PM invariants;
5. direct-grant validation and complete copy;
6. canonical spawn-request structural validation;
7. derived aligned stack occupancy at most 2,048 bytes;
8. PM process/PID capacity and parent child capacity;
9. one lifecycle reservation; and
10. collaborator stages in directed order.

Steps 1 through 8 do not consume a PID. Once PM reserves the record, every
expected later failure consumes that PID as required by ADR-0046.

Collaborator results map as follows:

- capacity exhaustion maps to `NO_SPACE`;
- missing path maps to `NOT_FOUND`;
- a nondirectory traversal result and a non-regular or non-executable final
  path map to `ACCESS`;
- a copied path component longer than `MICROS_VFS_NAME_MAX` maps to
  `TOO_BIG`;
- an empty executable snapshot or runtime ELF rejection maps to
  `EXECUTABLE`;
- executable or derived-stack size excess maps to `TOO_BIG`;
- only validation or copy failure of the caller-created parent-to-PM grant
  maps to `GRANT`;
- expected global grant-registry exhaustion while a trusted service creates
  an internal direct grant maps to `NO_SPACE`;
- RAMFS read failure maps to `IO`; and
- descriptor-action semantic failure maps to `DESCRIPTOR`.

Unknown commands, token mismatch, impossible stage, malformed PM-generated
internal messages, post-mutation validation failure, or local/kernel state
disagreement is corruption and is fatal. After a trusted PM, VFS, or VM grant
is created successfully, any internal grant validation, copy, or revocation
failure is likewise fatal. None is mapped to a user result.

## Trusted init transaction

PM exposes one internal function for a parentless init transaction. It uses
the same canonical request, PID reservation, VFS, VM, kernel, rollback, and
activation path as public spawn, with these differences:

- no application call or reply token exists;
- the request path must be absolute;
- PID 1 is consumed;
- parent is `NONE`;
- the process becomes the sole reaper;
- descriptor action count must be zero; and
- VFS installs console descriptors 0, 1, and 2 through its trusted console
  path.

The public `SPAWN` flags cannot request init behavior. The node-P QEMU build
uses one fixture-only PM trigger and fixture path. It begins only after PM
receives the existing kernel-origin launcher-sealed event, which proves VFS
readiness and irreversible bootstrap completion. Production init selection
and launch policy remain node Q.

## VFS executable transaction

VFS owns one global spawn transaction:

```text
IDLE
IMAGE_PREPARED
DESCRIPTORS_PREPARED
HIDDEN_COMMITTED
```

The transaction stores:

- a nonzero VFS image token;
- the exact PM spawn token;
- child semantic PID;
- exact parent endpoint or `NONE` for trusted init;
- init mode;
- a complete canonical request copy;
- the complete executable file and exact byte count;
- exact VM-targeted executable and request grant tokens;
- resolved executable vnode reference;
- reserved child root and working-directory references;
- reserved child descriptor/open-file-description references; and
- the exact prepared child endpoint after hidden commit.

### Image preparation

Only the exact PM endpoint may request image preparation.

PM first creates the PM-to-VFS request grant over its immutable request page.
Global grant-registry exhaustion returns public `NO_SPACE` before the VFS
call. Any other creation failure is fatal because PM generated the exact
wired range and endpoint tuple.

VFS:

1. validates the internal message and PM-to-VFS request grant;
2. copies the complete request into its resident page;
3. revalidates the canonical object;
4. checks every non-`.`/`..` path component against
   `MICROS_VFS_NAME_MAX`, returning `TOO_BIG` for a longer component;
5. resolves the path from the mounted root for trusted init or from the
   parent's exact root and working directory for public spawn;
6. maps RAMFS `NOT_DIRECTORY` to `ACCESS` and requires a regular RAMFS file
   with at least one execute bit set;
7. rejects a zero-byte file as `EXECUTABLE` and a file larger than 131,072
   bytes as `TOO_BIG` before grant creation;
8. reads the exact complete file through the existing RAMFS protocol into a
   zeroed resident executable buffer;
9. treats early EOF, changing file size, or backend inconsistency as `IO`;
10. creates separate direct read grants from VFS to the exact VM endpoint for
   the nonzero executable bytes and complete request bytes; and
11. returns one nonzero image token plus the two grant/size pairs.

The VFS buffer is the immutable transaction snapshot. Later modification or
unlink of the RAMFS vnode does not change the child image.

VFS does not parse ELF policy, allocate frames, construct stack pointers, or
forward PM's grant.

Once PM's request grant exists, VFS validation or copy failure is fatal.
VFS-created RAMFS transfer grants and VM-targeted grants may return
`NO_SPACE` only when the global registry has no free slot before creation.
Any other creation failure, any backend `GRANT` result for a successfully
created trusted grant, or any validation/copy failure is fatal.

Every expected preparation failure clears any partial request or executable
copy, releases temporary vnode references, creates no VM grant or image token,
and returns to `IDLE`. PM revokes its request grant after every returning
result.

### Descriptor preparation

After VM freeze, PM asks VFS to prepare descriptors for the same transaction.

For a public child, VFS:

- requires the parent endpoint still identify the same live VFS process;
- duplicates the parent's root and working-directory vnode references;
- requires every source descriptor name a live parent descriptor;
- reserves each unique child destination below 16;
- increments the referenced open-file-description count once per action; and
- performs no visible child insertion.

For trusted init, VFS:

- uses the mounted root as both root and working directory;
- requires zero user descriptor actions; and
- reserves console descriptors 0, 1, and 2 through the existing trusted
  console-attach mechanism.

All references are reserved before success. Expected failure releases every
new reference and leaves the transaction at `IMAGE_PREPARED`.

### Hidden commit

After kernel execution preparation, PM supplies the exact reserved child
endpoint and descriptor token. VFS validates that:

- the endpoint is nonzero and differs from every live VFS process endpoint;
- the transaction, image, and descriptor tokens match;
- the parent and every reserved reference remain valid; and
- the endpoint cannot yet be used because the kernel still holds it reserved.

VFS then installs the child process record and reserved references keyed by
that endpoint. The record is complete and application requests from that exact
endpoint are valid once kernel activation makes it active. No later VFS
publication call is required, avoiding a runnable-before-VFS-visible race.

Before activation, rollback may remove this hidden record because no caller
can present the inactive source endpoint.

### Image release

After successful activation, PM must release the image transaction before
replying to the parent. VFS:

- revokes both VM-targeted grants;
- clears the executable and request buffers;
- releases the executable vnode reference;
- invalidates image and descriptor transaction tokens; and
- returns to `IDLE` while retaining the now-normal child VFS record.

At that point failure is impossible after complete token validation. Any
grant revocation failure, other failure, or state mismatch is fatal because
activation has already published the child.

## Internal VFS control protocol

The private IPC message types are:

```text
MICROS_VFS_MESSAGE_PM_CONTROL        = 0x00040009
MICROS_VFS_MESSAGE_PM_CONTROL_RESULT = 0x0004000a
```

The exact PM-to-VFS commands are:

```text
MICROS_VFS_CONTROL_PREPARE_IMAGE       = 1
MICROS_VFS_CONTROL_PREPARE_DESCRIPTORS = 2
MICROS_VFS_CONTROL_COMMIT_HIDDEN       = 3
MICROS_VFS_CONTROL_REVERT_HIDDEN       = 4
MICROS_VFS_CONTROL_ABORT_DESCRIPTORS   = 5
MICROS_VFS_CONTROL_RELEASE_IMAGE       = 6
MICROS_VFS_CONTROL_EXIT_PROCESS        = 7
```

They are accepted only from the exact PM endpoint. Every request and reply
uses six little-endian 64-bit payload words. Request word 0 stores version 1
in bits 31..0 and the command in bits 63..32. Tokens use complete 64-bit
words. A grant/size pair stores the 32-bit grant token in bits 31..0 and the
unsigned 32-bit size in bits 63..32. An endpoint word stores the endpoint in
bits 31..0 and zero in bits 63..32. Every unused bit and word is zero.

`PREPARE_IMAGE` carries:

```text
word 0  version = 1, command
word 1  PM spawn token
word 2  child semantic PID
word 3  parent endpoint, or zero for trusted init
word 4  PM-to-VFS request grant and size
word 5  trusted-init bit only
```

`PREPARE_DESCRIPTORS` carries:

```text
word 0  version = 1, command
word 1  PM spawn token
word 2  VFS image token
word 3  child semantic PID
word 4  zero
word 5  zero
```

`COMMIT_HIDDEN` and `REVERT_HIDDEN` carry:

```text
word 0  version = 1, command
word 1  PM spawn token
word 2  VFS image token
word 3  VFS descriptor token
word 4  reserved child endpoint
word 5  zero
```

`ABORT_DESCRIPTORS` carries:

```text
word 0  version = 1, command
word 1  PM spawn token
word 2  VFS image token
word 3  VFS descriptor token
word 4  zero
word 5  zero
```

`RELEASE_IMAGE` carries:

```text
word 0  version = 1, command
word 1  PM spawn token
word 2  VFS image token
word 3  zero
word 4  zero
word 5  zero
```

`EXIT_PROCESS` carries:

```text
word 0  version = 1, command
word 1  kernel exit token
word 2  closing child endpoint
word 3  child semantic PID
word 4  zero
word 5  zero
```

Every result carries:

```text
word 0  version = 1 in bits 31..0, echoed command in bits 63..32
word 1  signed 32-bit control result in bits 31..0, zero in bits 63..32
word 2  command-specific output or zero
word 3  command-specific output or zero
word 4  command-specific output or zero
word 5  zero
```

The private stable control results are:

```text
  0  OK
 -1  MALFORMED
 -2  CALLER
 -3  STATE
 -4  NO_SPACE
 -5  NOT_FOUND
 -6  ACCESS
 -7  TOO_BIG
 -8  IO
 -9  DESCRIPTOR
-10  EXECUTABLE
```

`MALFORMED`, `CALLER`, and `STATE` from a PM-generated request are fatal to
PM. The remaining expected results map to the public PM result of the same
name. For `PREPARE_IMAGE`, VFS maps ordinary RAMFS `NOT_DIRECTORY` to
`ACCESS`; its explicit copied-component bound maps caller-controlled path
`RANGE` to `TOO_BIG` before lookup. Because the copied path is at most 256
bytes and trusted root/start handles are validated before the call, any later
RAMFS `RANGE` is local/backend disagreement and fatal rather than a public
result. No VFS control result represents an internal grant failure.

Successful `PREPARE_IMAGE` returns:

```text
word 2  VFS image token
word 3  VFS-to-VM executable grant and byte size
word 4  VFS-to-VM request grant and byte size
```

Successful `PREPARE_DESCRIPTORS` returns the nonzero descriptor token in
word 2. Every other successful command returns zero output words.

`EXIT_PROCESS` carries the kernel exit token and exact active/closing child
endpoint. It cancels pending console work, releases descriptors and open-file
references, releases root and working-directory references, invalidates the
VFS process record, and returns success only after no VFS-owned child state
remains.

## Runtime ELF subset

VM validates the complete buffered executable before beginning any kernel
load transaction.

The accepted ELF header is exactly:

```text
class             ELFCLASS64
data              ELFDATA2LSB
ident version     EV_CURRENT
OS ABI            ELFOSABI_SYSV
ABI version       0
ident padding     zero
type              ET_EXEC
machine           EM_RISCV
ELF version       EV_CURRENT
entry             0x0000000040000000
ELF header size   64
program offset    64
program entry size 56
program count     3
flags             no bits except optional EF_RISCV_RVC
```

The runtime parser ignores the section table. It never follows a section,
symbol, string-table, relocation, interpreter, dynamic, note, or TLS offset.
Exactly three program headers means every header is `PT_LOAD`; no
`PT_INTERP`, `PT_DYNAMIC`, or `PT_TLS` can be present.

The ordered program headers must be:

| Index | Flags | Purpose |
| --- | --- | --- |
| 0 | `PF_R | PF_X` | text |
| 1 | `PF_R` | read-only data |
| 2 | `PF_R | PF_W` | data and BSS |

For every segment:

- `p_align` is exactly 4,096;
- `p_offset`, `p_vaddr`, `p_paddr`, and `p_memsz` are page-aligned;
- `p_paddr == p_vaddr`;
- `p_memsz` is nonzero;
- `p_filesz <= p_memsz`;
- `p_offset mod 4096 == p_vaddr mod 4096`;
- `p_offset + p_filesz` is within the buffered file;
- `p_vaddr + p_memsz` is overflow-free and within user range;
- flags are exact, so no page is writable and executable; and
- every arithmetic operation is checked before comparison or rounding.

The image shape must also satisfy:

- segment 0 begins at `0x40000000`;
- each later segment begins exactly at the prior segment end;
- segment file ranges are in program-header order and do not overlap;
- the entry point lies in segment 0;
- the union contains at most 15 pages;
- the image does not intersect VM's scratch VA or the application stack page;
  and
- no mapping exists outside the three segments and one stack page.

VM zeroes every destination page before copying the exact intersection with
each segment's `p_filesz`. Bytes from `p_filesz` through `p_memsz`, page
fragments not covered by file data, and the complete initial stack page begin
as zero.

VFS rejects an empty file before grant creation. A truncated header, short
program table, invalid range, invalid permission, unsupported field, or wrong
shape returns `EXECUTABLE` from VM before any frame allocation. A file over
131,072 bytes or image over 15 pages returns `TOO_BIG`.

## Host application-image validation

Host validation is intentionally stronger than runtime validation.

The build adds an application-specific checker that validates the full link
ELF before any stripping:

- the complete runtime ELF subset above;
- exact linker symbols and segment boundaries;
- the application-specific `_start` instruction sequence;
- the canonical runtime entry symbol;
- no undefined symbols;
- no dynamic sections, interpreter, TLS, relocations, PLT, GOT-based dynamic
  binding, constructors, or destructors;
- no allocatable section outside the three load segments;
- no writable executable section;
- one external stack boundary at `0x80000000`; and
- the 16-page resident and 128-KiB file budgets.

The RAMFS seed may use a stripped runtime artifact only after the full link
ELF passes. A second checker validates the seeded artifact and proves its ELF
header, program headers, entry, and every `PT_LOAD` byte match the checked
link ELF. Debug or symbol metadata may differ; executable bytes may not.

The existing service-image checker remains authoritative for static services.
The application checker is separate because applications use a different
startup contract and do not contain a bootstrap service configuration.

## Initial application stack

VM constructs one complete stack page from the canonical request.

The downward-growing frame begins at a 16-byte-aligned `sp` and contains:

```text
uint64_t argc
uint64_t argv[argc + 1]
uint64_t envp[envc + 1]
zero padding to a 16-byte boundary
struct micros_application_auxv auxv[5]
densely packed argv and envp strings
```

Every pointer is a child virtual address within the same stack page. The final
`argv` and `envp` entries are zero. Strings retain request order and terminal
NUL bytes. The executable path is not copied unless the caller also placed it
in `argv`.

Each auxiliary entry is two little-endian 64-bit words: type and value. The
exact ordered entries are:

```text
MICROS_AT_PM_ENDPOINT  = 1, exact PM endpoint
MICROS_AT_VFS_ENDPOINT = 2, exact VFS endpoint
MICROS_AT_PID          = 3, child semantic PID
MICROS_AT_PAGE_SIZE    = 4, value 4096
MICROS_AT_NULL         = 0, value 0
```

Endpoint values require zero upper 32 bits. PID retains the complete unsigned
64-bit semantic value. PM's source endpoint, VM's manifest VFS endpoint, and
PM's child PID are the authorities for these values; request bytes cannot
select them.

The frame size is computed with checked arithmetic. VM chooses:

```text
sp = align_down(0x80000000 - frame_size, 16)
occupied = 0x80000000 - sp
```

`occupied` must be at most 2,048 bytes, so:

```text
sp >= 0x000000007ffff800
```

PM checks the same bound before PID consumption, and VFS and VM independently
revalidate it. Alignment gaps and the complete region below `sp` remain zero.
The at-least-2,048-byte region below `sp` is the initial downward-growing
runtime stack reserve; it is not part of argv/env storage. Exceeding the bound
returns `TOO_BIG`.

VM records the exact entry and `sp` in the frozen load token. Every general
register except `sp` is initially zero. The application startup assembly:

1. disables linker relaxation in the startup sequence;
2. explicitly clears `gp` and `tp`;
3. reads and validates `argc`, `argv`, `envp`, and the exact ordered auxv from
   the stack;
4. stores PM endpoint, VFS endpoint, PID, and page size in
   application-runtime globals;
5. calls:

   ```c
   int micros_application_main(
       int argc,
       char *argv[],
       char *envp[]
   );
   ```

6. converts a normal return to a PM `EXIT` call with the returned signed
   32-bit code; and
7. executes a fatal breakpoint loop if accepted exit unexpectedly returns.

Malformed startup state never calls application code.

This is a separate application runtime and entry point. The no-argument
service `_start` and `micros_service_main` contract in ADR-0042 remain
unchanged.

## VM loader transaction

VM accepts load and exit requests only from the exact PM endpoint. It owns one
global loader transaction with:

```text
IDLE
INPUT_COPIED
LOADING
FROZEN
EXIT_RELEASING
```

Separately, VM retains at most `MICROS_PROCESS_CAPACITY` active dynamic
address-space records. Successful post-activation load commit moves the frozen
transaction into one such record and returns the loader transaction to
`IDLE`, so a running child does not prevent a later sequential spawn.

Its resident transaction state includes:

- the kernel process-reservation token;
- the kernel-issued nonzero load token;
- the exact kernel process handle returned by `LOAD_BEGIN`;
- exact child PID;
- complete executable and request copies;
- validated segment and stack plans;
- at most 16 ordered destination-page records;
- four selected page-table frame addresses;
- current scratch mapping, if any;
- nonzero mapping generation after freeze; and
- a complete local-frame rollback ledger.

Each active address-space record retains the process handle, semantic PID,
load token, mapping generation, root frame, ordered data-frame mappings, and
four page-table frames until normal exit release.

VM extends its mutable post-handoff frame view with local-only
`LOAD_RESERVED`, `PROCESS_WIRED`, and `PROCESS_PAGE_TABLE` states.
`LOAD_RESERVED` is used only for the four selected table frames while the
kernel still classifies them as `FREE`; it prevents VM from selecting them as
data frames. Successful kernel transitions are mirrored locally before VM
handles another message.

Exact dynamic ownership is not a second per-managed-frame table. VM owns
exactly 64 slot-indexed 512-byte mapping records:

```text
32-byte header:
  uint64 load_token
  uint64 mapping_generation
  uint64 semantic_pid
  uint32 process_generation
  uint16 process_slot
  uint8  phase
  uint8  entry_count

20 entries, each 24 bytes:
  uint64 virtual_address
  uint64 physical_address
  uint32 permissions
  uint8  role
  uint8  reserved[3]
```

The roles cover at most 16 user pages and exactly four private page-table
frames. The record indexed by the reserved kernel process slot holds the
hidden load transaction and later becomes that process's active mapping
record, so no 65th record is needed. An unused record is all zero.

The array is exactly:

```text
64 * 512 = 32,768 bytes = 8 pages
```

VM derives exact process ownership by scanning the nonzero records and
requires set equality between record entries and all local
`PROCESS_WIRED`/`PROCESS_PAGE_TABLE` frame-state bytes. A physical frame
appears in at most one record entry. Free-frame selection requires both the
local byte state `FREE` and absence from every dynamic record.

These are runtime-only values. The version-1 boot-information object and its
accepted initial frame-state values remain unchanged; VM introduces the
extended values only after successful handoff.

### VM control protocol

The private IPC message types are:

```text
MICROS_VM_MESSAGE_PM_CONTROL        = 0x00050001
MICROS_VM_MESSAGE_PM_CONTROL_RESULT = 0x00050002
```

Commands are:

```text
MICROS_VM_CONTROL_LOAD         = 1
MICROS_VM_CONTROL_ABORT_LOAD   = 2
MICROS_VM_CONTROL_COMMIT_LOAD  = 3
MICROS_VM_CONTROL_EXIT_RELEASE = 4
```

Every request and result uses six little-endian 64-bit payload words. Word 0
stores version 1 in bits 31..0 and the command in bits 63..32. Grant/size and
endpoint words use the same canonical packing as the VFS control protocol.
Every unused bit and word is zero.

`LOAD` carries:

```text
word 0  version = 1, command = LOAD
word 1  kernel process-reservation token
word 2  child semantic PID
word 3  VFS executable grant and byte size
word 4  VFS request grant and byte size
word 5  zero
```

VM requires both grants name the exact VFS endpoint as grantor and the exact
VM endpoint as grantee. It copies the executable in ascending chunks of at
most one page and the request in one page-bounded copy into zeroed resident
buffers. Every chunk uses the existing checked direct-grant operation and the
immutable VFS transaction snapshot. VM revalidates the request, validates the
ELF, constructs the complete page and stack plans, and only then invokes
kernel `LOAD_BEGIN`.

Because VFS created both grants from exact wired resident buffers, any grant
validation or copy failure is fatal. An expected executable, size, or
frame-capacity failure before `LOAD_BEGIN` clears both buffers and every
transient plan and returns VM to `IDLE`.

`ABORT_LOAD` and `COMMIT_LOAD` carry:

```text
word 0  version = 1, command
word 1  kernel load token
word 2  mapping generation
word 3  child semantic PID
word 4  zero
word 5  zero
```

`EXIT_RELEASE` carries:

```text
word 0  version = 1, command
word 1  kernel exit token
word 2  kernel load token
word 3  mapping generation
word 4  child semantic PID
word 5  zero
```

Every result carries:

```text
word 0  version = 1 in bits 31..0, echoed command in bits 63..32
word 1  signed 32-bit control result in bits 31..0, zero in bits 63..32
word 2  command-specific output or zero
word 3  command-specific output or zero
word 4  command-specific output or zero
word 5  command-specific output or zero
```

The private stable VM results are:

```text
 0  OK
-1  MALFORMED
-2  CALLER
-3  STATE
-4  NO_SPACE
-5  EXECUTABLE
-6  TOO_BIG
```

`MALFORMED`, `CALLER`, and `STATE` from a PM-generated request are fatal to
PM. Other expected failures map to the public PM result of the same name. No
VM control result represents a VFS-created grant validation or copy failure;
such a failure is fatal.

Successful `LOAD` returns:

```text
word 2  kernel load token
word 3  mapping generation
word 4  entry
word 5  stack pointer
```

Other successful commands return zero output words. PM treats entry and stack
pointer as diagnostic cross-checks. Kernel preparation uses token-bound values
retained by the kernel, not PM-supplied replacements.

`ABORT_LOAD` invokes kernel load abort, clears every local transaction and
reservation state, and returns VM to `IDLE`.

After kernel activation, `COMMIT_LOAD` invokes kernel load commit, moves the
frozen frame ledger into one active address-space record, clears the input
buffers and transient plans, and returns the loader transaction to `IDLE`.
Because the child is already published, this exact command is mandatory and
non-failing after token validation; disagreement is fatal.

`EXIT_RELEASE` requires loader `IDLE`, finds the exact active record, invokes
kernel exit release, mirrors every released frame as `FREE`, clears the active
record, and acknowledges only after no VM-owned child state remains.

### Frame selection

VM chooses the lowest-address eligible free physical frames from its handed-
off database. It reserves:

- one data frame for every image page;
- one data frame for the stack page; and
- exactly four page-table frames.

The four page-table choices enter VM-local `LOAD_RESERVED` state before data
frame selection. Kernel ownership remains `FREE` until freeze. Any failure
before freeze clears the local reservations after aborting transferable data
frames.

Deterministic assignment is exact: the first four eligible physical frames in
ascending order become root, level-1, image-leaf, and stack-leaf candidates;
the next eligible frames are assigned to child destination pages in ascending
virtual-address order.
VM proves the complete required set exists before `LOAD_BEGIN`, so ordinary
frame exhaustion causes no kernel mutation.

The four Sv39 page-table frames are:

```text
one root
one level-1 table for VPN2 index 1
one level-0 table for the image branch near 0x40000000
one level-0 table for the stack branch near 0x80000000
```

The private root's VPN2 index 1 points to the new application level-1 table.
Every other root entry is copied byte-for-byte from the immutable kernel root
and remains supervisor-only. Those shared kernel subtrees consume no child
frame and are not part of the four private page-table-frame count.

VM's scratch VA is:

```text
0x000000007fffe000
```

It is unmapped in VM's normal handed-off address space, page-aligned, below
VM's external stack, and uses VM's existing stack-side page-table path. The
TTY UART uses the same numeric VA only in TTY's distinct root; the two
mappings never coexist in one address space.

For each destination page in ascending child VA order:

1. VM asks the kernel to claim the selected free frame as
   `VM_TRANSFERABLE`, bind it to the exact load token and intended child
   VA/permissions, and map it only at VM scratch VA;
2. VM zeroes all 4,096 bytes;
3. VM copies the exact ELF file intersections or generated stack bytes;
4. VM asks the kernel to remove the scratch PTE and retain the frame as an
   unmapped transaction-bound transferable frame; and
5. VM records the completed page in its local rollback ledger.

At most one scratch mapping exists. The scratch PTE is never global and is
exactly `V | U | R | W | A | D`, with execute and global clear. It is
invalidated with local `sfence.vma` on map and removal.

No `VM_TRANSFERABLE` frame may appear in the child's root. This scratch alias
is the sole exception to ADR-0040's no-live-transferable-mapping rule.

### Exact VM-root scratch exception

The scratch commands deliberately choose a serialized active-root mutation;
they do not switch temporarily to the kernel root.

Operation-12 load state distinguishes:

```text
SCRATCH_CLEAR
SCRATCH_MAPPED
```

`SCRATCH_MAP` enters the kernel from the exact current VM thread while VM's
own root is active and SIE is clear. Preflight requires:

- the exact recorded VM process, thread, endpoint, profile, and root;
- current `satp` naming that root;
- `SCRATCH_CLEAR`;
- one exact live load token;
- scratch VA currently absent;
- the selected frame in kernel `FREE` state and absent from every private
  user-subtree leaf across all process roots;
- canonical intended child VA and permissions; and
- complete VM-root validity under the ordinary handed-off rules.

After preflight, one non-failing commit:

1. binds the load token, frame, intended child mapping, VM root, and scratch VA
   in kernel operation-12 state;
2. changes the frame to ownerless transaction-bound `VM_TRANSFERABLE`;
3. installs the exact scratch PTE in the active VM root;
4. executes `sfence.vma` for the scratch VA; and
5. enters `SCRATCH_MAPPED` before returning to VM.

This exact ordering supersedes ADR-0021 only for this command. SIE remains
clear throughout the PTE/phase commit, and an impossible post-preflight
condition is fatal rather than returned after partial mutation.

Complete handed-off root validation gains one phase-aware exception. A live
`VM_TRANSFERABLE` leaf is valid only when all of these match:

- the process is the exact manifest VM generation;
- the root is VM's exact attached process root;
- operation-12 state is `SCRATCH_MAPPED`;
- the VA is exactly `0x000000007fffe000`;
- the PTE is exactly user R/W, non-executable, and non-global;
- its physical frame is the exact frame bound to the live load token;
- that frame has exact ownerless `VM_TRANSFERABLE` ownership;
- no other private user-subtree leaf names the frame; and
- no second scratch or transferable leaf exists.

The immutable supervisor direct-map leaf for that physical frame remains
present and is not child or scratch authority. The two absence/uniqueness
checks above intentionally exclude immutable shared supervisor mappings.

The exception remains valid while another root is selected. Scheduler
preemption may therefore switch away after `SCRATCH_MAP`, validate VM's
inactive attached root, and later reactivate it. Reactivation still performs
the ordinary full ASID-zero fence sequence. `SCRATCH_COMMIT` requires VM to
have resumed and current `satp` to name that same root before it mutates the
PTE.

Every static VM code, data, stack, and table frame retains its ordinary
ADR-0040 ownership and reachability rule. Scheduler return and complete-root
validation recognize the exact exception above. Ordinary managed lookup,
byte translation, IPC-buffer translation, grant creation, grant copy,
PM-control output translation, and mapping inventory reject or exclude the
scratch leaf. VM may access it only through ordinary U-mode hardware loads and
stores while its own root is active.

`SCRATCH_COMMIT` enters from that exact state, validates the recorded PTE and
frame identity exactly, clears the active-root PTE, executes `sfence.vma` for
the scratch VA, enters `SCRATCH_CLEAR`, and retains the now-unmapped
transferable frame in the load ledger. `LOAD_ABORT` performs the same exact
clearing sequence first if scratch is mapped.

No other active-root mutation, transferable mapping, VA, caller, frame type,
permission set, or validation exception is introduced.

### Freeze

After every data page is committed and scratch VA is unmapped, VM submits:

- the exact load token;
- the four selected page-table physical addresses;
- image page count;
- entry;
- stack pointer; and
- a digest of the ordered destination-page plan.

The kernel independently validates:

- the empty PM-reserved process and exact reservation token;
- exact VM caller and load-token phase;
- all committed transferable frames and intended mappings;
- ascending, unique, canonical image and stack VAs;
- exact PTE permissions;
- no scratch mapping;
- four distinct aligned eligible free page-table frames;
- exact immutable kernel-root entries outside VPN2 index 1;
- the 15-plus-1 page bound;
- the exact entry and stack range; and
- the plan digest.

One non-failing commit:

- zeroes and constructs the four page tables;
- copies every immutable kernel-root entry outside VPN2 index 1 into the
  private root and installs only the private application subtree at index 1;
- transitions every data frame from `VM_TRANSFERABLE` to exact
  process-generation `VM_WIRED` ownership;
- transitions the four table frames from `FREE` to exact
  `PROCESS_PAGE_TABLE` ownership;
- installs the root in the reserved process;
- assigns a nonzero mapping generation;
- binds the entry, stack pointer, page ledger, and load token to that
  generation;
- marks the process `VM_MAPPINGS_FROZEN`; and
- leaves it without thread, endpoint, profile, scheduler assignment, or
  runnable state.

Only after syscall success does VM mirror the owner changes in its local
database and reply to PM.

## Kernel operation 12

The syscall number remains:

```text
MICROS_SYSCALL_ABI_VM_HANDOFF = 12
```

The existing commands remain unchanged:

```text
MICROS_VM_HANDOFF_READY        = 1
MICROS_VM_HANDOFF_MAP_TTY_UART = 2
```

After handoff, the exact VM generation may additionally invoke:

```text
MICROS_VM_LOAD_BEGIN     = 3
MICROS_VM_SCRATCH_MAP    = 4
MICROS_VM_SCRATCH_COMMIT = 5
MICROS_VM_LOAD_FREEZE    = 6
MICROS_VM_LOAD_ABORT     = 7
MICROS_VM_LOAD_COMMIT    = 8
MICROS_VM_EXIT_RELEASE   = 9
```

No other service or application gains operation-12 authority.

Complex inputs and outputs use fixed-size 8-byte-aligned objects wholly
readable or writable through VM's exact handed-off wired mapping. The kernel
validates and retains complete physical access plans before mutation.
Reserved fields, alignment, exact size, phase, caller, token, owner, and
object-state checks precede each commit.

The 32-byte load-begin output object is:

```text
bytes  0..3   version = 1
bytes  4..7   size = 32
bytes  8..15  nonzero load token
bytes 16..17  kernel process slot
bytes 18..19  zero
bytes 20..23  kernel process generation
bytes 24..31  zero
```

`LOAD_BEGIN` uses:

```text
a0  MICROS_VM_LOAD_BEGIN
a1  kernel process-reservation token
a2  writable output-object address
a3  exact output size 32
a4..a6  zero
a7  MICROS_SYSCALL_ABI_VM_HANDOFF
```

It validates one PM-reserved empty process and returns its exact process
handle plus a nonzero load token without allocating a frame.

`SCRATCH_MAP` uses:

```text
a0  MICROS_VM_SCRATCH_MAP
a1  load token
a2  intended child virtual page
a3  selected physical frame
a4  exact child permission bits
a5  VM scratch VA = 0x000000007fffe000
a6  zero
a7  MICROS_SYSCALL_ABI_VM_HANDOFF
```

`SCRATCH_MAP` atomically:

- validates one selected kernel-`FREE` physical frame;
- changes it to transaction-bound `VM_TRANSFERABLE`;
- records intended child VA and exact permissions; and
- installs the sole VM scratch PTE.

`SCRATCH_COMMIT` uses:

```text
a0  MICROS_VM_SCRATCH_COMMIT
a1  load token
a2  intended child virtual page
a3  selected physical frame
a4  VM scratch VA
a5..a6  zero
a7  MICROS_SYSCALL_ABI_VM_HANDOFF
```

`SCRATCH_COMMIT` atomically removes the scratch PTE, performs local
`sfence.vma`, and retains the unmapped transferable frame in the load ledger.

The 96-byte freeze plan is:

```text
bytes  0..3   version = 1
bytes  4..7   size = 96
bytes  8..15  load token
bytes 16..47  four page-table physical addresses in root, level-1,
              image-leaf, stack-leaf order
bytes 48..55  entry = 0x0000000040000000
bytes 56..63  initial stack pointer
bytes 64..67  image page count in [3, 15]
bytes 68..71  total data page count = image page count + 1
bytes 72..79  ordered page-plan digest
bytes 80..95  zero
```

The digest is 64-bit FNV-1a with offset basis
`14695981039346656037` and prime `1099511628211`, over the little-endian
image-page count, total-page count, and, for each committed page in ascending
child-VA order, its 64-bit child VA, 64-bit physical address, 32-bit
permissions, and one zero 32-bit word.

The 32-byte freeze output is:

```text
bytes  0..3   version = 1
bytes  4..7   size = 32
bytes  8..15  nonzero mapping generation
bytes 16..31  zero
```

`LOAD_FREEZE` uses:

```text
a0  MICROS_VM_LOAD_FREEZE
a1  readable freeze-plan address
a2  exact plan size 96
a3  writable freeze-output address
a4  exact output size 32
a5..a6  zero
a7  MICROS_SYSCALL_ABI_VM_HANDOFF
```

It performs the complete mapping commit above.

`LOAD_ABORT` uses:

```text
a0  MICROS_VM_LOAD_ABORT
a1  load token
a2  mapping generation, or zero before freeze
a3..a6  zero
a7  MICROS_SYSCALL_ABI_VM_HANDOFF
```

`LOAD_ABORT`:

- before freeze, removes any scratch mapping and returns every transaction
  transferable frame to `FREE`; or
- after freeze and before activation, removes the child root and PTEs and
  returns every child process-generation `VM_WIRED` and page-table frame to
  `FREE`.

It requires the exact load token and expected mapping generation. It is
forbidden while execution is prepared; PM must first invoke
`ABORT_PREPARED`.

`LOAD_COMMIT` uses:

```text
a0  MICROS_VM_LOAD_COMMIT
a1  load token
a2  mapping generation
a3..a6  zero
a7  MICROS_SYSCALL_ABI_VM_HANDOFF
```

It requires the process already be active through PM-control activation,
changes the kernel mapping record from activated-pending-VM-commit to active,
and frees the sole kernel load-transaction slot while retaining the
per-process mapping ledger.

`EXIT_RELEASE` uses:

```text
a0  MICROS_VM_EXIT_RELEASE
a1  kernel exit token
a2  kernel load token
a3  mapping generation
a4..a6  zero
a7  MICROS_SYSCALL_ABI_VM_HANDOFF
```

`EXIT_RELEASE` requires the exact kernel exit token after execution detach.
It removes the root and PTEs, returns every user and page-table frame to
`FREE` through exact process-generation `VM_WIRED` and
`PROCESS_PAGE_TABLE` transitions, clears the mapping/load record, and leaves
one empty exiting process for PM's final release.

Every successful owner transition is mirrored by VM before it processes
another request. A kernel/VM ownership disagreement is fatal.

## Page permissions

The child PTEs are exact:

```text
text      V | U | R | X | A
rodata    V | U | R | A
data/BSS  V | U | R | W | A | D
stack     V | U | R | W | A | D
```

Global is clear. Writable executable mappings are impossible. Supervisor-only
or identity aliases are absent from the child's user subtree. That subtree
contains no service, kernel, physical-memory, executable-buffer,
request-buffer, or VM scratch mapping. Outside VPN2 index 1, the root retains
the exact immutable supervisor-only kernel entries required for kernel
execution, trap entry, trap stacks, and managed-memory access.

## Kernel operation 13

The syscall number remains:

```text
MICROS_SYSCALL_ABI_PM_CONTROL = 13
```

The existing commands remain:

```text
MICROS_PM_CONTROL_RESERVE        = 1
MICROS_PM_CONTROL_ABORT_RESERVED = 2
```

After this ADR, `RESERVE` additionally requires the singleton kernel load
transaction be idle. Persistent active-process mapping records do not block a
new reservation. This prevents a corrupt PM from reserving another child
during the short activated-pending-VM-commit phase.

The exact PM generation additionally receives:

```text
MICROS_PM_CONTROL_PREPARE_EXECUTION      = 3
MICROS_PM_CONTROL_ABORT_PREPARED         = 4
MICROS_PM_CONTROL_ACTIVATE               = 5
MICROS_PM_CONTROL_EXIT_BEGIN             = 6
MICROS_PM_CONTROL_EXIT_DETACH            = 7
MICROS_PM_CONTROL_RELEASE_EXITED_PROCESS = 8
```

Every operation-13 object is fixed-size, 8-byte-aligned, and wholly
translated through PM's exact handed-off wired mapping before mutation.

### Prepare execution

The 32-byte output object is:

```text
bytes  0..3   version = 1
bytes  4..7   size = 32
bytes  8..15  nonzero prepared token
bytes 16..19  reserved child endpoint
bytes 20..31  zero
```

Input names:

- the exact process-reservation token;
- kernel load token;
- mapping generation; and
- exact output object.

Registers are:

```text
a0  MICROS_PM_CONTROL_PREPARE_EXECUTION
a1  process-reservation token
a2  kernel load token
a3  mapping generation
a4  writable output-object address
a5  exact output size 32
a6  zero
a7  MICROS_SYSCALL_ABI_PM_CONTROL
```

After complete validation, one non-failing commit:

- creates exactly one thread;
- reserves exactly one endpoint generation without publishing it;
- installs exactly the immutable `APPLICATION` profile;
- binds token-held `sepc`, `sp`, canonical U-mode `sstatus`, and zero in every
  remaining general register as the initial context;
- assigns and holds exact hart 0, priority 9, preemptible policy, and a
  1,000,000-counter-tick quantum;
- executes local `fence.i`;
- seals the exact mapping generation against further load mutation;
- returns the prepared token and reserved endpoint; and
- leaves the process hidden and non-runnable.

The command cannot select an alternative entry, stack, profile, thread count,
endpoint target set, or scheduler policy. Every static production service,
including PM, runs at priority 8. Since numerically lower priorities are
higher, releasing the priority-9 child cannot preempt PM inside activation.
When PM blocks in the mandatory VM and VFS post-activation calls, each
priority-8 callee runs ahead of the child and wakes priority-8 PM. The child
therefore cannot execute before both post-activation commits finish. Once PM
replies and returns to receive, the application is schedulable; any ready
service preempts a compute-bound application, and the finite quantum prevents
one application from starving another at priority 9.

### Abort prepared

`ABORT_PREPARED` requires the reservation, load, mapping-generation, and
prepared tokens. It:

- removes the held thread;
- discards the reserved endpoint;
- clears context, scheduler assignment, and `APPLICATION` profile;
- removes the execution seal; and
- returns the process to the exact frozen-mapping shape.

It cannot run after activation.

Registers are:

```text
a0  MICROS_PM_CONTROL_ABORT_PREPARED
a1  process-reservation token
a2  kernel load token
a3  mapping generation
a4  prepared token
a5..a6  zero
a7  MICROS_SYSCALL_ABI_PM_CONTROL
```

### Activate

`ACTIVATE` requires all four exact tokens. It revalidates:

- the exact PM caller attests that hidden PM/VFS commit completed;
- the frozen and sealed mapping generation is unchanged;
- the one thread, endpoint, profile, context, and scheduler shape is exact;
- the endpoint generation remains unpublished and collision-free; and
- the process is not exiting.

One non-failing commit:

- publishes the endpoint;
- changes the process to `RUNNING`;
- changes the mapping record to activated-pending-VM-commit;
- releases the held sole thread into its assigned runnable state; and
- consumes the prepared token and process-reservation token; and
- clears the kernel's sole active PM reservation slot.

Registers are:

```text
a0  MICROS_PM_CONTROL_ACTIVATE
a1  process-reservation token
a2  kernel load token
a3  mapping generation
a4  prepared token
a5..a6  zero
a7  MICROS_SYSCALL_ABI_PM_CONTROL
```

Activation returns to the current PM thread. PM immediately changes its
already committed local record from `PM_VFS_COMMITTED` to `RUNNING`, stores
the active endpoint, asks VM to commit the activated load into its persistent
address-space table, releases the VFS image transaction, and replies to the
parent. No fallible local operation occurs between activation return and the
PM state update. The post-activation VM and VFS commits are mandatory and
non-failing after exact token validation; disagreement is fatal.

On one hart, the fixed priorities prevent the newly released child from being
selected before VM commit, VFS release, and the parent's spawn reply. The
child can run only after PM next blocks or yields with no ready priority-8
service work.

### Exit command ABI

The 32-byte exit-begin output object is:

```text
bytes  0..3   version = 1
bytes  4..7   size = 32
bytes  8..15  nonzero kernel exit token
bytes 16..31  zero
```

`EXIT_BEGIN` uses:

```text
a0  MICROS_PM_CONTROL_EXIT_BEGIN
a1  active child endpoint with zero upper 32 bits
a2  writable output-object address
a3  exact output size 32
a4..a6  zero
a7  MICROS_SYSCALL_ABI_PM_CONTROL
```

`EXIT_DETACH` uses:

```text
a0  MICROS_PM_CONTROL_EXIT_DETACH
a1  kernel exit token
a2  closing child endpoint with zero upper 32 bits
a3..a6  zero
a7  MICROS_SYSCALL_ABI_PM_CONTROL
```

The exact PM caller invokes it only after successful VFS cleanup. The kernel
does not invent or trust a VFS token.

`RELEASE_EXITED_PROCESS` uses:

```text
a0  MICROS_PM_CONTROL_RELEASE_EXITED_PROCESS
a1  kernel exit token
a2..a6  zero
a7  MICROS_SYSCALL_ABI_PM_CONTROL
```

## Directed spawn sequence

The exact sequence is:

1. validate the public caller or trusted init request;
2. copy and canonically validate the complete request;
3. reserve the PM record and consume the PID;
4. invoke PM-control `RESERVE`;
5. create a PM-to-VFS request grant;
6. invoke VFS `PREPARE_IMAGE`;
7. revoke the PM-to-VFS grant after every returning VFS result;
8. on VFS success, invoke VM `LOAD` with the kernel reservation and two VFS
   grants;
9. receive the load token and mapping generation;
10. invoke VFS `PREPARE_DESCRIPTORS`;
11. invoke PM-control `PREPARE_EXECUTION`;
12. invoke VFS `COMMIT_HIDDEN` with the reserved endpoint;
13. commit PM hidden metadata;
14. invoke PM-control `ACTIVATE`;
15. immediately publish the PM local `RUNNING` record;
16. invoke VM `COMMIT_LOAD`, which cannot fail after token validation;
17. invoke VFS `RELEASE_IMAGE`, which cannot fail after token validation; and
18. reply to the public parent with the child PID, or complete trusted init
    without a reply.

PM performs only one collaborator call at a time. Other application calls may
queue in kernel IPC but cannot create a concurrent lifecycle transaction.
The resulting `RUNNING` PM record retains the kernel load token and mapping
generation required for exact normal-exit release.

## Prepublication rollback

Expected rollback is exact reverse ownership order:

| Highest completed stage | Required rollback |
| --- | --- |
| PM record reserved | clear PM record; retain consumed PID |
| Kernel process reserved | PM-control `ABORT_RESERVED`; clear PM record |
| VFS image prepared | VFS `RELEASE_IMAGE`; abort reservation; clear PM record |
| VM mappings frozen | VM `ABORT_LOAD`; release image; abort reservation; clear PM record |
| VFS descriptors prepared | VFS `ABORT_DESCRIPTORS`; abort load; release image; abort reservation; clear PM record |
| Kernel execution prepared | PM-control `ABORT_PREPARED`; then preceding rollback |
| VFS hidden committed | VFS `REVERT_HIDDEN`; abort prepared; then preceding rollback |
| PM hidden committed | revert PM metadata; VFS hidden record; abort prepared; then preceding rollback |

The VM internally aborts every scratch and transferable-frame resource before
returning an expected load failure to PM. PM never receives a half-loaded VM
stage.

Loss of the parent's reply token or parent exit before activation triggers the
same rollback and no reply. Since the parent has one thread and is blocked in
spawn, it cannot voluntarily exit concurrently; cancellation is still covered
for kernel teardown and future fault integration.

Activation failure is expected only when complete precommit validation rejects
the command and changes nothing. PM then performs the last rollback row.
Failure after activation is never reported as failed spawn.

Stale, duplicate, foreign, zero, wrapped, or out-of-order tokens are rejected.
Token-space exhaustion is fatal rather than reuse.

## Normal exit

The existing public `EXIT`, `WAIT`, result layout, normal exit kind, and
semantic-PID behavior remain unchanged.

An accepted `EXIT` call requires:

- exact source endpoint resolving to one `RUNNING` application;
- the process is not the designated init reaper;
- no hidden spawning child remains owned by that process;
- the sole thread blocked in that call to PM;
- a nonzero reply token;
- canonical flags and signed status; and
- complete PM lifecycle invariants.

PM then performs:

1. PM-control `EXIT_BEGIN`;
2. one non-failing PM `EXITING` and child-reparent commit;
3. VFS `EXIT_PROCESS`;
4. PM-control `EXIT_DETACH`;
5. VM `EXIT_RELEASE`;
6. PM-control `RELEASE_EXITED_PROCESS`;
7. PM transition to one `ZOMBIE`; and
8. wait completion or later one-shot wait discovery.

### Exit begin

`EXIT_BEGIN` atomically:

- marks the endpoint closing so new IPC initiation fails;
- makes the reply-blocked sole thread permanently non-runnable;
- binds the pending PM call/reply state to a nonzero exit token;
- freezes process, endpoint, mapping, and frame identity for teardown; and
- leaves the endpoint generation and process root present for VFS lookup and
  later VM release.

After successful exit-begin there is no transition back to `RUNNING`.

### PM exit and reparent commit

Before `EXIT_BEGIN`, PM validates that the process is not the sole init
reaper. Init exit is fatal in v0.1 and never becomes an ordinary error,
zombie, or wait result.

The one-thread normal-exit path cannot own a concurrent hidden spawn because
the same thread would be blocked in that spawn call. PM nevertheless requires
the hidden-child set be empty before `EXIT_BEGIN`; a later kernel-forced exit
path must first complete reverse rollback for every such child.

After kernel exit-begin succeeds, PM invokes the already modeled non-failing
local transition with the exact process handle, exit token, normal exit kind,
and signed status. One commit:

- changes the record from `RUNNING` to `EXITING`;
- records exit stage `STOPPED`, normal kind, status, and exact exit token;
- clears the exiting process's outstanding wait state and any associated
  reap-pending mark;
- reparents every `RUNNING`, `EXITING`, or `ZOMBIE` child to the exact live
  init reaper; and
- leaves init's outstanding wait pending without selecting a zombie yet while
  retaining the exiting process's exact original parent handle.

No child retains the exiting parent's handle.

### VFS release

VFS cancels or completes cleanup for all child-owned pending operations,
releases descriptors, open-file descriptions, root and working-directory
references, and removes the process record. It acknowledges only when no VFS
state names the endpoint.

### Kernel detach

The exact PM caller invokes `EXIT_DETACH` only after successful VFS
acknowledgment. The command requires the exact exit token; the kernel does not
trust or consume a VFS-generated proof token. It:

- closes the endpoint and cancels queued IPC;
- destroys the pending reply token and every child-owned grant;
- removes scheduler and context state;
- removes the immutable profile;
- releases the sole thread; and
- leaves the process root and mapping ledger sealed for VM.

### VM release

PM sends the exit token to VM. VM cross-checks its local mapping ledger and
invokes operation-12 `EXIT_RELEASE`. After kernel success it marks every
released user and page-table frame free locally and acknowledges PM.

### Process and zombie release

`RELEASE_EXITED_PROCESS` requires the exact exit token and one empty process
with no root, frame, thread, endpoint, profile, scheduler, IPC, reply, or grant
state. It releases the kernel process and consumes the exit token.

Only then may PM publish `ZOMBIE`. The zombie retains semantic PID, exact live
parent handle, normal exit kind, and signed exit status, but no kernel, VM, or
VFS resource.

After zombie publication, PM builds a fixed affected-parent list containing
the exact init reaper followed by the exiting process's exact original parent.
If both handles are equal, PM deduplicates the list and performs one scan.
Otherwise it performs two independent scans in that order:

1. init's scan covers its complete child set, including every zombie adopted
   during the exit-begin commit; and
2. the original parent's scan covers its complete child set, including the
   newly published zombie.

Each scan uses only that parent's stored selector. An any-child wait selects
the lowest semantic PID in that parent's complete matching zombie set; an
exact-child wait selects only its named child. Because one parent has at most
one outstanding wait, publication stages at most two independent outcomes.
The PM local transition returns a fixed two-entry outcome bundle with a count
of zero through two, zero tail, and entries ordered as above. The service
attempts each token-bound reply in order before receiving another request.
Acceptance or expected reply failure is completed independently for each
parent: failure restores only that parent's zombie and wait state and does not
prevent the other staged reply. An unexpected reply failure remains fatal.

When the original parent is init, the single combined scan therefore compares
adopted zombies with the newly exited process before choosing the lowest PID.
When the parents differ, init and the original parent may both receive one
result from the same exit completion. A successful wait returns its status
once and frees that zombie's PM record. PID values are not reused during the
boot.

Failure before `EXIT_BEGIN` returns an explicit error to the still-running
caller. Failure after `EXIT_BEGIN` is fatal service or kernel corruption; PM
does not synthesize a zombie while resources remain or attempt to resume the
application.

Fault-generated exit remains deferred. A child trap is still fatal to the
node-P fixture rather than converted to a PM `FAULT` exit.

## Failure atomicity and validation order

Every new kernel command follows:

1. syscall number and known command;
2. exact caller process, thread, endpoint, role, profile, and operation bit;
3. VM handoff, launcher seal, and PM runtime phase;
4. aligned fixed-size object translation through the caller's complete wired
   mapping;
5. complete byte copy into kernel temporary storage;
6. version, size, reserved fields, overflow, and canonical value checks;
7. exact nonzero token and expected transaction phase;
8. complete process, object, owner, frame, PTE, scheduler, IPC, and grant
   invariants;
9. capacity and collision checks; and
10. one non-failing commit.

Every returning failure preserves:

- kernel and service object state;
- caller input and output objects;
- frame ownership;
- PTEs and TLB state;
- mapping generation;
- token phase; and
- PM-visible lifecycle state.

VM and VFS internal request parsers similarly perform complete fixed-payload,
caller, token, and phase validation before mutation.

## Production VM promotion

The real VM implementation currently lives in the VM-handoff QEMU source.
Node P promotes it into production-owned VM core and service files.

The production VM core owns:

- boot-information validation and immutable initial database;
- post-handoff local frame states;
- ELF and request parsing;
- deterministic frame selection;
- page and stack planning;
- load, freeze, abort, and exit-release transitions; and
- complete validation suitable for a native reference model.

The QEMU VM service owns only:

- readiness and PM IPC dispatch;
- direct-grant copies;
- private operation-12 wrappers;
- fatal handling of local/kernel disagreement; and
- reply construction.

No loader logic remains fixture-only.

## QEMU component evidence

The future `test-qemu-elf-spawn` target must run two deterministic target
scenarios. The first isolates the hardware scratch-preemption contract. The
second runs the complete production-service spawn transaction and
resource-baseline proof.

### Scratch preemption and stale-translation component

An isolated QEMU component image uses the real RISC-V trap, timer, scheduler,
context-switch, `satp`, and operation-12 paths with an exact VM scratch probe
and one test peer under distinct Sv39 roots. The fixture creates the exact
PM-reserved child/load-token state through the production transition code.
After the first successful `SCRATCH_MAP`, the VM probe writes a sentinel
through the scratch VA and reaches one labeled compile-time test hook before
`SCRATCH_COMMIT`.

That hook makes the peer runnable and shortens the current VM quantum to one
counter tick. The ordinary supervisor-timer and scheduler path must preempt
VM, activate the peer root, and execute the peer's labeled report. While the
peer root is current, the fixture invokes the production complete-root
validator on VM's inactive attached root and requires the token-bound scratch
exception to pass. The peer then blocks through the ordinary runtime path.
The scheduler must reactivate VM through the normal ASID-zero full-fence
sequence rather than a fixture `satp` write.

After reactivation, VM rereads the sentinel through the scratch VA, invokes
the real `SCRATCH_COMMIT`, and performs one labeled test-only load from the
same VA. Success requires an exact expected load-page-fault at that PC and VA,
an absent scratch PTE, `SCRATCH_CLEAR`, and the frame retained only as the
unmapped transaction-bound transferable ledger entry. This proves both usable
translation after VM-root reactivation and removal of any stale translation
after commit. The fixture uses no sleep, host timing, manual root switch, or
shortcut around the production scheduler and fence paths. Production images
contain none of these test hooks or labeled fault probes.

### Production integration and resource baseline

The node-P QEMU image contains the six production static services and seeds
two dynamic application ELFs in RAMFS:

- a test-only PID-1 reaper/spawn parent; and
- a child application.

After PID 1 activates, fixture instrumentation captures a resource baseline
covering:

- kernel processes, threads, endpoints, profiles, scheduler states, IPC
  queues, reply tokens, grants, roots, mappings, and frame owners;
- VM local frame counts and active load state;
- PM live/spawning/exiting/zombie records;
- VFS processes, descriptors, open-file descriptions, vnode references,
  pending console state, and image transaction;
- RAMFS node/reference state; and
- TTY pending/resident output state.

PID 1 then:

1. sends a structurally valid spawn whose descriptor action names an absent
   parent source descriptor, forcing `DESCRIPTOR` rollback after VM freeze;
2. verifies the exact failure, consumed hidden PID, and continued usability of
   its own descriptors;
3. sends one valid spawn with bounded argv and envp and duplicates parent
   descriptors 0, 1, and 2 into the same child numbers;
4. verifies the returned child PID is the next monotonic value after the
   failed transaction and waits for that exact child PID;
5. verifies one normal exit with the fixed child status;
6. verifies a second no-child wait result;
7. waits for the TTY's existing physical-drain proof; and
8. executes one fixture-only breakpoint report.

The child:

- validates `argc`, every argv and envp string, auxv endpoints, its PID, and
  page size;
- writes through inherited stdout;
- uses inherited VFS authority to read one seeded regular file;
- returns a fixed nonzero status from `micros_application_main`; and
- never calls VM, TTY, RAMFS, or a lifecycle syscall directly.

The final report requires the complete resource state, except for persistent
PID-1 resources and monotonic consumed PID/token counters, to equal the
post-PID-1 baseline. It specifically proves:

- no resource leak from the failed spawn after frozen mappings;
- no resource leak from child normal exit and wait;
- no live image/request grants;
- no VM scratch mapping or transferable frame;
- no child root, mapping, frame, process, thread, endpoint, profile,
  scheduler, IPC, reply, grant, descriptor, vnode, or zombie;
- launcher sealing and all six service readiness states remain intact; and
- output physically drained before clean fixture termination.

The harness uses no sleep or host timing to establish correctness.

## Native and host evidence

The implementation must add:

- byte-level spawn-request and PM/VFS/VM protocol tests for every boundary,
  malformed field, reserved bit, overflow, precedence rule, and exact
  round-trip;
- application-stack tests for minimum and maximum argc/envp, alignment,
  pointer rebasing, auxv values, empty environment, near-capacity strings, and
  exact 2,048-byte initial occupancy, retained 2,048-byte runtime reserve,
  and one-byte occupancy overflow rejection;
- ELF parser tests for every fixed field, short file, header-table bound,
  segment ordering, flags, alignment, file range, integer overflow, entry,
  contiguity, W+X, stack overlap, and page-count limit;
- VM transition tests for deterministic frame selection, every scratch
  map/commit failure, active- and inactive-root scratch validation, freeze,
  abort before and after freeze, exit release, and exact local/kernel owner
  agreement;
- kernel component tests for every operation-12 and operation-13 command,
  authority failure, token failure, output immutability, rollback shape,
  activation publication, and exit ordering;
- PM and VFS transition tests for every spawn stage, result mapping,
  descriptor action, hidden commit, rollback row, normal exit, zombie,
  same-parent combined lowest-PID selection, distinct init/original-parent
  two-waiter staging, independent reply completion/restoration, and one-shot
  wait. The required nested regression is `init -> C -> A -> B` with `B`
  already zombie, init blocked in any-child wait, and `C` blocked waiting for
  `A`: publishing `A` must stage `B` for init and `A` for `C`. Executable
  lookup tests require `regular-file/child -> ACCESS` and a component longer
  than `MICROS_VFS_NAME_MAX -> TOO_BIG`, including exact post-PID rollback
  and precedence over later executable, grant, or descriptor work;
- one replayable native model of mixed valid and invalid load, spawn,
  rollback, exit, and wait transitions with a printed seed and operation trace
  on failure;
- host application-link and seeded-runtime ELF checks;
- standalone PM, VM, VFS, and application image budget checks; and
- both QEMU component scenarios above.

The build exposes:

```text
test-elf-spawn-model
build-application-images
test-qemu-elf-spawn
```

These join the complete native and QEMU integration gates. The design does
not claim the targets exist until implementation lands.

## Invariants

- PM is the sole lifecycle coordinator.
- VFS is the sole path, executable authorization, and descriptor authority.
- VM is the sole frame-selection and application mapping-policy authority.
- The kernel is the sole owner-transition and publication authority.
- Every caller-owned request byte is copied before PID consumption.
- No grant is forwarded; every copy names its actual grantor and grantee.
- The executable and request are immutable snapshots for one VFS transaction.
- Runtime ELF validation precedes every child frame allocation.
- At most one VM scratch alias exists and it never appears in the child.
- No transferable frame appears in a child page table.
- Every frozen or active child user leaf names an exact process-generation
  `VM_WIRED` owner, and every private table names an exact
  `PROCESS_PAGE_TABLE` owner.
- Every child root entry outside VPN2 index 1 remains byte-for-byte identical
  to the immutable kernel root and supervisor-only.
- A frozen child has one exact root, 3 through 15 image pages, one stack page,
  four page-table frames, and no execution object.
- A prepared child adds exactly one held thread, one reserved endpoint, one
  `APPLICATION` profile, one scheduler assignment, and one sealed context.
- Activation is the only endpoint-publication and runnable transition.
- Activation clears the singleton hidden-process reservation so a running
  child does not block the next sequential spawn.
- No child is externally visible before activation.
- Every expected preactivation failure completely rolls back.
- Instruction fetch is synchronized before the child can run.
- Dynamic applications receive PM/VFS endpoints only through kernel- and
  service-derived auxv, never request-selected authority.
- An accepted exit cannot return to the caller.
- VFS cleanup precedes endpoint detach; endpoint detach precedes VM mapping
  release; mapping release precedes kernel process release.
- A zombie owns no kernel, VM, or VFS resource.
- Zombie publication independently reevaluates init and the exact original
  parent, deduplicates a shared parent, and stages at most one lowest-PID
  result per affected parent.
- No lifecycle token is zero, reused, or accepted out of phase.
- The one-thread and one-hart restrictions remain checked policy.

## Consequences

Positive:

- `micros` gains its first production dynamic application lifecycle without
  weakening service authority boundaries.
- The first publication point is atomic and every earlier stage is
  rollbackable.
- The executable format, stack ABI, descriptor actions, and resource budget
  are deterministic and directly testable.
- VM is promoted from a handoff fixture into a production service core.
- Production init and shell can build on one proven spawn/exit path instead of
  introducing lifecycle mechanics at the same time as command behavior.

Costs:

- VFS and VM each retain a complete executable copy during one spawn.
- Every application is fully resident and consumes four page-table frames.
- Spawn and exit are globally serialized.
- The application ABI is intentionally smaller than POSIX and requires a
  dedicated runtime.
- Post-activation invariant failure is fatal because successful publication
  cannot be represented as a failed spawn.

## Alternatives considered

### Implement init and shell in the same change

Rejected. It combines graph nodes P and Q, obscures loader and rollback
evidence, and violates the single-focus PR rule.

### Reuse the service startup path

Rejected. Services consume a patched bootstrap configuration and call
`micros_service_main(void)`. Dynamic applications require argv, envp, auxv,
runtime PM/VFS endpoints, PID, and normal-return exit behavior.

### Map the VFS executable buffer directly into the child

Rejected. It would couple application lifetime to VFS resident storage,
complicate permissions and ownership, and violate VM's frame-policy authority.

### Let VFS copy directly into child virtual addresses

Rejected. Grants are non-transitive, VFS does not own child mappings, and the
data path would recreate a confused-deputy boundary.

### Let VM edit child page tables directly

Rejected. Kernel frame ownership and exact frozen-shape validation would no
longer be atomic. VM selects policy; the kernel validates and installs PTEs.

### Allocate a permanent loader window

Rejected. One exact single-page scratch alias is sufficient for the bounded
eager loader and is easier to validate and remove.

### Pass service endpoints in registers or fixed constants

Rejected. Constants would couple applications to endpoint generations, while
register-only metadata is harder to inspect and extend. A bounded auxv keeps
authority values in the immutable initial-stack snapshot.

### Support arbitrary POSIX file actions

Rejected. Duplicate-only actions cover inherited standard descriptors and
preserve a small failure-atomic VFS transaction. Close, move, flags, and
open-during-spawn remain future work.

### Use MINIX's fork-plus-exec `posix_spawn`

Rejected. ADR-0009 deliberately requires spawn before fork, and v0.1 has no
copy-on-write or general fork semantics.

### Preserve a failed partially loaded child for diagnostics

Rejected. Hidden expected failures must release all resources. Replayable
native traces and fatal invariant reports provide diagnostics without leaking
or publishing invalid lifecycle state.

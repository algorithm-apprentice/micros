# MINIX RS and SEF Bootstrap Study

## Purpose

This study traces the fixed MINIX boot-service startup path before `micros`
defines its static bootstrap launcher and immutable embedded manifest.

The reference is MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

MINIX source is behavioral evidence only. `micros` remains an independent C17
and RISC-V implementation.

The trace covers:

- boot service descriptors and embedded image identity;
- process inhibition and privilege installation;
- endpoint visibility before readiness;
- RS initialization requests and SEF callbacks;
- readiness results and the reply that releases service startup;
- static start order and the limited synchronization encoded by MINIX;
- initialization timeouts; and
- the restart, heartbeat, replica, and live-update breadth that is deferred.

Endpoint encoding, immutable profile mechanics, blocking IPC, reply-token
authority, wired address-space reads, and the freestanding runtime remain
classified by their existing research documents and ADRs. This document does
not duplicate those ledgers.

## Static boot descriptors

### Kernel boot image

MINIX has one fixed kernel boot-image table. Each entry initially names:

- a process number; and
- a process name.

The table order must match the boot modules. Kernel startup fills each
non-kernel entry with:

- its generated endpoint;
- the boot module start address; and
- the boot module byte length.

See `minix/kernel/table.c:36-66`,
`minix/include/minix/type.h:148-155`, and
`minix/kernel/main.c:150-183`.

The descriptor therefore separates stable process identity and diagnostic
name from the boot-loader-provided image range. It is not a package format or
a dynamic registry.

### RS boot policy tables

RS adds three immutable tables:

1. `boot_image_priv_table`, containing endpoint, service label, and privilege
   class;
2. `boot_image_sys_table`, containing service-system flags; and
3. `boot_image_dev_table`, containing the major device assignment where one
   exists.

The privilege table explicitly states that its order is the order in which
boot services are made runnable and initialized
(`minix/servers/rs/table.c:10-30`). The system and device tables use exact
endpoint matches with one default entry
(`minix/servers/rs/table.c:32-51`).

RS copies these fixed records into private and public runtime descriptors.
Those descriptors add endpoint, label, process name, scheduling parameters,
VM-call masks, privilege state, device identity, initialization state, and
later recovery metadata
(`minix/servers/rs/type.h:56-101`,
`minix/include/minix/rs.h:165-187`).

This split matters:

- the immutable tables define boot policy;
- the runtime records track one live instance and its later lifecycle; and
- endpoint generation remains kernel process identity rather than a service
  label.

### Dynamic descriptor breadth

The general `rs_start` descriptor is much broader than the fixed boot tables.
It includes:

- command and program name;
- UID and signal manager;
- scheduler, priority, quantum, and CPU;
- heartbeat period and restart counters;
- major device number;
- IRQ and I/O-port arrays;
- PCI device and class ACLs;
- kernel-call and VM-call masks;
- IPC target strings;
- control labels;
- restart scripts;
- live-update state-transfer data; and
- heap and mapping preallocation.

See `minix/include/minix/rs.h:104-159`.

That descriptor supports runtime service creation, drivers, recovery, and
live update. It is not the minimum boot contract.

## Kernel inhibition and privilege setup

### Boot objects exist before RS releases them

Kernel startup creates every boot-image process entry and assigns its endpoint.
Only kernel tasks, RS, and VM receive immediately usable privilege structures.
Other system processes are given:

- `RTS_NO_PRIV`;
- `RTS_NO_QUANTUM`;
- `RTS_VMINHIBIT`;
- `RTS_BOOTINHIBIT`; and
- initially, `RTS_PROC_STOP`.

See `minix/kernel/main.c:185-270`.

The boot CPU later clears `RTS_PROC_STOP` for boot processes, but the remaining
inhibitors continue to prevent ordinary execution until the responsible
bootstrap component clears them
(`minix/kernel/main.c:34-68`).

### RS installs policy while the service is inhibited

During fresh RS initialization, RS:

1. obtains the kernel boot image;
2. verifies that its static privilege table covers the same number of system
   services;
3. initializes every RS runtime slot;
4. copies each service's exact label, privilege flags, trap mask, IPC mask,
   signal manager, kernel-call mask, VM-call mask, scheduler settings, device
   properties, endpoint, and command; and
5. installs and rereads the privilege structure before allowing the service
   to run.

See `minix/servers/rs/main.c:177-354`.

The kernel accepts `SYS_PRIV_SET_SYS` only while the target has
`RTS_NO_PRIV`. `SYS_PRIV_ALLOW` requires an installed privilege structure and
then clears that inhibitor
(`minix/kernel/system/do_privctl.c:38-64,86-175`).

This creates the important baseline boundary:

- identity and image already exist;
- privilege policy is installed while execution is inhibited; and
- one explicit transition makes the process runnable.

### Endpoint visibility is not a readiness gate

MINIX assigns the boot endpoint before RS installs the final privilege
structure. The normal send path rejects `RTS_NO_ENDPOINT`, but it does not use
`RTS_NO_PRIV` as an endpoint-visibility state
(`minix/kernel/proc.c:872-891`).

RS also places the endpoint in its public table and marks the service active
before service initialization completes
(`minix/servers/rs/main.c:316-354`).

Thus MINIX inhibition prevents execution, not naming. Boot peers may know the
endpoint before the service is ready. Correct startup depends on RS ordering,
privilege masks, and the initialization handshake rather than on a hidden
endpoint state.

## Boot release and initialization request

### Fixed release order

RS walks `boot_image_priv_table` in table order. For each ordinary system
service it:

1. starts scheduler policy;
2. invokes `SYS_PRIV_ALLOW`; and
3. sends an `RS_INIT` request.

RS and VM are special because they are already executing when RS performs this
loop. RS initializes its own state locally; VM still sends an initialization
result
(`minix/servers/rs/main.c:356-399`).

The request is sent by `init_service()`, which first marks the service
`RS_INITIALIZING`, records an initialization deadline, and supplies:

- fresh, restart, or live-update type;
- initialization flags;
- a grant for the public RS process table;
- old endpoint when applicable;
- restart count;
- optional preallocated mapping;
- live-update preparation state.

See `minix/servers/rs/utility.c:18-62` and
`minix/include/minix/ipc.h:1853-1867`.

The boot request uses asynchronous delivery so RS can continue startup without
blocking on each service. The service's readiness response supplies the later
synchronization point.

### SEF waits for the exact RS request

A normal MINIX service:

1. enters its service-specific `main`;
2. registers SEF initialization callbacks;
3. calls `sef_startup()`; and
4. enters its long-running request loop only after SEF startup returns.

PM and SCHED both use this shape
(`minix/servers/pm/main.c:49-131`,
`minix/servers/sched/main.c:22-126`).

`sef_startup()` obtains the process endpoint, name, privilege flags, and
initialization flags. Except for special RS and fresh-VM handling, it then
receives only from RS until it gets an exact `RS_INIT` request. Spurious
messages intended for an older instance are discarded
(`minix/lib/libsys/sef.c:68-142`).

SEF converts the request into `sef_init_info_t` and invokes exactly one
registered callback for:

- `SEF_INIT_FRESH`;
- `SEF_INIT_RESTART`; or
- `SEF_INIT_LU`.

See `minix/lib/libsys/sef_init.c:43-109,191-217` and
`minix/include/minix/sef.h:43-103`.

The generic process-entry runtime and the service initialization protocol are
therefore separate layers. The callback, not `crt0`, owns service-specific
initialization.

## Readiness result and reply

### Service result

After the initialization callback returns, SEF constructs an `RS_INIT`
message containing the callback result. The default response path performs
`ipc_sendrec(RS_PROC_NR, ...)`
(`minix/lib/libsys/sef_init.c:110-138,455-466`).

The response is both:

- a readiness result sent to RS; and
- a blocking gate that prevents the service from continuing until RS replies.

VM uses a one-time asynchronous response during fresh boot to avoid a
VM/RS deadlock, then restores the ordinary blocking response callback
(`minix/lib/libsys/sef_init.c:468-483`,
`minix/servers/vm/main.c:222-238`).

### RS acceptance

During boot, RS receives an exact `RS_INIT` result, checks it, replies `OK`
except for the asynchronous VM special case, clears `RS_INITIALIZING`, and
updates liveness timestamps
(`minix/servers/rs/main.c:785-821`).

During ordinary RS operation, `do_init_ready()` additionally:

- rejects a response from a service not marked initializing;
- treats a non-`OK` result as initialization failure;
- replies before performing later restart finalization; and
- permits only one successful initialization completion.

See `minix/servers/rs/request.c:462-526`.

The baseline readiness transition is therefore:

```text
inhibited/prepared
    -> released and initializing
    -> service callback complete
    -> readiness result received
    -> RS reply
    -> service startup returns
```

## Dependency and start ordering

MINIX encodes boot order primarily through the sequence of
`boot_image_priv_table`. A service marked `SF_SYNCH_BOOT` forces RS to receive
that service's initialization result before moving to the next table entry.
All other outstanding results are collected after the release loop
(`minix/servers/rs/main.c:356-417`).

The fixed reference does not define:

- an explicit prerequisite graph;
- a cycle check;
- a stable service-ID dependency mask;
- a deterministic topological tie-break independent from source order; or
- a rule that only one ordinary service may be initializing at a time.

The current boot table also does not assign `SF_SYNCH_BOOT` to its listed
services (`minix/servers/rs/table.c:32-42`). Its boot behavior is therefore a
fixed release sequence followed by collection of several potentially
overlapping initialization results.

Dynamic service startup has an even broader publication path:

1. create and execute the process;
2. activate the RS instance;
3. publish its label through DS and map driver identity where applicable;
4. allow the process; and
5. send its SEF initialization request.

See `minix/servers/rs/manager.c:531-710,787-980`.

Publication and readiness are distinct. A dynamically started service may be
published before initialization completes.

## Timeout and fatality

RS records a per-initialization check time and uses a ten-second-equivalent
tick budget, expressed as `system_hz * 10`
(`minix/servers/rs/const.h:46-50`,
`minix/servers/rs/utility.c:18-25`).

Its periodic alarm:

- checks initializing services against that budget;
- checks heartbeat periods;
- requests liveness notifications;
- detects missed replies; and
- triggers restart or update handling.

See `minix/servers/rs/request.c:943-1045`.

MINIX therefore has an internal progress clock; it does not use a host-test
timeout or a sleep inside service startup as correctness evidence.

## Restart, recovery, and live-update breadth

RS is not only a boot launcher. The fixed reference also implements:

- heartbeat notifications and periodic liveness checks;
- initialization failure classification;
- executable copies and replicas;
- endpoint replacement;
- restart scripts;
- exponential restart backoff;
- reincarnation with a new endpoint;
- detached restart;
- clone and unclone operations;
- multi-component live update;
- state transfer;
- VM-first update ordering;
- rollback;
- RS self-update and restart;
- DS publication and unpublication; and
- driver, PCI, and device-manager rebinding.

Representative paths are:

- `minix/servers/rs/manager.c:328-354,713-760,988-1300`;
- `minix/servers/rs/request.c:943-1085`; and
- `minix/servers/rs/update.c:328-980`.

Those paths depend on DS, VM, PM, scheduling, driver protocols, state-transfer
support, and mature service lifecycle. They are not a prerequisite for a
deterministic first launcher.

## Sole ADR-0025 classification ledger

This table is the sole classification ledger for choices introduced by the
static bootstrap-launcher design. Existing endpoint, IPC, grant, handoff, and
runtime classifications remain in their earlier documents.

| ID | `micros` choice relative to the fixed MINIX baseline | Classification | Reason or replacement point |
| --- | --- | --- | --- |
| BL-01 | One immutable boot descriptor binds each static service identity to its exact boot policy/profile identity | Baseline parity | MINIX combines the kernel boot image and RS boot policy tables for the same boot-time identity and policy purpose |
| BL-02 | The kernel reserves every static process, first thread, root, endpoint, image mapping, stack, and initial context before ordinary service release | Baseline parity | MINIX creates boot processes and image identity before RS permits them to run |
| BL-03 | Exact privilege policy is installed while the service thread is held | Baseline parity | MINIX installs privileges while `RTS_NO_PRIV` inhibits execution |
| BL-04 | A non-launcher endpoint stays kernel-reserved and externally hidden until the atomic release transition | Compatible extension | MINIX exposes the endpoint earlier; staged visibility prevents clients from observing a service before exact profile installation and release |
| BL-05 | The launcher and kernel use exact process and endpoint generations in addition to stable service IDs | Compatible extension | Generation checks preserve MINIX service identity while rejecting stale slot reuse |
| BL-06 | Service prerequisites are explicit and validated with a deterministic lowest-ID topological order | Compatible extension | MINIX table order and optional synchronous flags are preserved while cycles and accidental source-order policy are rejected |
| BL-07 | Only one ordinary service is released and awaiting readiness at a time | Staged substitution | The serial development DAG replaces MINIX's overlapping boot initialization until a later control plane has a reviewed concurrency need |
| BL-08 | A service performs one versioned readiness call and waits for one launcher acknowledgment | Baseline parity | This preserves the SEF result-plus-reply gate without importing the complete SEF callback framework |
| BL-09 | The readiness call uses the accepted fixed IPC message and one-shot reply token | Compatible extension | The token is the accepted least-privilege replacement for MINIX `sendrec` reply authority |
| BL-10 | Readiness requires the exact active manifest endpoint generation and one starting-state transition | Compatible extension | MINIX checks the initializing slot; generation and staged-state checks strengthen stale and duplicate rejection |
| BL-11 | A RISC-V `time`-counter deadline and supervisor timer enforce progress | Required adaptation | The target has no MINIX `system_hz` ABI; the existing one-hart timer supplies deterministic target progress without sleeps |
| BL-12 | Embedded service images use checked, generated descriptors for the accepted fixed freestanding ELF shape | Required adaptation | The RISC-V ELF/runtime contract replaces MINIX multiboot and hosted executable mechanisms |
| BL-13 | All static service contexts are prepared before the one-way VM handoff | Required adaptation | ADR-0040 closes generic context preparation after handoff, so later launcher release may change only profile, endpoint, and scheduler state |
| BL-14 | VM readiness is accepted only after the separate `VM_READY` ownership commit | Required adaptation | ADR-0006 makes the irreversible handoff a required target transition that MINIX represents differently |
| BL-15 | TTY release and readiness are gated by the accepted begin/commit console states and exact manifest UART/IRQ assignment | Required adaptation | ADR-0012 requires an explicit two-phase QEMU `virt` ownership transfer |
| BL-16 | Service profiles are immutable named table entries and are never inferred from names, roles, devices, or prerequisites | Compatible extension | Exact profile identity preserves MINIX privilege setup while removing mutable mask construction from the launcher |
| BL-17 | Initialization failure, malformed readiness, timeout, or service loss is fatal with no restart fallback | Staged substitution | Recovery remains at the later DS/RS milestone |
| BL-18 | Dynamic commands, replicas, heartbeats, restart scripts, state transfer, live update, and DS publication are absent | Staged substitution | The later RS control plane replaces the static launcher after stable services and protocols exist |
| BL-19 | The launcher clears its controller binding, seals bootstrap authority, transitions its endpoint to exact-generation source-only state, and remains as a held wired process | Staged substitution | PM/VM teardown is not dependency-ready; source-only identity preserves already staged acknowledgments while rejecting every new launcher destination |
| BL-20 | `init` is created later through the PM/VFS/VM spawn transaction rather than appearing in the static manifest | Required adaptation | ADR-0009 assigns init descriptors and executable preparation to the reviewed spawn path; ADR-0043 explicitly corrects ADR-0007's older revocation wording |
| BL-21 | The kernel patches one bounded per-service configuration with exact self, launcher, and static peer endpoints | Required adaptation | ADR-0042 supplies no arguments or environment, while generation-safe endpoints cannot be inferred from fixed process slots; the data replaces MINIX `sys_whoami` plus the RS public-table grant without becoming authority |
| BL-22 | Every manifest entry carries explicit image-page, stack-page, configuration, and readiness-timeout bounds validated before allocation | Compatible extension | Fixed bounds make failure deterministic and allocation-free without changing the valid fixed-service startup behavior |

No unclassified divergence remains for this outcome.

## Derived `micros` boundary

The smallest dependency-ready launcher therefore requires:

- one pointer-free, versioned, immutable manifest with a fixed entry capacity;
- one immutable generated image catalog for already linked service ELFs;
- exact service IDs, names, process slots, profile IDs and names, page limits,
  prerequisites, timeout budgets, and narrowly defined role fields;
- one bounded kernel-patched startup configuration carrying exact endpoint
  generations as data;
- all static roots, mappings, stacks, threads, contexts, and reserved endpoints
  prepared before launcher entry;
- one active launcher endpoint and held non-launcher threads;
- one atomic profile-install, endpoint-publication, and thread-release
  transition at a time;
- one versioned readiness call and one token-bound acknowledgment;
- exact role gates for later VM and console transitions;
- deterministic timeout and fatal diagnostics;
- one irreversible bootstrap-authority seal; and
- no service restart, update, discovery, or dynamic manifest machinery.

## Source map

- `minix/kernel/table.c`
- `minix/kernel/main.c`
- `minix/kernel/proc.c`
- `minix/kernel/system/do_privctl.c`
- `minix/include/minix/type.h`
- `minix/include/minix/rs.h`
- `minix/include/minix/sef.h`
- `minix/include/minix/ipc.h`
- `minix/servers/rs/table.c`
- `minix/servers/rs/type.h`
- `minix/servers/rs/main.c`
- `minix/servers/rs/utility.c`
- `minix/servers/rs/request.c`
- `minix/servers/rs/manager.c`
- `minix/servers/rs/update.c`
- `minix/lib/libsys/sef.c`
- `minix/lib/libsys/sef_init.c`
- `minix/servers/pm/main.c`
- `minix/servers/sched/main.c`
- `minix/servers/vm/main.c`

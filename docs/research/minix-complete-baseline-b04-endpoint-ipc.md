# B04 Endpoint and IPC Baseline Audit

## Status

```text
audit unit:       B04 endpoints and IPC
micros commit:    da0bd95148f2704d5347c2af4c64085b90132746
micros tree:      3b90c7716d69efd68930b3f8ab86a32813bdd6c3
MINIX commit:     4db99f4012570a577414fe2a43697b2f239b699e
result:           audited; five blocking findings and one staged finding; not certified
implementation:   forbidden during complete-audit phase
```

B04 audits endpoint identity and lifecycle, immutable privileges, global
endpoint/object validation, sender/receiver queues, send/receive/call,
reply tokens, reply/receive, notifications, deadlock, endpoint close,
captured IPC syscalls, and deferred user-buffer delivery.

## Exact primary path manifest

B04 has 39 primary paths in the pinned tree.

### Public contracts

| Path | Responsibility |
| --- | --- |
| `include/micros/endpoint.h` | Endpoint/profile/state ABI and registry |
| `include/micros/ipc.h` | Fixed message and public IPC constants |
| `include/micros/ipc_abi.h` | Stable user-visible IPC results |
| `include/micros/ipc_buffer.h` | User-message snapshot/write planning |
| `include/micros/ipc_core.h` | Portable synchronous IPC operations |
| `include/micros/ipc_runtime.h` | Authoritative target IPC registry |

### Endpoint owners

| Path | Responsibility |
| --- | --- |
| `kernel/endpoint/endpoint.c` | Checked resolution and authorization |
| `kernel/endpoint/endpoint_close.c` | Source-only seal and final endpoint close |
| `kernel/endpoint/endpoint_codec.c` | Canonical profiles/records/zero state |
| `kernel/endpoint/endpoint_completion_validation.c` | Reply/delivery/completion shape validation |
| `kernel/endpoint/endpoint_identity.c` | Packing, owner/process/thread relations |
| `kernel/endpoint/endpoint_internal.h` | Endpoint-internal shared contract |
| `kernel/endpoint/endpoint_object_validation.c` | Complete registry/object relation validation |
| `kernel/endpoint/endpoint_private.h` | Endpoint-private declarations |
| `kernel/endpoint/endpoint_privilege.c` | Profile lookup and operation/target checks |
| `kernel/endpoint/endpoint_publication.c` | Reserve/profile/activate/publish |
| `kernel/endpoint/endpoint_queue_validation.c` | Queue/notification/matchability validation |
| `kernel/endpoint/endpoint_registry.c` | Profile-table initialization and registry validation |

### IPC owners

| Path | Responsibility |
| --- | --- |
| `kernel/ipc/ipc.c` | Send, receive, and call orchestration |
| `kernel/ipc/ipc_abi.c` | Portable-to-ABI result mapping |
| `kernel/ipc/ipc_buffer.c` | Up-to-two-page message plans and copies |
| `kernel/ipc/ipc_buffer_internal.h` | IPC-buffer private declarations |
| `kernel/ipc/ipc_close.c` | Endpoint cancellation preflight/commit |
| `kernel/ipc/ipc_common.c` | Message/thread/process/runtime helpers |
| `kernel/ipc/ipc_completion.c` | Delivery/no-message completion shaping |
| `kernel/ipc/ipc_deadlock.c` | Blocked-dependency walk |
| `kernel/ipc/ipc_internal.h` | IPC-private plans/contracts |
| `kernel/ipc/ipc_notifications.c` | Pending/immediate/kernel notification |
| `kernel/ipc/ipc_queue.c` | Raw queue match/append/unlink |
| `kernel/ipc/ipc_rendezvous.c` | Sender/receiver enqueue and delivery transactions |
| `kernel/ipc/ipc_reply.c` | Reply and atomic reply/receive |
| `kernel/ipc/ipc_runtime.c` | Target registry/profile initialization |
| `kernel/ipc/ipc_runtime_internal.h` | Target IPC-private declarations |
| `kernel/ipc/ipc_syscall.c` | Captured current-thread syscall guard/dispatch |
| `kernel/ipc/ipc_syscall.h` | Target syscall interface |
| `kernel/ipc/ipc_syscall_internal.h` | Captured syscall private contract |
| `kernel/ipc/ipc_syscall_request.c` | ABI decode and portable operation dispatch |
| `kernel/ipc/ipc_syscall_result.c` | Stable result/error publication |
| `kernel/ipc/ipc_tokens.c` | Reply-token availability, lookup, and delivery |

## Cross-owner source dependencies

| Path family | B04 operation | Primary/later owner |
| --- | --- | --- |
| `kernel/objects/**` thread/process IPC fields | B04-O01/O03-O09 | B03 |
| `kernel/scheduler/**` RTS block/wake, current guard, return completion | B04-O04-O11 | B03 |
| `kernel/memory/user_address_space_*` | B04-O10/O11 message translation | B02 |
| `kernel/trap/**`, `kernel/execution/**` | B04-O10 captured `ecall` and return | B05 |
| `arch/riscv64/ipc_ecall_test.S`, `ipc_syscall_test.S`, `ipc_syscall_panic_test.S` | B04-O10/O11 target evidence | B14 |
| `kernel/boot/**`, `kernel/tty/**` | B04-O07 kernel-origin notification callers | B06/B09 |

## Fixed MINIX source evidence

The B04 comparison uses only local source:

- `minix/include/minix/endpoint.h`
- `minix/include/minix/ipc_filter.h`
- `minix/include/minix/ipc.h`
- `minix/kernel/ipc_filter.h`
- `minix/kernel/proc.h`
- `minix/kernel/proc.c`
- `minix/kernel/ipc.h`
- `minix/kernel/priv.h`
- `minix/kernel/system.c`
- `minix/kernel/system/do_privctl.c`
- `minix/kernel/system/do_statectl.c`
- `minix/lib/libc/arch/i386/sys/_ipc.S`
- `minix/lib/libc/arch/arm/sys/_ipc.S`
- `minix/servers/rs/main.c`
- `minix/servers/rs/manager.c`
- `minix/servers/rs/utility.c`

## Logical variables

| Variable | Meaning |
| --- | --- |
| `pcap` | process/endpoint capacity |
| `tcap` | thread capacity |
| `hcap` | hart capacity |
| `fcap` | privilege-profile capacity |
| `q_s` | senders in one endpoint queue |
| `q_r` | receivers in one endpoint queue |
| `d` | blocked-dependency chain length |
| `ipcf_pool` | global MINIX IPC-filter slot capacity |
| `ipcf_max` | maximum elements embedded in one MINIX filter slot |
| `ipcf_chain` | filters linked to one receiver |
| `ipcf_e` | elements supplied in one installed filter |
| `Vobj` | B03 complete object/scheduler validation work |
| `Rmem` | B02 complete message-buffer translation work |

The pinned capacities are 64 endpoint/process slots, 128 thread slots,
8 harts, and 32 privilege profiles.

## Validation cost definitions

One endpoint-registry validation performs profile-name duplicate checks,
target-mask closure, and all endpoint record shapes:

```text
Vreg = O(fcap^2 * profile_name_bytes + pcap^2)
```

`micros_endpoint_registry_validate_objects()` additionally:

- runs `Vobj`;
- resolves profiles from process rows, repeatedly invoking `Vreg`;
- validates all queues and pending notification sources;
- detects any matchable sender/receiver pair with nested queue scans; and
- checks reply-token uniqueness with pairwise prior-thread scans.

```text
Vep = O(Vobj
        + pcap*Vreg
        + pcap^2
        + tcap^2
        + total queue links)
```

Because `pcap*Vreg` includes `pcap * pcap^2`, `Vep` contains a
`pcap^3` term.

The exact constant multiple matters: ordinary IPC operations call `Vep`
directly and call public resolution/authorization/rendezvous helpers that
invoke `Vep` and `Vreg` again.

## Operation ledger

### B04-O01 Endpoint identity, publication, resolution, and close

MINIX encodes slot plus generation and resolves by direct process-table slot
plus full endpoint equality. Privilege installation occurs while a process is
inhibited. Shutdown inhibits communication before cancellation and slot
release.

`micros` uses one endpoint record per process slot and
`FREE -> RESERVED -> ACTIVE -> SOURCE_ONLY/FREE` transitions. Direct
pack/unpack and validated-record lookup are `O(1)`. The public resolve helpers
first run `Vep`.

| Difference | Classification |
| --- | --- |
| Slot/generation identity and exact liveness | Baseline parity |
| 12/20 unsigned layout and generation quarantine | Compatible extension |
| Hidden reservation and source-only lifecycle | Compatible extension |
| Public resolution coupled to complete registry/object validation | Divergence requiring correction |

See B04-F001.

### B04-O02 Privilege profiles and authorization

MINIX stores operation and destination bitmaps in one privilege structure.
After installation, permission checks are direct bit tests.

`micros` stores an immutable indexed profile table with separate call, send,
notify, reply, reply/receive, and kernel-operation authority. This is a valid
least-privilege extension. However each profile lookup reruns `Vreg`.
Target authorization performs operation lookup plus additional source and
destination profile lookups, while endpoint authorization has already rerun
`Vep`.

```text
MINIX installed privilege check: O(1)
micros profile check:            one or more Vreg
micros target authorization:     repeated Vep + repeated Vreg
```

| Difference | Classification |
| --- | --- |
| Immutable named operation-specific profiles | Compatible extension |
| Revalidating the sealed profile/endpoint table for every bit query | Divergence requiring correction |

See B04-F002.

### B04-O02B Receiver-owned IPC filters

MINIX receivers may install bounded blacklist or whitelist filter chains.
Each filter contains bounded source/type match elements. Installation validates
and appends one filter; clearing walks the receiver chain and returns every
slot to the fixed pool. The fixed definitions are:

```text
ipcf_pool = 2 * NR_SYS_PROCS
ipcf_max  = 2 * NR_SYS_PROCS
```

Each allocation scans the global pool, each slot embeds the complete maximum
element array, and append walks the receiver's existing chain.
`CANRECEIVE` applies the receiver's filters to immediate send, queued-sender
selection, notification acceptance, and asynchronous delivery. RS composes a
temporary whitelist with CLOCK alarms to implement timed receive.

```text
install: O(ipcf_pool + ipcf_e + ipcf_chain)
clear:   O(ipcf_chain)
match:   O(total receiver filter elements)
receive queue search with filters:
         O(q_s * filter work)
storage: O(ipcf_pool * ipcf_max) static pool
         = O(NR_SYS_PROCS^2)
```

`micros` has no receiver-owned source/type filter or equivalent timed selective
receive.

**Classification:** Staged substitution; SOM-24.

### B04-O03 Complete registry/object relation validation

The complete validator is valuable diagnostic evidence. It verifies exact
process/endpoint agreement, queue topology, pending notification generations,
reply/completion shapes, unique tokens, and absence of immediately matchable
queued work.

Its worst valid-path work includes `Vep`, with `O(tcap^2)` token and
sender/receiver matchability terms and `O(pcap^2)` notification/profile
relations. It is invoked by ordinary endpoint resolution, authorization,
send/receive/call/reply/notify, rendezvous, close, runtime validation, and
selected-return completion.

| Difference | Classification |
| --- | --- |
| Explicit complete registry/object diagnostic validator | Compatible extension |
| Invoking that validator on ordinary IPC valid paths | Divergence requiring correction |

See B04-F001.

### B04-O04 Sender/receiver queues and rendezvous

MINIX appends blocked senders to one FIFO caller queue. It stores the head but
walks the complete queue to find the append position. A receiving process
records exact source or `ANY`; receive scans past unmatched senders without
reordering.

`micros` uses separate generation-safe sender and receiver queues because
blocked state belongs to threads and future processes may have multiple
threads. It also stores exact tail handles.

```text
MINIX sender append:     O(q_s)
micros sender append:    O(1)
unlink known member:     O(q)
find matching sender:    O(q_s)
find matching receiver:  O(q_r)
```

| Difference | Classification |
| --- | --- |
| FIFO order, unmatched preservation, arbitrary unlink | Baseline parity |
| Separate receiver queues and generation-safe links | Compatible extension |
| Additional tail index and constant-time sender append | Divergence requiring correction |

Their local work is also obscured by repeated `Vep` preflights. See B04-F006.

### B04-O05 Send, receive, and call

MINIX checks identity/permissions, attempts delivery, copies the fixed message
before mutation, checks deadlock only when blocking, and mutates the affected
queue/RTS state.

`micros` preserves that order and canonicalizes source/token fields. A blocked
call retains independent SEND and REPLY reasons. On the local mechanism:

```text
immediate send/call: O(q_r)
blocked send/call:   O(q_r + deadlock + enqueue)
receive:             O(notification selection + q_s)
```

With MINIX receiver filters, immediate/queued candidate checks multiply queue
search by receiver filter work. That mechanism is currently absent under
SOM-24.

Immediate sender/receiver or caller/receiver delivery uses the scheduler's
pair-transition helper, which places the 5,120-byte
capacity-sized transition-candidate array on the supervisor stack and invokes
B03 complete validation. A pending-notification receive that wakes only the
receiver uses the smaller single-wake plan.

Top-level and nested helpers add multiple `Vep`/`Vreg` passes before these
local operations.

| Difference | Classification |
| --- | --- |
| Match before deadlock/block and FIFO mutation | Baseline parity |
| Kernel-owned 64-byte snapshot and source overwrite | Baseline parity |
| One-shot token for call reply authority | Compatible extension |
| Repeated global validation around one local transition | Divergence requiring correction |
| Receiver-owned message filtering absent | Staged substitution; SOM-24 |

See B04-F001 and B04-F002.

### B04-O06 Reply tokens and atomic reply/receive

MINIX replies through an ordinary authorized send to the known caller
endpoint. `micros` uses an opaque monotonically increasing token bound to the
exact caller thread and callee endpoint.

Token allocation still scans all `tcap` threads to prove the new monotonic
token is unused. Reply lookup scans all threads for the token, scans again to
ensure the token-bearing request is no longer staged, then performs ordinary
reply preflight. Atomic reply/receive reuses that path. Reply and every
reply/receive branch that wakes two participants use the 5,120-byte scheduler
pair-transition frame plus nested validation witnesses.

```text
MINIX reply destination: direct endpoint/process reference
micros token allocate/resolve/pending checks: O(tcap)
```

| Difference | Classification |
| --- | --- |
| Opaque one-shot exact-thread reply authority | Compatible extension |
| Failure-atomic reply/receive | Compatible extension |
| Capacity scan instead of indexed token binding | Divergence requiring correction |

See B04-F004.

### B04-O07 Notifications

Both systems deliver immediately to an eligible receiver or coalesce pending
state by source. Reply waits exclude notifications. Lowest source identity wins
for `ANY`.

MINIX scans notification bitmap chunks and then advances source IDs until it
finds the first set bit in the first nonzero chunk. `micros` has exactly one
64-bit source bitmap and advances source slots until the first set bit. This is
the same lowest-pending-source algorithm specialized to one bitmap word.

```text
coalesce known source: O(1)
specific-source select: O(1)
ANY select: O(pcap) worst case in both implementations
storage: O(pcap^2) event-mask words plus O(pcap) source bitmaps
```

The quadratic word storage preserves arbitrary per-source/per-destination
event masks and has the same `O(pcap^2)` endpoint-wide class as
per-destination pending bitmaps.

| Difference | Classification |
| --- | --- |
| Nonblocking source-coalesced notification and reply-wait exclusion | Baseline parity |
| 64-bit event-mask payload and kernel-origin pseudo-source | Required adaptation |
| One-word lowest-source scan | Baseline parity |

Immediate user notification delivery that wakes notifier and receiver uses the
5,120-byte pair-transition frame. Deferred kernel notification preparation
uses the smaller single-wake plan.

### B04-O08 Deadlock detection

MINIX follows blocked endpoint dependencies directly through process records,
stopping at `ANY`, no dependency, or the caller. Work is linear in chain
length `d`.

`micros` resolves each endpoint in `O(1)`, but maps its owner to the sole live
thread by scanning all `tcap` threads. It also checks every new thread against
the linear visited prefix.

```text
MINIX:  O(d)
micros: O(d*tcap + d^2), worst O(tcap^2)
stack:  O(tcap) visited thread handles
```

The generation-safe bounded walk and SEND/REPLY/specific-RECEIVE precedence
are correct. The endpoint-to-thread projection algorithm is not.

**Classification:** Divergence requiring correction. See B04-F003.

The one-thread projection is staged by SOM-08; future wait-set exploration may
replace only this internal projection.

### B04-O09 Endpoint close and cancellation

MINIX inhibits communication, detaches the closing process, scans process
state, removes notification references, and wakes exact blocked peers.

`micros` preflights all threads, all endpoint queues, token/reply/staged
references, and notifications into one bounded close plan. It then performs
one scheduler transition batch and non-failing cancellation commit.

```text
local preflight/commit: O(pcap + tcap + total queue links)
plan storage:           O(tcap)
```

The close plan is about 3 KiB at the pinned capacity. Its nested scheduler
transition can add a 5,120-byte automatic candidate array plus B03 validator
witnesses on the 16 KiB supervisor stack.

| Difference | Classification |
| --- | --- |
| Exact dependent cancellation and unrelated `ANY` preservation | Baseline parity |
| Reply-token and staged-completion cancellation | Compatible extension |
| Full failure-atomic close plan | Compatible extension |

The repeated `Vep`/B03 validation and stack maximum remain evidence gaps.

### B04-O10 Captured syscall and user buffers

MINIX architecture stubs route a known caller into IPC and copy messages
through kernel/VM mechanisms. `micros` captures RV64 arguments, snapshots an
outbound 64-byte message before mutation, and stages inbound delivery until
the selected return path.

One message plan translates at most two pages. Because translation currently
uses B02's complete address-space path, each plan can cost up to two `Rmem`
terms.

| Difference | Classification |
| --- | --- |
| RV64 `ecall` register ABI and exact result mapping | Required adaptation |
| Up-to-two-page retained physical copy plan | Compatible extension |
| Complete B02 validation for each translated page | Divergence requiring correction; B02-F005 |

### B04-O11 Retained-buffer failure and deferred MINIX mechanisms

MINIX suspends a process and asks VM to resolve a delivery fault. `micros`
panics for a trusted-bootstrap buffer that became invalid after acceptance.

| Difference | Classification |
| --- | --- |
| VM-assisted IPC-buffer fault/retry absent | Staged substitution; SOM-04 |
| Asynchronous/nonblocking MINIX IPC absent | Staged substitution; SOM-23 |
| Multithread deadlock wait-set projection absent | Staged substitution; SOM-08 |
| POSIX thread cancellation interactions absent | Staged substitution; SOM-08 |
| Signal interruption/restart behavior absent | Staged substitution; SOM-09 |
| IPC timeout/timed-wait behavior absent | Staged substitution; SOM-10 |

## Time, stack, and auxiliary-space envelope

| Operation | MINIX valid path | `micros` valid path | Storage |
| --- | --- | --- | --- |
| Endpoint resolve | `O(1)` slot/equality | `Vep + O(1)` | Registry `O(pcap^2+fcap)` |
| Privilege bit check | `O(1)` | one or more `Vreg = O(fcap^2*name+pcap^2)`, often after repeated `Vep` | Immutable `O(fcap)` profiles |
| Queue append/match | append `O(q_s)`, match `O(q)` | append `O(1)`, match `O(q)`, plus repeated validators | Head/tail intrusive links |
| Send/receive/call | identity/auth + queue/filter/deadlock work | constant multiples of `Vep/Vreg` + local work | Fixed messages/plans; immediate pair paths add 5,120-byte scheduler candidates |
| Reply | direct caller endpoint | repeated `O(tcap)` token scans plus validators | Token in thread state; pair wake adds 5,120 bytes |
| Notification ANY | bitmap chunk/bit scan, `O(pcap)` worst | one-word bit scan, `O(pcap)` worst | `O(pcap^2)` event masks; immediate pair wake adds 5,120 bytes |
| Deadlock | `O(d)` | `O(d*tcap+d^2)` | `O(tcap)` visited handles |
| Close | `O(pcap+queues)` | `Vep + O(pcap+tcap+queues+Vobj)` | ~3 KiB close plan plus nested ~5 KiB scheduler candidates |
| Message copy | selected process/buffer | up to two `Rmem` translations | Fixed two-page plan |

Registry initialization also uses an automatic `O(fcap)` candidate profile
array, about 2 KiB at the pinned layout. Ordinary immediate pair-delivery,
sender-consuming receive, reply/reply-receive, and user-notification paths may
nest the 5,120-byte scheduler transition array with endpoint/object validator
witnesses. No current evidence measures the maximum nested
close/syscall/completion stack against 16 KiB.

## Executable evidence mapping

| Evidence | What it proves | What it does not prove |
| --- | --- | --- |
| `endpoint-profile` | identity, lifecycle, immutable profiles, authorization, endpoint model | direct-resolution/bit-check work |
| `ipc-model` | queues, all operations, tokens, notifications, deadlock, close, seeded mixed transitions | production validation/probe counts |
| `test-qemu-endpoint` | target endpoint generation/profile/publication behavior | blocking IPC |
| `test-qemu-ipc`, `test-qemu-ipc-ecall-core`, `test-qemu-ipc-syscall` | real queues, calls/replies, notification, ABI, registers, teardown | local versus global validation cost |
| `test-qemu-ipc-syscall-panic` | retained-buffer fatal policy | VM-assisted retry |
| scheduler selected-completion evidence | staged IPC return integration | `Vipc` and translated-page operation counts |

## Evidence gaps

| ID | Gap | Later owner |
| --- | --- | --- |
| B04-E001 | No validator counter separates profiles, process relations, queues, notifications, matchability, token uniqueness, and `Vobj` | B14 |
| B04-E002 | No operation trace counts repeated nested `Vep`/`Vreg` calls for one send/receive/call/reply/notify | B14 |
| B04-E003 | No deadlock counter records dependency hops, full thread scans, and visited-prefix comparisons | B14 |
| B04-E004 | No token-index counter separates allocation, reply lookup, and pending-request scans | B14 |
| B04-E005 | No receiver-filter install/match/clear/timed-receive evidence exists because SOM-24 is absent | B14 after SOM-24 design |
| B04-E006 | No maximum nested immediate pair, reply/receive, notification, close, syscall, completion, and validator stack evidence exists for the 16 KiB thread stack | B14 |
| B04-E007 | No asynchronous/nonblocking IPC evidence exists because SOM-23 is absent | B14 after SOM-23 design |
| B04-E008 | No queue-operation counter distinguishes MINIX linear append from the `micros` tail-index optimization | B14 |

## Findings register

### B04-F001: Complete endpoint/object validation is on every IPC path

- Severity: High
- `micros` evidence:
  - `micros_endpoint_registry_validate_objects`
  - public resolve and every IPC operation/rendezvous/close/completion caller
- MINIX evidence: direct endpoint/privilege/queue checks in `proc.c`
- Difference: local operations pay `Vep`, including `Vobj`, capacity-wide
  profile/endpoint/notification state, nested queue matchability, and
  pairwise token checks. Repeated process-profile resolution makes `Vep`
  include `pcap*Vreg` and therefore a `pcap^3` term.
- Impact: IPC latency scales with global configured state rather than the
  source, destination, and affected queues.
- Smallest correction boundary: preserve one explicit complete diagnostic
  validator while ordinary operations use one retained validated
  source/destination/profile/queue context and local invariants.
- Dependencies: B03-F001, B03-F006, B02-F005.

### B04-F002: Resolution and authorization repeatedly revalidate immutable state

- Severity: High
- `micros` evidence:
  - `endpoint.c`
  - `endpoint_privilege.c`
  - nested rendezvous resolution
- MINIX evidence: direct installed privilege bitmaps and direct endpoint slot
- Difference: one top-level operation reruns `Vep` for source/target helpers
  and reruns `Vreg` for immutable source/destination profile queries.
- Impact: constant-time endpoint and bitmask checks become repeated
  capacity-wide audits.
- Smallest correction boundary: one operation-scoped validated endpoint/profile
  view passed through authorization and rendezvous; sealed profile queries
  become direct indexed bit tests.
- Dependencies: B04-F001.

### B04-F003: Deadlock projection is quadratic in thread capacity

- Severity: High
- `micros` evidence:
  - `ipc_deadlock.c:resolve_sole_live_thread`
  - linear visited-prefix check
- MINIX evidence: direct process-table blocked-on traversal
- Difference: every dependency hop scans all threads and the prior visited
  chain.
- Impact: worst `O(tcap^2)` work before one caller blocks.
- Smallest correction boundary: maintain/directly resolve the v0.1 sole-thread
  handle per process and use a thread-slot bitmap for visited membership,
  yielding `O(d)`. SOM-08 later replaces the projection with wait-set logic.
- Dependencies: B03 object lifecycle and SOM-08.

### B04-F004: Reply tokens have no direct binding index

- Severity: High
- `micros` evidence:
  - `ipc_reply_token_is_available`
  - `resolve_reply_waiter`
  - `reply_request_is_pending`
- Difference: allocation/delivery/reply repeatedly scan all live threads for
  one opaque monotonic token.
- Impact: reply latency and call setup become `O(tcap)` despite one active
  token per thread and monotonic nonreuse.
- Smallest correction boundary: a bounded token-to-thread index or equivalent
  direct binding that preserves opaque values, exact callee generation,
  one-shot consumption, and close cancellation.
- Dependencies: B04-F001 and B04 close audit.

### B04-F005: Receiver-owned MINIX IPC filters are absent

- Severity: High
- Classification: Staged substitution under SOM-24; this finding does not
  block the pre-node-Q baseline queue.
- `micros` evidence: no endpoint/receiver filter state or operation
- MINIX evidence:
  - `CANRECEIVE` in `minix/kernel/ipc.h`
  - filter pool/install/match/clear in `minix/kernel/system.c`
  - `do_statectl.c` and RS timed receive
- Difference: `micros` cannot install receiver-owned blacklist/whitelist
  source/type filters. Queue and immediate matching therefore lack a MINIX
  acceptance predicate.
- Impact: selective/timed receive, filtered queued senders, filtered
  notifications, and asynchronous delivery cannot reproduce the reference.
- Smallest correction boundary: SOM-24 design/implementation for a bounded
  filter pool/chain, exact matching order, VM-safe message-type access,
  install/clear authority, and queue-search multiplication.
- Dependencies: SOM-10 timers, SOM-01 RS, SOM-23 asynchronous IPC.

### B04-F006: Sender tail indexing is a pre-baseline optimization

- Severity: Medium
- `micros` evidence: endpoint `sender_tail` and `ipc_append_thread`
- MINIX evidence: caller-queue traversal before append in `mini_send`
- Difference: MINIX append is `O(q_s)` over a head-only list; `micros` adds a
  tail index and appends in `O(1)` without a measured post-baseline decision.
- Impact: the first implementation no longer reproduces the reference queue
  data structure/algorithm, even though observable FIFO order is unchanged.
- Smallest correction boundary: reproduce the head-only linear append for the
  accepted baseline, then evaluate the tail index later with measured queue
  pressure and an optimization decision.
- Dependencies: B14 queue operation counts.

## Unit conclusion

B04 primary-path and operation coverage is complete for the pinned tree. The
audit adds B04-F001 through B04-F004 and B04-F006 as blocking findings,
records B04-F005 as the SOM-24 staged-substitution finding, confirms
B03-F006's completion dependency, and records SOM-04, SOM-08, SOM-09,
SOM-10, SOM-23, and SOM-24.

B04 is **audited but not certified**. Independent review corrected
receiver-filter source/complexity/storage closure, registry and endpoint
validation bounds, notification selection, sender-queue append structure, and
ordinary IPC stack accounting. Final re-review reported B04 complete. No B04
correction may start during the read-only audit.

## References

- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [ADR-0081](../adr/0081-complete-posix-compatible-system-goal.md)
- [Complete audit contract](minix-complete-baseline-audit.md)
- [Staged-omission manifest](posix-staged-omission-manifest.md)
- [B03 object/scheduler audit](minix-complete-baseline-b03-objects-scheduler.md)
- [Endpoint/IPC study](minix-endpoint-and-ipc.md)
- [Endpoint source baseline](minix-kernel-endpoint-source-organization.md)
- [IPC source baseline](minix-kernel-ipc-source-organization.md)

# MINIX TTY Source-Organization Baseline

## Reference and scope

This is the ADR-0025 responsibility trace for the Accepted
[TTY source contract](../architecture/tty-source-organization.md), not another
terminal design. Accepted ADR-0012 and ADR-0047 retain authority. The earlier
[TTY/console/IRQ behavioral study](minix-tty-console-and-irq.md) remains the
full adaptation ledger.

The user-provided local MINIX checkout is
`/Users/alephling/Repo/minix`, fixed at
`4db99f4012570a577414fe2a43697b2f239b699e`. The checkout HEAD and selected
TTY, serial, character-driver, kernel utility and early-serial paths were
verified unchanged against that commit before tracing. This study records
responsibilities and observations, not copied code or ABI.

## Actual source trace

### Request and notification dispatch are distinct from device policy

`minix/drivers/tty/tty/tty.c:138-222` separates hardware notification handling
from ordinary character requests. A hardware event invokes serial device
inspection; notification count is not terminal input. The common character
layer retains a separate task-reply function carrying endpoint, request ID
and result (`minix/lib/libchardriver/chardriver.c:126-150`).

The reusable boundary is dispatch versus request state and completion
ownership. `micros` keeps its existing one-peer submit/notify/collect
protocol, reply-token release boundary and source-`NONE` IRQ envelope.
It does not import MINIX message structures or deferred-reply transport.

### Pending read, write and cancellation have exact owners

`tty.c:469-579` checks per-direction occupancy and request bounds before
retaining caller/grant/count/ID and advancing device events. Reads may wait
for canonical input while output progresses independently.

`tty.c:778-810` cancels only an exact endpoint/request-ID match; another
pending request is not selected by a stale cancellation.

These are baseline responsibilities for the existing `micros` read, write
and request/collection owners. ADR-0047's separate accepted request and
collection semantics remain an intentional bounded adaptation, not a reason
to merge terminal queues or consume a busy request ID.

### Canonical queue and grant delivery are separate phases

`tty.c:901-945` drains pending device events and then processes input delivery
and completion. `tty.c:949-1006` stages bounded resident bytes, uses safe-copy
grant transfer, updates retained queue/request state and stops at a canonical
line boundary.

The `micros` source correction must expose, not redesign, its stronger
existing all-or-error staging/discard/commit boundary: failed grant delivery
does not consume input. The cohesive terminal queue and UART state modules
remain intact.

### UART resident acceptance differs from physical drain

`minix/drivers/tty/tty/arch/earm/rs232.c:203-281` copies grant-backed output
into resident serial storage, applies output conversion, starts hardware
work and completes accepted source bytes. Physical output may continue.
`rs232.c:292-311` has a separate bounded echo path.

This matches the responsibility split between resident write publication,
UART effects and independent output observations. `micros` retains its
original echo priority, CR/LF state, completion collection and resident-only
writable hint. No shift-register flush protocol or new low-level ring is
added.

### Mapping and explicit IRQ acknowledgment are platform adaptations

`rs232.c:503-565` requests bounded device memory authority, maps the UART,
installs IRQ policy and enables the serial mechanism. `rs232.c:588-603`
handles a hardware notification by draining the UART handler before explicit
IRQ reenable.

The parity is exact resource authority and acknowledgment after device
inspection. The mechanisms differ deliberately: `micros` uses one manifest
tuple, one post-handoff device PTE without allocation, PLIC source 10 retained
in service, and exact TTY-controlled completion. MINIX's dynamic legacy IRQ
hook and broad physical mapper do not become new `micros` interfaces.

## Classification ledger

| Topic | Existing relationship | Source-correction constraint |
| --- | --- | --- |
| Dispatch/request ownership | Baseline parity with fixed `micros` transport | Separate facade, read/write and request/reply owners without wire changes. |
| Pending request identity | Baseline parity | Preserve endpoint generation, ID, grant and slot checks and all first failures. |
| Cancellation | Baseline parity | Keep exact pending-read selection and complete original release. |
| Read delivery | Bounded all-or-error `micros` adaptation | Preserve stage, copy, discard/commit and notification chronology. |
| Write completion | Resident-acceptance parity | Preserve physical drain, completion collection and writable-hint distinction. |
| UART/line discipline | Baseline separation | Leave cohesive terminal and UART state modules unchanged. |
| MMIO mapping | Required Accepted platform adaptation | No generic mapping, frame allocation, root growth or delegated device grant. |
| Notification/acknowledgment | Parity with PLIC adaptation | Preserve coalescing, retained claim and explicit completion after draining. |
| Early console/panic | Required Accepted ownership adaptation | Keep one normal owner and terminal seizure; no normal fallback. |
| Model and native doubles | Existing `micros` evidence, not imported MINIX machinery | Retain independent reference state, callbacks, actual jump targets and exact observations. |
| Fixture generator | Existing `micros` bootstrap representation | Keep descriptor order, real quotas/topology and exact generated bytes. |
| POSIX/recovery breadth | Later work | No termios, signals, job control, multiple terminals, workers or restart machinery. |

The exact 26-owner/156-original inventory is a current `micros` organizational
choice. It is not a claim that MINIX has the same file list, policies, fixture
layout or protocol.

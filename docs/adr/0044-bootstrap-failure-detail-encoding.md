# ADR-0044: Bootstrap Failure Detail Encoding

- Status: Accepted
- Date: 2026-10-08
- Refines: ADR-0014, ADR-0033, ADR-0041, and ADR-0043
- Supersedes in part:
  - ADR-0043's operation-11 common register shape requiring `a4` to be zero
    for every bootstrap-control command.
  - ADR-0043's `FAIL` register shape, which provides a failure reason but no
    field capable of carrying the required `ready-malformed` detail code.

## Context

ADR-0043 requires every bootstrap fatal path to emit one stable record:

```text
MICROS_BOOTSTRAP_FAILURE reason=<reason> service=0x<16 hex> endpoint=0x<16 hex> phase=<phase> state=<state> detail=0x<16 hex>
```

Most detail values are derived authoritatively by the kernel:

- `manifest-cycle` uses the unresolved service-ID mask;
- `release-order` uses the expected next service ID; and
- `ready-timeout` uses the expired absolute deadline.

`ready-malformed` is different. The launcher validates the readiness message
after ordinary IPC has returned it to user space and classifies the first
malformed field as one of nine stable codes. The kernel no longer retains the
delivered message bytes, so it cannot reconstruct that field code from the
operation-11 inputs defined by ADR-0043.

ADR-0043 assigns:

```text
a0  FAIL
a1  implicated service ID
a2  implicated endpoint
a3  failure reason
a4  zero
a5  zero
a6  zero
a7  BOOTSTRAP_CONTROL
```

This shape cannot satisfy its own diagnostic contract. Emitting zero or the
failure-reason enum as `detail` would make the stable record false. Retaining
the complete last delivered message in the kernel solely for diagnostics
would duplicate IPC payload state, complicate completion clearing, and grant
no additional authority.

The discrepancy was found during the independent review of ADR-0043's kernel
preparation and bootstrap-control implementation, before the launcher wrapper
or its QEMU acceptance images were added.

## Decision

Operation 11 keeps the same syscall number and commands. `FAIL` uses `a4` as
one explicit 32-bit launcher failure-detail field:

```text
a0  FAIL
a1  implicated service ID, or zero
a2  implicated endpoint, or MICROS_ENDPOINT_NONE
a3  launcher failure reason
a4  launcher failure detail
a5  zero
a6  zero
a7  MICROS_SYSCALL_OPERATION_BOOTSTRAP_CONTROL
```

`a0`, `a1`, `a2`, `a3`, and `a4` require zero upper 32 bits for `FAIL`.
`RELEASE`, `ACCEPT_READY`, and `COMPLETE` continue to require `a4`, `a5`, and
`a6` to be zero. No generic runtime wrapper is added; the launcher-private
wrapper owns this register shape.

### Detail authority

The launcher may provide a nonzero detail only for:

```text
reason = ready-malformed
```

For that reason, `a4` is exactly one ADR-0043 malformed-field code:

```text
1  message type
2  reply token
3  protocol version
4  service ID
5  manifest version
6  flags
7  declared endpoint
8  reserved payload tail
9  unrelated kernel notification or message class
```

Every other launcher-submitted failure reason requires `a4 == 0`.

The kernel does not trust launcher-supplied detail for values it can derive:

- `release-order` ignores no caller value because `a4` must be zero and emits
  the exact next service ID from authoritative bootstrap state;
- `ready-timeout` is kernel-originated and emits the armed absolute deadline;
- manifest failures are kernel-originated and emit their validator-produced
  detail; and
- all reasons whose ADR-0043 detail is zero emit zero.

The launcher-provided malformed-field code is diagnostic data, not authority.
It cannot select a process, endpoint, token, transition, profile, or recovery
action.

### Validation and precedence

Operation-11 decode retains ADR-0043's common ordering.

For `FAIL`, ABI validation rejects with `MICROS_SYSCALL_ABI_ARGUMENT` before
current-thread resolution when:

- any of `a0` through `a4` exceeds its defined 32-bit width;
- `a5` or `a6` is nonzero;
- or the command is unknown.

After current-thread resolution, controller authorization, phase checking, and
complete runtime validation, command-specific validation rejects an unknown
failure reason, a `ready-malformed` detail outside `[1, 9]`, or a nonzero
detail for any other reason. The service, endpoint, and lifecycle checks then
remain those defined by ADR-0043. This preserves the accepted precedence:
malformed reason/detail semantics cannot hide an unauthorized caller, sealed
phase, or runtime corruption.

A recoverable rejection preserves bootstrap, IPC, scheduler, object, and
output state except for the ordinary syscall result and advanced `sepc`.

Successful `FAIL` remains nonreturning:

1. validate the exact controller authority and running phase;
2. validate the complete runtime and request semantics;
3. derive every kernel-owned diagnostic field;
4. accept the launcher field code only for `ready-malformed`;
5. record phase `FAILED`;
6. emit exactly one stable bootstrap failure record; and
7. enter `MICROS_PANIC reason=bootstrap-failure`.

There is no retry, recovery, message retention, or alternate classification.

## Behavioral baseline classification

The canonical
[MINIX RS/SEF study](../research/minix-rs-sef-bootstrap.md) remains the sole
ADR-0025 classification ledger for this outcome:

- BL-17 remains a **Staged substitution**: malformed readiness is fatal with
  no restart fallback until the later DS/RS milestone; and
- BL-23 classifies transport of one structured malformed-field code as a
  **Compatible extension** for deterministic `micros` observability.

The new register is therefore an ABI repair for an existing staged failure
policy, not a new service-management mechanism.

## Test evidence

Implementation begins with native operation-11 decoder regressions covering:

- every malformed-field detail value 1 through 9;
- zero, 10, upper-bit, and unrelated-reason detail rejection;
- `a5` and `a6` rejection;
- output preservation on every rejection; and
- unchanged `RELEASE`, `ACCEPT_READY`, and `COMPLETE` shapes.

The malformed-readiness QEMU scenario later proves that one real launcher
`FAIL` ecall emits the exact reason and field-code detail before the ordinary
bootstrap panic. Existing operations 1 through 10 and their register
preservation evidence remain unchanged.

## Consequences

- ADR-0043's stable `ready-malformed` diagnostic becomes implementable.
- One previously unused operation-11 register gains a narrow, versioned
  meaning only for `FAIL`.
- Kernel-derived diagnostic fields remain authoritative.
- The kernel does not retain delivered readiness payloads.
- The launcher-private wrapper gains one scalar argument; the generic
  ADR-0042 runtime remains unchanged.

## Alternatives considered

### Pack reason and detail into `a3`

Rejected. It overloads the register that is already a full-width reply token
for `ACCEPT_READY`, obscures the command-specific ABI, and adds shifts and
masks where one unused register is available.

### Retain the last readiness message in kernel bootstrap state

Rejected. The launcher already owns classification after ordinary IPC return.
Duplicating payload bytes in the kernel complicates completion lifetime and
does not improve authority.

### Emit zero or the failure reason as detail

Rejected. Either value violates ADR-0043's stable diagnostic contract and
would make acceptance evidence unable to identify the malformed field.

### Remove malformed-field detail from ADR-0043

Rejected. The bounded field code is useful deterministic evidence and costs
only one already available syscall register.

# ADR-0087: Phase-Bounded VM Load Temporaries

- Status: Accepted
- Date: 2026-11-01
- Refines: ADR-0033, ADR-0034, ADR-0050, ADR-0057, ADR-0080, and ADR-0082
- Supersedes in part: ADR-0084's prohibition on production edits in EV03,
  solely for the automatic-storage lifetime correction below

## Context

EV03's three designs #180, #181, and #182 are merged. Its uncommitted
implementation joined actual C and assembly objects to linked images and
rejected an observed VM service bound of 4,240 bytes against the unchanged
4,096-byte user stack. The corresponding metadata-only bound is 3,664 bytes.
No observed TTY guest was launched after that static failure.

The measured chain retains the 1,696-byte observed
`micros_vm_load_prepare` frame while the complete state validator constructs
another ELF/stack plan and decodes the request. Its transaction candidate,
temporary plans, and frame arrays are no longer needed at the final
validation call, but their function frame remains live. Compiler entry/exit
instrumentation makes this existing lifetime overlap exceed the capacity.

[The research record](../research/vm-load-temporary-stack-evidence.md)
contains the exact artifacts, fixed MINIX comparison, and resource account.
The implementation has been preserved as a non-authoritative patch and
abandoned under ADR-0033. This design does not certify that patch.

## Decision

Permit one narrowly bounded source correction inside the restarted EV03
implementation: give the existing automatic preparation objects a private
function lifetime that ends before final complete validation.

The sole production source affected is `servers/vm/vm_load.c`.
No public signature, data structure, persistent storage, allocation policy,
algorithm, validation predicate, protocol, error value, or budget changes.
The exact contract is in
[the stack specification](../architecture/stack-bound-evidence.md#phase-bounded-vm-preparation).

### Three sequential phases

`micros_vm_load_prepare` remains the public entry and owns:

1. the existing null-argument checks, complete initial validation with the
   same staged-input expression, IDLE check, and source-shape check;
2. one call to private `vm_load_prepare_transaction`, passing the same eight
   input arguments; and
3. after successful helper return, the existing
   `vm_core_validate_state(state, false)` call and its exact result.

The helper owns all existing preparation-only automatic aggregates:
the transaction candidate, ELF plan, application-stack plan, four-element
table-frame array, and application-capacity data-frame array. It retains
the existing temporary scalar work needed by those objects.

Its work is exactly the original sequence from `prepare_binary_plans`
through `state->phase = MICROS_VM_LOADER_INPUT_COPIED`. Existing error
returns are unchanged. On publication it returns `MICROS_VM_CORE_OK`;
the public entry immediately performs the final validator.

No complete state-validation call occurs inside the helper, and no
preparation aggregate remains in the public entry. A lexical block alone
is insufficient: actual compiler and machine evidence must establish
separate frames and release of the helper before final validation.

### Preserved authority and failure behavior

The existing serialized VM state remains the only transaction owner.
Arguments, VFS-to-VM direct grants, process reservation, semantic PID,
endpoint checks, frame selection, and kernel scratch/freeze authority do
not change.

All preflight and failure precedence remains exact. Initial-validation,
phase, and source-shape failures return before preparation. Binary and
capacity failures use the same existing cleanup and returned errors.
Successful input publication retains its exact copy/zero order, by-value
transaction assignment, phase write, and final complete validation.

The local arrays' initial zeroing moves with their declarations into the
helper. It remains before their first read and changes no externally
observable memory or state. No pointer to these temporaries escapes.
Persistent snapshots and the transaction still contain values, not pointers
to a released frame.

This does not move complete validation off the runtime path, cache its
results, select a new allocator, change byte-copy algorithms, or retire a
B07 finding. Those corrections remain at VM01-VM06 and their prerequisites.

### Exact exception to the evidence barrier

This one storage-lifetime edit is a tightly coupled prerequisite repair
within EV03, not a new parallel runtime task or permission to begin VM01.
It is the only production edit newly admitted by this ADR before EV04.
The correction DAG's finding ownership, node/edge set, preferred order,
BASE01, STACK13, and RECERT01 gates remain unchanged.

The implementation restarts only after this design is independently reviewed,
Accepted, and separately merged. EV03 then implements the helper and complete
evidence as one outcome under all four designs. Further budget failures
remain blockers; this exception cannot be generalized to other functions.

### Evidence authority and resource accounting

The source baseline is the unchanged load function at the merged EV02
reference `fc98edd26505ac522d3752ad85f09366e8085f32`.
An independent structural check must prove the three-phase split and exact
original statement/argument/order preservation, including every error return,
initialization, assignment, cleanup, and validator invocation.

The check may inline only this one declared helper for historical source
comparison. It reads the actual candidate bodies; it does not obtain a
replacement body from Git, discard arbitrary calls, normalize runtime edits,
or regenerate an expected digest. Existing header, source-owner, dependency,
case, recipe, and algorithm evidence remains active.

The source contract and the stack oracle are distinct. Inline reconstruction
proves preserved statements, not improved stack use. Actual `.su`,
`.stack_sizes`, machine calls, return restoration, and the closed graph prove
resource use. The helper's frame is fully counted while it is live.

Neither the ordinary nor the observed graph may exceed 4,096 bytes.
Do not increase stack/image limits, reduce coverage, exclude functions from
instrumentation, replace the observer ABI, change compiler optimization, or
subtract instrumentation cost.

## Acceptance

The restarted implementation must provide:

- a fresh failing regression for the current overlapping preparation and
  validation lifetime, before changing `vm_load.c`;
- an exact source/phase check rejecting reordered validation, skipped cleanup,
  changed arguments or returns, escaping local addresses, extra mutable
  globals, incomplete helper expansion, and aggregates left in the facade;
- native VM load, invalid/staged-input, capacity, rollback, and service
  evidence with existing state/result behavior preserved;
- independent agreement of actual compiler frame sources, a real linked
  helper call and its normal-return continuation before final validation,
  and the complete ordinary/observed VM-service bound within 4 KiB;
- every ADR-0084/0085/0086 static configuration, all ten target scopes,
  metadata byte passivity, observer ABI/captures, and blocked B13 record;
- the complete current author gate and clean independent implementation
  review before publication and merge.

The measured 4,240-byte chain is a Red witness, not the future expected
maximum. Neither this design nor a narrow VM regression closes EV03 without
its complete final acceptance.

## Phase barrier

This PR contains documentation only. No helper, test, build scaffold,
prototype, instrumentation change, or runtime correction is authored here.
`ev03-stack-evidence-implementation` remains blocked on
`ev03-vm-temporary-lifetime-design` until its separate design merge.

## Alternatives considered

- Enlarge the stack or subtract hook overhead: hides a required resource
  failure and contradicts ADR-0084.
- Remove complete validation now: changes failure/complexity policy owned by
  later VM01 and VM03.
- Promote the candidate to static state: changes lifetime, reentrancy, and
  ownership rather than releasing temporary automatic storage.
- Use a lexical scope, union overlay, optimizer hint, or forced tail call:
  does not independently establish the required frame lifetime.
- Switch to bare-entry instrumentation or omit functions: changes another
  Accepted observation contract without solving this bounded ownership
  overlap.
- Broadly refactor the VM loader: unnecessary for this one demonstrated
  stack constraint.

## Design review disposition

Independent GPT-6.1 Sol review inspected the complete design and exact author
evidence and reported no substantive issue. Acceptance records the design
result, not a working helper, reduced stack bound, or EV03 certificate.
The separate merge remains the prerequisite for implementation restart.

## References

- [Measured VM stack evidence](../research/vm-load-temporary-stack-evidence.md)
- [Stack contract](../architecture/stack-bound-evidence.md)
- [VM source ownership](../architecture/vm-service-source-organization.md)
- [B07 audit](../research/minix-complete-baseline-b07-vm.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)

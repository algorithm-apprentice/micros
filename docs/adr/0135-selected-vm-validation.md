# ADR-0135: Selected VM Validation

- Status: Accepted
- Date: 2026-11-13
- Refines: ADR-0045, ADR-0050, ADR-0055, ADR-0057, ADR-0077, ADR-0078,
  ADR-0080, ADR-0083, ADR-0084, ADR-0087, ADR-0107, ADR-0114, ADR-0119,
  ADR-0123, ADR-0128 and ADR-0133
- Supersedes after Acceptance and separate design merge: complete VM
  validation as an ordinary-transition pre/postcondition, its unconsumed-
  corruption precedence, and only the dependent command-admission/root-
  binding boundaries specified by the canonical contract

## Context

BOOT03 #270 is independently reviewed and merged. VM01 owns B07-F001 and
has its actual BOOT02, MEM04 and GRANT01 predecessors. The preferred
sequence does not add a BOOT03 dependency edge.

The [fixed study](../research/minix-selected-vm-validation.md) traces
MINIX's endpoint-slot/ACL and selected process/region/VMCTL admission.
The fixed VM build disables its optional whole-state sanity machinery.

Current `micros` service transitions rehash the imported boot object,
inspect inactive input/frame storage, scan every frame and mapping record,
and recompute frame selection. Kernel dynamic commands reenter complete
VM, bootstrap and PM validation before their own operation-specific work.
Memory root validation then reenters complete VM state through its
exception-authority getter.

ADR-0077/0078 correctly retain complete digest/corruption coverage, but
also require that complete cadence on ordinary transitions. ADR-0087
preserves the same calls while separating temporary lifetimes. Those
invocation requirements need explicit refinement; simply deleting calls
would expose unbounded record counts, unchecked frame indices, backend
effects before validation and late failures after publication.

## Decision

After independent review, Acceptance and separate merge, implement
[the selected VM contract](../architecture/selected-vm-validation.md).
This phase authors no executable artifacts.

### Keep complete diagnostics, select ordinary state

Retain the complete service/kernel VM validators and their original
digest, zero-tail, global-membership, uniqueness and corruption semantics.
Cold import, service initialization, VM-ready/TTY bootstrap acceptance
and explicit diagnostics retain their assigned complete boundaries.

Replace the forty direct ordinary calls in the fixed ledger, plus the
specified transitive root-plan binding path. Admit scalar/header and
active geometry before access, then only the current transaction,
requested record/frame and genuinely consumed search prefixes.

No whole boot digest, inactive executable/request tail, whole frame
census or unrelated mapping-table audit is an ordinary precondition.
Do not hide one in a helper named selected.
Malformed consumed metadata still fails before access or publication;
unvisited corruption remains complete-diagnostic scope.

### Preserve real cross-owner authority

Keep exact VM/PM endpoint generations, process/thread/root/profile
relations, operation bits, load/reservation/mapping/exit tokens, phases
and owner kinds. Numeric root agreement alone is insufficient.

A narrow read-only sealed-service selector may reuse BOOT02's checked
relation at the bootstrap owner. It accepts no controller authority,
adds no state and does not change any RUNNING control API.

The PM owner provides VM-command admission for its existing selected
binding/reservation/prepared/activation/exit relations, without whole
object/VM audits or the all-frame owner sweep. Existing full PM APIs and
ordinary PM callers retain their checks. This is a consumed dependency,
not the PM01/PM03 policy correction.

The memory owner retains its complete PTE/ownership walks and collision
searches for VM mutation plans. A narrowly named VM-plan entry reuses
those walks with selected committed exception descriptions after actual
VM-operation authority is established. Complete diagnostic root APIs
retain their full getters. Descriptive MEM04 inspectors never authorize
a command or a mutation.

### Prepare before every effect

Keep decode, caller, phase, fixed-object access, token and resource order.
The service adapter must validate consumed state before its first grant/
kernel backend effect, not only in a local transition called afterward.
Keep exact reply/output preservation and kernel/local mirroring.

Every selected frame/entry/count needed by abort or exit is preflighted
before the first store. Retain the protected scratch, freeze, activation,
abort and release commit sequences and their existing fences.

Do not reenter a complete command gate during a valid intermediate commit:
freeze installs the child root before publishing kernel VM FROZEN state.
Its existing post-install memory certificate remains a memory-owned
postcondition, not a second PM/VM authorization attempt.

### Keep the next corrections separate

Retain actual lowest-frame construction, physical-membership/collision
searches, token/exit lookups, complete root walks, selected-record local
shape checks and active transaction ELF/stack-plan checks.
They must have bounded consumed metadata before traversal.
Disclose their work rather than calling every command constant.

VM02 still owns root/frame/record indexing and uniqueness algorithms;
VM03 owns one retained load plan and repeated per-page plan work;
VM04 owns allocator policy; VM05 owns movement primitives; VM06 owns
snapshot lifetime. No future finding is closed here.

### Evidence and storage

Require actual unchanged-production native and same-guest dynamic VM Red,
then independent semantic/work Green, including before-backend rejection,
selected-versus-diagnostic corruption, bounds, output canaries, rollback
and complete local/kernel lifecycle agreement.

Reuse the existing complete VM fixtures and their valid managed-count
matrix. Keep every current model, scenario and complete diagnostic test.
No authority-success stub or candidate-derived expected result is admitted.
Use one existing collector per participating image and the existing
ELF-spawn guest/owned readback path, not another guest or transport framework.

Add no production BSS, cache, index, state-layout change or capacity-sized
temporary. Any new borrowed selection view is at most 64 bytes.
Keep existing preparation temporary lifetimes and all image/stack/deadline
budgets, including VM's 160 resident pages and 4-KiB user stack.
All 44 static configurations, 113 ELFs and ten observed stack scopes remain;
B13 is blocked. Current qualification upkeep belongs here under ADR-0123.

## Supersession boundary

ADR-0077's complete validator implementation, canonical digest, active
bitmap chunking, inactive tails and logarithmic zero-run proof remain.
ADR-0078's valid minimum-six topology and boundary/corruption matrix remain.
Only their ordinary invocation/corruption-precedence requirements change.

ADR-0087's preparation-helper lifetime and stack constraint remain;
its exact initial/final complete-call requirement becomes the selected
preflight and preserved preparation publication described here.
ADR-0050/0055/0057 retain ABI, phases, identity, commit and failure contracts
except the explicitly scoped unconsumed-corruption precedence.
The dependent memory/PM changes do not authorize their later algorithms.

## Phase and non-goals

Independent design review is clean and this decision is Accepted.
Implementation remains pending the separate design merge. No code, tests,
models, harnesses, build scaffolding or reusable executable prototypes are
authored in this phase. Design alone does not close B07-F001.

No new protocol, public syscall, role, recovery, concurrency, validation
cache, transaction framework, generalized partial-validation flag,
allocator/index/copy redesign, larger budget, hosted-CI acceptance or
node-Q/POSIX completion.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
ten-document proposal, relevant Accepted authority, fixed local MINIX/current
source and exact four-command author evidence at Proposed staged tree
`55a880bad17608bb880533ccb139e32d646cf7f2`.
It reported no substantive design issue remaining, with High confidence.
Targeted reproduction: None.

Acceptance records that design result, not an implemented validation change
or B07-F001 closure. The separate design merge still precedes implementation.

## References

- [Selected VM contract](../architecture/selected-vm-validation.md)
- [Fixed MINIX/current study](../research/minix-selected-vm-validation.md)
- [B07 audit](../research/minix-complete-baseline-b07-vm.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)

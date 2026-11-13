# ADR-0133: Selected Bootstrap Command Validation

- Status: Accepted
- Date: 2026-11-13
- Refines: ADR-0043, ADR-0044, ADR-0045, ADR-0047, ADR-0050, ADR-0060,
  ADR-0080, ADR-0083, ADR-0084, ADR-0107, ADR-0119, ADR-0121, ADR-0123,
  ADR-0124, ADR-0128 and ADR-0132
- Supersedes after Acceptance and separate design merge: complete-audit
  preconditions and unconsumed-corruption precedence at B06's selected
  command boundaries, repeated complete post-command checks, and only the
  bootstrap role/UART source-consumer qualifications specified below

## Context

BOOT01 #266 is independently reviewed and merged. BOOT02 owns
B06-F002/F003 and has its six registered predecessors. It must not be
combined with BOOT03 or later VM/PM/TTY command corrections.

The [fixed local study](../research/minix-selected-bootstrap-command-validation.md)
traces MINIX's selected service and privilege admission. The current
bootstrap still runs a complete runtime audit before every command,
complete control checks inside ordinary transitions, and another complete
runtime audit after success. Console/role helpers additionally reach
complete ownership, application-mapping and root validation.

Those checks also establish real safety conditions. Deleting them without
selected replacements would expose unchecked manifest indices, stale
bindings, missing role authority or a late failure after publication.

## Decision

After independent review, Acceptance and separate merge, implement
[the selected command contract](../architecture/selected-bootstrap-command-validation.md).
This phase contains no executable artifacts.

### Admit the consumed relation

Keep BOOT01's separate immutable indices and bounded after-plans.
Use a private automatic selected service view, at most 64 bytes, for
the binding/manifest/runtime/live process/thread/endpoint relation.
Check scalar header/count/order readiness before access and every selected
generation, owner, root, profile and lifecycle field before publication.

The exact captured controller remains the authority. A view is borrowed
within one unchanged protected operation, not a persistent cache, valid
bit, new registry, lease or permission to skip another owner's contract.
Unindexed or unrelated corruption remains explicit complete-diagnostic
scope; consumed malformed state never becomes absence or success.

### Keep one preparation and the existing commit order

Replace only the canonical nine B06 caller rows / ten complete control
or runtime calls. Keep complete APIs and their ten other caller rows.

Release prepares its selected runtime, endpoint and scheduler work before
publication. Expose the scheduler's existing non-failing admission stores
so this caller does not reenter the checked admission wrapper after
endpoint publication. Other checked scheduler callers retain their API.

Ready keeps the reversible guard and exact expected-caller reply.
Bootstrap/TTY state changes only after reply success; failed reply keeps
the original rollback and captured-context behavior.
Complete keeps its all-ready and revocation barriers, source-only hold/
removal order and one PM notification.
Console begin retains its original quiesce/deadline/ownership/notification
sequence. FAIL and deadline handling retain their fatal policy and records.

No plan crosses a user return, context switch, SIE enable or unrelated
mapping/owner mutation. Fixed local guard checks are not replaced by a
generic transaction framework merely to eliminate every repeated load.

### Replace the hidden role/root audits at their actual owners

The bootstrap-only VM role-ready path uses the existing scalar identity
predicate, committed handoff/ownership phase and exact live binding rather
than complete application-mapping and owner-table diagnosis.

The TTY owner provides one selected bootstrap binding validator, retaining
the authoritative owner pointer, complete live tuple, phase/route and
retained-deadline relation. The complete TTY API still owns its complete
endpoint/object audit. Other VM/TTY commands and IRQ paths remain unchanged.

A private B06-only UART point query uses MEM04's checked three-level path
for canonical absent or exact mapped state. It preserves physical/owner/
permission and busy/phase rules, but allocates nothing, writes no PTE or
owner state and returns no mutation lease. Actual VM mapping mutation
and the complete APIs used by other consumers remain separate.

MEM04/GRANT01's descriptive exception inspector still cannot authorize a
command. Selected bootstrap role authority is established independently.
No primitive magic, state validator or mapping algorithm is cloned.

### Preserve real diagnostics and necessary scans

Cold preparation/publication and explicit diagnostics retain complete
validation. Later VM/PM/TTY owners keep their current boundaries.
No whole audit may be hidden in a newly named selected helper.

Retain endpoint publication's owned-thread/inhibition work, actual
scheduler/IPC prefixes, and completion's service/PM-role, staged-thread,
grant and endpoint-reference barriers. Their work remains measured and
disclosed; this decision does not claim that every command is constant.
No GRANT02 index dependency or speculative new owner index is introduced.

### Evidence and storage

Preserve current behavior except the explicitly bounded unconsumed-
corruption precedence change. Keep the original corrupt inputs and full
diagnostic rejection while adding observable selected-success cases.

Actual portable and same-guest captured-command Red must expose the old
audit calls. Green requires zero complete audits at the replaced
boundaries, independent consumed-work bounds, exact failure/rollback and
ordinary/observed agreement. No authority-success stub or candidate-derived
expected result is admitted.

Keep the 43 BOOT01 cases, original 4,096-step model and every current
native/target/integration/image/stack gate. Current source/consumer and
observation upkeep belongs here under ADR-0123. One sufficient author
plan and fresh independent review precede the first permanent commit.

Persistent runtime/control/context sizes remain 448/2,416/3,816 bytes;
selected/ready/complete plans remain 64/136/200 bytes. New views are
bounded automatic storage only. No stack, image or deadline budget grows.
B13 and complete POSIX admission remain later work.

## Phase and non-goals

The independent design review is clean and this decision is Accepted.
`boot02-implementation` depends on its separate design merge and remains
pending. Design adds no code, tests,
models, harnesses, build scaffolding or reusable executable prototype.

No boot policy, service, wire format, privilege, recovery, concurrency,
cache, ready bitmap, global registry, general verifier or transaction
framework; no BOOT03 copy/fill migration; no VM/PM/TTY ordinary-command
redesign, hosted-CI acceptance or node-Q/POSIX completion.

## Alternatives

- Delete complete calls without selected replacements: unsafe consumed
  identity/index/role/PTE assumptions and late publication failures.
- Keep global audits around selected helpers: preserves both findings.
- Cache success across commands: adds invalidation and stale authority.
- Treat the device snapshot as command permission: exceeds its accepted
  descriptive/exclusion-only role.
- Rewrite all endpoint/scheduler/VM/TTY operations together: combines
  separate owners rather than correcting B06's actual command boundary.
- Remove all seal scans to claim constant work: loses required
  authority-revocation evidence.

## Design review disposition

Fresh independent read-only GPT-6.1 Sol review inspected the complete
nine-document proposal, relevant Accepted authority, fixed local
MINIX/current source and exact four-command author evidence.
It reported no substantive design issue remaining, with High confidence.
Targeted reproduction: None.

Acceptance records that design result, not an implemented command change
or B06-F002/F003 closure. The separate design merge still precedes
implementation.

## References

- [Selected command contract](../architecture/selected-bootstrap-command-validation.md)
- [Fixed local study](../research/minix-selected-bootstrap-command-validation.md)
- [Indexed transitions](../architecture/indexed-bootstrap-transitions.md)
- [Correction register](../research/minix-complete-baseline-findings-and-correction-dag.md)

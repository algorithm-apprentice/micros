# Selected-Ownership Native Source Authority Gap

## Status and fixed evidence

This read-only study supports Accepted
[ADR-0106](../adr/0106-granular-selected-ownership-native-source-authority.md)
and the [EV01-C05 contract](../architecture/selected-ownership-native-source-authority.md).
It does not specify MEM04's runtime algorithm or authorize a source waiver.

The inspected source is independently reviewed MEM03 #219:

```text
commit:   da258293f507c745c8e157cd84bd13b2fb4b5e77
tree:     5793c993338c22cf30aa552c9397c8aa234c33cc
facts:    4290
families: 18
```

The checkout was clean. Research used existing readers and fixed sources;
no code, test, model, generator or executable prototype was authored.
The empty MEM04 design branch was removed when this separate evidence
prerequisite became the sole active design.

## Runtime reason for the inspection

MEM04 owns B02-F005/B02-E004 and depends on merged MEM02 and MEM03.
MEM03 has already removed whole-ledger/object validation from the selected
ownership operations. It has not removed user-address-space scratch clearing,
complete private-tree validation or orphan-owner scans from ordinary lookup,
translation and activation.

At #219, `user_address_space_mapping.c` has a fixed three-level leaf walker,
but public lookup and translation first acquire scratch and completely
validate the selected root. The walker relies on that earlier validation
before dereferencing table frames. `user_address_space_activation.c` also
uses the complete preflight, and `scheduler_context.c` explicitly calls the
complete public validator before committing a selected root.

The unchanged fixed MINIX reference is
`4db99f4012570a577414fe2a43697b2f239b699e`, tree
`80db89de58d5ecaf31efe48b4d1d98c7e285e81f`.
Read-only inspection verified the local reference and inspected:

| Fixed MINIX source | Relevant mechanism |
| --- | --- |
| `minix/kernel/arch/i386/memory.c:325-371` | Fixed-level `vm_lookup`, early absent-entry failure, large-page branch and byte offset |
| `minix/kernel/arch/arm/memory.c:302-350` | Section/coarse-table lookup with bounded selected entries |
| `minix/servers/vm/pagetable.c:1358-1422` | Selected process-slot page-directory mapping and address-space binding |
| `minix/servers/vm/region.c:616-645` | Separate AVL region lookup, not a whole-tree check before every hardware translation |
| `minix/kernel/system/do_vmctl.c:17-185` | Exact endpoint selection before architecture delegation |
| `minix/kernel/arch/i386/klib.S:605-626` | Selected root switch, including zero/same-root short circuits |
| `minix/kernel/arch/arm/arch_system.c:196-220` | Selected TTBR binding |
| `minix/kernel/proc.c:299-349` | Scheduler return reaches selected address-space binding |

This is not a claim that all MINIX VM work is constant-time. Region lookup,
range translation and mapping policy have different costs. MEM04 must
separately specify Sv39, generation/owner validation, special mappings,
corrupt-state error order and activation authority under the Accepted
`micros` decisions. MEM05 and VM02 retain their separate enumeration,
release and all-process work.

The current study encountered frozen shared declaration inputs before
completing that runtime design. It therefore stops at the evidence boundary,
rather than choosing an API solely to avoid a guard.

## Actual current admission

`tools/generate_selected_ownership_fixture.py:111-146` reads current bytes
for `Stage.GREEN`, but rejects every changed input outside MEM03's production
set. Its fixed membership comes from #214, not the current filesystem.
Both `include/micros/user_address_space.h` and
`kernel/memory/user_address_space_internal.h` are noncandidate inputs.
MEM02's existing keys do not cover this separate MEM03 rejection.

`tests/host/selected_ownership_source.py:727-769` retains:

- exact historical and noncandidate source bytes;
- three precise current header extensions;
- original definition sets plus seven named helpers;
- original compact signatures and preprocessing ownership;
- raw retained function-body equality;
- exact mutable-function and added-helper call sets; and
- source skeletons after exact function-fragment removal.

The observation and inverse at lines 1182-1197 invoke the same guard.
Legacy native admission invokes it at lines 1471-1482 when using MEM03's
replacement for an earlier clause. The author context validates all twenty
production subjects, whereas the native bundle contains only eleven of them.
These enforcement paths are not additional independently retireable copies
of each underlying clause.

For fixed dependencies, the generator checks all C and header inputs.
Observation/inverse processing also checks fixed C bytes. It does not process
the noncandidate headers; `baseline_source()`'s permitted paths do not admit
those headers either. The declaration must preserve this consumer distinction.

The retained/migrated audit ledger is a different invariant:
`measure_audit_boundaries()` counts complete audits globally, but only
includes migrated metadata/active calls whose triples are in its declared
wanted set. A new metadata call outside that set is not automatically an
audit-ledger violation. It is not part of this missing-identity census.

## Aggregate is not a replacement

The unchanged inventory has eight MEM03 design/oracle facts, including:

```text
tests/host/selected_ownership_source.py:source-projection:MEM03
```

Its 38 payloads include shared `c_source_records.py`, `work_count_evidence.py`,
original design documents and unrelated production/adapters. It has no
per-function, definition, skeleton, header or native-input clauses, and
does not bind the two noncandidate user-address-space headers.

EV01's unchanged closure builder adds the fact owner and every payload to
the candidate closure. Retiring this aggregate would pull shared immutable
inspectors into that closure, conflate literal declaration transport with
current executable assertions and still not identify the header rejections.

Payload intersection alone is not proof of a byte-freeze assertion.
Other facts mentioning a header, such as a stack authority anchor, must be
interpreted according to their real comparison. No unrelated identity is
declared retired by this analysis.

No runtime Red or transition attempt is claimed. Static source and inventory
evidence establish the missing authority. Executable missing-identity Red
belongs to implementation after separate design merge.

## Closed census

The existing fixed-source/function readers produced this census. Paths below
are relative to `kernel/`; policy is #219 and original source is #214.

| C owner | Original functions | Retained raw bodies | Mutable call sets | Added helpers | In native bundle |
| --- | ---: | ---: | ---: | ---: | --- |
| `memory/frame_ownership.c` | 21 | 21 | 0 | 1 | Yes |
| `memory/frame_ownership_validation.c` | 12 | 7 | 5 | 4 | Yes |
| `memory/frame_ownership_bootstrap.c` | 7 | 3 | 4 | 0 | Yes |
| `memory/frame_ownership_handoff.c` | 4 | 1 | 3 | 0 | Yes |
| `memory/frame_ownership_vm_claim.c` | 2 | 1 | 1 | 0 | Yes |
| `memory/frame_ownership_vm_release.c` | 4 | 3 | 1 | 0 | Yes |
| `memory/bootstrap_memory.c` | 34 | 32 | 2 | 2 | Yes |
| `memory/user_address_space.c` | 21 | 20 | 1 | 0 | No |
| `memory/user_address_space_validation.c` | 29 | 28 | 1 | 0 | No |
| `memory/user_address_space_handoff.c` | 13 | 11 | 2 | 0 | No |
| `memory/user_address_space_vm_scratch.c` | 19 | 18 | 1 | 0 | No |
| `memory/user_address_space_vm_freeze.c` | 23 | 22 | 1 | 0 | No |
| `boot/bootstrap_runtime_validation.c` | 2 | 1 | 1 | 0 | No |
| `boot/bootstrap_syscall_release.c` | 12 | 11 | 1 | 0 | No |
| `vm/vm_bootstrap.c` | 17 | 15 | 2 | 0 | No |
| `vm/vm_handoff_runtime.c` | 10 | 9 | 1 | 0 | No |
| `pm/pm_control_runtime_core.c` | 14 | 13 | 1 | 0 | Yes |
| Total | 244 | 216 | 28 | 7 | Eight owners |

The 251 current function clauses, seventeen definition sets, seventeen
skeletons and three production-header clauses give 288 bindings.

The fixed native bundle has 207 members: 51 C sources and 156 headers.
Eleven are production subjects above: eight C files and three headers.
The other **196** fixed-input identities are 43 C files and 153 headers.
Thus the complete addition is **484** records.

## Correction boundary, complexity and provenance

The Accepted contract requires a separate literal declaration and narrow
inventory materialization, preserving all 4,290 prior complete records,
eighteen fingerprints, five claims and seventy retired keys. Every source
guard, native generator, observation, original expectation and production
file remains unchanged.

Later runtime work must explicitly supersede exact clauses and establish
independent actual-source replacement evidence. This prerequisite itself
does not make a changed MEM04 source admissible.

This is a compatible host-evidence extension under ADR-0082, not a new
MINIX runtime algorithm. No MINIX source is copied. For `b = 484` bindings
and literal length `s`, parsing/record construction costs `O(s + b)` plus
existing deterministic sorting/fingerprinting, with `O(s + b)` auxiliary
storage and ordinary-file checks over the bounded declared path set.
There is no kernel BSS, target stack, image, syscall or hot-path change.

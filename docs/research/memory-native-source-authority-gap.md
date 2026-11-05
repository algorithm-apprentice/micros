# Memory-Native Source Authority Gap

## Status and fixed evidence

This is the read-only source study for the Accepted EV01-C04 prerequisite.
It does not specify or implement MEM03's ownership algorithm.

The inspected `micros` source is the separately reviewed MEM02 implementation
#212:

```text
commit: bf4eef8f0c59a9d06ae3a20d4a52c5f07943c2ee
tree:   910e04a2129a692ec74ede4c34ff19c8f18e2397
facts:  3954
families: 16
```

The worktree was clean when this inspection began. No runtime, test,
generator, model or executable prototype was changed during the study.
The empty MEM03 design branch was removed when this separate prerequisite
became the sole active design.

MEM03 owns B02-F002: complete fixed-capacity ownership validation on ordinary
valid paths. Its actual runtime prerequisite, MEM01 #210, is merged.
MEM02 #212 is the source of the additional evidence constraint described
here, not a newly invented runtime edge from MEM02 to MEM03.

## The runtime reason to inspect these guards

The fixed MINIX reference remains
`4db99f4012570a577414fe2a43697b2f239b699e`. Read-only inspection verified
that local revision and found no changes in the inspected VM sources.

Relevant source facts are:

| Fixed MINIX source | Operation | Evidence |
| --- | --- | --- |
| `minix/servers/vm/region.c` | `physblock_get`, `physblock_set` | Selected page-offset indexing and selected process totals |
| `minix/servers/vm/pb.c` | `pb_link`, `pb_unreferenced` | Selected physical-block links/refcounts; unlink may traverse that block's reference list |
| `minix/servers/vm/utility.c` | `vm_isokendpt` | Selected slot, endpoint generation and live-state checks |
| `minix/servers/vm/vm.h` | `SANITYCHECKS` | The fixed configuration sets it to zero |
| `minix/servers/vm/sanitycheck.h` | `SANITYCHECK` | Whole-state diagnostic machinery is separate; its enabled macro also contains an explicit false condition |
| `minix/servers/vm/main.c` | VM dispatch | Exact caller validation and authorized operation dispatch |

This is not a claim that every MINIX memory operation is constant-time.
Selected region metadata uses direct indexing, unlinking a shared physical
block can cost its reference-list length, and allocation and region search
retain their own costs. MINIX has no equivalent dense generation-bound
kernel ownership ledger.

At #212, `micros_frame_ownership_vm_transition_preflight()` still calls the
complete ownership validator. Ordinary user-root validation reaches
`micros_frame_ownership_runtime_validate()`, which also validates the complete
object/scheduler registry and scans process-bound owners. The complete
validator deliberately retains its active/inactive frame, range, bitmap,
count and cursor checks.

Those are the already accepted B02-F002/B02-E002 subjects. Correcting their
ordinary invocation requires changing real ownership sources, not merely
renaming a test or removing an assertion. The exact selected-operation,
handoff and diagnostic contract remains MEM03 design work after this
prerequisite.

## Two existing current-source admission mechanisms

### MEM01 retained-source clauses

`tests/host/bootstrap_allocation_source.py:validate_retained_source` accepts
the exact historical source for `Stage.RED`. Its current stage separately
checks:

- the single cursor insertion in the allocator header;
- the complete definition set in each of five C owners;
- original signatures and preprocessing ownership;
- unchanged bodies outside the original MEM01 mutation set;
- closed call sets for the original mutable functions;
- exact signatures, preprocessing ownership and call sets for four added
  helpers;
- the exact selected-frame test guard; and
- the remaining include/type/macro/storage skeleton.

These are real executable rejection conditions. For example, changing
`micros_frame_ownership_vm_transition_preflight()` from complete validation
to a selected preflight changes a retained original body. Adding a necessary
ownership helper also changes an exact definition set.

The current inventory has one aggregate
`tests/host/bootstrap_allocation_source.py:source-projection:MEM01` record
for the literal projection declaration. It has no separate identity for
either of those current-source clauses.

### MEM02 fixed native dependencies

`tools/generate_kernel_mapping_fixture.py:source_bundle` obtains its closed
source/header membership from fixed MEM01 #210. In its current stage it
reads the actual checkout, but rejects changed bytes in every member except
the two kernel-map C owners and their private header.

The fixed membership contains 186 files: 30 C sources and 156 headers.
Exactly 183 are subject to this noncandidate byte comparison. The set
includes the real frame-ownership implementation, its public/private
headers, bootstrap runtime wrappers, object/scheduler dependencies and the
headers copied into the generated native bundle.

`tests/host/kernel_mapping_source.py:validate_retained_source` independently
retains fixed dependency bytes when observing a noncandidate source. For the
three admitted mapping subjects it checks the exact private-header
extension, definition sets, preprocessing/signature/linkage rules, retained
bodies, call sets, one include normalization and source skeletons.

The inventory has one aggregate
`tests/host/kernel_mapping_source.py:source-projection:MEM02` declaration
record, not an individual current fixed-input or function-clause identity.
Changing only a later ownership implementation would therefore still be
rejected before the existing mapping work/semantic corpus executes.

## Actual aggregate closures

The unchanged inventory was queried through `current_inventory()`. Both
aggregate records have 17 payload paths.

| Aggregate | Evidence value | Representative collateral paths |
| --- | --- | --- |
| MEM01 source projection | `fe2acbe83e9e00f51a9b4283b8a81989176e42bad3697979383cb638c5bb9acc` | `tests/host/c_source_records.py`, `tests/host/work_count_evidence.py`, original ADR/design/research, unrelated allocator subjects |
| MEM02 source projection | `ace3948855b8ff63a561ec51e69e3127310919030acf706e44536d50c28d1a7a` | The same shared inspectors, `tools/stack_bound_assembly.py`, MMU assembly/fixture, original ADR/design/research, all three mapping subjects |

`evidence_authority_validation.py:_expected_closures` adds the fact owner
and every affected payload to the candidate closure.
`_validate_environment` rejects any intersection with independently declared
command/oracle inputs.

Consequently, using an entire aggregate as a substitute for one missing
ownership-source clause is not a narrow transition. It puts shared source
and observation inspectors into the candidate closure, and would also
conflate historical declaration identity with individual current assertions.
The correct response is not to change those inspectors' classification,
rename input labels, rebaseline a digest or remove overlap rejection.

No runtime Red, source edit or transition attempt is claimed by this
read-only analysis. The current generator condition and closure construction
establish the prerequisite statically. Implementation must add an executable
regression for the missing asserted identities after design merge.

## Closed assertion census

The census used the existing fixed-source readers and C function-record
inspector, not edited source. MEM01's runtime reference is
`bcace37522e5beb101ef5278889a2f87742f5721`; MEM02's is
`3f7e33026f92c8c1765ce00c2b03b5c2d6855197`.

### MEM01

| Owner | Original definitions | Retained bodies | Mutable call sets | Added helpers |
| --- | ---: | ---: | ---: | ---: |
| `frame_allocator.c` | 11 | 8 | 3 | 0 |
| `bootstrap_memory.c` | 31 | 30 | 1 | 3 |
| `frame_ownership.c` | 21 | 18 | 3 | 0 |
| `frame_ownership_bootstrap.c` | 7 | 5 | 2 | 0 |
| `frame_ownership_validation.c` | 11 | 10 | 1 | 1 |
| Total | 81 | 71 | 10 | 4 |

Each current function has one clause combining its actually enforced
signature, preprocessing, body/call-set and helper rules. The 85 function
clauses, five definition sets, five skeletons, one header rule and one
selected-frame guard give **97** bindings.

### MEM02

| Owner | Current definitions | Retained bodies | Call sets |
| --- | ---: | ---: | ---: |
| `address_space.c` | 15 | 6 | 9 |
| `address_space_validation.c` | 27 | 5 | 22 |
| Total | 42 | 11 | 31 |

The 183 fixed native inputs, 42 function clauses, two definition sets, two
skeletons, one private-header rule and one include normalization give
**231** bindings.

The complete addition is therefore **328** existing current-source
assertions. It is not a count of new tests, runtime operations or independent
performance oracles.

## Correction boundary

The Accepted [exact contract](../architecture/memory-native-source-authority.md)
adds a closed, literal-only declaration and inventory materialization for
those 328 assertions. It preserves all 3,954 predecessor complete records,
all sixteen predecessor fingerprints, the original source declarations,
every current admission failure, and all prior committed claims.

The two original aggregate records continue to bind their exact historical
literal declarations. The new records identify their distinct executable
current-source clauses. A later correction must supersede the relevant
current contract explicitly and retire only those new keys; it cannot use
this prerequisite as authorization.

The repair does not make MEM03 source admissible now. Its implementation
does not change either native generator, either source guard, a primitive
observation, a native expectation or any runtime source.

## Complexity and provenance

This is a compatible host-evidence extension under ADR-0082, not a new
MINIX runtime algorithm. No MINIX source is copied.

For `b = 328` declared bindings and total literal input length `s`,
materialization costs `O(s + b)` parsing and record construction plus the
existing deterministic sort/fingerprint work, and `O(s + b)` auxiliary
storage. Ordinary-file validation remains proportional to the bounded
declared path set. There is no kernel BSS, target stack, syscall, executable
image, runtime hot-path or collector-ABI change.

Independent declaration generation during implementation must use the fixed
#212 guard policy and the two fixed runtime references above, before editing
the inventory or its tests. The candidate extractor's output is not an
expected-value source.

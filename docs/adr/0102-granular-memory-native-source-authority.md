# ADR-0102: Granular Memory-Native Source Authority

- Status: Accepted
- Date: 2026-11-05
- Refines: ADR-0033, ADR-0034, ADR-0080, ADR-0082, ADR-0100 and ADR-0101
- Preserves: every existing runtime contract, source assertion, historical
  declaration/value, complete fact, family fingerprint and committed claim

## Context

MEM02 implementation #212 is independently reviewed and merged as
`bf4eef8f0c59a9d06ae3a20d4a52c5f07943c2ee`, tree
`910e04a2129a692ec74ede4c34ff19c8f18e2397`.
The next dependency-ready MEM03 design must separate selected ownership
invariants from complete diagnostic validation.

Source inspection found a distinct evidence prerequisite before any MEM03
implementation. MEM01's current native admission freezes retained function
bodies, signatures, call sets, definition sets and source skeletons.
MEM02's current native generator additionally freezes 183 complete dependency
source/header files against #210. Necessary later ownership changes fail
those checks before the existing work/semantic corpus executes.

The current inventory has aggregate literal source-projection records, but
not separate keys for those actual current-source assertions. Each aggregate
has seventeen payloads, including shared immutable inspectors. Retiring an
aggregate to bypass one clause is neither granular nor closure-disjoint.

The [fixed-source study](../research/memory-native-source-authority-gap.md)
records the guards, exact census and unchanged closure rejection. MEM03
design is blocked; its implementation has not started.

## Decision

After separate review, Acceptance and merge, implement the
[granular source-authority contract](../architecture/memory-native-source-authority.md)
as the sole EV01-C04 prerequisite.

### Materialize existing assertions, not a new runtime policy

Add one closed, literal-only declaration owner and the
`memory-native-admission` family. Its 328 `TRANSPORT_IDENTITY` records
identify:

- 97 MEM01 current function/definition/skeleton/header/test-guard clauses;
- 183 MEM02 fixed native dependency identities; and
- 48 MEM02 mapping function/definition/skeleton/header/include clauses.

Each function record combines only the signature, preprocessing,
body/call-set and linkage conditions that the existing guard actually
enforces. Do not turn unconstrained fields into new assertions.

Each fixed-input identity has both existing consumers where applicable.
It cannot be retired at the generator while remaining silently enforced by
the observation path, or vice versa.

The literal declaration is interpreted with existing checked readers, never
executed by the inventory resolver. Its values derive independently from
fixed #212 policy and the fixed MEM01/MEM02 runtime references before any
extractor or test edit.

### Keep narrow source ownership

A binding names one actual source/header subject, its exact assertion owner,
and the declaration owner. MEM02 fixed inputs additionally name the
generator. Shared C inspectors, operation-count policy, stack tooling and
build files do not become candidate payloads merely because they interpret
the assertion.

Do not change EV01 closure construction or its overlap rejection. A real
candidate/immutable overlap remains an error.

### Conserve every prior authority

Preserve all 3,954 complete predecessor records and sixteen fingerprints.
The new inventory has 4,282 facts in seventeen families, with one
independently declared new fingerprint.

The original aggregate source-projection records still bind their historical
literal declarations. The new records bind individual current executable
clauses; they do not replace, delete or rebaseline those declarations.

Keep all four committed claims and thirty prior retired identities usable
without author journals. Historical comparisons retain their original
expectations. Only assertions explicitly about current catalog totals advance.

### Do not admit a changed algorithm here

This implementation does not modify either native source guard or generator,
their primitives or fixtures, or production source. Every current negative
control must still fail.

A later runtime correction must declare the exact new keys it supersedes,
preserve unrelated clauses, independently bind actual current helper and
observation closures, and complete ADR-0082's real Red/Green/recertification
sequence. Projection may not substitute historical code for a changed
current algorithm.

MEM03 remains responsible for selected-record semantics, mutation plans,
complete diagnostics, generation authority, failure atomicity and ordinary
validation cadence. No part of that runtime design is accepted here.

## Acceptance

Require a native Red for the actual missing asserted identities, not a
missing scaffold. Green proves complete independent 328-record equality,
exact predecessor conservation, narrow closures, strict schema/path/identity
negatives and unchanged existing guard behavior.

Then require all fifty current fresh author commands, complete existing
native/target/stack acceptance, ordinary journal-free prior authority and
independent implementation review before the first permanent commit.
Publish only the identical reviewed/committed/remote/preview/squash tree and
clean exact task refs.

## Phase barrier and non-goals

This proposal contains documents only. No declaration module, executable
test, extractor, fixture, model, generator, build scaffold or prototype is
authored before separate design review, Acceptance and merge.

No runtime source or algorithm, current admission relaxation, identity
retirement, new author journal, collector ABI, shared parser, transition
state machine, build recipe, target workflow, capacity, timeout, stack/image
budget, hosted-CI acceptance or node-Q implementation is in scope.

## Design review disposition

Independent read-only review inspected the complete ten-document proposal,
canonical context, actual current-source guards, the closed census, metadata
policy and exact author evidence at staged tree
`4cd435adcc67e0a5d26cbb8ab475f7a0366a777a`.
All four documentation-phase commands were FRESH/exit0; implementation
native/build/QEMU/stack checks were correctly N/A.

The reviewer reported: "No substantive design issue remains." The 328
bindings, preservation requirements, unchanged overlap rejection and MEM03
stop boundary were consistent. No targeted reproduction or gate rerun was
needed. Acceptance records this design result, not an implemented binding
family or permission to change memory runtime code. Implementation remains
pending the separate design merge.

## Alternatives

- Retire an entire source-projection aggregate: loses granularity and puts
  shared immutable policy in the candidate closure.
- Recompute old hashes: lets the candidate replace independent authority.
- Ignore all noncandidate native changes: weakens actual-source and helper
  closure before a replacement oracle exists.
- Run old ownership code inside the current mapping fixture: no longer
  measures the actual current dependencies.
- Add only a filename-level waiver: cannot distinguish retained functions,
  call sets, signatures, definitions and storage.
- Build a general future-correction registry: unnecessary; this repair has
  one closed two-fixture census and uses existing EV01 machinery.
- Fold the evidence gap silently into MEM03: obscures which independent
  authority was missing before the runtime correction.

## References

- [Fixed-source gap and census](../research/memory-native-source-authority-gap.md)
- [Exact declaration and acceptance contract](../architecture/memory-native-source-authority.md)
- [Evidence authority transition](0082-evidence-authority-transition.md)
- [Bootstrap allocation decision](0100-minix-bootstrap-allocation.md)
- [Kernel mapping decision](0101-minix-kernel-mapping-topology.md)
- [Correction DAG](../research/minix-complete-baseline-findings-and-correction-dag.md)

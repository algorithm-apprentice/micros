# ADR-0080: MINIX Structural and Algorithmic Baseline

- Status: Accepted
- Date: 2026-10-18
- Supersedes: ADR-0025
- Refines: ADR-0033, ADR-0034, and ADR-0079

## Context

ADR-0025 made the fixed MINIX revision a mandatory behavioral reference
before subsystem implementation. That corrected major architectural mistakes,
including the original scheduler design, but its definition of baseline did
not require an explicit comparison of logical data structures, algorithms, or
asymptotic complexity.

Behavior alone is not enough to establish a useful first implementation. Two
implementations may produce the same small acceptance transcript while one
uses a constant-time index and the other scans a table, or while one sorts in
expected `O(n log n)` work and the other uses an unconditional `O(n^2)`
algorithm. Compile-time capacity bounds do not make that difference
irrelevant: the capacity remains the logical input size for algorithm review,
and repeated work can dominate a service even at modest bounds.

The pre-merge node-Q implementation exposed this gap:

- MINIX `ls` passes an `FTSENT` pointer array to the NetBSD
  Bentley-McIlroy `qsort` implementation, while the candidate used insertion
  sort and a separate linear duplicate search;
- MINIX `ps` sorts its contiguous process snapshot with `qsort`, while the
  candidate PM snapshot used selection sort; and
- the candidate `ps` validated every parent with a prefix scan, adding another
  quadratic path; and
- the candidate `echo` accumulated the complete logical output instead of
  retaining the reference's bounded streaming-buffer shape.

Those differences passed behavioral tests. They are nevertheless design
defects under the required baseline-first strategy.

## Decision

### Fixed reference

The initial reference remains MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

MINIX and its imported NetBSD code remain evidence, not source donors. Every
`micros` implementation is written independently in project-owned C17 and
narrowly scoped RISC-V assembly.

### Baseline dimensions

Before a dependency-ready subsystem is implemented, its research ledger must
trace all of these dimensions:

1. externally observable behavior and failure precedence;
2. authority, ownership, and mechanism/policy boundaries;
3. logical data structures, including tables, queues, lists, trees, hashes,
   indexes, links, keys, and authoritative lookup paths;
4. algorithms and the order of their important mutations;
5. time complexity over the logical input or capacity, including average,
   worst-case, and amortized bounds where they differ;
6. auxiliary-space and stack complexity; and
7. the hot paths on which the operation occurs.

The ledger identifies the exact MINIX files and functions that establish each
fact. It then identifies the corresponding `micros` owner and gives every
difference one classification.

### Independent structural parity

Structural parity means reproducing the logical organization and algorithmic
mechanism needed to obtain the same properties. It does not mean copying:

- source text, comments, tests, identifiers, or control-flow spelling;
- byte-for-byte C structure layout where no shared ABI requires it;
- architecture-specific x86 or ARM assembly;
- pointer authority across an IPC boundary; or
- known memory-safety, failure-atomicity, validation, or portability defects.

A bounded array may replace a dynamically allocated array when the Accepted
MVP capacity requires it. A slot/generation handle may replace a raw pointer
when isolation requires it. Such adaptations must retain the reference
indexing, ordering, queueing, and algorithmic properties unless the ledger
proves why the target cannot do so.

Using a simpler implementation, a small current capacity, or an easier test
fixture is not sufficient justification for changing the algorithm or making
its asymptotic bound worse.

### Algorithmic parity and later optimization

The first integrated implementation uses the MINIX algorithm family and
complexity envelope. A different algorithm that is intended to be faster,
more general, or more scalable is an optimization, even when it appears
obviously better. It follows only after baseline integration and requires:

1. a measured limitation or an explicit product requirement;
2. an ADR when authority, data structure, ABI, lifecycle, algorithm family, or
   observable behavior changes;
3. preserved baseline conformance tests; and
4. new evidence for the claimed improvement.

An implementation with a worse asymptotic bound is not a baseline and is not
an optimization.

### Safety and validation extensions

Generation checks, failure-atomic plans, complete state validation, zero-tail
checks, and deterministic diagnostics remain valid compatible extensions.
Their cost is part of the operation that executes them. A parity ledger must
therefore state their additional work and storage.

A compatible extension may add constant work or preserve the reference
asymptotic bound. If it makes a valid-path operation asymptotically worse, it
requires a separate Accepted justification with a measured bound; otherwise
it is a divergence requiring correction. A fixed compile-time capacity does
not waive this rule.

### Difference classifications

Every row uses exactly one classification:

1. **Baseline parity**: behavior, authority, logical structure, algorithm
   family, and complexity envelope are reproduced.
2. **Required adaptation**: an Accepted platform or isolation constraint
   requires a different representation or mechanism while retaining behavior
   and no-worse asymptotic complexity.
3. **Compatible extension**: a safety or development extension preserves the
   baseline and does not make the valid path asymptotically worse.
4. **Staged substitution**: the development DAG temporarily substitutes an
   owner or mechanism; the replacement point and interim complexity are
   explicit.
5. **Divergence requiring correction**: any unrequired behavioral,
   structural, algorithmic, ownership, or complexity difference.

The ledger must not combine classifications or use "bounded" as a substitute
for analyzing the logical input size.

### Required evidence

Portable data structures and algorithms start with deterministic native
evidence. Depending on the mechanism, this includes:

- exact topology, membership, and index invariants;
- operation counters for comparisons, probes, traversed links, or moved
  records;
- boundary and adversarial input orders;
- stack and auxiliary-storage bounds; and
- model comparison against a small independent reference.

Wall-clock thresholds are not correctness evidence. Target benchmarks are
added only when hardware behavior or a measured optimization requires them.

Review verifies both the semantic tests and the ledger-to-code mapping. A
passing end-to-end scenario cannot waive a structural or complexity
divergence.

### Node-Q correction

ADR-0079 remains authoritative for node-Q behavior, authority, wire formats,
and scope. Before its implementation may resume, the init/shell research and
architecture contracts must additionally require:

- MINIX's single-session policy specialized to one scalar foreground shell
  identity rather than a general session list;
- the one-entry immutable PATH specialization instead of a command hash,
  with its no-search rationale and complexity recorded;
- the reusable block-buffer and complete-write algorithm used by `cat`;
- sequential bounded-buffer output for `echo`, without a complete-output
  aggregate;
- the MINIX/NetBSD three-way quicksort family for `ls` and the PM process
  snapshot, including insertion sorting only for the same small partitions;
- a lightweight order/index vector rather than repeated full-name movement;
- adjacent duplicate-name validation after sorting rather than a separate
  quadratic collection scan; and
- logarithmic parent lookup in the already PID-sorted `ps` snapshot rather
  than a prefix scan.

The complete node-Q valid path, including any PM validation invoked by process
snapshot publication, must have an explicit complexity account. Any inherited
quadratic validation on that path is a blocking finding rather than an
unmentioned safety cost.

### Transition for merged work

Accepted subsystem behavior remains in force; this ADR does not silently
rewrite or remove merged code. Earlier ADR-0025 ledgers are behavioral
evidence, not automatic structural certification.

The complete audit is the documentation-only phase defined by
[the pinned audit contract](../research/minix-complete-baseline-audit.md). It
reviews exact `micros` commit `da0bd95148f2704d5347c2af4c64085b90132746`
and tree `3b90c7716d69efd68930b3f8ab86a32813bdd6c3`. Its closed path and
operation inventories assign every runtime/evidence path and meaningful
operation to one audit unit or an explicit non-MINIX exclusion.

No runtime correction, node-Q implementation, optimization, refactor, or new
feature work starts until all audit units and the consolidated findings
register are accepted. The accepted audit then creates one dependency-ordered
correction DAG. Corrections proceed sequentially through separate
design/implementation pairs, and every affected unit is re-certified after
its correction. Baseline completion requires every blocking finding to be
merged and re-certified; merely opening or reviewing a correction task is not
completion.

## Consequences

- Baseline research becomes deeper, but prevents behaviorally invisible
  performance regressions.
- Review can distinguish independent reimplementation from arbitrary
  reinvention.
- Fixed-capacity MVP structures remain permitted, but their logical
  complexity stays visible.
- Some existing complete-state validators may require later restructuring or
  measured justification.
- Optional algorithm redesign is deferred until a faithful, measurable
  baseline exists.
- The complete audit is read-only and precedes every correction, preventing
  patch-driven discovery.
- Retrospective correction is sequential and does not authorize one broad
  repository rewrite.

## Alternatives considered

### Keep behavioral parity only

Rejected. It permits the same output to hide inferior lookup, queueing, and
sorting mechanisms.

### Copy MINIX source and exact layouts

Rejected. It violates independent implementation and can import
architecture-specific assumptions, raw pointer authority, and known defects.
The required outcome is logical and algorithmic parity, not copied text.

### Treat every fixed-capacity scan as constant time

Rejected. It would make complexity review meaningless and would hide repeated
work as capacities grow or operations become frequent.

### Select theoretically better algorithms immediately

Rejected. The project first reproduces the proven reference mechanism.
Measured optimization follows from that baseline through a separate decision.

## References

- [Superseded behavioral baseline](0025-minix-behavioral-baseline-before-optimization.md)
- [Node-Q MINIX study](../research/minix-init-shell-basic-commands.md)
- [Structural-baseline transition audit](../research/minix-structural-algorithm-transition.md)
- [Complete baseline audit contract](../research/minix-complete-baseline-audit.md)
- [Node-Q architecture](../architecture/init-shell.md)
- [Development dependency DAG](../architecture/development-dag.md)
- [Testing strategy](../testing-strategy.md)
- [AI-native workflow](../development/ai-native-workflow.md)

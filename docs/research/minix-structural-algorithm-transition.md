# MINIX Structural and Algorithmic Baseline Transition

## Purpose

This document records the transition from ADR-0025's behavioral baseline to
the structural and algorithmic baseline defined by
[ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md).

It does not certify every merged subsystem. It defines the audit boundary,
records the first concrete node-Q findings, and prevents later work from
mistaking an existing behavioral ledger for a completed data-structure or
complexity comparison.

## Fixed reference and review variables

The source reference remains MINIX commit:

```text
4db99f4012570a577414fe2a43697b2f239b699e
```

Complexity uses logical variables rather than compile-time constants:

| Variable | Meaning |
| --- | --- |
| `p` | live or inspected process records |
| `d` | non-synthetic directory entries |
| `a` | command arguments |
| `b` | bytes copied, parsed, or emitted |
| `q` | ready, sender, receiver, or other queue members |
| `m` | mappings, frames, descriptors, or other subsystem-owned records |

A capacity of 64 still has complexity in `p` or `d`. The capacity supplies a
hard upper bound; it does not change a table scan into the reference's indexed
operation.

## Required audit record

Each subsystem audit records:

| Field | Required content |
| --- | --- |
| Operation | One externally or internally meaningful transition |
| MINIX source | Exact files and functions at the fixed revision |
| MINIX structure | Authoritative table, queue, list, tree, hash, or index |
| MINIX algorithm | Lookup, mutation, traversal, sorting, or scheduling steps |
| Complexity | Average, worst-case, amortized, stack, and auxiliary storage where relevant |
| `micros` owner | Exact production owner and representation |
| Classification | One ADR-0080 classification |
| Evidence | Deterministic structural, operation-count, model, or target proof |

Unknown complexity or an untraced hot path blocks implementation readiness.

## Node-Q audit

The node-Q behavioral scope remains the one fixed by
[the init/shell study](minix-init-shell-basic-commands.md). The following
structure and algorithm facts are additionally authoritative.

| Operation | MINIX structure and algorithm | Complexity envelope | Required `micros` mapping | Classification |
| --- | --- | --- | --- | --- |
| Single-user shell ownership | `single_user()` retains one child PID and waits until that exact PID exits | Exact single-user child identity is `O(1)` | Init retains one exact shell PID | Baseline parity |
| Multi-session registry | Multi-user init uses a doubly linked `session_t` list plus a PID-keyed DB | Indexed PID lookup with `O(1)` list unlink once found | Node Q has one fixed console and omits getty/login sessions until POSIX expansion | Staged substitution |
| Shell tokenization | The full shell parser builds syntax nodes; simple command text is consumed once before evaluation | Linear in input bytes plus syntax-node storage | The staged simple grammar uses one in-place 256-byte line and an eight-pointer argv vector, with one forward pass | Staged substitution |
| Command lookup | `exec.c` uses 31 chained hash buckets for repeated PATH lookup; slash-bearing names bypass the hash | Expected `O(name + chain)` after caching; PATH search is proportional to components and pathname work | Node Q temporarily fixes `PATH=/bin`, constructs that sole candidate directly, and passes explicit paths unchanged; the hash returns with multi-component/PATH mutation support in POSIX expansion | Staged substitution |
| Foreground child tracking | `jobs.c` uses an array of jobs and per-job process records for the general shell | General lookup may scan jobs/processes | The staged shell permits exactly one foreground child and retains one exact PID scalar | Staged substitution |
| `echo` | Sequential argv traversal through a fixed stdio buffer | `O(a + b)` time and `O(1)` auxiliary buffering | Write each operand, separator, and final newline sequentially through the complete-write client; do not aggregate the complete result | Required adaptation |
| `cat` | One reusable block-sized buffer; each read is followed by a complete partial-write loop | `O(b)` time and `O(block)` auxiliary storage | One reusable 4,096-byte application page and the shared complete-write loop | Required adaptation |
| `ls` entry ownership | FTS produces a linked list of `FTSENT` records and constructs a reusable pointer array for sorting | `O(d)` records and `O(d)` pointer storage | Bounded getdents records populate a fixed name table plus a lightweight order-index vector | Required adaptation |
| `ls` ordering | `fts_sort()` invokes NetBSD's Bentley-McIlroy three-way `qsort`: insertion below seven, median-of-three above seven, pseudomedian-of-nine above 40, four-cursor equal-key partition, recursive left then tail-iterated right | Expected `O(d log d * name_length)`, worst `O(d^2 * name_length)`; expected `O(log d)` and worst `O(d)` call depth | Independently implement the same pivot/partition/order over the order-index vector; use an explicit pending-right range stack for the 2 KiB target stack; detect adjacent duplicates after ordering | Required adaptation |
| Process-list ordering | `ps` retrieves a contiguous `kinfo_proc2` array and calls the same complete `qsort` algorithm | Expected `O(p log p)`, worst `O(p^2)`; expected `O(log p)` and worst `O(p)` call depth | PM collects lightweight snapshot entries and uses the same pivot/partition/order with the bounded explicit range stack | Required adaptation |
| `ps` parent validation | MINIX trusts the kernel/KVM snapshot's parent fields; its display pass is linear after sorting | `O(p log p)` including sort | The untrusted PM grant is a safety extension; validate each non-init parent with binary search in the sorted prefix | Compatible extension |
| PM complete validation on snapshot publication | MINIX PM keeps a slot-indexed `mproc` table; parent handles are direct slots and PID lookup is a single table scan | `O(p)` for one lookup or complete scan | Build PID-ordered and endpoint-ordered slot vectors plus child counts; sort once per vector, use adjacent uniqueness and binary exact-child lookup | Compatible extension |

### Blocking candidate findings

The pre-merge node-Q candidate is not an accepted implementation. Its audit
found:

| Candidate mechanism | Candidate complexity | Required correction |
| --- | --- | --- |
| `echo` complete-output buffer | `O(b)` auxiliary storage | Stream operands and separators with bounded client buffering |
| `ls` duplicate detection during collection | `O(d^2)` byte comparisons | Remove the collection scan; reject adjacent duplicates after sort |
| `ls` insertion sort over every entry | `O(d^2)` | Use the MINIX/NetBSD quicksort family, retaining insertion sort only for its small partitions |
| PM snapshot selection sort | `O(p^2)` | Sort lightweight entries with the same quicksort family |
| `ps` parent prefix scan | `O(p^2)` total | Binary-search each parent in the sorted prefix |
| Complete PM validation on snapshot publication | currently contains nested process-table work | Replace nested identity/wait scans with ordered slot vectors and child counts, yielding `O(p log p)` |

Behavioral output tests did not expose these findings. Deterministic
comparison/probe-count evidence is therefore required in addition to the
existing command and QEMU scenarios.

## Complete-audit transition

Existing studies vary in structural depth. Scheduler and IPC research already
records important queue and state representations, while other studies focus
primarily on ownership and behavior. None is automatically certified under
ADR-0080 without an explicit complexity ledger and code/evidence mapping.

The closed inventory, pinned tree, B01-B14 sequence, required unit outputs,
explicit exclusions, completion conditions, and post-audit correction gate are
defined by
[the complete baseline audit contract](minix-complete-baseline-audit.md).
No implementation begins while that read-only audit is incomplete. A finding
is not resolved by creating a task; it must later be corrected, merged, and
re-certified before baseline completion.

## Evidence rules

- Count comparisons, probes, links, or records; do not use wall-clock timing as
  a deterministic complexity test.
- Test sorted, reverse-sorted, equal-key, boundary, and adversarial inputs
  relevant to the reference algorithm.
- Preserve exact semantic and failure-atomicity tests.
- Treat a better algorithm as later optimization, not as retrospective
  baseline parity.
- Keep test-only instrumentation out of production structures and authority.

## References

- [ADR-0080](../adr/0080-minix-structural-and-algorithmic-baseline.md)
- [Complete baseline audit contract](minix-complete-baseline-audit.md)
- [ADR-0025](../adr/0025-minix-behavioral-baseline-before-optimization.md)
- [Node-Q MINIX study](minix-init-shell-basic-commands.md)
- [Scheduler and context-switch study](minix-scheduler-and-context-switch.md)
- [Endpoint and IPC study](minix-endpoint-and-ipc.md)

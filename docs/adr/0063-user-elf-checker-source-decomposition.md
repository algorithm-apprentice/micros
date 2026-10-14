# ADR-0063: User ELF Checker Source Decomposition

- Status: Proposed
- Date: 2026-10-14
- Refines: ADR-0025, ADR-0031, ADR-0032, ADR-0033, ADR-0034, ADR-0042,
  ADR-0050, and ADR-0051

## Context

The repository source-module audit assigns `tools/check_user_elf.py` and
`tests/host/test_check_user_elf.py` to one separate tooling correction.
At the merged scheduler outcome `dfa9d00`, these sources contain 1,596
physical lines, 44 function definitions, and two functions over 120 lines.
Binary decoding, section closure, service-startup policy, instruction
inspection, command-line handling, and fixture construction have different
responsibilities despite sharing one representation.

The next application-runtime outcome needs a separate application checker
under ADR-0050. That checker must not duplicate the existing ELF decoder and
structural checks merely because their implementation is mixed with the
service policy. Complete this source-only correction before extending that
tooling family. This is a maintenance prerequisite, not a new runtime-DAG
node or a redesign of either ELF contract.

The fixed MINIX trace and classifications are recorded in the
[ELF checker source-organization study](../research/minix-user-elf-checker-source-organization.md).
Accepted ADR-0042 remains authoritative for service images; ADR-0050 remains
authoritative for the later application checker.

## Decision

Apply the exact
[user ELF checker source contract](../architecture/user-elf-checker-source-organization.md).

The implementation exposes six production Python owners:

- the existing checker path remains the supported facade and CLI;
- one format/query leaf owns binary records and checked read-only queries;
- one parser owns complete binary decoding;
- one structural owner checks headers, segments, and sections;
- one instruction leaf owns the existing bounded destination decoder; and
- one service-policy owner checks symbols, startup, instructions, and budgets.

Imports form the acyclic graph in the canonical contract. Private modules
never import the facade. The facade explicitly re-exports every existing
checker-defined name; it does not duplicate types, tables, or functions.
Direct-script and package invocation use the same package-owned definitions.
There is no fallback importer, plugin mechanism, cache, or configurable
policy framework.

The existing test module remains the sole discovery entry for its named
`UserElfCheckerTest`. Private non-discovered fixture and case modules separate
binary construction, parsing, structure, policy, and new CLI contract evidence.
The twelve original case names and discovery identities remain intact through
plain case mixins, not duplicated `TestCase` subclasses or module-name tricks.
The existing fixture import path remains valid.

Two functions receive semantic phase extraction:

- symbol validation retains its inventory and missing-symbol guard before
  boundary, load-range, and section-range checks; and
- fixture construction exposes text, symbols, ELF/program headers, and
  load/metadata section encoding, preserving all bytes and metadata.

Every original function and checker data/type declaration has one named owner.
Only the two reviewed phase expansions and explicit import transport may
change their structural representation. No function-length exception remains.

## Preserved contracts

The correction preserves:

- supported Python versions, CLI options, defaults, statuses, and diagnostics;
- parser exception types and messages, complete error-list ordering, and
  validation-before-publication behavior;
- five frozen record schemas, all 38 checker data definitions, and one
  definition of every shared object;
- service ELF, linker, startup, raw-stub, register-write, and resident budgets;
- the mutable fixture bytearray, keyword-only inputs, metadata keys, symbol
  indices, and every existing generator's output;
- all current native, QEMU, image, and preset identities; and
- the exact four-dimensional classification of both original source paths.

The complete six-file checker set becomes a dependency of both existing
user-image link templates and all eight generated-fixture dependency blocks.
Checker command lines still invoke only `tools/check_user_elf.py`.
Classification changes are limited to two exact source-equivalence rows and
registration of one host-only structural gate. Planner execution and reuse
policy do not change.

## Non-goals

This outcome does not implement application startup or its ELF policy, dynamic
PID-1 integration, production init, commands, a new ELF format, a complete
RISC-V disassembler, new validation precedence, parser hardening, command
reuse, QEMU runner decomposition, directory layout, or a general validation
framework. Existing unrelated defects are not folded into source movement.

## Evidence and phase barrier

Design is documentation-only. It receives independent read-only review before
Acceptance and merge. The dependent implementation remains pending until
that barrier passes.

Implementation begins with a failing structural regression at the existing
host layer. It preserves original bodies before the two phase extractions,
uses small green commits, and completes both literal-empty migration
collections. AST checks must be stable across supported Python versions and
must verify caller/return boundaries, not erase arbitrary helper calls.
Independent positive/negative fixtures and immutable baseline byte records
supplement the existing semantic tests.

The final implementation runs the authoritative PR plan once. Shared CMake
and planner changes currently select the complete native, 37-QEMU,
three-image, documentation, and diff matrix. Independent implementation
review inspects that exact evidence without routinely repeating it.
Acceptance, correction, carry-forward, merge-tree equality, and branch
cleanup follow ADR-0034 and the agent contract.

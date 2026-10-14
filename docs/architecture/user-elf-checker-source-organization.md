# User ELF Checker Source Organization

## Status and outcome

This is the exact source contract for Accepted
[ADR-0063](../adr/0063-user-elf-checker-source-decomposition.md).
It implements the existing audit task pair
`elf-tooling-decomposition-design` /
`elf-tooling-decomposition-implementation`.
Independent design review found one image-inheritance gap, which was
corrected and independently re-reviewed with no substantive issue remaining.
Implementation remains pending this separate Accepted design's merge.

The single outcome is a behavior-preserving correction of
`tools/check_user_elf.py` and `tests/host/test_check_user_elf.py`.
It precedes the application checker included in `elf-spawn-app-runtime`,
so that checker need not duplicate the existing binary and structural logic.
This maintenance dependency does not change the runtime development graph.

The baseline is merged `dfa9d00c2a12923449e9ba06338d79b5b6b1bafb`:
1,596 physical lines, 44 function definitions, six checker classes, 38
checker data definitions, twelve test cases, and two long functions.
The [fixed-reference study](../research/minix-user-elf-checker-source-organization.md)
records provenance and classified adaptations.

## Authority and non-goals

Accepted ADR-0042 defines service ELF/startup policy. ADR-0050 defines the
separate future application image and startup. ADR-0051 defines cohesive
source responsibilities; ADR-0032 and ADR-0034 govern selection and evidence.
This contract changes none of their behavioral decisions.

There is no new executable policy, accepted ELF field, error precedence,
runtime authority, allocation policy, application startup, PID-1 scenario,
shell, disassembler, cache, command reuse, recovery, or generalized framework.
No C/assembly/runtime ABI or generated image byte changes are intended.
Kernel ELF checking and QEMU runner source corrections are separate tasks.

## Exact production owners

There are six production sources, including the existing entry path.

| Owner | Original function definitions |
| --- | --- |
| `tools/check_user_elf.py` | `validate_elf`, `load_validated_elf`, `main` |
| `tools/user_elf_format.py` | `_bounded_slice`, `_cstring`, `_defined_symbols`, `defined_symbols`, `_program_file_bytes`, `_section_by_name` |
| `tools/user_elf_parse.py` | `_parse_header`, `_parse_programs`, `_parse_raw_sections`, `_parse_sections`, `_parse_symbols`, `parse_elf`, `load_elf` |
| `tools/user_elf_structure.py` | `_validate_header`, `_validate_programs`, `_validate_sections` |
| `tools/user_elf_instructions.py` | `_sign_extend`, `_compressed_destination`, `_instruction_destination`, `_instruction_writes` |
| `tools/user_elf_policy.py` | `_validate_symbols`, `_validate_instructions`, `resident_page_count`, `validate_resident_page_limit` |

The format owner defines `ElfFormatError(ValueError)` and the five original
`@dataclass(frozen=True)` records. Their field order and defaults remain exact:

- `ElfHeader`: `elf_type`, `machine`, `version`, `entry`, `program_offset`,
  `section_offset`, `flags`, `header_size`, `program_entry_size`,
  `program_count`, `section_entry_size`, `section_count`,
  `section_names_index`.
- `ProgramHeader`: `index`, `program_type`, `flags`, `offset`,
  `virtual_address`, `physical_address`, `file_size`, `memory_size`,
  `alignment`.
- `Section`: `index`, `name`, `section_type`, `flags`, `address`, `offset`,
  `size`, `link`, `info`, `alignment`, `entry_size`.
- `ElfSymbol`: `name`, `binding`, `symbol_type`, `section_index`, `value`,
  `size`.
- `ElfImage`: `data`, `header`, `programs`, `sections`, `symbols`.

The 38 checker data definitions have exactly one owner:

- `REQUIRED_SYMBOLS` belongs to policy;
- `FORBIDDEN_PROGRAM_TYPES`, `FORBIDDEN_SECTION_NAMES`, and
  `FORBIDDEN_SECTION_PREFIXES` belong to structure; and
- the remaining 34 struct layouts, scalar constants, bounds, flags, and
  binary tags belong to format.

Definitions retain their original values and object kinds. In particular,
this is not an opportunity to replace a dictionary with a proxy, copy a
policy table per consumer, or add a mutable validation context.
Records and parser results remain immutable; per-call error lists, byte
slices, symbol maps, duplicate sets, and decoded-write lists remain local.

### Import direction and facade

Format is the leaf. Parse, structure, and instructions import only the
required format definitions. Policy imports format and instructions.
The facade imports those owners and explicitly re-exports all 38 data
definitions, six classes, and 27 original callable names, including the
existing underscored names. It retains only its three owned definitions.
Private owners never import the facade, each other contrary to this graph,
or test code. No wildcard import or duplicate definition is permitted.

The existing package path `tools.check_user_elf` and direct script path
`tools/check_user_elf.py` remain valid. For direct invocation only, the facade
may insert its resolved repository parent into `sys.path`, following the
existing fixture generators, before importing package-owned modules.
There is no catch-and-retry import fallback or caller-directory authority.
Both modes use the same package-defined record and helper objects.
Natural function/class owner-module metadata changes are not a new
serialization or monkey-patching interface.

The root's `validate_elf` keeps its current five-stage order and accumulation:
header, programs, sections, symbols, instructions. Its error list remains
automatic. A missing load or symbol does not suppress unrelated stages beyond
the original helpers' own guards.
`load_validated_elf` still joins errors with `"; "` in `ElfFormatError`.

The CLI remains `main(argv=None)` with:

- required `--elf`, parsed as `Path`;
- optional integer `--resident-page-limit`;
- integer `--stack-page-count`, default zero;
- the existing requires-limit and file-existence argument checks, in order;
- status 2 for `argparse` errors, 1 for caught/validation failure, and 0 for
  success; and
- exact ordered stderr diagnostics prefixed `user ELF validation failed:`.

The original caught exception tuple, explicit file reads, no-output success,
and `raise SystemExit(main())` entry remain. Do not silently skip a malformed
record, substitute an empty image, or change diagnostic multiplicity.

## Exact host owners and discovery

There are six host-family sources, excluding the new structural gate.

| Owner | Responsibility |
| --- | --- |
| `tests/host/test_check_user_elf.py` | Sole `UserElfCheckerTest` discovery root, existing helper re-exports, and existing `unittest.main()` entry |
| `tests/host/user_elf_test_fixture.py` | `align`, `make_string_table`, `build_elf`, its nested symbol helper, and `validation_errors`; private encoding phases and one immutable layout |
| `tests/host/user_elf_parse_cases.py` | Two original header/parser cases |
| `tests/host/user_elf_structure_cases.py` | Six original segment/section cases |
| `tests/host/user_elf_policy_cases.py` | Four original acceptance, budget, symbol, and instruction cases |
| `tests/host/user_elf_cli_cases.py` | New facade/import-mode, CLI status/diagnostic, and ordered-error regressions |

The four case owners define plain mixin classes named `UserElfParseCases`,
`UserElfStructureCases`, `UserElfPolicyCases`, and `UserElfCliCases`.
They do not subclass `unittest.TestCase`. The existing root's
`UserElfCheckerTest` inherits them in that order, then `unittest.TestCase`.
It does not duplicate their methods or change `__module__` dynamically.
Private filenames do not match `test_*.py`.

Thus the original twelve names remain under the same discovery root for
both aggregate discovery and qualified-module execution:

| Case owner | Original method names |
| --- | --- |
| Parse | `test_rejects_header_identity_flags_and_entry`, `test_rejects_truncated_or_malformed_tables` |
| Structure | `test_rejects_forbidden_program_headers_and_bad_load_shape`, `test_rejects_load_overlap_size_and_range`, `test_rejects_forbidden_allocatable_runtime_sections`, `test_rejects_relocation_sections`, `test_rejects_bss_or_allocatable_section_mismatch`, `test_rejects_shortened_text_and_file_backed_bss` |
| Policy | `test_accepts_exact_freestanding_shape`, `test_enforces_complete_resident_page_budget`, `test_rejects_undefined_symbols`, `test_rejects_raw_stub_startup_return_and_gp_tp_mutations` |

The old root continues to re-export the four existing fixture functions.
Imports such as
`from tests.host.test_check_user_elf import build_elf` do not change.
Cases import fixture helpers directly rather than importing their discovery
root. The fixture imports the supported checker facade, not private producer
modules. CLI cases use temporary files with deterministic cleanup and no
sleep. No shared mutable fixture, RNG, or process-global test state is added.

Of 44 original function definitions, three facade functions stay in place
and 41 move to the named owners. This includes the twelve inherited methods
and nested `build_elf.add_symbol`, whose lifetime remains inside symbol
construction. All six original checker classes and 38 data definitions move
once and are re-exported, not recreated.

## Required semantic phases

### Symbol validation: 140 lines

`_validate_symbols` remains the service-policy root. It first creates its
local `errors`, obtains symbols/duplicates, appends sorted duplicate errors,
appends missing required names in their original order, appends sorted
undefined names, and retains the exact missing-required-symbol return.
Only after that guard may it invoke:

1. `_validate_symbol_boundaries(image, symbols, errors)`: retain `_start`
   binding/entry checks, construct the exact automatic boundary map, check
   alignment and ordered closure, reject empty text and stack overlap, and
   return that same map.
2. `_validate_symbol_load_ranges(loads, boundaries, errors)`: preserve the
   `len(loads) == 3` guard, all three exact linker-range comparisons, and the
   writable-file-end/BSS-start check.
3. `_validate_symbol_section_ranges(image, boundaries, errors)`: preserve
   all required-section comparisons and the final separate BSS comparison.

The root returns its same error list and symbol map. Helpers append to the
borrowed list; they neither replace it nor publish another result.
The separate final BSS diagnostic is retained even when another comparison
already reports that shape. There is no error sorting or deduplication.

### Fixture construction: 293 lines

`build_elf` keeps the exact three keyword-only parameters and defaults:
`data_section_name=".data"`, `extra_relocation=False`,
`undefined_symbol=False`. It returns the same fresh mutable `bytearray` and
metadata dictionary, in that order.

The root retains scalar geometry, request-variant decisions, buffer
allocation/copying, and final metadata publication. It exposes these encoding
phases in original order:

1. `_fixture_text`: produce the exact page-sized text and nine instruction
   words.
2. `_fixture_symbols`: produce symbol strings and symbol bytes, retaining
   automatic `symbols` and the nested `add_symbol` closure; the optional
   undefined symbol and every existing index remain exact.
3. `_fixture_elf_header`: encode the existing identification and ELF header.
4. `_fixture_program_headers`: encode the three original load records.
5. `_fixture_section_headers`: orchestrate `_fixture_load_sections` for
   the four load sections, `_fixture_metadata_sections` for optional
   relocation and the three metadata sections, then pack the final records.

The seven named helpers own encoding, not a general ELF builder API.
For bounded argument transport, one per-call frozen `FixtureLayout` records
only the existing scalar geometry:
`base`, `text_offset`, `rodata_offset`, `data_offset`, `relocation_offset`,
`symbol_offset`, `string_offset`, `section_string_offset`, `section_offset`,
`section_count`, `symbol_section_index`, `string_section_index`, and
`section_string_index`. `relocation_offset` retains its existing `None`
or integer value. No byte buffer, error list, callback, or new authority is
stored there. It does not escape `build_elf`.

Construct the layout from those exact automatic locals after the existing
layout/allocation/copy work and before header encoding. Header and section
helpers borrow it read-only; remaining byte/string inputs stay explicit.
Do not move the fixture's mutable image or nested symbol closure to globals,
change literal padding, compress tuple syntax to discharge size, or freeze
the returned bytearray.

The returned metadata remains exactly the ordered keys
`program_offset`, `section_offset`, `symbol_offset`, `raw_symbol_index`,
`text_offset`; the raw symbol index remains 3.
Every final function is at most 120 physical lines.

### Independent fixture-byte evidence

These hashes were measured from the immutable baseline before implementation.
The variants are `(data_section_name, extra_relocation, undefined_symbol)`.

| Name | Relocation | Undefined | Bytes | SHA-256 |
| --- | --- | --- | ---: | --- |
| `.data` | false | false | 13,552 | `2d678b43c9b6f03107827e6c447a6a80d38dec113f0ccea56ffc7cc0cd342be7` |
| `.data` | false | true | 13,592 | `f9614cd78cc3cf26df3f8b6eac9e638a66cd35bab24240f56e48c9579d039789` |
| `.data` | true | false | 13,656 | `b02f235ea2ac0aef4141c70d7b88a11603a306ed601323876b3ad419733aed06` |
| `.data` | true | true | 13,696 | `49608d4e14c187e242f52a89355aa57f38325cf1fc8f72a15afbfd7b6f5f0b77` |
| `.got` | false | false | 13,552 | `76ece86afa1d440e14d436222896fd2b0e75072043f82f08b67b591fa92c2bc4` |
| `.got` | false | true | 13,592 | `7b9224c5e2a61a8f6652966e60679179dc47394577bdcec10a5a0222b56ed083` |
| `.got` | true | false | 13,656 | `cc619fbe97daf1e8f806a1a4906ed458834bf3fb7676148a86b320798a1d49ba` |
| `.got` | true | true | 13,696 | `ddeb102fff69d95e968140ed912df3eeedf13fcfd164746e8b87a2fba418c4fd` |

All variants retain `program_offset=64`, `raw_symbol_index=3`, and
`text_offset=4096`. Their `(section_offset, symbol_offset)` pairs, in table
order, are `(13040,12296)`, `(13080,12296)`, `(13080,12320)`,
`(13120,12320)`, then the same four pairs. These finite witnesses supplement,
not replace, preserved general builder logic and the existing generator
render hashes.

## Build and validation ownership

Define one ordered complete `MICROS_USER_ELF_CHECKER_SOURCES` CMake list:
entry, format, parse, structure, instructions, policy, using absolute
repository-rooted paths. Both existing user-image `LINK_DEPENDS` templates
must include this complete set alongside the unchanged linker script.
All eight fixture `DEPENDS` blocks that currently name only the checker
entry must use the complete set: runtime, PM, scratch-preemption, VFS, RAMFS,
TTY, VM-handoff, and bootstrap fixture generation.

The five existing post-link checker invocation templates remain unchanged,
including every budget argument. In particular, keep one entry-script
argument, not one invocation per private module. No target, preset, service
image, generated fixture, compile/link option, or QEMU marker is renamed or
added. Editing an imported checker owner must retrigger the same linked-image
check and fixture generation rather than leave stale validation.

Add exactly two `PATH_CLASSIFICATION_EQUIVALENTS` rows:
the five private production paths inherit `tools/check_user_elf.py`;
the five private host paths inherit `tests/host/test_check_user_elf.py`.
Register only `tests/host/test_user_elf_source_layout.py` as a new
`HOST_UNIT_ONLY_INPUTS` entry. Also expand each `IMAGE_INPUTS` tuple through
the existing `with_path_classification_equivalents` helper, using the same
data-initialization pattern as `GATE_INPUTS` and `SLOW_MODEL_INPUTS`.
Without that explicit image-map expansion, the new checker paths inherit
QEMU ownership but silently lose all three image workflows.
This adds no selector function or policy: existing path classifications
remain identical, and the new paths receive their exact baseline image
owners. Original planner functions, execution, discovery, and reuse remain
unchanged.

The baseline metadata was measured independently:

| Original path | Requires full tier | QEMU ownership | Slow models | Images |
| --- | --- | --- | --- | --- |
| `tools/check_user_elf.py` | false | The thirteen workflows below | none | TTY, RAMFS, VFS image workflows |
| `tests/host/test_check_user_elf.py` | false | `test-qemu-user-runtime` | none | none |

The thirteen checker workflows are `test-qemu-user-runtime`,
`test-qemu-bootstrap-launcher`, `test-qemu-vm-handoff`,
`test-qemu-pm-service`, `test-qemu-vm-scratch-preemption`, `test-qemu-tty`,
`test-qemu-ramfs`, `test-qemu-vfs`, `test-qemu-vm-ready-early`,
`test-qemu-vm-self-fault`, `test-qemu-vm-self-fault-sealed`,
`test-qemu-bootstrap-ready-timeout`, and
`test-qemu-bootstrap-manifest-panic`, in existing inventory order.
Do not replace either metadata row with a convenient broader or narrower
default. Shared CMake/planner edits still select the full current matrix.

Directly coupled peer layout assertions may change only where the exact
accepted CMake dependency spelling or two aliases invalidate them.
Derive each corrected fingerprint independently from immutable `dfa9d00`
plus this finite accepted delta. Never bless a hash from the edited file,
relax unrelated ownership, or add blanket new-path allowances.

## Structural and semantic acceptance

The new host structural gate records the complete original owners, five
record schemas, 38 data definitions, public re-exports, import DAG, original
twelve case names, six/six source inventories, complete CMake dependencies,
and all four metadata dimensions.
Original bodies first move under immutable normalized AST fingerprints.
The two reviewed phase roots then retain equivalent original statement
trees after narrowly checked expansion of only their named helpers.

For symbol phases, verify exact caller assignments, positional arguments,
missing-symbol return, borrowed error-list identity, helper tail returns,
and final root result before expansion. For the fixture, verify the exact
13-field frozen layout construction and read-only uses, nested closure
custody, helper arguments/results, mutation/packing order, and final
bytearray/metadata return before expanding and normalizing field transport.
Reject unknown phase calls or control exits. This is a bounded source
regression, not a general Python interpreter or a proof beyond its inputs.

AST normalization ignores only source locations and supported-version empty
metadata. It must reject meaningful new syntax rather than discard a
nonempty `type_params` field or rely on one Python version's raw AST dump.
No result may be rebaselined from rewritten code.

Independent positive/negative fixtures cover missing/duplicate owners,
duplicate class/table definitions, import cycles, facade wrappers/copies,
wrong record fields, diagnostic reordering or loss, early success/return,
wrong helper transport, escaping fixture storage, altered fixture bytes,
duplicate/disappearing test discovery, missing transitive build dependency,
and any metadata reduction or expansion.
In particular, removing image-map expansion while retaining both equivalence
rows must fail the four-dimensional inheritance check.

CLI regressions exercise package and direct invocation from a separate
working directory, success, argument errors, caught malformed inputs,
resident-limit failure, exact diagnostic order, and facade object identity.
Use current fixtures and temporary files rather than build the target for
these host contracts. Existing original test methods, subtests, mutations,
and all generated fixture render hashes stay unchanged.

Implementation records one finite owner-migration collection and two-function
phase collection; both are literal empty tuples at completion.
No empty production owner or unchanged-long exception remains.

## Implementation sequence and gates

Only after the design merge:

1. Demonstrate the focused structural Red; integrate the source inventory,
   dependency list, aliases, and gate in one green slice with finite pending
   obligations.
2. Move data, types, parsing, queries, structure, instructions, and policy
   bodies under frozen fingerprints; retain explicit facade exports and
   green original checker/generator cases.
3. Extract the three symbol phases, preserving complete ordered results.
4. Move fixture and case owners, split the fixture phases, retain original
   discovery identities and all eight frozen byte witnesses, add CLI
   contracts, and empty both migration collections.

Use the documented native unittest invocation for focused checker, structural,
and affected fixture-generator cases. No native C model or target execution
identity is replaced.
Before independent implementation review, run
`python3 tools/validation_plan.py --tier pr --execute` against the complete
clean staged candidate. The shared CMake/planner delta currently requires
all 45 commands: complete native tests, 37 QEMU workflows, three service
images, documentation, and three diff layers.
Do not separately repeat commands already completed by that final plan.

Record exact HEAD/tree, commands/results, retained log identity, clean
tracked/index state, and empty untracked output. Review stays read-only and
uses the author's exact evidence; no routine suite reruns. A justified
correction invalidates only ownership-intersecting evidence under ADR-0034.
Merge the exact reviewed head, verify squash-tree equality, fast-forward
main, and remove the exact obsolete branches after tip/worktree checks.

Design acceptance uses only documentation/structure and three-layer diff
checks. No executable implementation artifact belongs in the design PR.

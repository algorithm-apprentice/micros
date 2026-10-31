# ADR-0085: Stack Metadata Artifact Boundary

- Status: Accepted
- Date: 2026-11-01
- Refines: ADR-0033, ADR-0034, ADR-0080, ADR-0082, and ADR-0084
- Supersedes in part: ADR-0084's metadata artifact build ordering, solely at
  the exact complete-ELF seed boundaries below

## Context

EV03 design #180 is merged. Its uncommitted implementation built all 42
ordinary/metadata configurations, but the required byte-passivity check
correctly failed for the ELF-spawn RAMFS and kernel images.

Clang's `.stack_sizes` is non-allocatable in an application ELF. The current
seed generators nevertheless retain the complete validated ELF file.
Metadata therefore changes the seeded file bytes, seed digest and size,
RAMFS constants and retained data, then the kernel's embedded RAMFS image.
Checking only the leaf ELF's loaded segments misses this dependency.

The exact measured evidence and existing source owners are recorded in
[the artifact-boundary study](../research/stack-metadata-artifact-boundary.md).
The implementation is blocked and its branch abandoned. Its patch is
non-authoritative reference, not a current implementation or certificate.

## Decision

Keep both Clang metadata flags on the actual target C objects. Keep every
stack budget, frame/edge/root check, ordinary workflow, static variant,
dynamic scenario, and future B13 admission requirement of ADR-0084.

Add one metadata-only artifact canonicalization boundary before complete
application ELF bytes enter a seed or seeded-runtime copy. This is host
artifact transport, not a runtime algorithm, ELF ABI, seed format, compiler
optimization, or observation-policy change.

### Exact current leaf set

Canonicalization applies only to these target outputs:

| Target | Full ELF | Existing consumers |
| --- | --- | --- |
| `micros_elf_spawn_init` | `applications/micros-elf-spawn-init.elf` | ELF-spawn seed |
| `micros_elf_spawn_child` | `applications/micros-elf-spawn-child.elf` | ELF-spawn seed |
| `micros_application_runtime_probe` | `applications/micros-application-runtime-probe.elf` | Seeded-runtime copy and application seed |

The set is instantiated only in configurations that build each target.
Other executables are not automatically included because their filenames
end in `.elf`. A later complete-ELF consumer needs an independently reviewed
binding before joining this set.

The existing runtime copy of the normalized application-probe ELF then
produces identical ordinary and metadata `.runtime.elf` bytes. It does not
need a second transformation. The seed generators and their complete-file
validation algorithms remain unchanged.

### Paired authority and build ordering

The dispatcher first completes the ordinary counterpart from the same
clean candidate in its separate ordinary directory. It seals the exact
repository/HEAD/tree/source identity, tool identity, mode, compile/link
recipe, leaf identity, relative output path, and complete ordinary file
identity before the metadata build starts.

Only the two metadata flags and the corresponding ordinary/metadata build
directory spellings may differ in normalized compile/link comparisons.
Source, generated-input bytes, compiler/ISA/ABI options, and every other
recipe input must agree. No older receipt, unsealed file, other
configuration, different source tree, or arbitrary caller-supplied file
can supply the ordinary counterpart.

For each bound metadata leaf:

1. run the existing post-link ELF checker;
2. validate the sealed ordinary counterpart and current candidate/tools;
3. check exact loaded bytes, segment geometry/permissions, entry/ISA flags,
   and retained runtime symbols against that counterpart;
4. replace only the metadata leaf's complete-file representation with the
   verified ordinary bytes;
5. recheck the replacement identity, unchanged ordinary file and source;
6. finish the target; and
7. only then permit its existing copy or seed consumer.

The CMake dependency must wait for the complete checked transformation,
not merely the linker's file creation. Multiple leaf targets may finish in
any dependency-valid order; every consumer waits for every leaf it uses.
The ordinary build must not depend on the metadata build.

### Preserved compiler evidence

Object `.stack_sizes`, `.su`, machine instructions/relocations and the
metadata build's map remain available for the actual compiled functions.
Canonicalization does not delete or fabricate those objects or frame
records.

The final canonical leaf ELF has the ordinary representation. Its loaded
bytes and runtime symbol locations were proved identical before replacement,
so the same qualified object/map/linked-code checks remain mandatory.
A result records the pre-canonical file hash and the resulting complete-file
identity, not a new compiler execution or an invented stack certificate.
There is no need to commit a duplicate image or source tree.

### Fail-closed transformation

The prospective helper accepts one sealed pair record and a fixed leaf ID,
not arbitrary commands, extra flags, or unrestricted paths. The record binds
format version, exact candidate and tool/recipe identities, configuration,
`metadata` mode, target/leaf ID, paired relative paths, and ordinary file
kind/mode/size/hash.

Validate source/tool/recipe authority before artifact eligibility; validate
ordinary identity and both ordinary/metadata file kinds before ELF equality.
Only regular, nonlinked files below the two exact build directories are
eligible. Unknown IDs, absent or changed ordinary files, path escape,
partial ELF, input drift, or any allocatable/runtime-symbol difference fail.

Every pre-publication failure leaves both files unchanged. Publication writes
the verified bytes to a same-directory temporary file and atomically replaces
only the metadata file, preserving its required file mode. A failure after
replacement may leave canonical bytes present, but still fails the build and
publishes no success record. A subsequent consumer failure likewise cannot
turn an incomplete producer into success. Ordinary files are never modified.

This transformation is forbidden in observed mode. Observed images contain
intentional code/data instrumentation and must retain their own compiler
graph, resident limits and target results. Copying ordinary bytes into them
would conceal the required measurement rather than prove it.

## Acceptance

The restarted implementation begins with a failing complete-ELF seed
passivity case, then proves:

- unchanged full leaf, seeded-runtime, seed and downstream RAMFS/kernel bytes
  for each applicable ordinary/metadata pair;
- preservation of actual `.su` and object frame records;
- exact current candidate, tool, recipe, leaf and path binding;
- rejection of code/data, entry, symbol, source, input, ordinary-file,
  missing-leaf, unknown-ID and mode drift;
- no modification on pre-publication failure and no false success after
  publication/consumer failure;
- transformation after the original ELF check and before every consumer;
- observed and ordinary modes receiving no canonicalization;
- all 42 ordinary/metadata configurations, all ten current stack scopes,
  and the complete native/QEMU/image/documentation gates required by ADR-0084;
- unchanged historical source/recipe authorities and independent review.

No stack budget, deadline, model size, compiler optimization level, seed
algorithm, source identity or selected gate is weakened.

## Phase barrier

This is the sole active corrective-design prerequisite for EV03.
It contains documentation only: no normalizer, test, harness, model,
build scaffold or reusable prototype is authored here.

Only after independent review, Acceptance and separate design merge may
`ev03-stack-evidence-implementation` return to pending/in-progress.
It restarts from current `main`, references #180 and this corrective design,
and reintroduces valid reference behavior through test-first green slices.
Abandoned build results do not certify the restarted candidate.

## Design review disposition

Independent GPT-6.1 Sol review inspected the complete corrective design,
its exact current documentation evidence and the measured complete-ELF
dependency. It reported no substantive design issue. Acceptance records
that result, not an executable canonicalizer or restarted implementation.

## Alternatives considered

### Ignore metadata-induced seed changes

Rejected. This directly contradicts the unchanged-image requirement.

### Remove frame evidence or change seed semantics

Rejected. Dropping a required frame source or packaging a different runtime
ELF subset weakens another Accepted contract.

### Copy an ordinary file without proving loaded-byte equality

Rejected. It could erase a real runtime edit and approve itself.

### Normalize every ELF, including observed images

Rejected. The current boundary is finite, and observed code must not be
replaced by uninstrumented bytes.

### Change runtime budgets to absorb the metadata

Rejected. This is an artifact dependency defect, not a reason to enlarge
production or evidence stack/image limits.

## References

- [Artifact-boundary evidence](../research/stack-metadata-artifact-boundary.md)
- [Checked stack contract](../architecture/stack-bound-evidence.md)
- [ADR-0084](0084-checked-stack-bound-evidence.md)

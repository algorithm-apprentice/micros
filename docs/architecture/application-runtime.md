# Application Runtime Implementation Map

## Authority and current scope

This Step 14 slice implements the already Accepted application startup,
initial-stack, host-image and normal-return contracts in
[ADR-0050](../adr/0050-post-handoff-elf-spawn.md), separately reviewed and
merged as #87 / `b6b291d9f7acf922759b732362be38a1ad67ecd1`.
Its task is `elf-spawn-app-runtime`. The production coordinator #118 and
ELF-checker correction #126 are merged predecessors.
The native decoder and separate host application-image policy are implemented
with focused boundary and full/runtime-byte-parity cases. Target
assembly/frontend, generated checked seed artifacts and the component
extension remain in progress, not delivered.

The no-argument service entry, service linker/checker behavior, raw syscall
ABI, bootstrap configuration, immutable application profile and PM lifecycle
remain unchanged. This slice adds no heap, libc, TLS, constructor, mapping
authority, production init, shell or descriptor policy.
The full dynamic PID-1 spawn/rollback/descriptor/wait/resource-baseline
scenario remains the dependent `elf-spawn-implementation` task.

## Source and interface ownership

| Owner | Responsibility |
| --- | --- |
| `include/micros/application_runtime.h` | Application C entry, four startup globals and nonreturning exit declaration |
| `lib/runtime/application_internal.h` | Private bounded decoder/result and startup adapter contract |
| `lib/runtime/application_stack.c` | Read-only decoding of one canonical initial stack page |
| `lib/runtime/application.c` | Publish validated startup values, call application C code, encode PM `EXIT`, and fail explicitly if exit returns |
| `lib/runtime/application_start.S` | Separate fixed `_start`, explicit `gp`/`tp` clearing, original-SP transfer, and fatal breakpoint loops |
| `lib/runtime/application.ld` | Application-specific fixed image and resident bounds |
| `tools/check_application_elf.py` and private application-policy owner | Full-link application policy and checked seeded-artifact parity, reusing existing ELF representation and structural owners |
| `tests/host/application_runtime_test.c` and focused private fixture/case owners as needed | Native decoder, frontend publication, failure and exact PM-wire evidence |
| Checked application fixture and existing PM/VM component adapters | Real target startup/normal-return evidence before the final PID-1 integration |

These are implementation owners, not a second ABI specification. The stack
layout, bounds, auxv order and `int micros_application_main(int, char *[],
char *[])` signature remain exactly ADR-0050.

The four process-local globals are `micros_application_pm_endpoint`,
`micros_application_vfs_endpoint`, `micros_application_pid`, and
`micros_application_page_size`. Endpoint globals use `micros_endpoint_t`;
PID and page size retain `uint64_t`. They are data, never kernel authority.
The runtime publishes them only after complete startup decoding.
`micros_application_exit(int32_t)` is nonreturning.

## Bounded startup decoding

The private decoder receives a readable 4,096-byte page, its exact byte
count, the original child virtual `sp`, and a caller-owned result.
It translates child-local pointer values to checked page offsets rather
than dereferencing them as host pointers. This keeps the same implementation
native-testable without changing target addresses or using a fixed-address
host mapping.

Before reading encoded data, it rejects null arguments, a wrong page extent,
unaligned `sp`, or an initial frame outside the upper 2,048 bytes.
It then checks:

- one through eight arguments and their exact terminal null;
- at most eight environment pointers and their terminal null;
- zero alignment padding and the five exact ordered auxiliary entries;
- representable, nonreserved endpoint values, nonzero full-width PID,
  page size 4,096, and exact null auxiliary value;
- densely packed argument/environment strings, each NUL-terminated within
  256 bytes, with no pointer into metadata or outside the page; and
- only the original zero-filled final alignment gap below the page top.

The runtime does not recheck zero bytes below original `sp`: its own C
frames legitimately use that reserved downward-growing region.
The decoder constructs a local candidate and publishes its result only after
every check. Argument and malformed-input errors are explicit private results,
not a success-shaped empty environment.
No allocation, resettable registry, cache, fallback or kernel query is added.

The target adapter receives original `sp` from assembly, decodes the fixed
stack page, publishes all four globals, and only then invokes application
code with its child-local argument vectors. Malformed state never invokes
application code. Startup and unexpected-exit failure labels retain a
diagnostic value in `a0` before their fatal breakpoint loops.

## Normal return and exit

The frontend converts the returned C `int` to its signed 32-bit PM code.
It sends one zero-initialized 64-byte message through the existing call
wrapper to the validated PM endpoint:

- type `MICROS_PM_MESSAGE_EXIT`;
- protocol version at payload offset 0;
- zero flags at offset 4;
- exact code bits at offset 8; and
- zero remaining payload bytes.

The kernel supplies source/reply authority. An accepted exit never replies.
Every unexpected call return, including a negative kernel result, reaches
the fatal exit-return label; the runtime does not resume application code,
retry, synthesize success or fall through.

## Image and seed boundary

The application checker remains separate from the service entry point.
It reuses the existing format/parser, structural checks, bounded instruction
decoder and common linker-boundary primitives. It does not call the
service-startup policy or add an application switch to that checker.

The host checker enforces the complete ADR-0050 runtime subset and stronger
full-link closure before a stripped image can enter a RAMFS seed.
The seeded artifact must retain the checked ELF header, program headers,
entry and every load byte; removed debug/symbol metadata grants no exception
to executable-byte parity. Bounds and denied features are not widened.
The implemented checker entry is:

```text
python3 tools/check_application_elf.py --elf FULL_LINK_ELF
python3 tools/check_application_elf.py --elf FULL_LINK_ELF --runtime-elf SEEDED_ELF
```

The second form validates the full link image first, then checks the seeded
runtime subset without following its section/symbol metadata. Header and
program-header bytes must remain identical even when non-load metadata is
removed. Keeping a checked full image as the seed payload is valid; no
stripping step may silently rewrite a required header or load byte.
`build-application-images` exposes the checked image/seed artifacts, not a
claim that the later production PID-1 scenario has run.

## Target component and later integration

Extend the existing `test-qemu-pm-service` component rather than replacing
its kernel-lifecycle evidence or adding a duplicate QEMU execution.
Its real PM and VM services retain reserve, prepare/abort/reprepare,
activation, exact exit-begin, detach, VM release and resource-baseline proof.
The synthetic instruction-only child is replaced by a checked application
fixture with a canonical initial stack.

The selected small component image retains the existing three image pages
plus one stack page; its fixture generator must reject a shape that does
not fit those exact mappings. This is a component-image bound, not a
restriction of the general fifteen-image-page application ABI.
Component metadata comes from exact PM and the existing isolated test peer;
that peer is not production VFS and confers no additional application
privilege. The fixture proves startup values and PM exit-wire behavior, not
VFS application I/O or the later init-reaper transaction.

First tighten the PM component's accepted exit to a decoded versioned
request and a fixed nonzero result that the application entry returns only
after checking its startup values. The old synthetic child must fail that
target oracle before application assembly is authored.
Only the checked application image and canonical VM-built stack may make
the scenario pass. Keep all previous kernel lifecycle/resource checks.

Native frontend tests separately prove malformed startup cannot call main,
failure leaves startup publication untouched, and every unexpected exit
return is fatal. The final PID-1 fixture will prove actual production VFS
metadata, inherited I/O, ordinary coordinator exit, wait and complete child
cleanup through all six production services.

## Green sequence and evidence

1. Add the smallest failing native decoder case, implement complete bounded
   decoding, then extend deterministic malformed-input and boundary coverage.
2. Add application-specific host policy and seeded-byte-parity regressions
   over the shared ELF owners without changing service acceptance.
3. Establish the PM component's failing target oracle, then add the separate
   startup assembly/frontend, checked fixture image, VM stack adapter and
   normal-return path to make it pass.
4. Complete frontend failure/publication tests, image/seed budget and parity
   checks, and precise build/validation ownership.
5. Run the complete current PR plan, record exact HEAD/staged tree,
   commands/results and clean/untracked state, then obtain independent
   read-only implementation review.

The deterministic native target is `application_runtime_test`, registered as
CTest `application-runtime` in both native tiers. It reuses the complete
portable VM image source set and only the existing native spawn fixture
support file, not that family's entry point or case inventory.
The original spawn ABI execution and its mutable fixture owner are unchanged;
each executable has its own process-local fixture instance.
Its focused commands are:

```text
cmake --preset host-debug
cmake --build build/host-debug --target application_runtime_test
ctest --test-dir build/host-debug --output-on-failure -R '^application-runtime$'
```

These names are planned until their implementation lands. Existing documented
native, `test-qemu-pm-service`, service-image and final PR-plan commands remain
the acceptance entry points. New application-image ownership joins the plan
explicitly. Directly coupled source-layout expectations must be updated from
the immutable predecessor plus the exact new feature delta, never blindly
rebaselined from edited files.

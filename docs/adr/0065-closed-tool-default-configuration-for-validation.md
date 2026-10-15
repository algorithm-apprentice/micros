# ADR-0065: Closed Tool Default Configuration for Validation

- Status: Accepted
- Date: 2026-10-15
- Refines: ADR-0032, ADR-0033, ADR-0034, ADR-0064
- Supersedes in part: ADR-0064's initial admitted-root
  restriction, solely for the selected Homebrew LLVM installation's
  recognized `etc/clang` configuration alias under the conditions below

## Context

Design #129 is merged. Its implementation draft #130 correctly found a
previously unmodeled input: the selected versioned LLVM installation exposes
`etc/clang` as a link to the Homebrew prefix's non-versioned `etc/clang`.
The actual directory exists and contains compiler configuration files.
The initial profile's strict root check correctly refuses both target
workflows instead of publishing incomplete evidence.

The author preserved three green commits, closed #130 without merging and
abandoned its branch. Independent corrective design review is clean;
implementation remains blocked until this design merges.
No native/QEMU/image execution or receipt-reuse success
was inferred from the read-only metadata probes.

This is a development-tool closure correction, not a MINIX runtime change.
The fixed [MINIX validation baseline](../research/minix-validation-driver-baseline.md)
and all coverage, source, ownership, freshness and phase rules remain intact.

## Decision

The corrected initial profile retains the three workflows in
[the canonical validation contract](../architecture/validation-evidence.md).
It adds one exact compiler-configuration data root and makes implicit tool
configuration explicit rather than ignoring it.

### Compiler configuration

Reusable recording requires the caller's inherited
`CLANG_NO_DEFAULT_CONFIG=1`. The tool does not insert or remove this variable
silently. Default/fresh iteration without it keeps existing behavior and is
ineligible for reusable evidence.

LLVM's
[fixed driver source](https://github.com/llvm/llvm-project/blob/1ee8805f01a663b7f2b09215a8eab5ab2d15ce35/clang/lib/Driver/Driver.cpp#L1426-L1434)
disables automatic configuration loading when that variable is nonempty.
Its
[explicit-configuration path](https://github.com/llvm/llvm-project/blob/1ee8805f01a663b7f2b09215a8eab5ab2d15ce35/clang/lib/Driver/Driver.cpp#L1330-L1395)
is separate. Therefore selected compile/link recipes must also contain no
explicit `--config`, `--config=`, configuration-directory override or
response-file argument. `CCC_OVERRIDE_OPTIONS` and other unresolved compiler
argument-injection inputs make the workflow ineligible. Unsupported driver
families or unrecognized default-config semantics also remain ineligible.
The v1 declarations recognize Apple Clang major21 for the native host and
LLVM Clang major23 for the RV64 target. Other driver families or majors run
fresh without reusable receipts. Exact executable bytes and versions remain
fingerprinted; the family check never makes a patch-version change reusable.

The sole new root admission is:

- identify the selected LLVM versioned Cellar installation from the actual
  pinned compiler, not an environment-supplied prefix;
- recognize only that installation's `etc/clang` alias;
- derive the expected target as the same Homebrew prefix's `etc/clang`;
- require an existing directory with exactly that resolved target;
- fingerprint the alias kind, spelling and target, and the complete target
  tree's names, kinds, modes, file bytes and link identities.

An absent alias has an explicit absent identity. A real directory already
inside the versioned installation needs no external-root exception.
Wrong targets, unreadable/dangling aliases or another escaping link remain
ineligible. This does not authorize following arbitrary package links.

The configuration tree is opaque input data, never parsed or executed.
Automatic loading is disabled and explicit loading is excluded, so config
file contents cannot introduce transitive runtime inputs. Even so, their
complete identity is checked; the alias is not omitted from the manifest.
No raw configuration contents or environment values enter receipts or logs.

### QEMU default configuration

The current runner does not pass `-no-user-config`. The upstream
[fixed default-config loader](https://github.com/qemu/qemu/blob/8ee6c77176cfee03be0ec7665b6652b2f31f1531/system/vl.c#L2342-L2354)
reads relocated `CONFIG_QEMU_CONFDIR/qemu.conf`; absence is permitted, while
a present file can add otherwise undeclared guest inputs.

The initial Homebrew profile therefore requires explicit absent identities
for both the selected versioned QEMU installation's `etc/qemu/qemu.conf`
and the same Homebrew prefix's `etc/qemu/qemu.conf`, plus `qemu-bundle`
beside the resolved executable. The
[fixed relocation function](https://github.com/qemu/qemu/blob/8ee6c77176cfee03be0ec7665b6652b2f31f1531/util/cutils.c#L1144-L1168)
can redirect configuration and firmware below that bundle while both reported
data roots still remain inside the installation. Mere containment is not
proof that the two config paths are authoritative.

Resolve the executable and inspect its compiled path bytes without launching
QEMU, then check all three absences before any QEMU metadata invocation.
This includes `-L help`: default configuration loading precedes its handling.
Any present file, directory or link, or an unreadable/ambiguous state at any
of the three locations, is ineligible before introspection.
The installed layout,
relocated data roots and compiled default-config location must match this
reviewed layout. Unknown layouts are ineligible. No user configuration is opened,
interpreted, copied or silently disabled.
The bounded resolver requires the selected QEMU major11 image to contain
the expected NUL-terminated absolute versioned default-config path, after
deduplicating identical spellings across architecture slices. A different
matching config spelling is unsupported. Its actual `-L help` roots must
stay under that same selected installation.

The current no-explicit-configuration guest command remains unchanged.
An explicit config option or response input makes the profile ineligible.
This is stricter receipt eligibility, not a change to guest invocation.
The argument builder is bound to the reviewed `build_qemu_command` in
`tools/run_qemu_smoke.py` at `0f61a2b2ff2b80aa34f55c51352407090f96b5d0`;
another builder requires a separately reviewed closure declaration.

## Preserved invariants and non-goals

This decision does not change gate coverage, Git discovery, exact HEAD/tree
binding, log publication, invalidation, locking, fresh defaults/full,
independent review or source-oracle independence. It adds no general compiler
or QEMU configuration parser, package resolver, environment normalization,
arbitrary-command cache, configuration migration or operating-system change.
Installed paths and content digests remain runtime data.

## Acceptance and phase barrier

This Accepted design contains only documents and passes
documentation/structure plus the three diff checks. Independent correction
review reports no substantive issue. No implementation, tests, models,
harnesses or prototype belongs to this corrective design.

Only then may `validation-plan-dedup-implementation` restart from current
`main`, referencing this design and #129. It must test alias mutation,
wrong/dangling/unreadable targets, new config members, missing disable flags,
explicit/response/injected arguments and QEMU default-file presence.
The QEMU bundle-presence cases must prove refusal occurs before any QEMU
metadata subprocess, including when the bundle is a symlink or unreadable.
Every unsupported case stays fresh without a reusable receipt.
Actual native, smoke and image producer/reuse demonstrations and a fresh full
milestone gate remain mandatory before independent implementation review.

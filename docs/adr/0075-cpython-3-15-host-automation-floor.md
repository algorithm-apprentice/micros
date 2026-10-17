# ADR-0075: CPython 3.15 Host Automation Floor

- Status: Accepted
- Date: 2026-10-17
- Refines: ADR-0002, ADR-0033, ADR-0034, ADR-0064, ADR-0065, and ADR-0072
- Supersedes in part: ADR-0002's generic "Python 3" host-automation selection
  and the repository documentation's Python 3.8 minimum

## Context

The repository currently documents Python 3.8 or newer, while the reusable
Darwin validation profile requires the active Xcode `/usr/bin/python3` shim
and its CPython 3.9 framework. That profile fingerprints an interpreter family
that is no longer the project's intended development floor.

The user selected a latest-stable-only policy and installed Homebrew
Python 3.15.0 at:

```text
/opt/homebrew/opt/python@3.15/bin/python3.15
```

Python.org released CPython 3.15.0 as the stable final release on
2026-10-09. "Latest stable" must become a reproducible reviewed version
boundary, not an automatically moving dependency that changes when a future
formula appears.

Read-only local probes found:

- the executable resolves to the versioned Homebrew
  `python@3.15/3.15.0` Cellar installation;
- `sys.prefix == sys.base_prefix` inside
  `Python.framework/Versions/3.15`;
- the normal Homebrew startup imports a 3,404-byte `sitecustomize.py`, rewrites
  interpreter prefixes, adds `/opt/homebrew/lib/python3.15/site-packages`, and
  may append split `tk`/`gdbm` formulae;
- `-E -S -B` preserves repository-current-directory imports while disabling
  environment processing, `site`, user/site customization, and bytecode;
- macOS still honors `PYTHONEXECUTABLE` despite `-E`;
- CPython consumes `__PYVENV_LAUNCHER__` before project Python code can inspect
  it, and the normal Homebrew executable uses that variable internally, while
  a direct framework launch can expose its effect through `sys.executable`;
- the selected standard-library tree contains 77 extension modules in the
  observed patch installation, including external Homebrew dylib dependencies
  such as OpenSSL; and
- the existing 988-case host discovery under CPython 3.15 reached 942 passes
  and 46 failures in interpreter-owned AST serialization rather than production
  behavior. A project-owned stable AST representation that removes only the
  new default `type_params=[]` and `is_lazy=0` fields made all 988 tests pass
  without changing an expected digest.

The exact baseline and inventory are recorded in
[the Python 3.15 host-runtime study](../research/python-3-15-host-runtime.md).
This is host tooling, not a MINIX runtime subsystem. ADR-0025 has no source
behavior to reproduce here.

## Decision

Adopt the exact
[CPython 3.15 host-runtime contract](../architecture/python-host-runtime.md).

### Supported version

The supported host automation interpreter is:

```text
implementation: CPython
major/minor:    3.15
release level:  final
minimum patch:  3.15.0
```

Support covers final CPython 3.15 patch releases. CPython 3.8 through 3.14,
pre-releases, PyPy and other implementations, and a future CPython 3.16 are
unsupported until a separately reviewed decision changes the floor.
The selected build must be the ordinary GIL-enabled ABI with
`sys.implementation.cache_tag == "cpython-315"`; the free-threaded CPython
variant is outside the initial contract.

This is not a rolling rule. A package-manager update may change the selected
3.15 patch and invalidate receipts through normal tool fingerprints, but it
does not silently change the supported major/minor family.

No compatibility shim, alternate expected value, version branch, backport, or
normalization layer is added for older interpreters.

### Selection and configuration

Fresh CMake configuration resolves the interpreter in this order:

1. explicit CMake cache `MICROS_PYTHON`;
2. inherited `MICROS_PYTHON`;
3. `${MICROS_PYTHON_ROOT}/bin/python3.15`;
4. `brew --prefix python@3.15` on macOS; then
5. `python3.15` on `PATH`.

The selected path must identify an executable CPython 3.15 final release at or
above 3.15.0. Wrong versions stop configuration before any generated fixture,
host test, QEMU command, or validation receipt is produced.

Reusable validation requires inherited `MICROS_PYTHON` as an absolute path
and requires its spelling to match the configured CMake cache. Homebrew/PATH
discovery remains fresh-only and cannot publish or reuse a receipt.

The validation-plan process itself, every configured child interpreter, and
the cache must resolve to the same executable identity for reusable evidence.
An unrelated interpreter may not orchestrate a receipt for a different
configured Python.

Reusable planner subprocess argv uses the explicit `MICROS_PYTHON` spelling
that matches the cache. Fresh-only execution without that variable may use the
current validated `sys.executable`.

### Invocation boundary

Every supported project-owned Python execution uses this exact prefix:

```text
<selected-python> -E -S -B
```

- `-E` ignores ordinary `PYTHON*` configuration;
- `-S` prevents `site`, `.pth`, `sitecustomize`, `usercustomize`, system site
  packages, user site packages, and Homebrew split-package path mutation; and
- `-B` prevents bytecode writes.

Safe-path or isolated mode is not enabled because existing `-m` commands and
direct repository scripts require the reviewed repository/script import
roots. QEMU's direct runner retains its explicit repository-root insertion.

`PYTHONEXECUTABLE` is rejected before metadata because macOS honors it despite
`-E`. Every project-owned boundary that can inspect an environment before
executing Python--CMake configuration and each child-process constructor--
rejects a nonempty outgoing `__PYVENV_LAUNCHER__`.

The already-running planner does not claim to inspect that consumed variable.
It rejects launcher effects instead: resolved `sys.executable` must equal the
resolved selected executable and `sys.prefix` must equal `sys.base_prefix`.
On Darwin framework execution, `sys._base_executable` must additionally remain
in the selected versioned framework. The Darwin checks admit the normal
Homebrew launcher but reject a direct framework launch whose inherited
launcher value rewrites executable identity. Non-Darwin CPython 3.15 retains
fresh-only validation without a framework requirement. Project code neither
silently unsets nor rewrites either variable.

The implementation centralizes the CMake argument list and changes every
project-owned CMake Python command to use it. Validation-plan documentation
checks use the selected current interpreter and the same flags, not the bare
`python3` spelling. Executable tool shebangs use Python 3.15 with the same
flags, but CMake and author evidence never rely on shebang discovery.

The reviewed process-site inventory also includes:

- validation-context interpreter and loader metadata probes;
- validation-plan child documentation execution;
- user/application ELF checker script and module CLI subprocesses;
- QEMU runner direct-help and serial-pipe child interpreters; and
- validation tests that reproduce those subprocess boundaries.

Module-mode ELF checker tests run with the repository root as child working
directory and keep their ELF inputs in independent temporary directories.
They do not restore `PYTHONPATH`; script-mode tests retain an unrelated
temporary working directory.

### Closed Homebrew runtime

The initial reusable profile remains Darwin arm64/Ninja and advances to a new
profile identity.

It records:

- requested and resolved Python executable paths;
- complete version output and CPython 3.15/final metadata;
- GIL-enabled ABI, `cpython-315` cache tag, and runtime-derived import-loader
  suffix lists;
- `-E -S -B` flags and exact `sys.path`;
- versioned `python@3.15` Cellar installation and
  `Python.framework/Versions/3.15`;
- executable/framework symlinks and complete tree membership, modes, and
  bytes;
- every standard-library extension module and recursively resolved non-system
  Mach-O dependency; and
- explicit absence of site/user paths from the active import search.

The Homebrew `sitecustomize.py` remains inert because `-S` prevents import.
It is still covered as a byte inside the fingerprinted Cellar tree; there is
no content allowlist and no permission to load it.

The external Homebrew site-packages directory and split Python formulae are
not admitted import roots. Standard-library extension dylibs may add only
their resolved versioned Homebrew installation roots through the existing
Mach-O closure.

The observed `_hashlib` and `_ssl` extensions use Homebrew OpenSSL 4.
Supported execution requires:

```text
OPENSSL_CONF=/dev/null
SSL_CERT_FILE=/dev/null
SSL_CERT_DIR=/dev/null
```

The profile verifies `/dev/null` is the expected system character device,
requires `ssl.get_default_verify_paths()` to resolve no active CA file or
directory, and rejects every other nonempty `OPENSSL_*` or `SSL_CERT_*`
variable plus `RANDFILE` and `CTLOG_FILE`. This disables the mutable
`/opt/homebrew/etc/openssl@4` configuration and trust-store aliases.
Provider/module binaries remain inside the fingerprinted versioned OpenSSL
installation reached through Mach-O closure.

The old Xcode `Python3.framework`, CPython 3.9 startup allowlist, and special
inactive `.pth` declaration leave the profile.

### Stable project AST evidence

CPython's `ast.dump()` text is interpreter implementation data. CPython 3.15
changes default empty-field rendering and adds default `type_params` and
`is_lazy` fields, which invalidated compact hashes despite identical source
semantics.

The implementation does not rebaseline those expected digests. It defines one
project-owned canonical AST representation:

- emit the complete field form equivalent to the existing CPython 3.9
  evidence format;
- remove only empty `type_params=[]` and zero `is_lazy=0`;
- reject nonempty type parameters and nonzero lazy-import state as unreviewed
  syntax;
- retain the existing `Index` compatibility projection already owned by the
  source-record helper; and
- leave every other node name, field, value, order, operator, context, and
  literal exact.

Every direct `ast.dump()` evidence site and the existing `ast_key()` helper
migrate to one shared host-only canonical serializer. A structural inventory
forbids another direct AST serialization path.

This is evidence-format stability, not support for running the repository on
an older interpreter. The selected runtime remains CPython 3.15 only.

A session-only probe applying exactly those two new-field projections made all
988 existing host tests pass under CPython 3.15 in 68.944 seconds with no
expected digest update. Raw source SHA-256 values, generated output bytes,
C/ELF/protocol evidence, command plans, marker strings, and runtime behavior
remain unchanged.

## Preserved invariants and non-goals

This decision does not:

- add third-party Python packages, `pip`, a virtual environment, `pyproject`,
  lockfile, or package resolver;
- automatically follow CPython 3.16 or another future release;
- keep compatibility with Python 3.8 through 3.14;
- refactor Python code merely to use new syntax;
- alter C, assembly, kernel, service, protocol, image, or runtime behavior;
- weaken exact source, generated-byte, validation, or receipt evidence;
- admit Homebrew site packages or execute `sitecustomize`;
- make Homebrew discovery reusable;
- add CI or a new runtime-DAG node; or
- change the selected LLVM, LLD, QEMU, CMake, or Ninja policy.

## Acceptance and phase barrier

This design task is `python-version-floor-design`; the dependent implementation
task is `python-version-floor-implementation`.

The design pull request contains documentation only. It adds no source edit,
test, CMake change, dependency, fixture, generated file, or prototype.

Before implementation starts, independent review must verify:

- the version policy is pinned rather than rolling;
- interpreter selection and reusable explicitness are complete;
- every supported execution receives `-E -S -B`;
- observable macOS escape variables fail before execution and consumed
  launcher effects plus virtual environments fail through exact
  platform-appropriate runtime identity;
- site/customization paths cannot enter the active import closure;
- Homebrew framework/extensions/dylibs remain completely fingerprinted;
- OpenSSL configuration/trust inputs are disabled and environment overrides
  fail closed;
- every child interpreter construction site uses the exact invocation prefix
  without `PYTHONPATH`;
- the shared canonical AST serializer preserves all existing expected digests
  and rejects nondefault new syntax;
- direct-script and `-m` import behavior remains feasible;
- older/future interpreters reject explicitly; and
- validation producer/reuse evidence remains exact-current-context.

The implementation starts with failing version, flag, site/OpenSSL closure,
environment-injection, CMake/child-command, loader-suffix, AST-serializer, and
CPython-3.15 host-suite tests. It must then pass the complete
native/QEMU/producer/reuse gates defined by the architecture contract.

## Design review disposition

Complete independent read-only design review found three initial gaps:
OpenSSL configuration and trust escaped the versioned runtime closure, the
child-interpreter inventory omitted subprocess forms that depended on
`PYTHONPATH`, and AST-format authority was delegated to implementation through
a proposed digest rebaseline.

The corrected design disables external OpenSSL configuration and trust,
closes every Python process-construction site under one invocation prefix, and
defines one project-owned AST serializer that preserves all existing expected
digests. Correction review then found that CPython consumes
`__PYVENV_LAUNCHER__` before project code can inspect it. The final contract
rejects that variable only at observable pre-exec boundaries and checks its
current-process effects through exact executable and prefix identity, plus
framework-base identity on Darwin.

A targeted CPython 3.15 launcher reproduction verified normal Homebrew launch,
consumed launcher state, direct-framework executable rewriting, and visible
`PYTHONEXECUTABLE` behavior. Final independent review concluded:

> The prior Medium is closed; no substantive issue remains.

Acceptance records that clean design result. It does not claim the dependent
CMake, Python tooling, tests, runtime closure, or reusable profile exists.

## Alternatives considered

### Continue supporting Python 3.8+

Rejected by project policy. Compatibility work for obsolete interpreters adds
cost without a current deployment requirement.

### Support every Python version greater than or equal to 3.15

Rejected. Future AST, import, startup, stdlib, and extension changes are not
reviewed merely because their version number is larger.

### Keep Xcode Python only for reusable evidence

Rejected. It would preserve a different interpreter from the supported
development floor and force duplicate host-runtime policies.

### Admit Homebrew sitecustomize and site-packages

Rejected. The startup file mutates prefixes and import roots and may admit
unreviewed packages. `-S` supplies a smaller standard-library-only boundary.

### Use isolated mode

Rejected for the initial migration because it removes the repository current
directory from `sys.path` and would require a broader import/packaging change.
`-E -S -B` closes environment/site inputs while preserving existing import
ownership.

### Rebaseline every AST digest to CPython 3.15

Rejected. Hundreds of values would change solely because of interpreter-owned
empty-field formatting, obscuring real source changes. A small project-owned
canonical representation preserves the already reviewed evidence while the
runtime itself moves exclusively to 3.15.

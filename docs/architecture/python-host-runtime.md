# CPython 3.15 Host Runtime

## Status and outcome

This is the Accepted exact contract for
[ADR-0075](../adr/0075-cpython-3-15-host-automation-floor.md).

```text
design task:         python-version-floor-design
implementation task: python-version-floor-implementation
runtime DAG node:    none
implementation:      CPython
supported family:    3.15 final
minimum patch:       3.15.0
```

The outcome is one standard-library-only host automation runtime used by
CMake, native/Python tests, fixture generators, ELF checks, QEMU runners,
documentation checks, validation planning, receipt production, and receipt
reuse. It replaces the documented Python 3.8 floor and Xcode CPython 3.9
validation profile without changing target behavior.

## Selection contract

### Fresh configuration

CMake selects:

```text
MICROS_PYTHON cache
MICROS_PYTHON environment
MICROS_PYTHON_ROOT/bin/python3.15
Darwin: brew --prefix python@3.15
PATH: python3.15
```

The selected executable must report CPython 3.15, final release, patch 0 or
newer. Selection stops on an invalid explicit cache/environment/root path; it
does not fall through to another interpreter.

### Reusable validation

Reusable profiles require:

```text
MICROS_PYTHON=/absolute/path/to/python3.15
```

That spelling must equal the configured cache spelling and resolve to the
interpreter running the validation planner. The resolved executable,
installation, framework, version, extension modules, libraries, flags, and
paths enter the context/input identity.

Automatic Homebrew/PATH selection remains fresh-only.

## Process invocation

The exact supported argv prefix is:

```text
<MICROS_PYTHON> -E -S -B
```

All CMake Python commands use one central argument list. Repository
documentation and validation-plan subprocesses use the same prefix.
Project-owned executable Python tools receive the equivalent Python 3.15
shebang, while build and validation authority continues to use explicit argv.

The selected process must report:

```text
sys.implementation.name == "cpython"
sys.version_info[:2] == (3, 15)
sys.version_info.releaselevel == "final"
sys.version_info >= (3, 15, 0)
sys.implementation.cache_tag == "cpython-315"
sys._is_gil_enabled() == true
sysconfig.get_config_var("Py_GIL_DISABLED") == 0
sys.flags.ignore_environment == 1
sys.flags.no_site == 1
sys.flags.dont_write_bytecode == 1
sys.flags.isolated == 0
sys.flags.safe_path == 0
resolve(sys.executable) == resolve(selected executable)
sys.prefix == sys.base_prefix
```

On Darwin framework execution,
`resolve(sys._base_executable)` is additionally inside the selected versioned
framework. Other platforms retain fresh-only CPython 3.15 validation without
inventing a framework requirement; the reusable profile remains the bounded
Darwin Homebrew profile below.

`sys.path` contains only:

- the reviewed repository, script, and test-discovery roots required by the
  invocation form;
- the selected installation's standard-library zip spelling;
- its `lib/python3.15`; and
- its `lib/python3.15/lib-dynload`.

No `site-packages`, user site, split-formula library path, external
`PYTHONPATH`, or virtual-environment path is admitted.

## Environment boundary

`-E` handles ordinary `PYTHON*` configuration, but Darwin CPython still honors
`PYTHONEXECUTABLE`. It remains visible after startup and is rejected before
metadata.

CPython consumes `__PYVENV_LAUNCHER__` before project Python code runs, and the
normal Homebrew `bin/python3.15` launcher uses that variable internally.
Literal current-process rejection is therefore neither observable nor valid.
Every project-owned pre-exec boundary that can observe the outgoing
environment--CMake configuration and each child-process constructor--rejects
a nonempty `__PYVENV_LAUNCHER__`.

The current planner rejects effects rather than claiming access to the
consumed input. Resolved `sys.executable` must equal the resolved selected
executable and `sys.prefix` must equal `sys.base_prefix`. On Darwin framework
execution, `sys._base_executable` must also remain inside the selected
versioned framework. A direct framework launch whose inherited value rewrites
`sys.executable` is unsupported; the normal Homebrew launcher remains
supported. Non-Darwin fresh validation has no framework-containment check.

The existing validation environment rejection of `PYTHONPATH`, `PYTHONHOME`,
and `PYTHONSTARTUP` remains. The previous `PYTHONNOUSERSITE=1` requirement is
removed because `-S` establishes the stronger no-site boundary and `-E`
ignores Python environment configuration.

Python standard extensions use OpenSSL. Supported execution additionally
requires:

```text
OPENSSL_CONF=/dev/null
SSL_CERT_FILE=/dev/null
SSL_CERT_DIR=/dev/null
```

Every other nonempty `OPENSSL_*` or `SSL_CERT_*` variable, plus `RANDFILE` and
`CTLOG_FILE`, is rejected. `/dev/null` must be the expected system character
device. The selected interpreter must report no active default CA file or
directory.

Project code does not silently unset or rewrite inherited variables. Invalid
observable configuration or runtime identity fails explicitly.

## Darwin Homebrew closure

The observed installation resolves through:

```text
/opt/homebrew/opt/python@3.15
  -> /opt/homebrew/Cellar/python@3.15/3.15.0
```

Paths are runtime data, not source constants. The profile derives the
versioned Cellar root from the selected resolved executable.

It fingerprints:

- the complete `python@3.15` installation;
- `Python.framework/Versions/3.15`;
- interpreter and framework symlink spellings/targets;
- standard-library and extension module bytes/modes/names;
- the exact Python executable and version output; and
- every non-system Mach-O dependency reached from the interpreter or standard
  extension modules.

The observed `sitecustomize.py` SHA-256 is
`f155c7521b70d6a17d7103e82598ec903edc75ea29638e5049606dfa04bfc436`.
This value is evidence, not an allowlist. The file remains inert under `-S`
and is covered by the complete installation fingerprint.

Observed `_ssl` depends on Homebrew OpenSSL 4. Such dependencies are admitted
only through the existing resolved versioned-installation closure. The design
does not freeze an extension count or package patch path.

OpenSSL configuration and trust paths under `/opt/homebrew/etc` are not
admitted. Configuration and default trust loading are disabled by the required
environment above. Provider/module binaries remain within the fingerprinted
versioned OpenSSL installation.

The complete Cellar includes an inert `site-packages` subtree that is absent
from active `sys.path` under `-S`. Its links retain membership, mode, spelling,
and resolved-path identity, but an external link target's bytes are not
admitted as runtime inputs. Active standard-library, extension, provider, and
Mach-O links remain fail-closed under the ordinary closure rule.

The reusable profile identity advances from the Xcode-Python declaration so
old receipts cannot match accidentally.

## CMake and command ownership

`MICROS_PYTHON` remains the selected executable cache entry. A central
`MICROS_PYTHON_ARGS` owns `-E;-S;-B`.

Configuration accepts that executable only after the runtime helper validates
its own process and returns the exact response to a fresh per-configuration
challenge. Exit zero without that challenge response is a failure.

Every CMake command that executes Python must use:

```cmake
COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS} ...
```

Dependencies that name the interpreter executable remain explicit and do not
expand the argument list.

The validation planner's documentation command uses the actual current
selected executable and flags. Reusable execution uses the explicit
`MICROS_PYTHON` cache spelling after proving it resolves to the running
interpreter; fresh-only execution may use the current validated executable.
Command validation accepts only that derived fixed tuple; it does not admit
caller-supplied Python argv.

The implementation structural gate inventories every Python process
construction site:

| Owner | Required adaptation |
| --- | --- |
| All CMake `${MICROS_PYTHON}` command sites | Central exact argv prefix |
| Validation-context runtime metadata probe | Exact selected prefix |
| Validation-context QEMU loader probe | Exact selected prefix |
| Validation-plan documentation subprocess | Derived selected prefix |
| User ELF CLI script/module tests | Script keeps temporary cwd; module uses repository-root cwd |
| Application ELF CLI script/module tests | Script keeps temporary cwd; module uses repository-root cwd |
| QEMU serial-pipe inline child commands | Exact selected prefix |
| QEMU runner direct-help test | Exact selected prefix |
| Validation-context subprocess reproductions | Exact selected prefix |
| Executable project tool shebangs | Equivalent 3.15 `-E -S -B` prefix |

`PYTHONPATH` is removed rather than worked around. Temporary ELF inputs remain
outside the repository and use absolute paths. One structural inventory
prevents a new process-construction site from bypassing the selected prefix.

## Stable AST evidence

The current CPython 3.15 probe reports 46 failures because direct
interpreter-owned AST serialization changed. The implementation does not
rebaseline any expected digest.

One shared host-only serializer owns both existing AST evidence forms:

1. complete textual field serialization compatible with the already reviewed
   expected hashes; and
2. the existing tuple-key semantic representation.

Under CPython 3.15 it removes only:

```text
type_params=[]  # empty new field
is_lazy=0       # disabled new import field
```

Nonempty type parameters or nonzero lazy imports fail explicitly. Existing
`Index` projection remains. Every other AST field/value/order remains exact.
All direct `ast.dump()` evidence sites migrate to the shared serializer, and
a structural gate rejects another direct serializer.

A session-only probe using exactly these rules passed all 988 current host
tests under CPython 3.15 in 68.944 seconds without changing an expected digest.
Generated bytes, raw source hashes, plans, discovery names/order,
C/ELF/QEMU/protocol markers, and results remain independently checked.

The QEMU direct-script loader closure records the CPython 3.15 cache tag plus
the runtime-reported source, bytecode, and extension suffix lists. The observed
extension list includes `cpython-315`, Darwin-specific `abi3`, and `abi3t`
spellings. All same-stem package, bytecode, and native-extension alternatives
remain rejected before QEMU metadata.

## Implementation sequence

1. Add structural Red for interpreter selection, exact flags, observable
   environment rejection, consumed-launcher effect detection, CMake invocation
   inventory, shebangs, and profile identity.
2. Add failing CPython 3.15 profile/loader/import/Mach-O/OpenSSL closure tests.
3. Change CMake selection and every command to the exact runtime prefix.
4. Update every non-CMake child interpreter site and remove `PYTHONPATH`.
5. Replace Xcode-specific validation closure with the Homebrew 3.15 closure.
6. Introduce the shared AST serializer without changing expected digests.
7. Update direct documentation and canonical commands.
8. Run the complete current producer, QEMU/image evidence selected by the
   changed recipe/tooling paths, and exact reusable-profile consumer.

Each permanent commit remains green. AST serializer transport is separate from
tool selection/profile code so review can distinguish evidence format from
runtime closure.

## Implementation realization

The implementation owns:

- selection and exact CMake argv in `CMakeLists.txt`;
- metadata, environment, path, GIL, launcher-effect, and OpenSSL checks in
  `tools/python_host_runtime.py`;
- planner command identity and reusable closure in
  `tools/validation_plan.py`, `tools/validation_context.py`, and
  `tools/validation_inputs.py`;
- exact child-process construction in the ELF/QEMU host cases; and
- stable AST text/key evidence in `tests/host/ast_evidence.py`.

The reusable identity is `darwin-arm64-ninja-python315-v2`. Existing v1
receipts cannot match it.

## Acceptance

Before implementation review:

- wrong implementation/version/release/flags fail explicitly;
- all project-owned Python executions use the exact prefix;
- site customization and site-packages are absent from active imports;
- `PYTHONEXECUTABLE` and pre-exec `__PYVENV_LAUNCHER__` fail explicitly;
- consumed launcher effects and virtual environments fail through exact
  executable/prefix identity and, on Darwin, framework identity while normal
  Homebrew launch succeeds;
- OpenSSL configuration, trust, provider, and environment boundaries are
  closed;
- every child interpreter and shebang uses the exact prefix without
  `PYTHONPATH`;
- CMake cache, current planner, and reusable profile name one interpreter;
- framework, extension, and dylib closure is complete;
- the complete Python host suite passes under 3.15;
- all existing AST digests pass unchanged through the shared serializer;
- QEMU direct-script loader/source closure passes with CPython 3.15 suffixes;
- docs and all diff/clean checks pass;
- the authoritative current producer is complete; and
- actual selected receipt reuse succeeds under the new profile.

Independent implementation review remains read-only and does not rerun the
author's complete matrix without a concrete suspected defect.

## Non-goals

No Python packages, virtual environment, packaging metadata, compatibility
layer, automatic major-version upgrade, CI platform, runtime-DAG node, target
behavior, or broad Python refactor is added.

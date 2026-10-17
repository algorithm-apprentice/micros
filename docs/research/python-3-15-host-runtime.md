# Python 3.15 Host Runtime Study

## Reference and scope

Python.org published
[CPython 3.15.0](https://www.python.org/downloads/release/python-3150/) as a
stable final release on 2026-10-09. Its release schedule is
[PEP 790](https://peps.python.org/pep-0790/).

The local read-only installation used for design research is:

```text
/opt/homebrew/opt/python@3.15/bin/python3.15
Python 3.15.0
```

This study concerns host automation only. It does not define or change
`micros` kernel, service, user ABI, runtime protocol, or target behavior.

## Current repository baseline

The repository currently:

- documents Python 3.8 or newer;
- selects the first `python3` found by CMake;
- invokes configured Python primarily with `-B`;
- uses bare `python3` for the documentation command in the validation plan;
- requires `/usr/bin/python3` and Xcode `Python3.framework` for reusable
  evidence;
- recognizes an inactive CPython 3.9 vendor `.pth` file; and
- freezes many Python AST digests produced by CPython 3.9.

These choices form one coupled interpreter/command/evidence boundary. Changing
only the prerequisite text would make the build and reusable profile
contradict each other.

## Homebrew 3.15 observations

### Version and paths

With `-I -S`, the interpreter reported:

```text
implementation: cpython
version:        3.15.0 final
resolved exe:   /opt/homebrew/Cellar/python@3.15/3.15.0/bin/python3.15
prefix:         .../Python.framework/Versions/3.15
stdlib:         .../lib/python3.15
lib-dynload:    .../lib/python3.15/lib-dynload
```

The opt prefix exposes both `python3` and `python3.15` symlinks. The resolved
interpreter links the Homebrew Python framework plus the system library.

### Startup customization

Normal `-B` startup loaded Homebrew `sitecustomize.py`, enabled user site, and
added:

```text
/opt/homebrew/lib/python3.15/site-packages
```

The startup file also rewrites `sys.executable`, prefixes, site prefixes, and
may append split `tk` or `gdbm` formula paths. Its observed SHA-256 is:

```text
f155c7521b70d6a17d7103e82598ec903edc75ea29638e5049606dfa04bfc436
```

The existing validation profile correctly rejects such startup customization.
Allowlisting this file would still admit a global package directory and
formula-dependent path mutation.

`-E -S -B` instead reported:

```text
ignore_environment=1
no_site=1
dont_write_bytecode=1
isolated=0
safe_path=0
```

Its path contained only the current directory plus the selected
standard-library zip, stdlib, and extension directory. Existing `-m unittest`
and direct QEMU-runner help probes succeeded with that boundary.

On macOS, `PYTHONEXECUTABLE=/tmp/fake` still changed `sys.executable` despite
`-E` and remained visible in `os.environ`.

A targeted launcher probe found a different boundary for
`__PYVENV_LAUNCHER__`:

- CPython removed it from `os.environ` before project code ran;
- the normal Homebrew `bin/python3.15` launcher replaced an inherited
  `/tmp/fake` value and retained its normal resolved executable, framework
  base, and prefix identities; and
- direct execution of the framework application with that inherited value
  changed `sys.executable` to `/tmp/fake`.

The literal variable can therefore be rejected only at project-controlled
pre-exec boundaries. An already-running planner must reject its effects
through selected/resolved executable and prefix identity, with the
framework-base check scoped to Darwin framework execution.

### Native dependencies

The observed installation contains 77 standard extension modules. This count
is patch data, not a contract. `_ssl` links Homebrew OpenSSL 4; other modules
may add versioned Homebrew libraries. The existing Mach-O resolver can close
those inputs when the complete framework and extension inventory is selected.

The selected build reported:

```text
cache tag:       cpython-315
GIL enabled:     true
Py_GIL_DISABLED: 0
extension suffixes:
  .cpython-315-darwin.so
  .abi3-darwin.so
  .abi3.so
  .abi3t-darwin.so
  .abi3t.so
  .so
```

The initial contract therefore excludes the free-threaded build while still
closing every runtime-reported native-extension shadow spelling.

### OpenSSL configuration and trust

`hashlib.sha256` is provided by `_hashlib`, and `_ssl` links Homebrew
OpenSSL 4.0.3. The runtime reported:

```text
OPENSSLDIR: /opt/homebrew/etc/openssl@4
MODULESDIR: .../Cellar/openssl@4/4.0.3/lib/ossl-modules
```

The external `openssl.cnf` activates `openssl_conf`, and the default CA file is
an external symlink to Homebrew `ca-certificates`. These inputs are outside the
versioned OpenSSL installation closure.

OpenSSL documents these overrides and automatic configuration through
[openssl-env(7)](https://docs.openssl.org/master/man7/openssl-env/) and
[config(5)](https://docs.openssl.org/master/man5/config/).

With:

```text
OPENSSL_CONF=/dev/null
SSL_CERT_FILE=/dev/null
SSL_CERT_DIR=/dev/null
```

Python SHA-256 and OpenSSL import still succeeded, while
`ssl.get_default_verify_paths()` reported no active CA file/directory and a
default context loaded zero certificates. The design therefore disables these
unused mutable configuration/trust inputs and rejects other OpenSSL overrides.

### Child interpreter inventory

The parent-only 988-case probe did not prove child command isolation. Existing
child sites are:

- validation-context interpreter metadata and QEMU-loader probes;
- user and application ELF checker script/module CLI tests;
- three QEMU serial-pipe inline Python children;
- QEMU runner direct-help execution;
- validation-context child-probe reproduction; and
- the validation-plan documentation subprocess plus all CMake Python commands.

The two module-mode ELF checker tests currently add repository `PYTHONPATH`
while running from a temporary directory. Under `-E` that path is ignored.
The required adaptation is repository-root `cwd` for module mode with absolute
temporary ELF paths; script mode remains outside the repository.

## Existing-suite probe

The current complete Python host discovery under:

```text
/opt/homebrew/opt/python@3.15/bin/python3.15 -E -S -B
```

ran 988 tests in 73.952 seconds and reported 46 failures.

The failures were confined to interpreter-owned AST serialization:

- 38 generated-source AST mismatches;
- shared validation-context/build-manifest owner AST digests;
- QEMU runner/planner/test module AST projections; and
- source-organization long-function/helper AST inventories.

The observed failure owners were:

| Host test module | Cases |
| --- | ---: |
| `test_kernel_object_source_layout` | 7 |
| `test_qemu_runner_source_layout` | 6 |
| `test_trap_syscall_source_layout` | 9 |
| `test_kernel_bootstrap_source_layout` | 4 |
| `test_ramfs_source_layout` | 3 |
| `test_tty_source_layout` | 3 |
| `test_endpoint_source_layout` | 2 |
| `test_grant_data_plane_source_layout` | 2 |
| `test_kernel_directory_layout` | 2 |
| `test_pm_service_source_layout` | 2 |
| `test_vm_service_source_layout` | 2 |
| `test_frame_ownership_source_layout` | 1 |
| `test_kernel_address_space_source_layout` | 1 |
| `test_validation_context_layout` | 1 |
| `test_vfs_test_source_layout` | 1 |

The trap/syscall total includes its source-layout, meta-fixture-lifecycle, and
valid-extraction classes.

CPython 3.15's complete AST dump differs from 3.9 through two default fields
used by repository sources:

```text
type_params=[]
is_lazy=0
```

The repository already owns a semantic `ast_key()` that rejects nonempty type
parameters and normalizes the removed legacy `Index` wrapper. A session-only
probe made direct AST text use complete-field output minus only the two default
fields above, and added `is_lazy` handling to that semantic key. All 988 tests
then passed in 68.944 seconds without changing any expected digest.

This demonstrates that the correct migration is one shared project AST
serializer and an inventory of every direct AST serialization call site, not a
46-row expected-value rebaseline.

Raw source and generated behavior were not shown to differ. The implementation
must preserve every existing expected digest and reject any direct
interpreter-owned serializer outside the reviewed call-site inventory.

Representative direct execution passed:

```text
run_qemu_smoke.py --help
42 validation-execution unit tests
```

## Classified adaptations

| Observation | Classification | Required adaptation |
| --- | --- | --- |
| Standard-library-only Python automation | Existing project behavior | Preserve. |
| CPython 3.15 final instead of generic 3.8+/Xcode 3.9 | User-selected tool policy | Pin exact 3.15 family and reject others. |
| Homebrew sitecustomize/global site packages | Unreviewed implicit input | Disable with `-S`; do not allowlist. |
| Python environment variables | Unreviewed implicit input | Use `-E`, reject observable pre-exec launcher inputs, and verify consumed-launcher effects through runtime identity. |
| Bytecode writes | Unnecessary mutable output | Preserve `-B`. |
| Homebrew framework/extensions/dylibs | Required host adaptation | Fingerprint complete selected closure. |
| OpenSSL config/trust outside Cellar | Unreviewed implicit input | Require explicit disabled config/trust and reject overrides. |
| Child interpreter `PYTHONPATH`/partial flags | Unclosed subprocess input | Inventory every child and use exact prefix/rooted module execution. |
| CPython-version AST schema differences | Interpreter formatting | Use one stable project serializer; preserve expected digests. |
| Automatic future major upgrades | Nondeterministic dependency | Reject; require another reviewed floor change. |

## Decision reference

See Accepted
[ADR-0075](../adr/0075-cpython-3-15-host-automation-floor.md) and the
[exact host-runtime contract](../architecture/python-host-runtime.md).

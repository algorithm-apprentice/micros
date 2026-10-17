#!/usr/bin/env -S python3.15 -E -S -B

"""Exact CPython 3.15 host-runtime boundary from ADR-0075."""

import argparse
import json
import os
from pathlib import Path
import platform
import stat
import sys
import sysconfig


PYTHON_ARGS = ("-E", "-S", "-B")
REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
REQUIRED_ENVIRONMENT = {
    "OPENSSL_CONF": "/dev/null",
    "SSL_CERT_FILE": "/dev/null",
    "SSL_CERT_DIR": "/dev/null",
}


class UnsupportedPythonRuntime(RuntimeError):
    pass


def _inside(path, root):
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def validate_environment(environment):
    for name, expected in REQUIRED_ENVIRONMENT.items():
        if environment.get(name) != expected:
            raise UnsupportedPythonRuntime(f"{name} must equal {expected}")
    null = Path("/dev/null")
    try:
        information = null.lstat()
    except OSError as error:
        raise UnsupportedPythonRuntime("/dev/null identity is unavailable") from error
    if not stat.S_ISCHR(information.st_mode):
        raise UnsupportedPythonRuntime("/dev/null is not the expected character device")

    forbidden = {
        "PYTHONPATH",
        "PYTHONHOME",
        "PYTHONSTARTUP",
        "PYTHONEXECUTABLE",
        "__PYVENV_LAUNCHER__",
        "RANDFILE",
        "CTLOG_FILE",
    }
    rejected = []
    for name, value in environment.items():
        if not value:
            continue
        if name in forbidden:
            rejected.append(name)
            continue
        if name.startswith("OPENSSL_") and name not in REQUIRED_ENVIRONMENT:
            rejected.append(name)
            continue
        if name.startswith("SSL_CERT_") and name not in REQUIRED_ENVIRONMENT:
            rejected.append(name)
    if rejected:
        raise UnsupportedPythonRuntime(
            "unsupported Python runtime environment: "
            + ", ".join(sorted(rejected))
        )


def _validate_paths(metadata, repository_root):
    prefix = Path(metadata["base_prefix"]).resolve()
    stdlib = Path(metadata["stdlib"]).resolve()
    expected = [
        prefix / "lib/python315.zip",
        stdlib,
        stdlib / "lib-dynload",
    ]
    paths = metadata["paths"]
    if len(paths) <= len(expected):
        raise UnsupportedPythonRuntime("Python import path is outside the reviewed closure")
    invocation_roots = [Path(path).resolve() for path in paths[:-len(expected)]]
    if any(
        not _inside(path, repository_root.resolve())
        for path in invocation_roots
    ):
        raise UnsupportedPythonRuntime("Python invocation root escapes the repository")
    if [Path(path).resolve() for path in paths[-len(expected):]] != expected:
        raise UnsupportedPythonRuntime("Python standard-library path is unsupported")
    if any(
        "site-packages" in Path(path).parts or "dist-packages" in Path(path).parts
        for path in paths
    ):
        raise UnsupportedPythonRuntime("Python site package path is unsupported")


def _validate_darwin_framework(metadata):
    base_executable = Path(metadata["base_executable"]).resolve()
    framework = next(
        (
            path
            for path in base_executable.parents
            if path.name == "3.15"
            and path.parent.name == "Versions"
            and path.parent.parent.name == "Python.framework"
        ),
        None,
    )
    if framework is None or not _inside(base_executable, framework):
        raise UnsupportedPythonRuntime("Darwin Python framework identity is unsupported")
    if Path(metadata["base_prefix"]).resolve() != framework:
        raise UnsupportedPythonRuntime("Darwin Python prefix differs from its framework")


def validate_runtime_metadata(
    metadata,
    selected,
    *,
    repository_root=REPOSITORY_ROOT,
    system=None,
):
    if metadata.get("implementation") != "cpython":
        raise UnsupportedPythonRuntime("host automation requires CPython")
    version = tuple(metadata.get("version", ()))
    if (
        len(version) != 3
        or version[:2] != (3, 15)
        or version < (3, 15, 0)
        or metadata.get("releaselevel") != "final"
    ):
        raise UnsupportedPythonRuntime("host automation requires CPython 3.15 final")
    if metadata.get("cache_tag") != "cpython-315":
        raise UnsupportedPythonRuntime("Python cache tag is unsupported")
    if metadata.get("gil_enabled") is not True or metadata.get("py_gil_disabled") != 0:
        raise UnsupportedPythonRuntime("free-threaded Python is unsupported")
    if metadata.get("flags") != {
        "ignore_environment": 1,
        "no_site": 1,
        "dont_write_bytecode": 1,
        "isolated": 0,
        "safe_path": 0,
    }:
        raise UnsupportedPythonRuntime("Python must run with exact -E -S -B flags")

    executable = Path(metadata["executable"]).resolve()
    if executable != Path(selected).resolve():
        raise UnsupportedPythonRuntime("selected Python differs from the running interpreter")
    prefix = Path(metadata["prefix"]).resolve()
    base_prefix = Path(metadata["base_prefix"]).resolve()
    if prefix != base_prefix:
        raise UnsupportedPythonRuntime("Python virtual environments are unsupported")
    if metadata.get("cafile") is not None or metadata.get("capath") is not None:
        raise UnsupportedPythonRuntime("Python default trust paths are active")
    if metadata.get("certificate_count") != 0:
        raise UnsupportedPythonRuntime("Python default trust store is active")
    _validate_paths(metadata, Path(repository_root))
    if (platform.system() if system is None else system) == "Darwin":
        _validate_darwin_framework(metadata)


def current_metadata():
    import ssl

    verify_paths = ssl.get_default_verify_paths()
    return {
        "implementation": sys.implementation.name,
        "version": list(sys.version_info[:3]),
        "releaselevel": sys.version_info.releaselevel,
        "cache_tag": sys.implementation.cache_tag,
        "gil_enabled": (
            sys._is_gil_enabled()
            if hasattr(sys, "_is_gil_enabled")
            else None
        ),
        "py_gil_disabled": sysconfig.get_config_var("Py_GIL_DISABLED"),
        "flags": {
            "ignore_environment": sys.flags.ignore_environment,
            "no_site": sys.flags.no_site,
            "dont_write_bytecode": sys.flags.dont_write_bytecode,
            "isolated": sys.flags.isolated,
            "safe_path": getattr(sys.flags, "safe_path", None),
        },
        "executable": sys.executable,
        "base_executable": sys._base_executable,
        "prefix": sys.prefix,
        "base_prefix": sys.base_prefix,
        "stdlib": sysconfig.get_path("stdlib"),
        "paths": [
            str((REPOSITORY_ROOT if not path else Path(path)).resolve())
            for path in sys.path
        ],
        "cafile": verify_paths.cafile,
        "capath": verify_paths.capath,
        "certificate_count": len(ssl.create_default_context().get_ca_certs()),
    }


def selected_executable(environment=None):
    environment = os.environ if environment is None else environment
    selected = environment.get("MICROS_PYTHON")
    if not selected:
        return sys.executable
    path = Path(selected)
    if not path.is_absolute():
        raise UnsupportedPythonRuntime("MICROS_PYTHON must be an absolute path")
    return selected


def require_current_runtime(selected=None, *, environment=None):
    environment = os.environ if environment is None else environment
    validate_environment(environment)
    selected = selected_executable(environment) if selected is None else selected
    metadata = current_metadata()
    validate_runtime_metadata(metadata, selected)
    return metadata


def python_command(executable, *arguments, environment=None):
    validate_environment(os.environ if environment is None else environment)
    return (str(executable), *PYTHON_ARGS, *arguments)


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--selected")
    parser.add_argument("--metadata", action="store_true")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    try:
        metadata = require_current_runtime(arguments.selected)
    except (OSError, UnsupportedPythonRuntime) as error:
        print(f"unsupported Python runtime: {error}", file=sys.stderr)
        return 2
    if arguments.metadata:
        print(json.dumps(metadata, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

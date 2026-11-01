#!/usr/bin/env -S python3.15 -E -S -B

"""Fail-closed hosted CI preflight under Accepted ADR-0088."""

import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.python_host_runtime import UnsupportedPythonRuntime, require_current_runtime
from tools.stack_bound_bindings import AUTHORITY_COMMIT
from tools.stack_bound_fatal_firmware import resolve_default_firmware
from tools.stack_bound_types import StackEvidenceError
from tools.validation_context import capture_candidate, compiler_version, target_compiler, validate_environment
from tools.validation_inputs import UnsupportedContext, _file_identity, _run


ROOT = Path(__file__).resolve().parent.parent


class CiEnvironmentError(ValueError):
    pass


def git_object_id(value):
    return type(value) is str and re.fullmatch(r"(?:[a-f0-9]{40}|[a-f0-9]{64})", value) and set(value) != {"0"}


def tool_record(spelling, arguments=("--version",)):
    if not spelling or not Path(spelling).is_absolute() or not os.access(spelling, os.X_OK):
        raise CiEnvironmentError("CI lacks a selected executable tool")
    resolved = Path(spelling).resolve(strict=True)
    identity = _file_identity(resolved)
    version = _run((spelling, *arguments)).decode("utf-8")
    if Path(spelling).resolve(strict=True) != resolved or _file_identity(resolved) != identity:
        raise CiEnvironmentError("CI tool changed during preflight")
    return {"path": str(spelling), "resolved": str(resolved), "identity": identity, "version": version}


def collect_tools(environment):
    validate_environment(environment, target=True)
    compiler = target_compiler(environment)
    tools = {
        "python": tool_record(environment["MICROS_PYTHON"], ("-E", "-S", "-B", "--version")),
        "compiler": tool_record(str(compiler)),
        "linker": tool_record(environment["MICROS_LD_LLD"]),
    }
    for name in ("cmake", "ninja", "actionlint"):
        tools[name] = tool_record(shutil.which(name), ("-version",) if name == "actionlint" else ("--version",))
    for name in ("llvm-nm", "llvm-readelf"):
        tools[name] = tool_record(str(compiler.parent / name))
    return tools


def require_tool_versions(tools):
    compiler_version(tools["compiler"]["version"], target=True)
    if re.match(r"(?:Homebrew )?LLD 23\.", tools["linker"]["version"]) is None:
        raise CiEnvironmentError("CI requires LLVM LLD major 23")
    cmake = re.match(r"cmake version ([0-9]+)\.([0-9]+)", tools["cmake"]["version"])
    if cmake is None or tuple(map(int, cmake.groups())) < (3, 25):
        raise CiEnvironmentError("CI requires CMake 3.25 or newer")
    if re.match(r"[0-9]+\.[0-9]+", tools["ninja"]["version"]) is None:
        raise CiEnvironmentError("CI requires the selected Ninja implementation")
    for name in ("llvm-nm", "llvm-readelf"):
        if re.search(r"\bLLVM version 23\.", tools[name]["version"]) is None:
            raise CiEnvironmentError("CI LLVM companion tools must belong to major 23")
    if not tools["actionlint"]["version"].strip():
        raise CiEnvironmentError("CI workflow linter has no version identity")


def collect_environment(root, environment):
    runtime = require_current_runtime(environment=environment)
    host = {"system": platform.system(), "machine": platform.machine(), "version": platform.mac_ver()[0]}
    if (host["system"], host["machine"], host["version"].split(".")[0]) != ("Darwin", "arm64", "26"):
        raise CiEnvironmentError("CI requires the declared macOS 26 arm64 host")
    if (
        environment.get("GITHUB_ACTIONS") != "true"
        or environment.get("GITHUB_EVENT_NAME") not in ("pull_request", "push", "workflow_dispatch")
        or not git_object_id(environment.get("GITHUB_SHA"))
        or any(re.fullmatch(r"[1-9][0-9]*", environment.get(name, "")) is None
               for name in ("GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT"))
    ):
        raise CiEnvironmentError("CI event/run identity is unsupported")
    base = environment.get("CI_BASE")
    if base != "origin/main" and not git_object_id(base):
        raise CiEnvironmentError("CI comparison base is not a declared commit reference")
    candidate = capture_candidate(root)
    if not candidate.clean or candidate.source["head"] != environment["GITHUB_SHA"]:
        raise CiEnvironmentError("CI event does not name the exact clean checked-out candidate")
    if _run(("git", "rev-parse", "--is-shallow-repository"), cwd=root).strip() != b"false":
        raise CiEnvironmentError("CI requires complete repository history")
    _run(("git", "cat-file", "-e", AUTHORITY_COMMIT + "^{commit}"), cwd=root)
    resolved_base = _run(("git", "rev-parse", "--verify", base + "^{commit}"), cwd=root).decode().strip()
    if not git_object_id(resolved_base):
        raise CiEnvironmentError("CI base did not resolve to a complete commit identity")
    tools = collect_tools(environment)
    require_tool_versions(tools)
    qemu = shutil.which("qemu-system-riscv64")
    if qemu is None:
        raise CiEnvironmentError("CI has no selected QEMU")
    firmware = resolve_default_firmware(qemu)
    if capture_candidate(root) != candidate:
        raise CiEnvironmentError("CI source changed during preflight")
    return {
        "version": 1, "host": host, "image_version": environment.get("ImageVersion"),
        "event": {
            "name": environment["GITHUB_EVENT_NAME"], "commit": environment["GITHUB_SHA"],
            "run_id": environment["GITHUB_RUN_ID"], "attempt": environment["GITHUB_RUN_ATTEMPT"],
        },
        "candidate": candidate.source, "base": resolved_base,
        "runtime": runtime, "tools": tools, "firmware": firmware,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    try:
        result = collect_environment(ROOT, dict(os.environ))
    except (CiEnvironmentError, UnsupportedPythonRuntime, UnsupportedContext, StackEvidenceError, OSError) as error:
        print(f"CI_ENVIRONMENT_FAIL {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True, separators=(",", ":"), allow_nan=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

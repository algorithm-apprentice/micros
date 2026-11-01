"""Current-source build seals with complete compiler inputs and artifact bytes."""

import json
import os
from pathlib import Path
import shlex
import shutil
import tempfile

from tools import validation_context
from tools.python_host_runtime import require_current_runtime
from tools.stack_artifact_build import (
    FRAME_FLAGS, build_spelling, ninja_path, normalize_compile_argv, tool_identities,
)
from tools.stack_artifact_files import read_regular
from tools.stack_bound_collect import compiler_dependencies
from tools.stack_bound_types import StackEvidenceError, require_path
from tools.validation_inputs import _run


SEAL = "stack-build-seal.json"


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")


def owned_artifact(directory, name):
    require_path(name)
    path = Path(directory)
    for part in Path(name).parts:
        path /= part
        if path.is_symlink():
            raise StackEvidenceError("build seal contains a linked artifact path")
    return path


def snapshot_files(directory, names):
    if type(names) is not tuple or not names or len(names) != len(set(names)):
        raise StackEvidenceError("build seal has an empty or duplicate artifact inventory")
    try:
        return {name: read_regular(owned_artifact(directory, name))[1] for name in sorted(names)}
    except OSError as error:
        raise StackEvidenceError(f"build seal artifact is missing: {error}") from error


def require_snapshot(directory, snapshot):
    if type(snapshot) is not dict or not snapshot:
        raise StackEvidenceError("missing sealed artifact inventory")
    if encoded(snapshot_files(directory, tuple(snapshot))) != encoded(snapshot):
        raise StackEvidenceError("artifact content or kind changed after sealing")


def build_tools(directory):
    environment = dict(os.environ)
    tools = tool_identities(environment)
    runtime = require_current_runtime()
    selected = {
        "python": environment["MICROS_PYTHON"],
        "cmake": shutil.which("cmake"),
        "ninja": ninja_path(directory),
    }
    for name, spelling in selected.items():
        if not spelling or not Path(spelling).is_absolute():
            raise StackEvidenceError("build seal lacks a resolved required generation tool")
        path = Path(spelling).resolve(strict=True)
        _, identity = read_regular(path)
        argv = (spelling, "-E", "-S", "-B", "--version") if name == "python" else (spelling, "--version")
        tools[name] = {
            "path": spelling, "resolved": str(path), "identity": identity,
            "version": _run(argv).decode("utf-8"),
        }
    tools["python"]["runtime"] = runtime
    return tools


def compile_inventory(root, directory, mode, tools):
    commands = json.loads(read_regular(directory / "compile_commands.json")[0])
    if type(commands) is not list or not commands:
        raise StackEvidenceError("build seal has no complete compiler command inventory")
    dependencies = compiler_dependencies(directory)
    outputs, sources, recipes = set(), set(), []
    for entry in commands:
        if type(entry) is not dict or ("arguments" in entry) == ("command" in entry):
            raise StackEvidenceError("build seal has ambiguous compiler argv")
        argv = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
        if (
            type(argv) is not list or not argv or any(type(value) is not str for value in argv)
            or entry.get("directory") != str(directory)
            or argv[0] != tools["compiler"]["path"]
            or argv.count("-c") != 1 or argv.count("-o") != 1
            or max(argv.index("-c"), argv.index("-o")) + 1 >= len(argv)
            or any(value.startswith(("@", "-flto")) for value in argv)
        ):
            raise StackEvidenceError("compiler recipe is outside the admitted non-LTO build")
        source, output = Path(entry["file"]), Path(entry["output"])
        if not source.is_absolute():
            source = directory / source
        if not output.is_absolute():
            output = directory / output
        try:
            name = str(output.relative_to(directory))
        except ValueError as error:
            raise StackEvidenceError("compiled output leaves its build directory") from error
        actual_output = Path(argv[argv.index("-o") + 1])
        if not actual_output.is_absolute():
            actual_output = directory / actual_output
        if name in outputs or actual_output != output or Path(argv[argv.index("-c") + 1]) != source:
            raise StackEvidenceError("compiled source/output identity is duplicated or borrowed")
        c_source = source.suffix == ".c"
        if source.suffix not in (".c", ".S"):
            raise StackEvidenceError("unsupported stack compiler input language")
        expected_metadata = int(c_source and mode != "ordinary")
        if any(argv.count(flag) != expected_metadata for flag in FRAME_FLAGS) or (
            argv.count("-finstrument-functions-after-inlining") != int(c_source and mode == "observed")
        ):
            raise StackEvidenceError("compiler instrumentation mode differs from its seal")
        outputs.add(name)
        usage = output.with_suffix(".su")
        if c_source and mode != "ordinary" and usage.exists():
            outputs.add(str(usage.relative_to(directory)))
        sources.add(source)
        sources.update(dependencies.get(name, ()))
        recipes.append({"source": str(source), "output": name, "argv": argv})
    return outputs, sources, sorted(recipes, key=lambda row: row["output"])


def capture_build(root, directory, profile, mode, candidate):
    root, directory = Path(root).resolve(), Path(directory).resolve()
    if (
        mode not in ("ordinary", "metadata", "observed") or not candidate.clean
        or directory != root / "build/stack-evidence" / profile / mode
    ):
        raise StackEvidenceError("build seal has an unbound source/profile/mode")
    tools = build_tools(directory)
    outputs, dependencies, compile_commands = compile_inventory(root, directory, mode, tools)
    artifacts = {
        "CMakeCache.txt", "compile_commands.json", "build.ninja", "CMakeFiles/rules.ninja",
        *outputs,
    }
    for pattern in ("*.elf", "*.map", "*.bin", "*.json"):
        artifacts.update(str(path.relative_to(directory)) for path in directory.rglob(pattern)
                         if str(path.relative_to(directory)) != SEAL)
    inputs = {}
    compiler_root = Path(tools["compiler"]["resolved"]).parent.parent
    for source in sorted(dependencies):
        if source.is_relative_to(directory):
            artifacts.add(str(source.relative_to(directory)))
        elif source.is_relative_to(root):
            name = str(source.relative_to(root))
            inputs["source:" + name] = read_regular(owned_artifact(root, name))[1]
        else:
            resolved = source.resolve(strict=True)
            if not resolved.is_relative_to(compiler_root):
                raise StackEvidenceError("compiler dependency leaves the selected source/tool closure")
            inputs["compiler:" + str(resolved.relative_to(compiler_root))] = read_regular(resolved)[1]
    commands = _run((ninja_path(directory), "-C", directory, "-t", "commands")).decode("utf-8")
    return {
        "version": 1, "profile": profile, "mode": mode, "candidate": candidate.source,
        "tools": tools, "compile": compile_commands,
        "commands": [shlex.split(line) for line in commands.splitlines() if line],
        "inputs": inputs, "artifacts": snapshot_files(directory, tuple(sorted(artifacts))),
    }


def require_paired_compile_inputs(ordinary, metadata, first, second):
    if first["tools"] != second["tools"] or first["inputs"] != second["inputs"]:
        raise StackEvidenceError("ordinary/metadata tools or source inputs differ")
    recipes, generated = [], []
    for directory, measured, manifest in ((ordinary, False, first), (metadata, True, second)):
        recipes.append(tuple(sorted(
            (
                build_spelling(row["source"], directory), row["output"],
                normalize_compile_argv(
                    tuple(row["argv"]), directory,
                    measured and Path(row["source"]).suffix == ".c",
                ),
            )
            for row in manifest["compile"]
        )))
        generated.append({
            name: identity for name, identity in manifest["artifacts"].items()
            if Path(name).suffix in (".c", ".h", ".S", ".inc")
        })
    if recipes[0] != recipes[1] or generated[0] != generated[1]:
        raise StackEvidenceError("ordinary/metadata compiler recipe or generated input differs")


def replace_bytes(path, data, mode=0o600):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".stack-evidence-", delete=False) as stream:
            temporary = Path(stream.name)
            os.fchmod(stream.fileno(), mode)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        temporary = None
        descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
        if read_regular(path)[0] != data:
            raise StackEvidenceError("evidence changed during atomic publication")
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def publish_json(path, payload, *, validate=None):
    path = Path(path)
    if path.is_symlink() or path.exists() and not path.is_file():
        raise StackEvidenceError("evidence publication target is not an ordinary file")
    previous = read_regular(path) if path.exists() else None
    data = encoded(payload)
    if validate is not None:
        validate()
    complete = False
    try:
        replace_bytes(path, data)
        if validate is not None:
            validate()
        complete = True
    finally:
        if not complete and path.exists() and read_regular(path)[0] == data:
            if previous is None:
                path.unlink()
            else:
                replace_bytes(path, previous[0], previous[1]["mode"])


def seal_build(root, directory, profile, mode, candidate):
    payload = capture_build(root, directory, profile, mode, candidate)

    def validate():
        if validation_context.capture_candidate(root) != candidate:
            raise StackEvidenceError("source changed during build sealing")

    publish_json(Path(directory) / SEAL, payload, validate=validate)
    return payload


def verify_build_seal(root, directory, profile, mode, candidate):
    try:
        data = read_regular(Path(directory) / SEAL)[0]
        current = capture_build(root, directory, profile, mode, candidate)
        if data != encoded(current) or validation_context.capture_candidate(root) != candidate:
            raise StackEvidenceError("build seal is stale, incomplete or changed")
        return current
    except (OSError, ValueError) as error:
        if isinstance(error, StackEvidenceError):
            raise
        raise StackEvidenceError(f"invalid build seal: {error}") from error

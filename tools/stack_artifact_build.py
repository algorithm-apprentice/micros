"""Actual compiler/link recipes and sealed ordinary leaf artifacts."""

import hashlib
import json
import os
from pathlib import Path
import shlex
import tempfile

from tools.stack_artifact_authority import LEAVES
from tools.stack_artifact_files import read_regular
from tools.stack_bound_types import StackEvidenceError
from tools.validation_context import capture_candidate, target_compiler, validate_environment
from tools.validation_inputs import _file_identity, _run


FRAME_FLAGS = ("-fstack-usage", "-fstack-size-section")


def build_spelling(value, directory):
    directory = str(directory)
    if value == directory or value.startswith(directory + "/"):
        return "<build>" + value[len(directory):]
    for prefix in ("-I", "-Wl,-Map,"):
        if value.startswith(prefix + directory + "/"):
            return prefix + "<build>" + value[len(prefix + directory):]
    return value


def normalize_compile_argv(arguments, directory, metadata):
    if type(arguments) is not tuple or not arguments or any(type(value) is not str for value in arguments):
        raise StackEvidenceError("invalid complete compiler argv")
    for option in ("-o", "-c"):
        if arguments.count(option) != 1:
            raise StackEvidenceError("compiler recipe is incomplete")
        index = arguments.index(option)
        if index + 1 == len(arguments) or arguments[index + 1].startswith("-"):
            raise StackEvidenceError("compiler recipe has a missing file operand")
    expected = 1 if metadata else 0
    if any(arguments.count(flag) != expected for flag in FRAME_FLAGS):
        raise StackEvidenceError("compiler metadata flag set differs from its declaration")
    return tuple(
        build_spelling(value, directory) for value in arguments if value not in FRAME_FLAGS
    )


def tool_identities(environment):
    validate_environment(environment, target=True)
    compiler = Path(target_compiler(environment))
    linker = Path(environment["MICROS_LD_LLD"])
    if not linker.is_absolute() or not linker.is_file():
        raise StackEvidenceError("selected metadata linker is unavailable")
    result = {}
    for name, path in (("compiler", compiler), ("linker", linker)):
        resolved = path.resolve(strict=True)
        result[name] = {
            "path": str(path), "resolved": str(resolved),
            "identity": _file_identity(resolved),
            "version_sha256": hashlib.sha256(_run((path, "--version"))).hexdigest(),
        }
    return result


def ninja_path(directory):
    lines = (Path(directory) / "CMakeCache.txt").read_text().splitlines()
    selected = [
        line.partition("=")[2] for line in lines
        if line.startswith("CMAKE_MAKE_PROGRAM:") and "=" in line
    ]
    if len(selected) != 1 or not Path(selected[0]).is_absolute():
        raise StackEvidenceError("metadata recipe has no exact configured Ninja")
    return selected[0]


def link_argv(directory, target, relative):
    content = _run((ninja_path(directory), "-C", directory, "-t", "commands", target)).decode()
    selected = []
    for line in content.splitlines():
        words = shlex.split(line)
        blocks = [[]]
        for word in words:
            if word == "&&":
                blocks.append([])
            else:
                blocks[-1].append(word)
        for block in blocks:
            if "-o" not in block or "-c" in block:
                continue
            index = block.index("-o")
            if index + 1 == len(block):
                raise StackEvidenceError("incomplete linker file operand")
            output = Path(block[index + 1])
            if not output.is_absolute():
                output = Path(directory) / output
            if output == Path(directory) / relative:
                selected.append(tuple(build_spelling(word, directory) for word in block))
    if len(selected) != 1:
        raise StackEvidenceError("metadata leaf lacks one exact link recipe")
    return selected[0]


def leaf_recipes(root, directory, metadata):
    root, directory = Path(root), Path(directory)
    commands = json.loads((directory / "compile_commands.json").read_text())
    recipes = {}
    for target, relative in LEAVES.items():
        sources = []
        for entry in commands:
            output = Path(entry["output"])
            if not output.is_absolute():
                output = directory / output
            path = output.relative_to(directory)
            if len(path.parts) < 2 or path.parts[1] != target + ".dir":
                continue
            if entry["directory"] != str(directory):
                raise StackEvidenceError("compiler cwd changed within the leaf recipe")
            source = Path(entry["file"])
            if not source.is_absolute():
                source = directory / source
            arguments = tuple(shlex.split(entry["command"]))
            projected = normalize_compile_argv(
                arguments, directory, metadata and source.suffix == ".c",
            )
            sources.append((
                build_spelling(str(source), directory), projected, _file_identity(source),
            ))
        if not sources:
            continue
        payload = {
            "compile": sorted(sources), "link": link_argv(directory, target, relative),
        }
        recipes[target] = hashlib.sha256(json.dumps(
            payload, sort_keys=True, separators=(",", ":"),
        ).encode()).hexdigest()
    return recipes


def pair_record_path(root, profile):
    return Path(root) / "build/stack-evidence" / profile.configure / "pair.json"


def seal_ordinary_pair(root, profile, candidate):
    root = Path(root)
    if not candidate.clean or capture_candidate(root) != candidate:
        raise StackEvidenceError("ordinary build source differs from its candidate")
    ordinary = root / "build/stack-evidence" / profile.configure / "ordinary"
    recipes = leaf_recipes(root, ordinary, False)
    entries = []
    for leaf, recipe in recipes.items():
        _, identity = read_regular(ordinary / LEAVES[leaf])
        entries.append({
            "leaf": leaf, "path": LEAVES[leaf],
            "ordinary_identity": identity, "recipe": recipe,
        })
    record = {
        "version": 1, "mode": "metadata", "candidate": candidate.source,
        "profile": profile.configure, "tools": tool_identities(dict(os.environ)),
        "ordinary_root": str(ordinary.relative_to(root)),
        "metadata_root": "build/stack-evidence/" + profile.configure + "/metadata",
        "entries": entries,
    }
    if capture_candidate(root) != candidate:
        raise StackEvidenceError("ordinary source changed during pair sealing")
    path = pair_record_path(root, profile)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".stack-pair-", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(json.dumps(record, sort_keys=True).encode())
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return record

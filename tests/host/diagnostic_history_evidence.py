"""Current diagnostic storage and target artifact/tool identities."""

import json
import os
from dataclasses import asdict
from pathlib import Path
import shlex
import shutil

from tests.host.evidence_records import json_pairs
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact, snapshot_files
from tools.stack_bound_scope import require_image_outputs
from tools.user_elf_parse import load_elf
from tools.validation_inputs import _run


def current_target_manifest(root):
    from tools.stack_bound_fatal_firmware import resolve_default_firmware
    from tools.validation_context import capture_candidate

    root = Path(root).resolve()
    candidate = capture_candidate(root)
    if not candidate.clean or _run(
        ("git", "ls-files", "--others", "--exclude-standard", "-z"), cwd=root,
    ):
        raise ValueError("target evidence requires the complete clean staged candidate")
    names = tuple(sorted(name.decode() for name in _run(
        ("git", "ls-files", "-z"), cwd=root,
    ).split(b"\0") if name))
    if not names or len(names) != len(set(names)):
        raise ValueError("target evidence source membership is incomplete or ambiguous")
    roots = tuple(sorted({Path(name).parts[0] for name in names}))
    if _run(("git", "ls-files", "--others", "-z", "--", *roots), cwd=root):
        raise ValueError("target evidence contains untracked or ignored source inputs")
    result = {
        "candidate": asdict(candidate), "files": snapshot_files(root, names),
        "tools": closed_tool_inputs(root),
        "firmware": resolve_default_firmware(shutil.which("qemu-system-riscv64")),
    }
    if capture_candidate(root) != candidate:
        raise ValueError("target evidence source changed during input capture")
    return result


def closed_tool_inputs(root):
    from tools.validation_context import (
        DarwinTools, STANDARD_SEARCH_ROOTS, _cache, _validate_qemu_runner_family,
        _validate_qemu_runner_namespace, platform_identity, resolve_preset, validate_environment,
    )
    from tools.validation_inputs import _absent_paths, fingerprint_tree

    root = Path(root)
    environment = dict(os.environ)
    validate_environment(environment, target=True)
    profile = resolve_preset(root, "test-unit")
    cache = _cache(root, profile)
    if cache.get("MICROS_ENABLE_SANITIZERS") != "ON":
        raise ValueError("PLAT01 requires the existing enabled native sanitizer policy")
    tools = DarwinTools(root, profile, cache, environment)
    target_configuration = tools.target_preprocessor()
    tools._tool("target-linker", environment["MICROS_LD_LLD"])
    compiler = Path(environment["MICROS_CLANG"])
    for role in ("llvm-nm", "llvm-readelf"):
        tools._tool(role, compiler.parent / role)
    qemu = shutil.which("qemu-system-riscv64")
    if qemu is None:
        raise ValueError("PLAT01 requires its configured QEMU")
    tools.cache = {**cache, "MICROS_QEMU": qemu}
    qemu_inputs = tools.qemu()
    _validate_qemu_runner_family(root)
    _validate_qemu_runner_namespace(root, tools)
    tools.close_libraries(())
    roots = tuple(sorted(tools.roots))
    return {
        "platform": platform_identity(), "tools": tools.executables, "versions": tools.versions,
        "native_configuration": tools.configuration, "target_configuration": target_configuration,
        "standard_search_roots": _absent_paths(STANDARD_SEARCH_ROOTS),
        "python_spellings": tools.python_spellings, "qemu": qemu_inputs,
        "external_roots": {
            str(path): fingerprint_tree(
                path, roots, inert_link_roots=tools.python_inert_roots if path == tools.python_installation else (),
            )
            for path in roots
        },
    }


def history_storage(image):
    result = []
    for symbol in image.symbols:
        if symbol.name != "diagnostic_history":
            continue
        if not 0 < symbol.section_index < len(image.sections):
            raise ValueError("PLAT01 history symbol has no defined linked storage")
        section = image.sections[symbol.section_index]
        result.append({
            "size": symbol.size, "binding": symbol.binding, "symbol_type": symbol.symbol_type,
            "section_type": section.section_type, "section_flags": section.flags,
            "aligned": symbol.value % 4 == 0,
            "contained": section.address <= symbol.value
                and symbol.value + symbol.size <= section.address + section.size,
        })
    return tuple(result)


def build_artifacts(directory, *, configure=None):
    directory = Path(directory)
    names = {"CMakeCache.txt", "compile_commands.json", "build.ninja", "CMakeFiles/rules.ninja"}
    entries = json.loads(read_regular(directory / "compile_commands.json")[0], object_pairs_hook=json_pairs)
    if type(entries) is not list or not entries:
        raise ValueError("PLAT01 built image has no actual compiler input inventory")
    outputs = set()
    for entry in entries:
        arguments = entry.get("arguments") or shlex.split(entry["command"])
        if (
            entry["directory"] != str(directory) or arguments.count("-c") != 1
            or arguments.count("-o") != 1 or any(word.startswith(("@", "-flto")) for word in arguments)
        ):
            raise ValueError("PLAT01 compiler recipe escaped its ordinary fixed build")
        output = Path(arguments[arguments.index("-o") + 1])
        if output.is_absolute():
            output = output.relative_to(directory)
        name = str(output)
        owned_artifact(directory, name)
        if configure is None and not name.startswith("CMakeFiles/diagnostic_history_test.dir/"):
            continue
        if name in outputs:
            raise ValueError("PLAT01 build repeats a compiler output")
        outputs.add(name)
    if not outputs:
        raise ValueError("PLAT01 build has no compiled subject objects")
    names.update(outputs)
    if configure is None:
        names.add("diagnostic_history_test")
    else:
        names.update(require_image_outputs(directory, configure))
    return tuple(sorted(names))


def validate_history_storage(path):
    data, identity = read_regular(path)
    image = load_elf(path)
    if image.data != data or read_regular(path)[1] != identity:
        raise ValueError("diagnostic history image changed during artifact inspection")
    expected = ({
        "size": 12012, "binding": 0, "symbol_type": 1, "section_type": 8,
        "section_flags": 3, "aligned": True, "contained": True,
    },)
    if history_storage(image) != expected:
        raise ValueError("diagnostic history lacks its exact unique aligned BSS storage")
    return identity

#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize MEM05's fixed native source and finite platform bindings."""

import argparse
import ast
from functools import lru_cache
import hashlib
import io
from pathlib import Path
import subprocess
import sys
import tarfile

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host import selected_address_space_source as previous
from tests.host import selected_release_handoff_source as source
from tests.host.evidence_authority_source import literal_value, single_assignment
from tests.host.evidence_authority_scope import checked_source_view
from tests.host.evidence_authority_types import digest
from tests.host.work_count_types import Stage
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact


ROOT = Path(__file__).resolve().parents[1]
PREFIX = '#include "tests/host/selected_release_handoff_test.h"\n'
EXECUTION_FUNCTIONS = (
    "derive_user_status", "status_is_valid_user_return", "validate_user_mappings",
    "micros_user_execution_map_object_error",
    "micros_user_execution_resolve_thread_and_process",
    "micros_user_execution_validate_context",
)


@lru_cache(maxsize=4)
def native_source_members(root):
    text = subprocess.check_output(
        ("git", "show", source.RUNTIME_REFERENCE + ":tests/host/selected_address_space_source.py"),
        cwd=root, timeout=30,
    )
    recipe = literal_value(single_assignment(ast.parse(text.decode()), "RECIPE_ADMISSION").value)
    members = (
        *recipe["native_source_members"],
        "kernel/memory/user_address_space_handoff.c",
        "kernel/execution/user_execution_validation.c",
    )
    if len(members) != 58 or len(set(members)) != 58:
        raise ValueError("MEM05 fixed native C membership changed")
    return members


@lru_cache(maxsize=4)
def fixed_inputs(root):
    root = Path(root).resolve()
    tree = subprocess.check_output(
        ("git", "rev-parse", source.RUNTIME_REFERENCE + "^{tree}"), cwd=root, timeout=30,
    ).decode().strip()
    if tree != source.RUNTIME_TREE:
        raise ValueError("MEM05 fixed native commit/tree changed")
    members = {}
    for entry in subprocess.check_output(
        ("git", "ls-tree", "-r", "-z", source.RUNTIME_REFERENCE), cwd=root, timeout=30,
    ).split(b"\0"):
        if not entry:
            continue
        metadata, separator, path = entry.partition(b"\t")
        fields = metadata.decode().split()
        name = path.decode()
        if (
            not separator or len(fields) != 3 or fields[1] != "blob" or name in members
            or Path(name).is_absolute() or ".." in Path(name).parts or "\\" in name
        ):
            raise ValueError("MEM05 fixed native Git membership is malformed")
        members[name] = fields
    headers = tuple(sorted(
        name for name in members
        if name.endswith(".h") and name.startswith(("arch/", "include/", "kernel/"))
    ))
    if (
        len(headers) != 156
        or digest(headers) != "f6d999c1bb6b9f5fc57e097211f88ac72beb51577b964e6e4a24a777f057e3c2"
    ):
        raise ValueError("MEM05 fixed native header membership changed")
    selected = set(headers) | set(native_source_members(str(root)))
    if len(selected) != 214 or any(members[name][0] not in ("100644", "100755") for name in selected):
        raise ValueError("MEM05 native inputs are not its closed ordinary-file set")
    raw = subprocess.check_output(
        ("git", "-c", "tar.umask=022", "archive", source.RUNTIME_REFERENCE, *sorted(selected)),
        cwd=root, timeout=30,
    )
    result = {}
    with tarfile.open(fileobj=io.BytesIO(raw)) as archive:
        for item in archive:
            if item.isdir():
                continue
            if (
                not item.isfile() or item.name not in selected or item.name in result
                or item.mode & 0o777 != int(members[item.name][0], 8) & 0o777
            ):
                raise ValueError("MEM05 native archive has an unadmitted member")
            data = archive.extractfile(item).read()
            oid = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
            if oid != members[item.name][2]:
                raise ValueError("MEM05 native archive differs from fixed Git bytes")
            result[item.name] = (data, item.mode & 0o777)
    if set(result) != selected:
        raise ValueError("MEM05 native archive omitted a fixed input")
    return result


def _bootstrap_binding(text):
    names = (
        *previous.BOOTSTRAP_FUNCTIONS,
        "micros_frame_ownership_runtime_prepare_wired_process_user_set",
    )
    return (
        PREFIX + '#include "micros/kernel_object_runtime.h"\n#include <string.h>\n'
        "#define riscv_irq_is_enabled micros_as_native_irq_enabled\n"
        "static struct micros_frame_allocator bootstrap_allocator;\n"
        "static struct micros_frame_ownership bootstrap_ownership;\n"
        "static bool ownership_runtime_ready;\n"
        + previous.definitions(text, names)
        + "\nstruct micros_frame_allocator *micros_as_native_allocator(void)\n"
        "{ return &bootstrap_allocator; }\n"
        "struct micros_frame_ownership *micros_as_native_ownership(void)\n"
        "{ return &bootstrap_ownership; }\n"
        "void micros_as_native_bootstrap_reset(void)\n{\n"
        "    memset(&bootstrap_allocator, 0, sizeof(bootstrap_allocator));\n"
        "    memset(&bootstrap_ownership, 0, sizeof(bootstrap_ownership));\n"
        "    ownership_runtime_ready = false;\n}\n"
    )


def _scratch_binding(text, stage):
    text = PREFIX + '#include <string.h>\n' + previous.replace_body(
        text, "read_satp", "    return micros_as_native_read_satp();",
    )
    if stage is Stage.RED:
        return text + (
            "\nconst uint64_t *micros_as_native_scratch_bitmap(bool release)\n"
            "{ return release ? release_bitmap : reachable_bitmap; }\n"
            "bool micros_as_native_scratch_busy(void)\n"
            "{ return scratch_busy; }\n"
            "void micros_mem05_native_scratch_reset(void)\n{\n"
            "    memset(reachable_bitmap, 0, sizeof(reachable_bitmap));\n"
            "    memset(release_bitmap, 0, sizeof(release_bitmap));\n"
            "    scratch_busy = false;\n}\n"
        )
    return text + (
        "\nconst uint64_t *micros_as_native_scratch_bitmap(bool release)\n"
        "{ return release ? release_selection.bitmap : reachable_selection.bitmap; }\n"
        "bool micros_as_native_scratch_busy(void)\n"
        "{ return scratch_mode != MICROS_USER_ADDRESS_SPACE_SCRATCH_IDLE; }\n"
        "const struct micros_frame_selection *micros_mem05_native_scratch_selection(bool release)\n"
        "{ return release ? &release_selection : &reachable_selection; }\n"
        "uint8_t micros_mem05_native_scratch_mode(void)\n"
        "{ return scratch_mode; }\n"
        "void micros_mem05_native_scratch_set_mode(uint8_t mode)\n"
        "{ scratch_mode = mode; }\n"
        "void micros_mem05_native_scratch_reset(void)\n{\n"
        "    memset(&reachable_selection, 0, sizeof(reachable_selection));\n"
        "    memset(&release_selection, 0, sizeof(release_selection));\n"
        "    scratch_mode = MICROS_USER_ADDRESS_SPACE_SCRATCH_IDLE;\n}\n"
    )


def bind_native(path, text, stage):
    if stage not in (Stage.RED, Stage.GREEN):
        raise ValueError("MEM05 native binding has an unknown source stage")
    if path == "kernel/memory/bootstrap_memory.c":
        return _bootstrap_binding(text)
    if path == "kernel/memory/user_address_space_runtime.c":
        return _scratch_binding(text, stage)
    if path == "kernel/execution/user_execution_validation.c":
        anchor = "static void copy_context("
        if text.count(anchor) != 1:
            raise ValueError("MEM05 execution binding lost its fixed status-constant boundary")
        return PREFIX + text[:text.index(anchor)] + previous.definitions(text, EXECUTION_FUNCTIONS)
    bound = previous.bind_native(path, text, Stage.GREEN)
    return bound.replace(previous.NATIVE_PREFIX, PREFIX)


def source_bundle(root, stage, channel):
    root = Path(root).resolve()
    if stage not in (Stage.RED, Stage.GREEN) or channel not in source.CHANNELS:
        raise ValueError("MEM05 source materialization has an unadmitted stage or channel")
    source.validate_primitive_declarations(root)
    inputs, outputs = {}, {}
    for path, (original, mode) in sorted(fixed_inputs(str(root)).items()):
        raw = original
        if stage is Stage.GREEN:
            raw, identity = read_regular(root / path)
            if identity["mode"] != mode or path not in source.SOURCE_ROLES["production_paths"] and raw != original:
                raise ValueError("MEM05 changed an unadmitted native input or file mode: " + path)
        source.validate_retained_source(root, path, raw.decode(), stage)
        inputs[path] = {
            "kind": "file", "mode": mode, "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
        }
        text = raw.decode()
        if path in (*native_source_members(str(root)), *source.OBSERVED_HEADERS):
            text = source.observe_source(root, path, text, channel, stage)
            if source.project_observations(root, path, text, channel, stage).encode() != raw:
                raise ValueError("MEM05 native observation did not preserve its fixed source")
        outputs[path] = (bind_native(path, text, stage).encode(), mode)
    return inputs, outputs


def prepare_sources(root, directory, stage, channel):
    directory = Path(directory)
    if directory.is_symlink() or directory.exists() and not directory.is_dir():
        raise ValueError("MEM05 native output is not an owned ordinary directory")
    inputs, outputs = source_bundle(root, stage, channel)
    directory.mkdir(parents=True, exist_ok=True)
    generated = {}
    for path, (data, mode) in outputs.items():
        target = owned_artifact(directory, path)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists() or target.is_symlink():
            read_regular(target)
        target.write_bytes(data)
        target.chmod(mode)
        generated[path] = read_regular(target)[1]
    return inputs, generated


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.parse_args(argv)
    from tools.python_host_runtime import require_current_runtime

    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("MEM05 native generation requires the project root")
    directory = owned_artifact(ROOT, source.RECIPE_ADMISSION["generator_output"])
    for channel in source.CHANNELS:
        prepare_sources(ROOT, directory / channel, Stage.GREEN, channel)
    return 0


if __name__ == "__main__":
    try:
        with checked_source_view(ROOT):
            status = main()
        raise SystemExit(status)
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_MEM05_GENERATION_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error

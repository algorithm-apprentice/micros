#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize MEM03's actual fixed/current sources and native environment."""

import argparse
from functools import lru_cache
import hashlib
import io
from pathlib import Path
import subprocess
import sys
import tarfile

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.c_source_records import function_records
from tests.host.evidence_authority_source import regular_source
from tests.host.evidence_authority_scope import checked_source_view
from tests.host import selected_ownership_source as source
from tests.host.work_count_types import Stage
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact


ROOT = Path(__file__).resolve().parents[1]


@lru_cache(maxsize=4)
def fixed_inputs(root):
    root = Path(root)
    reference = source.SOURCE_PROJECTION["source_reference"]
    entries = subprocess.check_output(
        ("git", "ls-tree", "-r", "-z", reference), cwd=root, timeout=30,
    ).decode().split("\0")
    members = {}
    for entry in filter(None, entries):
        metadata, separator, name = entry.partition("\t")
        fields = metadata.split()
        if not separator or len(fields) != 3 or fields[1] != "blob" or name in members:
            raise ValueError("MEM03 fixed native Git membership is malformed")
        members[name] = (fields[0], fields[2])
    selected = {
        name for name in members
        if name.endswith(".h") and name.startswith(("include/", "arch/", "kernel/"))
    } | set(source.RECIPE_ADMISSION["native_source_members"])
    if any(members[name][0] not in ("100644", "100755") for name in selected):
        raise ValueError("MEM03 fixed native input is not an ordinary Git blob")
    data = subprocess.check_output(
        ("git", "-c", "tar.umask=022", "archive", reference, *sorted(selected)), cwd=root, timeout=30,
    )
    result = {}
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for member in archive:
            if member.isdir():
                continue
            if (
                not member.isfile() or member.name not in selected or member.name in result
                or member.mode & 0o777 != int(members[member.name][0], 8) & 0o777
            ):
                raise ValueError("MEM03 native archive has an unadmitted member")
            body = archive.extractfile(member).read()
            oid = hashlib.sha1(b"blob " + str(len(body)).encode() + b"\0" + body).hexdigest()
            if oid != members[member.name][1]:
                raise ValueError("MEM03 native archive differs from fixed Git bytes")
            result[member.name] = (body, member.mode & 0o777)
    if set(result) != selected:
        raise ValueError("MEM03 native archive omitted a fixed input")
    return result


def _definitions(text, names):
    records = function_records(text)
    if len(set(names)) != len(names) or not set(names) <= set(records):
        raise ValueError("MEM03 native runtime helper closure is incomplete")
    return (
        "".join(records[name].signature + ";\n" for name in names)
        + "".join(records[name].signature + "{" + records[name].body + "}\n" for name in names)
    )


def _bootstrap_unit(text, stage):
    names = source.SOURCE_PROJECTION["native_bootstrap_closure"]
    if stage is Stage.GREEN:
        names = (*names, *source.SOURCE_PROJECTION["new_helpers"]["kernel/memory/bootstrap_memory.c"])
    return (
        '#include "tests/host/selected_ownership_test.h"\n'
        '#include "micros/kernel_object_runtime.h"\n'
        "#include <string.h>\n"
        "static struct micros_frame_allocator bootstrap_allocator;\n"
        "static struct micros_frame_ownership bootstrap_ownership;\n"
        "static bool ownership_runtime_ready;\n"
        "#define riscv_irq_is_enabled micros_selected_native_irq_enabled\n"
        + _definitions(text, names)
        + "struct micros_frame_allocator *micros_selected_native_allocator(void)\n"
        "{ return &bootstrap_allocator; }\n"
        "struct micros_frame_ownership *micros_selected_native_ownership(void)\n"
        "{ return &bootstrap_ownership; }\n"
        "void micros_selected_native_reset(void)\n"
        "{\n"
        "    memset(&bootstrap_allocator, 0, sizeof(bootstrap_allocator));\n"
        "    memset(&bootstrap_ownership, 0, sizeof(bootstrap_ownership));\n"
        "    ownership_runtime_ready = false;\n"
        "}\n"
    )


def source_bundle(root, stage, channel):
    root = Path(root).resolve()
    channels = (*source.STATE_WORK_CONTRACT["channels"], "ordinary")
    if stage not in (Stage.RED, Stage.GREEN) or channel not in channels:
        raise ValueError("MEM03 source generation has an unknown stage or channel")
    inputs, outputs = {}, {}
    for name, (original, mode) in sorted(fixed_inputs(str(root)).items()):
        data = original
        if stage is Stage.GREEN:
            data, identity = read_regular(regular_source(root, name))
            mode = identity["mode"]
            if data != original and name not in source.SOURCE_ROLES["production_paths"]:
                from tests.host.selected_address_space_source import native_assertion_admission
                try:
                    admitted = "MEM03/native-input/" + name in native_assertion_admission(
                        root, "MEM03", name, data.decode("utf-8"), stage,
                    )
                except ValueError as error:
                    raise ValueError("MEM03 changed an unadmitted native dependency: " + name) from error
                if not admitted:
                    raise ValueError("MEM03 changed an unadmitted native dependency: " + name)
        inputs[name] = {
            "kind": "file", "mode": mode, "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
        text = data.decode("utf-8")
        if name in source.SOURCE_ROLES["production_paths"]:
            source.validate_retained_source(root, name, text, stage)
        if channel != "ordinary" and name.endswith(".c"):
            annotated = source.observe_source(root, name, text, stage, channel)
            if source.project_observations(root, name, annotated, stage, channel) != text:
                raise ValueError("MEM03 observations do not recover their exact source")
            text = annotated
        if name == "kernel/memory/bootstrap_memory.c":
            text = _bootstrap_unit(text, stage)
            if channel != "ordinary":
                text = source.OBSERVATION_PREFIX + text
        outputs[name] = (text.encode(), mode)
    return inputs, outputs


def prepare_sources(root, directory, stage, channel):
    root, directory = Path(root).resolve(), Path(directory)
    if directory.is_symlink() or directory.exists() and not directory.is_dir():
        raise ValueError("MEM03 native output is not an owned ordinary directory")
    directory.mkdir(parents=True, exist_ok=True)
    inputs, outputs = source_bundle(root, stage, channel)
    generated = {}
    for name, (data, mode) in outputs.items():
        target = owned_artifact(directory, name)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            read_regular(target)
        target.write_bytes(data)
        target.chmod(mode)
        generated[name] = read_regular(target)[1]
    return (
        tuple(directory / name for name in source.RECIPE_ADMISSION["native_source_members"]),
        inputs, generated,
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.parse_args(argv)
    from tools.python_host_runtime import require_current_runtime

    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("MEM03 native generation requires the project root")
    directory = owned_artifact(ROOT, source.RECIPE_ADMISSION["generator_output"])
    for channel in (*source.STATE_WORK_CONTRACT["channels"], "ordinary"):
        prepare_sources(ROOT, directory / channel, Stage.GREEN, channel)
    return 0


if __name__ == "__main__":
    try:
        with checked_source_view(ROOT):
            status = main()
        raise SystemExit(status)
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_MEM03_GENERATION_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error

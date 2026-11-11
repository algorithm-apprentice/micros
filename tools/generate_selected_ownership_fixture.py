#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize current ownership sources and their native environment."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.c_source_records import function_records
from tests.host.source_snapshot import checked_source_view, source_bytes
from tests.host import native_observations
from tests.host import selected_ownership_source as source
from tests.host.work_count_types import Stage
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact


ROOT = Path(__file__).resolve().parents[1]


def _definitions(text, names):
    records = function_records(text)
    if len(set(names)) != len(names) or not set(names) <= set(records):
        raise ValueError("MEM03 native runtime helper closure is incomplete")
    return (
        "".join(records[name].signature + ";\n" for name in names)
        + "".join(records[name].signature + "{" + records[name].body + "}\n" for name in names)
    )


def _bootstrap_unit(root, text):
    names = native_observations.declarations(root)["families"]["MEM03"]["subsets"]["kernel/memory/bootstrap_memory.c"]
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
    if stage is not Stage.GREEN or channel not in channels:
        raise ValueError("MEM03 source generation has an unknown stage or channel")
    inputs, outputs = {}, {}
    for name in native_observations.native_inputs(root, "MEM03"):
        data = source_bytes(root, root / name)
        mode = read_regular(root / name)[1]["mode"]
        if mode != 0o644:
            raise ValueError("MEM03 native input has an unadmitted mode: " + name)
        inputs[name] = {
            "kind": "file", "mode": mode, "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
        text = data.decode("utf-8")
        if name.endswith(".c"):
            text = native_observations.observe_source(root, "MEM03", name, text, channel)
        if name == "kernel/memory/bootstrap_memory.c":
            text = _bootstrap_unit(root, text)
        if name.endswith(".c") and channel != "ordinary":
            text = native_observations.PREFIX + text
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

#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize current release/handoff sources and finite native platform bindings."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host import selected_address_space_source as previous
from tests.host import selected_release_handoff_source as source
from tests.host import native_observations
from tests.host.source_snapshot import checked_source_view, source_bytes
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


def native_source_members(root):
    return native_observations.declarations(Path(root))["families"]["MEM05"]["native_sources"]


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
    if stage is not Stage.GREEN:
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
    if stage is not Stage.GREEN or channel not in source.CHANNELS:
        raise ValueError("MEM05 source materialization has an unadmitted stage or channel")
    inputs, outputs = {}, {}
    measured = {*native_source_members(root), *source.OBSERVED_HEADERS}
    for path in native_observations.native_inputs(root, "MEM05"):
        raw = source_bytes(root, root / path)
        mode = read_regular(root / path)[1]["mode"]
        if mode != 0o644:
            raise ValueError("MEM05 native input has an unadmitted mode: " + path)
        inputs[path] = {
            "kind": "file", "mode": mode, "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
        }
        text = raw.decode()
        if path in measured:
            text = native_observations.observe_source(root, "MEM05", path, text, channel)
        text = bind_native(path, text, stage)
        if path in measured and channel != "ordinary":
            text = native_observations.PREFIX + text
        outputs[path] = (text.encode(), mode)
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

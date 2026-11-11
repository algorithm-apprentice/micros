#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize current bootstrap allocation sources and native bindings."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.bootstrap_allocation_source import RECIPE_ADMISSION
from tests.host.c_source_records import function_records
from tests.host import native_observations
from tests.host.source_snapshot import checked_source_view, source_bytes
from tests.host.work_count_types import Stage
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact


ROOT = Path(__file__).resolve().parents[1]


def bootstrap_unit(root, text, stage, observed):
    records = function_records(text)
    names = native_observations.declarations(root)["families"]["MEM01"]["subsets"]["kernel/memory/bootstrap_memory.c"]
    if len(set(names)) != len(names) or not set(names) <= set(records):
        raise ValueError("MEM01 native bootstrap helper closure is incomplete")
    prefix = (
        '#include "tests/host/bootstrap_allocation_test.h"\n'
        "#include <stddef.h>\n#include <stdint.h>\n"
        "static struct micros_frame_allocator bootstrap_allocator;\n"
        "static uint64_t native_kernel_start;\nstatic uint64_t native_kernel_end;\n"
        "#define __kernel_start ((const unsigned char *)(uintptr_t)native_kernel_start)\n"
        "#define __kernel_end ((const unsigned char *)(uintptr_t)native_kernel_end)\n"
        '_Static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "native fixture requires LP64");\n'
    )
    declarations = "".join(records[name].signature + ";\n" for name in names)
    definitions = "".join(
        records[name].signature + "{" + records[name].body + "}\n" for name in names
    )
    adapters = (
        "struct micros_frame_allocator *micros_bootstrap_allocation_native_allocator(void)\n"
        "{ return &bootstrap_allocator; }\n"
        "void micros_bootstrap_allocation_native_bounds(uint64_t start, uint64_t end)\n"
        "{ native_kernel_start = start; native_kernel_end = end; }\n"
        "bool micros_bootstrap_allocation_native_validate(const struct micros_fdt_memory_map *map, bool all_free)\n"
        "{ return bootstrap_state_is_valid(map, all_free); }\n"
        "bool micros_bootstrap_allocation_native_frame_allowed(const struct micros_fdt_memory_map *map, uint64_t address)\n"
        "{ return managed_frame_is_allowed(map, address); }\n"
    )
    return prefix + declarations + definitions + adapters


def source_bundle(root, stage, *, observed=True):
    root = Path(root).resolve()
    if stage is not Stage.GREEN or type(observed) is not bool:
        raise ValueError("MEM01 native source generation has an unknown stage/mode")
    inputs, outputs = {}, {}
    for name in native_observations.native_inputs(root, "MEM01"):
        data = source_bytes(root, root / name)
        mode = read_regular(root / name)[1]["mode"]
        if mode != 0o644:
            raise ValueError("MEM01 native input has an unadmitted mode: " + name)
        inputs[name] = {"kind": "file", "mode": mode, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        text = data.decode("utf-8")
        if name.endswith(".c"):
            text = native_observations.observe_source(root, "MEM01", name, text, "aggregate" if observed else "ordinary")
        if name == "kernel/memory/bootstrap_memory.c":
            text = bootstrap_unit(root, text, stage, observed)
        if name.endswith(".c") and observed:
            text = native_observations.PREFIX + text
        outputs[name] = (text.encode(), mode)
    return inputs, outputs


def prepare_sources(root, directory, stage, *, observed=True):
    root, directory = Path(root).resolve(), Path(directory)
    if directory.is_symlink() or directory.exists() and not directory.is_dir():
        raise ValueError("MEM01 native output is not an owned ordinary directory")
    directory.mkdir(parents=True, exist_ok=True)
    inputs, outputs = source_bundle(root, stage, observed=observed)
    generated = {}
    for name, (data, mode) in outputs.items():
        target = owned_artifact(directory, name)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            read_regular(target)
        target.write_bytes(data)
        target.chmod(mode)
        generated[name] = read_regular(target)[1]
    sources = tuple(directory / name for name in (
        *RECIPE_ADMISSION["native_source_members"], "kernel/memory/bootstrap_memory.c",
    ))
    return sources, inputs, generated


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.parse_args(argv)
    from tools.python_host_runtime import require_current_runtime

    require_current_runtime()
    if Path.cwd().resolve() != ROOT:
        raise ValueError("MEM01 native generation requires the project root")
    output = owned_artifact(ROOT, RECIPE_ADMISSION["generator_output"])
    prepare_sources(ROOT, output, Stage.GREEN)
    return 0


if __name__ == "__main__":
    try:
        with checked_source_view(ROOT):
            status = main()
        raise SystemExit(status)
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.SubprocessError) as error:
        print("MICROS_MEM01_GENERATION_ERROR " + str(error), file=sys.stderr)
        raise SystemExit(2) from error

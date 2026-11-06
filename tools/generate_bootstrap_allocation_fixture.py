#!/usr/bin/env -S python3.15 -E -S -B

"""Materialize only MEM01's fixed/current native subjects and pure bindings."""

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

from tests.host.bootstrap_allocation_source import (
    OBSERVATION_PREFIX, RECIPE_ADMISSION, SOURCE_PROJECTION,
    baseline_source, observe_source, project_observations, validate_retained_source,
)
from tests.host.c_source_records import function_records
from tests.host.evidence_authority_source import regular_source
from tests.host.evidence_authority_scope import checked_source_view
from tests.host.work_count_types import Stage
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import owned_artifact


ROOT = Path(__file__).resolve().parents[1]


@lru_cache(maxsize=4)
def fixed_inputs(root):
    root = Path(root)
    reference = SOURCE_PROJECTION["source_reference"]
    entries = subprocess.check_output(
        ("git", "ls-tree", "-r", "-z", reference),
        cwd=root, timeout=30,
    ).decode().split("\0")
    members = {}
    for entry in filter(None, entries):
        metadata, separator, name = entry.partition("\t")
        fields = metadata.split()
        if not separator or len(fields) != 3 or fields[1] != "blob" or name in members:
            raise ValueError("MEM01 fixed native Git membership is malformed")
        members[name] = (fields[0], fields[2])
    headers = {
        name for name in members
        if name.endswith(".h") and name.startswith(("include/", "arch/", "kernel/"))
    }
    selected = headers | set(RECIPE_ADMISSION["native_source_members"]) | {"kernel/memory/bootstrap_memory.c"}
    if any(members[name][0] not in ("100644", "100755") for name in selected):
        raise ValueError("MEM01 fixed native input is not an ordinary Git blob")
    archive = subprocess.check_output(
        ("git", "-c", "tar.umask=022", "archive", reference, *sorted(selected)), cwd=root, timeout=30,
    )
    result = {}
    with tarfile.open(fileobj=io.BytesIO(archive)) as entries:
        for member in entries:
            if member.isdir():
                continue
            if (
                not member.isfile() or member.name not in selected or member.name in result
                or member.mode & 0o777 != int(members[member.name][0], 8) & 0o777
            ):
                raise ValueError("MEM01 fixed native archive has an unadmitted member")
            data = entries.extractfile(member).read()
            identity = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
            if identity != members[member.name][1]:
                raise ValueError("MEM01 native archive does not contain its exact fixed Git bytes")
            result[member.name] = (data, member.mode & 0o777)
    if set(result) != selected:
        raise ValueError("MEM01 native archive omitted a fixed source/header")
    return result


def bootstrap_unit(root, text, stage, observed):
    validate_retained_source(root, "kernel/memory/bootstrap_memory.c", text, stage=stage)
    annotated = observe_source("kernel/memory/bootstrap_memory.c", text, stage=stage, root=root) if observed else text
    if observed and project_observations(
        "kernel/memory/bootstrap_memory.c", annotated, stage=stage, root=root,
    ) != text:
        raise ValueError("MEM01 bootstrap observations do not recover their exact source")
    records = function_records(annotated)
    names = SOURCE_PROJECTION["baseline_bootstrap_helpers"]
    if stage is Stage.GREEN:
        names = (*names, *(row[0] for row in SOURCE_PROJECTION["new_helpers"]["kernel/memory/bootstrap_memory.c"]))
    if len(set(names)) != len(names) or not set(names) <= set(records):
        raise ValueError("MEM01 native bootstrap helper closure is incomplete")
    prefix = (
        (OBSERVATION_PREFIX if observed else "")
        + '#include "tests/host/bootstrap_allocation_test.h"\n'
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
    if stage not in (Stage.RED, Stage.GREEN) or type(observed) is not bool:
        raise ValueError("MEM01 native source generation has an unknown stage/mode")
    fixed = fixed_inputs(root)
    inputs, outputs = {}, {}
    for name, (original, mode) in sorted(fixed.items()):
        if stage is Stage.RED:
            data = original
        else:
            data, current = read_regular(regular_source(root, name))
            mode = current["mode"]
        inputs[name] = {"kind": "file", "mode": mode, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        text = data.decode("utf-8")
        if name == "kernel/memory/bootstrap_memory.c":
            text = bootstrap_unit(root, text, stage, observed)
        elif name in RECIPE_ADMISSION["generated_subjects"]:
            validate_retained_source(root, name, text, stage=stage)
            if observed:
                annotated = observe_source(name, text, stage=stage, root=root)
                if project_observations(name, annotated, stage=stage, root=root) != text:
                    raise ValueError("MEM01 native observations changed an actual selected body")
                text = annotated
        elif name == "include/micros/frame_allocator.h":
            validate_retained_source(root, name, text, stage=stage)
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
